#!/usr/bin/env python3
"""Select, verify, freeze, and run the versioned human-position engine suite.

Only `select` changes the tracked suite. Experiments use its frozen order and
the ratio_matchmaker controller, saving evidence under logs/.
"""

from __future__ import annotations

import argparse
import asyncio
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import random
import shlex
import shutil
import subprocess
import sys

import autoplay
import ratio_matchmaker as ratio_match


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "test/engine_suite/v1"
BUILD = ROOT / "build-canonical"
REFERENCE = ROOT / "logs/canonical/reference"
SEED = 20261021
ENGINE = "zero-capture-check-pw --pw-constant 2 --pw-alpha 0.5"
COUNTS = {"cheap": 24, "moderate": 24, "expensive": 12, "stress": 4}
SUBSET_SIZES = {"quick": 4, "standard": 8, "confirm": 16, "pool": 64, "stress": 4}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def engine_binary(build):
    path = build / "5dchess"
    return path if path.exists() else build / "5dchess.exe"


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def git(*args):
    return subprocess.check_output(["git", "-C", str(ROOT), *args], text=True).strip()


def build_record(build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("//", "#")) and "=" in line:
            key, value = line.split("=", 1)
            cache[key.split(":")[0]] = value
    if cache.get("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("canonical experiments require a Release build")
    if "-fno-rtti" not in shlex.split(cache.get("CMAKE_CXX_FLAGS", "")):
        raise ValueError("canonical experiments require CMAKE_CXX_FLAGS=-fno-rtti")
    compiler = cache["CMAKE_CXX_COMPILER"]
    return {
        "revision": git("rev-parse", "HEAD"),
        "describe": git("describe", "--tags", "--always"),
        "dirty_status": git("status", "--short"),
        "diff_sha256": hashlib.sha256(subprocess.check_output(
            ["git", "-C", str(ROOT), "diff", "HEAD"])).hexdigest(),
        "compiler": subprocess.check_output([compiler, "--version"], text=True).splitlines()[0],
        "engine_sources_sha256": hashlib.sha256(b"".join(
            str(p.relative_to(ROOT)).encode() + b"\0" + p.read_bytes()
            for p in sorted((ROOT / "src").rglob("*")) if p.is_file()
        )).hexdigest(),
        "cache": {k: v for k, v in cache.items() if k.startswith((
            "CMAKE_CXX_", "CMAKE_BUILD_TYPE", "CMAKE_INTERPROCEDURAL_", "Python_"))},
        "platform": platform.platform(),
        "processor": platform.processor(),
        "python": sys.version,
    }


def collect_game(path, module_dir):
    """Isolated worker: sample completed main-line states with 8 actions left.

    Keep a deterministic reservoir in each width/side stratum. No mate scans
    or full legal-action enumeration are needed: the recorded continuation
    supplies a witness, later checked with the reference and rules module.
    """
    rules = autoplay.load_rules(module_dir)
    game = rules.game.from_pgn(path.read_text())
    if game.get_promotion_options() != 1:
        raise ValueError("v1 is restricted to queen-only Turn Zero games")
    rng = random.Random(f"{SEED}:{path.name}")
    reservoirs = {}
    remaining = 0
    while game.has_parent():
        game.visit_parent()
        remaining += 1
        if remaining < 8:
            continue
        mandatory, optional, unplayable = game.get_current_timeline_status()
        playable = len(mandatory) + len(optional)
        black = game.get_current_present()[1]
        key = min(playable, 4), black
        rank = rng.random()
        if key in reservoirs and reservoirs[key][0] < rank:
            continue
        boards = game.get_current_boards()
        lines = {b[0] for b in boards}
        # v1 uses odd Turn Zero layout, whose initial timeline is 0.
        white_created = max(lines)
        black_created = -min(lines)
        advantage = (white_created - black_created) if black else (black_created - white_created)
        opening = autoplay.snapshot_pgn(game)
        restored = rules.game.from_pgn(opening)
        if (restored.get_current_hash() != game.get_current_hash()
                or restored.get_current_fen() != game.get_current_fen()
                or restored.get_current_present() != game.get_current_present()):
            raise ValueError("snapshot round trip changes the source state")
        continuation = game.get_child_actions(rules.SHOW_CAPTURE | rules.SHOW_PROMOTION)[-1]
        reservoirs[key] = rank, {
            "source": str(path.relative_to(ROOT)), "source_sha256": digest(path),
            "remaining_actions": remaining, "side_to_move": "black" if black else "white",
            "playable": playable, "mandatory": len(mandatory), "optional": len(optional),
            "total_timelines": len(lines), "board_count": len(boards),
            "timeline_advantage": advantage, "state_hash": str(game.get_current_hash()),
            "continuation": continuation.pgn, "pgn": opening,
        }
    records = []
    for _, record in reservoirs.values():
        record["actions_played"] = remaining - record["remaining_actions"]
        if record["actions_played"] >= 8:
            records.append(record)
    return records


async def probe_position(record, build, engine, seed):
    rules = autoplay.load_rules(build)
    game = rules.game.from_pgn(record["pgn"])
    player = autoplay.EngineProcess("suite probe", shlex.join([str(engine_binary(build)),
        *shlex.split(engine), "--seed", str(seed)]), 1500)
    try:
        await player.start()
        await player.new_game()
        result = {}
        for name, depth in (("first", 1), ("work", 10)):
            moves = await player.choose(autoplay.uci_position(game), [], 1000,
                asyncio.Queue(), depth=depth)
            if not moves:
                raise ValueError("reference found no legal action")
            check = rules.game.from_pgn(record["pgn"])
            error = autoplay.apply_action(rules, check, moves)
            if error:
                raise ValueError(f"reference action is illegal: {error}")
            sample = ratio_match.search_sample(player)
            result[name + "_seconds"] = sample.seconds
            result[name + "_iterations"] = sample.iterations
            result[name + "_ips"] = sample.ips
        result["cost_seconds"] = max(result["first_seconds"],
            100 * result["work_seconds"] / result["work_iterations"])
        return result
    finally:
        await player.close()


def isolated_collect(path, build):
    try:
        proc = subprocess.run([sys.executable, str(Path(__file__).resolve()),
            "_collect", str(path), "--build", str(build)],
            capture_output=True, text=True, timeout=8)
        if proc.returncode:
            return {"source": str(path.relative_to(ROOT)), "exit_code": proc.returncode,
                "error": proc.stderr[-1000:] or f"collector exited with code {proc.returncode}"}
        return json.loads(proc.stdout)
    except subprocess.TimeoutExpired:
        return {"source": str(path.relative_to(ROOT)), "error": "collection exceeded 8s watchdog"}


def selection_candidates(records, limit):
    """Round-robin width/history/advantage/side strata; at most one per game."""
    groups = defaultdict(list)
    for r in records:
        key = (min(r["playable"], 4), r["side_to_move"],
            min(r["board_count"] // 64, 3), (r["timeline_advantage"] > 0)
            - (r["timeline_advantage"] < 0))
        groups[key].append(r)
    rng = random.Random(SEED)
    for group in groups.values():
        rng.shuffle(group)
    keys = sorted(groups, reverse=True)
    selected, used = [], set()
    while len(selected) < limit:
        progress = False
        for key in keys:
            while groups[key] and groups[key][-1]["source"] in used:
                groups[key].pop()
            if not groups[key]:
                continue
            r = groups[key].pop()
            selected.append(r)
            used.add(r["source"])
            progress = True
            if len(selected) == limit:
                break
        if not progress:
            break
    return selected


def select(args):
    if (args.suite / "manifest.json").exists():
        raise ValueError("suite already exists; select into a NEW version directory")
    args.audit.mkdir(parents=True, exist_ok=False)
    provenance = build_record(args.build)
    # Read the headers to avoid depending on the variant index's representation.
    paths = [p for p in sorted((ROOT / "test/pgn/online").glob("*.5dpgn"))
        if '[Variant "Standard - Turn Zero"]' in p.read_text()]
    records, errors = [], []
    if args.cached_candidates:
        records = json.loads(args.cached_candidates.read_text())
        error_file = args.cached_candidates.parent / "collection-errors.json"
        if error_file.exists():
            errors = json.loads(error_file.read_text())
    else:
        with ThreadPoolExecutor(max_workers=2) as executor:
            for i, result in enumerate(executor.map(lambda p: isolated_collect(p, args.build), paths), 1):
                if isinstance(result, dict):
                    errors.append(result)
                else:
                    records.extend(result)
                if i % 50 == 0:
                    print(f"Collected {i}/{len(paths)} games", flush=True)
    save(args.audit / "collection-errors.json", errors)
    candidates = records if args.cached_candidates else selection_candidates(records, args.candidates)
    save(args.audit / "candidates.json", candidates)
    measured = []
    if args.cached_measured:
        measured = json.loads(args.cached_measured.read_text())
    else:
        for i, r in enumerate(candidates):
            try:
                r["probe"] = asyncio.run(probe_position(r, args.build, ENGINE, SEED))
                measured.append(r)
            except (ValueError, autoplay.ProtocolError) as exc:
                errors.append({"source": r["source"], "error": str(exc)})
            if (i + 1) % 24 == 0:
                print(f"Probed {i + 1}/{len(candidates)} positions", flush=True)
    save(args.audit / "measured.json", measured)
    save(args.audit / "errors.json", errors)
    if len(measured) < 128:
        raise ValueError("fewer than 128 successful probes; inspect the selection audit")
    # Same cost rank boundaries for both colors; select equal color counts
    # inside every group. Boundaries and all individual costs are persisted.
    ordered = sorted(measured, key=lambda r: (r["probe"]["cost_seconds"], r["source"]))
    boundaries = [ordered[int(len(ordered) * q)]["probe"]["cost_seconds"]
        for q in (0.375, 0.75, 0.9375)]
    groups = defaultdict(list)
    for r in ordered:
        group = list(COUNTS)[sum(r["probe"]["cost_seconds"] > b for b in boundaries)]
        r["cost_group"] = group
        groups[group, r["side_to_move"]].append(r)
    chosen = {}
    for group, count in COUNTS.items():
        for side in ("white", "black"):
            available = groups[group, side]
            if len(available) < count // 2:
                raise ValueError(f"not enough {side} {group} candidates; increase --candidates")
            # Greedily cover structural strata, then select spread-out costs.
            slots = Counter()
            picked = []
            for _ in range(count // 2):
                def priority(r):
                    structural = (min(r["playable"], 4), min(r["board_count"] // 64, 3),
                        (r["timeline_advantage"] > 0) - (r["timeline_advantage"] < 0))
                    cost = r["probe"]["cost_seconds"]
                    return slots[structural], -cost if group == "stress" else cost, r["source"]
                r = min(available, key=priority)
                available.remove(r)
                key = (min(r["playable"], 4), min(r["board_count"] // 64, 3),
                    (r["timeline_advantage"] > 0) - (r["timeline_advantage"] < 0))
                slots[key] += 1
                picked.append(r)
            chosen[group, side] = picked
    # Put structural contrasts into the small prefixes, rather than leaving
    # every wide or long-history example near the end of the pool.
    for group, side, preferred in (
        ("cheap", "white", lambda r: (r["playable"] == 1, r["board_count"])),
        ("cheap", "black", lambda r: (r["timeline_advantage"] < 0, r["board_count"])),
        ("moderate", "white", lambda r: (r["timeline_advantage"] < 0, r["board_count"])),
        ("moderate", "black", lambda r: (r["playable"] == 1, -r["board_count"])),
        ("expensive", "white", lambda r: (r["playable"], r["board_count"])),
        ("expensive", "black", lambda r: (r["playable"], r["board_count"])),
    ):
        picked = chosen[group, side]
        first = max(picked, key=preferred)
        picked.remove(first)
        picked.insert(0, first)
    # First 4: cheap W/B, moderate W/B. First 8 add cheap W,
    # moderate B, expensive W/B. First 16 total 6 cheap, 6 moderate,
    # 4 expensive, with equal side counts inside each group.
    schedule = [("cheap", "white"), ("cheap", "black"),
        ("moderate", "white"), ("moderate", "black"),
        ("cheap", "white"), ("moderate", "black"),
        ("expensive", "white"), ("expensive", "black"),
        ("cheap", "black"), ("moderate", "white"),
        ("cheap", "white"), ("cheap", "black"),
        ("moderate", "white"), ("moderate", "black"),
        ("expensive", "white"), ("expensive", "black")]
    final = [chosen[key].pop(0) for key in schedule]
    for group in COUNTS:
        for side in ("white", "black"):
            final.extend(chosen[group, side])
    args.suite.mkdir(parents=True, exist_ok=False)
    (args.suite / "positions").mkdir()
    for i, r in enumerate(final, 1):
        r["id"] = f"p{i:02d}"
        r["file"] = f"positions/{r['id']}.5dpgn"
        (args.suite / r["file"]).write_text(r.pop("pgn"))
        r["sha256"] = digest(args.suite / r["file"])
    manifest = {
        "schema_version": 1, "seed": SEED,
        "selection": {"source_games": len(paths), "candidate_states": len(records),
            "probed": len(candidates), "probe_successes": len(measured),
            "cost_boundaries_seconds": boundaries, "errors": errors,
            "method": "width/history/advantage/side round robin; one position per game; measured cost quantiles",
            "minimum_actions_played": 8, "minimum_recorded_actions_remaining": 8},
        "reference_engine": ENGINE, "reference_sha256": digest(engine_binary(args.build)),
        "selection_build": provenance, "positions": final,
        "subsets": {"quick": [r["id"] for r in final[:4]],
            "standard": [r["id"] for r in final[:8]],
            "confirm": [r["id"] for r in final[:16]],
            "pool": [r["id"] for r in final],
            "stress": [r["id"] for r in final if r["cost_group"] == "stress"]},
    }
    save(args.suite / "manifest.json", manifest)
    verify(args.suite)
    print(f"Selected 64 positions into {args.suite}")


def verify(suite):
    manifest = json.loads((suite / "manifest.json").read_text())
    positions = manifest["positions"]
    if len(positions) != 64 or len({r["id"] for r in positions}) != 64:
        raise ValueError("suite must contain 64 unique position IDs")
    if len({r["source"] for r in positions}) != 64:
        raise ValueError("suite must use 64 distinct source games")
    if len({r["state_hash"] for r in positions}) != 64:
        raise ValueError("suite contains duplicate state identities")
    if Counter(r["cost_group"] for r in positions) != Counter(COUNTS):
        raise ValueError("incorrect cost group quotas")
    for group, count in COUNTS.items():
        if Counter(r["side_to_move"] for r in positions if r["cost_group"] == group) != {
                "white": count // 2, "black": count // 2}:
            raise ValueError(f"unbalanced side to move inside cost group: {group}")
    by_id = {r["id"]: r for r in positions}
    contents = set()
    for r in positions:
        path = suite / r["file"]
        if digest(path) != r["sha256"] or digest(ROOT / r["source"]) != r["source_sha256"]:
            raise ValueError(f"content changed: {r['id']}")
        if r["sha256"] in contents:
            raise ValueError("duplicate snapshot")
        contents.add(r["sha256"])
        if min(r["actions_played"], r["remaining_actions"]) < 8:
            raise ValueError("position is too close to a game boundary")
    for name, size in SUBSET_SIZES.items():
        ids = manifest["subsets"][name]
        if len(ids) != size or len(set(ids)) != size or not set(ids) <= by_id.keys():
            raise ValueError(f"invalid subset {name}")
        if Counter(by_id[i]["side_to_move"] for i in ids) != {"white": size // 2, "black": size // 2}:
            raise ValueError(f"unbalanced side to move: {name}")
    for small, large in (("quick", "standard"), ("standard", "confirm"), ("confirm", "pool")):
        if manifest["subsets"][large][:SUBSET_SIZES[small]] != manifest["subsets"][small]:
            raise ValueError("subsets must be nested prefixes")
    return manifest


def freeze(args):
    verify(args.suite)
    record = build_record(args.build)
    args.reference.mkdir(parents=True, exist_ok=False)
    binary = args.reference / engine_binary(args.build).name
    shutil.copy2(engine_binary(args.build), binary)
    record.update({"binary_sha256": digest(binary), "engine": args.engine,
        "binary_name": binary.name,
        "suite_sha256": digest(args.suite / "manifest.json")})
    save(args.reference / "reference.json", record)
    print(f"Frozen reference: {binary}")


def summarize(output, rows):
    import csv
    games_path, searches_path = output / "games.csv", output / "searches.csv"
    games = list(csv.DictReader(games_path.open())) if games_path.exists() else []
    searches = list(csv.DictReader(searches_path.open())) if searches_path.exists() else []
    result = {"games": len(games), "searches": len(searches), "by_ratio": {}}
    for ratio in sorted({g["ratio"] for g in games}, key=float, reverse=True):
        selected = [g for g in games if g["ratio"] == ratio]
        summary = {"outcomes": dict(Counter(g["outcome"] for g in selected)),
            "action_limit_draws": sum(g["reason"] == "action limit reached" for g in selected),
            "failures": sum(any(s in g["reason"] for s in
                ("failure", "illegal action", "nonterminal", "controller error")) for g in selected),
            "target_time_capped_searches": sum(s["role"] == "target" and s["time_capped"] == "1"
                for s in searches if s["ratio"] == ratio), "by_cost": {}, "by_side_to_move": {}}
        for field, label in (("cost_group", "by_cost"), ("side_to_move", "by_side_to_move")):
            for value in sorted({r[field] for r in rows}):
                subset = [g for g in selected if rows[(int(g["pair"]) - 1) % len(rows)][field] == value]
                summary[label][value] = dict(Counter(g["outcome"] for g in subset))
        result["by_ratio"][ratio] = summary
    save(output / "summary.json", result)
    return result


def run(args):
    manifest = verify(args.suite)
    reference = json.loads((args.reference / "reference.json").read_text())
    ref_binary = args.reference / reference.get("binary_name", "5dchess")
    if digest(ref_binary) != reference["binary_sha256"]:
        raise ValueError("frozen reference binary changed")
    if reference["suite_sha256"] != digest(args.suite / "manifest.json"):
        raise ValueError("reference belongs to a different suite version")
    build = build_record(args.build)
    by_id = {r["id"]: r for r in manifest["positions"]}
    rows = [by_id[i] for i in manifest["subsets"][args.subset]]
    if args.skip_first >= len(rows):
        raise ValueError("--skip-first must leave at least one position")
    rows = rows[args.skip_first:]
    args.output.mkdir(parents=True, exist_ok=False)
    # Snapshot the executable so rebuilding during a run cannot change games.
    candidate = args.output / ("candidate" + engine_binary(args.build).suffix)
    shutil.copy2(engine_binary(args.build), candidate)
    openings = args.output / "openings"
    openings.mkdir()
    rules_dir = args.output / "rules"
    rules_dir.mkdir()
    binding_files = [p for pattern in ("engine*.so", "engine*.pyd")
        for p in args.build.glob(pattern)]
    if not binding_files:
        raise ValueError("build the Python rules binding before running experiments")
    for path in binding_files:
        shutil.copy2(path, rules_dir / path.name)
    for r in rows:
        shutil.copy2(args.suite / r["file"], openings / f"{r['id']}.5dpgn")
    controller_output = args.output / "controller"
    command = [sys.executable, str(ROOT / "scripts/ratio_matchmaker.py"),
        "--target", shlex.join([str(candidate), *shlex.split(args.engine)]),
        "--baseline", shlex.join([str(ref_binary), *shlex.split(reference["engine"])]),
        "--ratios", args.ratios, "--max-pairs", str(len(rows)), "--jobs", "1",
        "--baseline-movetime", str(args.movetime), "--max-target-movetime", str(args.target_cap),
        "--timeout", "2000", "--max-actions", str(args.max_actions),
        "--module-dir", str(rules_dir),
        "--output-dir", str(controller_output)]
    for r in rows:
        command.extend(["--opening", str(openings / f"{r['id']}.5dpgn")])
    provenance = {"schema_version": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
        "suite_sha256": digest(args.suite / "manifest.json"), "subset": args.subset,
        "skip_first": args.skip_first, "candidate_engine": args.engine,
        "positions": rows, "reference": reference, "candidate": build,
        "candidate_sha256": digest(candidate), "command": command,
        "controller_sha256": digest(ROOT / "scripts/ratio_matchmaker.py"),
        "runner_sha256": digest(Path(__file__)),
        "autoplay_sha256": digest(ROOT / "scripts/autoplay.py"),
        "binding_sha256": {p.name: digest(p) for p in rules_dir.iterdir()},
        "note": "Small runs are screening evidence; action-limit draws are artificial caps."}
    save(args.output / "run.json", provenance)
    print(shlex.join(command), flush=True)
    code = 130
    try:
        with (args.output / "controller.log").open("w") as log:
            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            try:
                for line in process.stdout:
                    print(line, end="", flush=True)
                    log.write(line)
                    log.flush()
                code = process.wait()
            except KeyboardInterrupt:
                process.wait(timeout=10)
    finally:
        result = summarize(controller_output, rows)
        save(args.output / "summary.json", result)
        provenance["controller_exit_code"] = code
        if code == 0 and any(r["failures"] for r in result["by_ratio"].values()):
            code = 2
        provenance["exit_code"] = code
        save(args.output / "run.json", provenance)
        print(json.dumps(result, indent=2))
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("select", "verify", "freeze", "run", "_collect"):
        p = sub.add_parser(name)
        p.add_argument("--build", type=Path, default=BUILD)
        p.add_argument("--suite", type=Path, default=SUITE)
        if name in ("freeze", "run"):
            p.add_argument("--reference", type=Path, default=REFERENCE)
        if name == "select":
            p.add_argument("--audit", type=Path, default=ROOT / "logs/canonical/selection-v1")
            p.add_argument("--candidates", type=int, default=256)
            p.add_argument("--cached-candidates", type=Path,
                help="reuse a previous candidate file instead of rescanning games")
            p.add_argument("--cached-measured", type=Path,
                help="reuse probe measurements from the same binary and machine")
        if name == "verify":
            p.add_argument("--native", action="store_true", help="check all snapshot states with the compiled binding")
        if name == "freeze":
            p.add_argument("--engine", default=ENGINE,
                help="reference engine and options to preserve with the executable")
        if name == "run":
            p.add_argument("--engine", default=ENGINE, help="candidate engine and options (executable comes from --build)")
            p.add_argument("--subset", choices=SUBSET_SIZES, default="standard")
            p.add_argument("--skip-first", type=int, choices=(0, 4, 8, 16), default=0,
                help="extend a nested subset without replaying its earlier positions")
            p.add_argument("--ratios", default="1", type=ratio_match.parse_float_list)
            p.add_argument("--movetime", type=int, default=200)
            p.add_argument("--target-cap", type=int, default=2000)
            p.add_argument("--max-actions", type=int, default=160)
            p.add_argument("--output", required=True, type=Path)
        if name == "_collect":
            p.add_argument("source", type=Path)
    args = parser.parse_args()
    # All subprocess paths must be independent of the caller's directory.
    for key, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, key, value.resolve())
    try:
        if args.action == "_collect":
            print(json.dumps(collect_game(args.source, args.build)))
        elif args.action == "select":
            if args.candidates < 128:
                raise ValueError("selection needs at least 128 probe candidates")
            select(args)
        elif args.action == "verify":
            manifest = verify(args.suite)
            if args.native:
                rules = autoplay.load_rules(args.build)
                for r in manifest["positions"]:
                    game = rules.game.from_pgn((args.suite / r["file"]).read_text())
                    source = rules.game.from_pgn((ROOT / r["source"]).read_text())
                    for _ in range(r["remaining_actions"]):
                        if not source.has_parent():
                            raise ValueError(f"source prefix missing: {r['id']}")
                        source.visit_parent()
                    if (source.get_current_fen() != game.get_current_fen()
                            or source.get_current_hash() != game.get_current_hash()):
                        raise ValueError(f"source prefix differs from snapshot: {r['id']}")
                    if str(game.get_current_hash()) != r["state_hash"]:
                        raise ValueError(f"state mismatch: {r['id']}")
                    if ("black" if game.get_current_present()[1] else "white") != r["side_to_move"]:
                        raise ValueError(f"side mismatch: {r['id']}")
                    if len(game.get_current_boards()) != r["board_count"]:
                        raise ValueError(f"history mismatch: {r['id']}")
                    if game.get_match_status() != rules.match_status_t.PLAYING:
                        raise ValueError(f"terminal suite position: {r['id']}")
            print("Verified 64 positions; nested 4/8/16 subsets; all side balances and content hashes")
        elif args.action == "freeze":
            freeze(args)
        elif args.action == "run":
            if min(args.movetime, args.target_cap, args.max_actions) <= 0:
                raise ValueError("budgets must be positive")
            args.ratios = ",".join(map(str, args.ratios))
            return run(args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as exc:
        parser.exit(2, f"Error: {exc}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
