#!/usr/bin/env python3
"""Measure legal-action counts at sampled positions from autoplay logs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import tempfile
import time
from concurrent.futures import Future, ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, TypeVar


ACTION_RE = re.compile(r"^(\d+)([wb])\.\s+(.+?)\s*:\s*(.+?)\s*$")
MATCH_RE = re.compile(r"match-(\d+)$")
T = TypeVar("T")

FIELDS = [
    "game_id",
    "source_log",
    "run_id",
    "match_id",
    "game_status",
    "game_result",
    "winner",
    "white_engine",
    "white_command",
    "black_engine",
    "black_command",
    "engine_to_move",
    "requested_movetime_ms",
    "turn",
    "actions_played",
    "side_to_move",
    "sample_reason",
    "history_sha256",
    "board_width",
    "board_height",
    "board_count",
    "timelines_total",
    "timelines_active",
    "timelines_inactive",
    "timelines_playable",
    "timelines_mandatory",
    "timelines_optional",
    "timelines_unplayable",
    "ln_hc_universe_volume",
    "ln_hc_non_new_volume",
    "legal_actions",
    "count_limit",
    "capped",
    "policy",
    "count_status",
    "count_error",
]

JOURNAL_FIELDS = [
    "history_sha256",
    "count_limit",
    "policy",
    "legal_actions",
    "status",
    "error",
    "elapsed_seconds",
]


@dataclass
class Game:
    game_id: str
    source_log: str
    run_id: str
    match_id: int
    pgn: str
    actions: list[str]
    final_stats: dict
    timeline_by_state: dict[int, int]
    game_status: str
    game_result: str
    winner: str
    engines: dict[str, str]
    commands: dict[str, str]
    movetimes: dict[str, str]
    warnings: list[str]


@dataclass(frozen=True)
class Position:
    game_index: int
    actions_played: int
    sample_reason: str
    history_sha256: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", type=Path, default=Path("logs/elo-runs"))
    parser.add_argument("--tool", type=Path, default=Path("build/5dtools"))
    parser.add_argument(
        "--output", type=Path, default=Path("logs/branching-autoplay.csv")
    )
    parser.add_argument(
        "--audit-output",
        type=Path,
        default=Path("logs/branching-autoplay-audit.json"),
    )
    parser.add_argument(
        "--journal",
        type=Path,
        default=Path("logs/branching-autoplay-count-journal.csv"),
    )
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--max", type=int, default=100001, dest="maximum")
    parser.add_argument("--recount-max", type=int, default=1000001)
    parser.add_argument("--policy", default="balanced")
    parser.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--stats-timeout", type=float, default=120.0)
    parser.add_argument("--count-timeout", type=float, default=900.0)
    parser.add_argument("--limit-games", type=int)
    parser.add_argument("--prepare-only", action="store_true")
    return parser.parse_args()


def turn_label(actions: int) -> str:
    if actions == 0:
        return "0"
    if actions % 2:
        return f"{(actions + 1) // 2}w"
    return f"{actions // 2}b"


def run_tool(
    tool: Path,
    pgn: str,
    arguments: list[str],
    timeout: float,
) -> dict:
    process = subprocess.run(
        [str(tool), *arguments],
        input=pgn,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )
    if process.returncode:
        message = process.stderr.strip() or process.stdout.strip()
        raise RuntimeError(f"exit {process.returncode}: {message}")
    return json.loads(process.stdout)


def extract_pgn(text: str) -> str:
    markers = [text.rfind("\n[Event "), text.rfind("\n[Timeline ")]
    start = max(markers)
    if start < 0:
        if text.startswith("[Event ") or text.startswith("[Timeline "):
            start = -1
        else:
            raise ValueError("no PGN header block found")
    pgn = text[start + 1 :]
    for trailer in ("\nTraceback (most recent call last):", "\nDuring handling of the above exception"):
        pgn = pgn.split(trailer, 1)[0]
    return pgn.strip() + "\n"


def action_history(text: str) -> list[str]:
    actions: list[str] = []
    for line in text.splitlines():
        match = ACTION_RE.match(line)
        if match:
            actions.append(match.group(4).strip())
    return actions


def classify_game(text: str) -> tuple[str, str, str]:
    result_lines = re.findall(r"^Result: (.*)$", text, re.MULTILINE)
    result = result_lines[-1] if result_lines else ""
    if result.startswith("white(") and " wins" in result:
        return "complete", "white_win", "white"
    if result.startswith("black(") and " wins" in result:
        return "complete", "black_win", "black"
    if result.startswith("draw: action limit reached"):
        return "complete", "draw_action_limit", ""
    if result.startswith("draw: stalemate"):
        return "complete", "draw_stalemate", ""
    if result.startswith("protocol failure"):
        return "protocol_failure", "unknown", ""
    if result.startswith("no result"):
        return "adjudication_error", "unknown", ""
    if "Interrupted:" in text:
        return "interrupted", "unknown", ""
    return "incomplete", "unknown", ""


def read_metrics(path: Path) -> tuple[
    dict[int, int], dict[str, str], dict[str, str], dict[str, str], list[str]
]:
    warnings: list[str] = []
    if not path.exists():
        return {}, {}, {}, {}, ["missing go-metrics.csv"]
    with path.open(newline="") as source:
        rows = list(csv.DictReader(source))
    timelines: dict[int, int] = {}
    engines: dict[str, str] = {}
    commands: dict[str, str] = {}
    movetimes: dict[str, str] = {}
    for row in rows:
        try:
            state = int(row["action"]) - 1
            timelines[state] = int(row["timeline_count"])
        except (KeyError, TypeError, ValueError):
            warnings.append("invalid timeline row in go-metrics.csv")
        color = row.get("color", "")
        if color in ("white", "black"):
            engines.setdefault(color, row.get("engine_name", ""))
            commands.setdefault(color, row.get("engine_command", ""))
            movetimes.setdefault(color, row.get("requested_movetime_ms", ""))
    return timelines, engines, commands, movetimes, warnings


def load_game(root: Path, log: Path, tool: Path, timeout: float) -> Game:
    relative = log.relative_to(Path.cwd()) if log.is_relative_to(Path.cwd()) else log
    text = log.read_text(errors="replace")
    pgn = extract_pgn(text)
    final = run_tool(tool, pgn, ["stats", "--format", "json"], timeout)
    actions = action_history(text)
    warnings: list[str] = []
    if len(actions) != int(final["actions_played"]):
        warnings.append(
            f"action log has {len(actions)} actions; PGN has {final['actions_played']}"
        )
    timelines, engines, commands, movetimes, metric_warnings = read_metrics(
        log.with_name("go-metrics.csv")
    )
    warnings.extend(metric_warnings)
    timelines[int(final["actions_played"])] = int(final["timelines"]["total"])
    match = MATCH_RE.match(log.parent.name)
    if not match:
        raise ValueError("parent directory is not match-NNNNNN")
    run_id = log.parent.parent.name
    match_id = int(match.group(1))
    status, result, winner = classify_game(text)
    return Game(
        game_id=f"{run_id}/{log.parent.name}",
        source_log=str(relative),
        run_id=run_id,
        match_id=match_id,
        pgn=pgn,
        actions=actions,
        final_stats=final,
        timeline_by_state=timelines,
        game_status=status,
        game_result=result,
        winner=winner,
        engines=engines,
        commands=commands,
        movetimes=movetimes,
        warnings=warnings,
    )


def sampling_reasons(game: Game, stride: int) -> dict[int, set[str]]:
    final = int(game.final_stats["actions_played"])
    reasons: dict[int, set[str]] = {}

    def add(state: int, reason: str) -> None:
        if 0 <= state <= final:
            reasons.setdefault(state, set()).add(reason)

    add(0, "initial")
    for state in range(0, final + 1, stride):
        add(state, "stride")
    add(final, "final")
    for state in range(1, final + 1):
        before = game.timeline_by_state.get(state - 1)
        after = game.timeline_by_state.get(state)
        if before is not None and after is not None and before != after:
            add(state - 1, "before_timeline_change")
            add(state, "after_timeline_change")
            add(state + 1, "after_timeline_change")
    return reasons


def history_hash(game: Game, actions: int) -> str:
    if len(game.actions) == int(game.final_stats["actions_played"]):
        history = "standard-8x8-odd\n" + "\nsubmit\n".join(game.actions[:actions])
    else:
        history = f"{game.source_log}\n{actions}"
    return hashlib.sha256(history.encode()).hexdigest()


def parallel_map(
    label: str,
    items: list[T],
    workers: int,
    function: Callable[[T], dict],
) -> dict[T, dict]:
    results: dict[T, dict] = {}
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures: dict[Future[dict], T] = {
            executor.submit(function, item): item for item in items
        }
        completed = 0
        for future in as_completed(futures):
            item = futures[future]
            completed += 1
            try:
                results[item] = {"status": "ok", "value": future.result()}
            except subprocess.TimeoutExpired as error:
                results[item] = {
                    "status": "timeout",
                    "error": f"exceeded {error.timeout:g}s",
                }
            except (RuntimeError, json.JSONDecodeError, KeyError, ValueError) as error:
                results[item] = {"status": "error", "error": str(error)}
            if completed == 1 or completed % 100 == 0 or completed == len(items):
                print(
                    f"{label}: {completed}/{len(items)}",
                    file=sys.stderr,
                    flush=True,
                )
    return results


def read_journal(path: Path) -> dict[tuple[str, int, str], dict[str, str]]:
    results: dict[tuple[str, int, str], dict[str, str]] = {}
    if not path.exists():
        return results
    with path.open(newline="") as source:
        for row in csv.DictReader(source):
            if row["status"] == "ok":
                results[(row["history_sha256"], int(row["count_limit"]), row["policy"])] = row
    return results


def append_journal(path: Path, rows: Iterable[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    exists = path.exists() and path.stat().st_size > 0
    with path.open("a", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=JOURNAL_FIELDS)
        if not exists:
            writer.writeheader()
        for row in rows:
            writer.writerow(row)
            output.flush()


def count_stage(
    positions_by_hash: dict[str, tuple[Game, int]],
    hashes: list[str],
    maximum: int,
    args: argparse.Namespace,
    journal: dict[tuple[str, int, str], dict[str, str]],
) -> dict[str, dict[str, str]]:
    completed: dict[str, dict[str, str]] = {}
    pending: list[str] = []
    for digest in hashes:
        key = (digest, maximum, args.policy)
        if key in journal:
            completed[digest] = journal[key]
        else:
            pending.append(digest)
    print(
        f"count limit {maximum}: {len(completed)} resumed, {len(pending)} pending",
        file=sys.stderr,
        flush=True,
    )

    def count_one(digest: str) -> dict[str, str]:
        game, actions = positions_by_hash[digest]
        started = time.monotonic()
        try:
            data = run_tool(
                args.tool,
                game.pgn,
                [
                    "count",
                    "--at",
                    turn_label(actions),
                    "--format",
                    "json",
                    args.policy,
                    str(maximum),
                ],
                args.count_timeout,
            )
            count = int(data["legal_actions"])
        except subprocess.TimeoutExpired:
            return {
                "history_sha256": digest,
                "count_limit": str(maximum),
                "policy": args.policy,
                "legal_actions": "",
                "status": "timeout",
                "error": f"exceeded {args.count_timeout:g}s",
                "elapsed_seconds": f"{time.monotonic() - started:.6f}",
            }
        except (RuntimeError, json.JSONDecodeError, KeyError, ValueError) as error:
            return {
                "history_sha256": digest,
                "count_limit": str(maximum),
                "policy": args.policy,
                "legal_actions": "",
                "status": "error",
                "error": str(error),
                "elapsed_seconds": f"{time.monotonic() - started:.6f}",
            }
        return {
            "history_sha256": digest,
            "count_limit": str(maximum),
            "policy": args.policy,
            "legal_actions": str(count),
            "status": "ok",
            "error": "",
            "elapsed_seconds": f"{time.monotonic() - started:.6f}",
        }

    with ThreadPoolExecutor(max_workers=args.workers) as executor:
        futures = {executor.submit(count_one, digest): digest for digest in pending}
        done = 0
        for future in as_completed(futures):
            digest = futures[future]
            result = future.result()
            append_journal(args.journal, [result])
            if result["status"] == "ok":
                completed[digest] = result
                journal[(digest, maximum, args.policy)] = result
            done += 1
            if done == 1 or done % 100 == 0 or done == len(pending):
                print(
                    f"count limit {maximum}: {done}/{len(pending)} new",
                    file=sys.stderr,
                    flush=True,
                )
    return completed


def flatten(game: Game, position: Position, stats: dict) -> dict[str, object]:
    timelines = stats["timelines"]
    hc = stats["hc"]
    side = stats["side_to_move"]
    return {
        "game_id": game.game_id,
        "source_log": game.source_log,
        "run_id": game.run_id,
        "match_id": game.match_id,
        "game_status": game.game_status,
        "game_result": game.game_result,
        "winner": game.winner,
        "white_engine": game.engines.get("white", ""),
        "white_command": game.commands.get("white", ""),
        "black_engine": game.engines.get("black", ""),
        "black_command": game.commands.get("black", ""),
        "engine_to_move": game.engines.get(side, ""),
        "requested_movetime_ms": game.movetimes.get(side, ""),
        "turn": turn_label(position.actions_played),
        "actions_played": stats["actions_played"],
        "side_to_move": side,
        "sample_reason": "|".join(sorted(position.sample_reason.split("|"))),
        "history_sha256": position.history_sha256,
        "board_width": stats["board_width"],
        "board_height": stats["board_height"],
        "board_count": stats["board_count"],
        "timelines_total": timelines["total"],
        "timelines_active": timelines["active"],
        "timelines_inactive": timelines["inactive"],
        "timelines_playable": timelines["playable"],
        "timelines_mandatory": timelines["mandatory"],
        "timelines_optional": timelines["optional"],
        "timelines_unplayable": timelines["unplayable"],
        "ln_hc_universe_volume": hc["ln_universe_volume"],
        "ln_hc_non_new_volume": hc["ln_non_new_volume"],
    }


def atomic_write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", newline="", dir=path.parent, prefix=f".{path.name}.", delete=False
    ) as output:
        temporary = Path(output.name)
        writer = csv.DictWriter(output, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary, path)


def main() -> int:
    args = parse_args()
    if args.stride <= 0 or args.maximum <= 0 or args.recount_max <= args.maximum:
        raise SystemExit("stride/max must be positive and --recount-max must exceed --max")
    if args.workers <= 0:
        raise SystemExit("--workers must be positive")

    logs = sorted(args.logs.glob("*/match-*/autoplay.log"))
    if args.limit_games is not None:
        logs = logs[: args.limit_games]
    games: list[Game] = []
    rejected: list[dict[str, str]] = []
    for index, log in enumerate(logs, start=1):
        try:
            game = load_game(args.logs, log, args.tool, args.stats_timeout)
        except (OSError, ValueError, RuntimeError, json.JSONDecodeError, subprocess.TimeoutExpired) as error:
            rejected.append({"source_log": str(log), "error": str(error)})
        else:
            games.append(game)
        if index == 1 or index % 25 == 0 or index == len(logs):
            print(f"audit: {index}/{len(logs)}", file=sys.stderr, flush=True)

    positions: list[Position] = []
    for game_index, game in enumerate(games):
        for actions, reasons in sampling_reasons(game, args.stride).items():
            positions.append(
                Position(
                    game_index=game_index,
                    actions_played=actions,
                    sample_reason="|".join(sorted(reasons)),
                    history_sha256=history_hash(game, actions),
                )
            )
    positions.sort(key=lambda item: (games[item.game_index].game_id, item.actions_played))

    positions_by_hash: dict[str, tuple[Game, int]] = {}
    for position in positions:
        positions_by_hash.setdefault(
            position.history_sha256,
            (games[position.game_index], position.actions_played),
        )

    audit = {
        "source": str(args.logs),
        "discovered_logs": len(logs),
        "accepted_games": len(games),
        "rejected_games": rejected,
        "sampled_rows": len(positions),
        "unique_histories": len(positions_by_hash),
        "stride": args.stride,
        "games": [
            {
                "game_id": game.game_id,
                "source_log": game.source_log,
                "actions_played": game.final_stats["actions_played"],
                "game_status": game.game_status,
                "game_result": game.game_result,
                "samples": sum(p.game_index == index for p in positions),
                "warnings": game.warnings,
            }
            for index, game in enumerate(games)
        ],
    }
    args.audit_output.parent.mkdir(parents=True, exist_ok=True)
    args.audit_output.write_text(json.dumps(audit, indent=2) + "\n")

    stats_items = list(positions_by_hash)

    def calculate_stats(digest: str) -> dict:
        game, actions = positions_by_hash[digest]
        if actions == int(game.final_stats["actions_played"]):
            return game.final_stats
        return run_tool(
            args.tool,
            game.pgn,
            ["stats", "--at", turn_label(actions), "--format", "json"],
            args.stats_timeout,
        )

    stats_results = parallel_map("stats", stats_items, args.workers, calculate_stats)
    stats_failures = {
        digest: result for digest, result in stats_results.items() if result["status"] != "ok"
    }
    if stats_failures:
        print(f"stats failures: {len(stats_failures)}", file=sys.stderr)
    if args.prepare_only:
        print(json.dumps(audit))
        return 0

    countable_hashes = [
        digest for digest, result in stats_results.items() if result["status"] == "ok"
    ]
    journal = read_journal(args.journal)
    first = count_stage(
        positions_by_hash, countable_hashes, args.maximum, args, journal
    )
    capped = [
        digest
        for digest, result in first.items()
        if int(result["legal_actions"]) == args.maximum
    ]
    second = count_stage(
        positions_by_hash, capped, args.recount_max, args, journal
    )

    rows: list[dict[str, object]] = []
    for position in positions:
        game = games[position.game_index]
        stats_result = stats_results[position.history_sha256]
        if stats_result["status"] != "ok":
            continue
        row = flatten(game, position, stats_result["value"])
        count = second.get(position.history_sha256) or first.get(position.history_sha256)
        if count is None:
            row.update(
                legal_actions="",
                count_limit="",
                capped="",
                policy=args.policy,
                count_status="error",
                count_error="count failed; see journal",
            )
        else:
            maximum = int(count["count_limit"])
            legal_actions = int(count["legal_actions"])
            row.update(
                legal_actions=legal_actions,
                count_limit=maximum,
                capped=str(legal_actions == maximum).lower(),
                policy=args.policy,
                count_status="ok",
                count_error="",
            )
        rows.append(row)
    atomic_write_csv(args.output, rows)

    summary = {
        "discovered_logs": len(logs),
        "accepted_games": len(games),
        "rejected_games": len(rejected),
        "rows": len(rows),
        "unique_histories": len(positions_by_hash),
        "stats_failures": len(stats_failures),
        "count_failures": sum(row["count_status"] != "ok" for row in rows),
        "recounted": len(capped),
        "still_capped": sum(
            int(result["legal_actions"]) == args.recount_max for result in second.values()
        ),
        "output": str(args.output),
        "audit_output": str(args.audit_output),
        "journal": str(args.journal),
    }
    print(json.dumps(summary))
    return 1 if stats_failures or summary["count_failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
