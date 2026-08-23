#!/usr/bin/env python3
"""Build paired White/Black branching samples and refit the space heuristic."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import tempfile
import tomllib
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace

import numpy as np

from branching_autoplay_experiment import (
    Game,
    count_stage,
    extract_pgn,
    history_hash,
    parallel_map,
    read_journal,
    run_tool,
    turn_label,
)


FIELDS = [
    "source_type",
    "game_id",
    "pair_id",
    "pair_member",
    "source_log",
    "turn",
    "actions_played",
    "side_to_move",
    "position_key",
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
    "count_origin",
]


@dataclass(frozen=True)
class Target:
    source_type: str
    game_id: str
    pair_id: str
    source_log: str
    actions_played: int
    digest: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--human", type=Path, default=Path("logs/branching-human.csv"))
    parser.add_argument(
        "--autoplay", type=Path, default=Path("logs/branching-autoplay.csv")
    )
    parser.add_argument("--games", type=Path, default=Path("logs/games.toml"))
    parser.add_argument("--tool", type=Path, default=Path("build/5dtools"))
    parser.add_argument(
        "--output", type=Path, default=Path("logs/branching-color-balanced.csv")
    )
    parser.add_argument(
        "--journal",
        type=Path,
        default=Path("logs/branching-color-balanced-count-journal.csv"),
    )
    parser.add_argument(
        "--model-output",
        type=Path,
        default=Path("logs/branching-color-balanced-model.json"),
    )
    parser.add_argument("--max", type=int, default=100001, dest="maximum")
    parser.add_argument("--recount-max", type=int, default=1000001)
    parser.add_argument("--policy", default="balanced")
    parser.add_argument("--workers", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--stats-timeout", type=float, default=120.0)
    parser.add_argument("--count-timeout", type=float, default=900.0)
    return parser.parse_args()


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as source:
        return list(csv.DictReader(source))


def synthetic_game(game_id: str, source_log: str, pgn: str) -> Game:
    return Game(
        game_id=game_id,
        source_log=source_log,
        run_id="",
        match_id=0,
        pgn=pgn,
        actions=[],
        final_stats={},
        timeline_by_state={},
        game_status="",
        game_result="",
        winner="",
        engines={},
        commands={},
        movetimes={},
        warnings=[],
    )


def stable_key(source_type: str, game_id: str, actions: int) -> str:
    return hashlib.sha256(f"{source_type}\n{game_id}\n{actions}".encode()).hexdigest()


def paired_white_rows(rows: list[dict[str, str]], game_field: str) -> list[dict[str, str]]:
    groups: dict[str, list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[row[game_field]].append(row)
    selected: list[dict[str, str]] = []
    for group in groups.values():
        final = max(int(row["actions_played"]) for row in group)
        by_action = {int(row["actions_played"]): row for row in group}
        for actions in range(0, final, 4):
            if actions in by_action and actions + 1 <= final:
                selected.append(by_action[actions])
    return selected


def common_row(
    source_type: str,
    game_id: str,
    pair_id: str,
    pair_member: str,
    source_log: str,
    position_key: str,
    data: dict,
) -> dict[str, object]:
    return {
        "source_type": source_type,
        "game_id": game_id,
        "pair_id": pair_id,
        "pair_member": pair_member,
        "source_log": source_log,
        "turn": data["turn"],
        "actions_played": data["actions_played"],
        "side_to_move": data["side_to_move"],
        "position_key": position_key,
        "board_width": data["board_width"],
        "board_height": data["board_height"],
        "board_count": data["board_count"],
        "timelines_total": data["timelines_total"],
        "timelines_active": data["timelines_active"],
        "timelines_inactive": data["timelines_inactive"],
        "timelines_playable": data["timelines_playable"],
        "timelines_mandatory": data["timelines_mandatory"],
        "timelines_optional": data["timelines_optional"],
        "timelines_unplayable": data["timelines_unplayable"],
        "ln_hc_universe_volume": data["ln_hc_universe_volume"],
        "ln_hc_non_new_volume": data["ln_hc_non_new_volume"],
        "legal_actions": data["legal_actions"],
        "count_limit": data["count_limit"],
        "capped": data["capped"],
        "policy": data["policy"],
        "count_status": data["count_status"],
        "count_error": data["count_error"],
        "count_origin": "existing",
    }


def flatten_stats(data: dict) -> dict[str, object]:
    timelines = data["timelines"]
    hc = data["hc"]
    return {
        "turn": turn_label(int(data["actions_played"])),
        "actions_played": data["actions_played"],
        "side_to_move": data["side_to_move"],
        "board_width": data["board_width"],
        "board_height": data["board_height"],
        "board_count": data["board_count"],
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


def atomic_write(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", newline="", dir=path.parent, prefix=f".{path.name}.", delete=False
    ) as output:
        temporary = Path(output.name)
        writer = csv.DictWriter(output, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary, path)


def fit(rows: list[dict[str, object]]) -> dict[str, object]:
    usable = [
        row
        for row in rows
        if int(row["legal_actions"]) > 0 and str(row["capped"]).lower() == "false"
    ]
    x = np.array([float(row["ln_hc_universe_volume"]) for row in usable])
    y = np.log(np.array([int(row["legal_actions"]) for row in usable]))
    matrix = np.column_stack([x, np.ones(len(x))])
    slope, intercept = np.linalg.lstsq(matrix, y, rcond=None)[0]
    prediction = slope * x + intercept
    residual = y - prediction
    tss = float(np.sum((y - np.mean(y)) ** 2))
    return {
        "positions": len(usable),
        "white": sum(row["side_to_move"] == "white" for row in usable),
        "black": sum(row["side_to_move"] == "black" for row in usable),
        "slope": float(slope),
        "intercept": float(intercept),
        "coefficient": float(math.exp(intercept)),
        "r_squared": 1.0 - float(np.sum(residual**2)) / tss,
        "rmse_log": float(np.sqrt(np.mean(residual**2))),
        "median_multiplicative_error": float(np.exp(np.median(np.abs(residual)))),
        "within_factor_two": float(np.mean(np.abs(residual) <= math.log(2))),
    }


def complete_usable_pairs(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    pairs: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        pairs[str(row["pair_id"])].append(row)
    return [
        row
        for pair in pairs.values()
        if len(pair) == 2
        and all(
            int(row["legal_actions"]) > 0
            and str(row["capped"]).lower() == "false"
            for row in pair
        )
        for row in pair
    ]


def evaluate(model: dict[str, object], rows: list[dict[str, object]]) -> dict[str, object]:
    usable = [
        row
        for row in rows
        if int(row["legal_actions"]) > 0 and str(row["capped"]).lower() == "false"
    ]
    x = np.array([float(row["ln_hc_universe_volume"]) for row in usable])
    y = np.log(np.array([int(row["legal_actions"]) for row in usable]))
    prediction = float(model["slope"]) * x + float(model["intercept"])
    residual = y - prediction
    tss = float(np.sum((y - np.mean(y)) ** 2))
    return {
        "positions": len(usable),
        "white": sum(row["side_to_move"] == "white" for row in usable),
        "black": sum(row["side_to_move"] == "black" for row in usable),
        "predictive_r_squared": 1.0 - float(np.sum(residual**2)) / tss,
        "rmse_log": float(np.sqrt(np.mean(residual**2))),
        "median_multiplicative_error": float(np.exp(np.median(np.abs(residual)))),
        "within_factor_two": float(np.mean(np.abs(residual) <= math.log(2))),
        "geometric_actual_over_prediction": float(np.exp(np.mean(residual))),
    }


def main() -> int:
    args = parse_args()
    human_rows = read_rows(args.human)
    autoplay_rows = read_rows(args.autoplay)
    human_white = paired_white_rows(human_rows, "game_index")
    autoplay_white = paired_white_rows(autoplay_rows, "game_id")

    games_toml = tomllib.loads(args.games.read_text())["decks"]["5dc"]
    game_objects: dict[tuple[str, str], Game] = {}
    for row in human_white:
        game_id = row["game_index"]
        key = ("human", game_id)
        if key not in game_objects:
            pgn = '[Board "Standard"]\n\n' + games_toml[int(game_id)]
            game_objects[key] = synthetic_game(
                f"human/{game_id}", f"logs/games.toml#5dc[{game_id}]", pgn
            )

    for row in autoplay_white:
        game_id = row["game_id"]
        key = ("autoplay", game_id)
        if key not in game_objects:
            source_log = Path(row["source_log"])
            text = source_log.read_text(errors="replace")
            pgn = extract_pgn(text)
            actions = []
            from branching_autoplay_experiment import action_history

            actions = action_history(text)
            game = synthetic_game(game_id, row["source_log"], pgn)
            game.actions = actions
            game.final_stats = {
                "actions_played": max(
                    int(item["actions_played"])
                    for item in autoplay_rows
                    if item["game_id"] == game_id
                )
            }
            game_objects[key] = game

    targets: list[Target] = []
    positions_by_hash: dict[str, tuple[Game, int]] = {}
    for source_type, white_rows, game_field in (
        ("human", human_white, "game_index"),
        ("autoplay", autoplay_white, "game_id"),
    ):
        for row in white_rows:
            game_id = row[game_field]
            game = game_objects[(source_type, game_id)]
            actions = int(row["actions_played"]) + 1
            pair_id = f"{source_type}/{game_id}/{row['actions_played']}"
            if source_type == "autoplay" and len(game.actions) >= actions:
                digest = history_hash(game, actions)
            else:
                digest = stable_key(source_type, game_id, actions)
            target = Target(
                source_type=source_type,
                game_id=game_id,
                pair_id=pair_id,
                source_log=game.source_log,
                actions_played=actions,
                digest=digest,
            )
            targets.append(target)
            positions_by_hash.setdefault(digest, (game, actions))

    unique_hashes = list(positions_by_hash)

    def calculate_stats(digest: str) -> dict:
        game, actions = positions_by_hash[digest]
        return run_tool(
            args.tool,
            game.pgn,
            ["stats", "--at", turn_label(actions), "--format", "json"],
            args.stats_timeout,
        )

    stats_results = parallel_map("black stats", unique_hashes, args.workers, calculate_stats)
    stats_failures = [r for r in stats_results.values() if r["status"] != "ok"]

    count_args = SimpleNamespace(
        tool=args.tool,
        policy=args.policy,
        count_timeout=args.count_timeout,
        workers=args.workers,
        journal=args.journal,
    )
    countable = [d for d in unique_hashes if stats_results[d]["status"] == "ok"]
    journal = read_journal(args.journal)
    first = count_stage(
        positions_by_hash, countable, args.maximum, count_args, journal
    )
    capped = [
        digest
        for digest, result in first.items()
        if int(result["legal_actions"]) == args.maximum
    ]
    second = count_stage(
        positions_by_hash, capped, args.recount_max, count_args, journal
    )

    target_by_pair = {target.pair_id: target for target in targets}
    output_rows: list[dict[str, object]] = []
    for source_type, white_rows, game_field in (
        ("human", human_white, "game_index"),
        ("autoplay", autoplay_white, "game_id"),
    ):
        for white in white_rows:
            game_id = white[game_field]
            pair_id = f"{source_type}/{game_id}/{white['actions_played']}"
            target = target_by_pair[pair_id]
            white_source = (
                f"logs/games.toml#5dc[{game_id}]"
                if source_type == "human"
                else white["source_log"]
            )
            output_rows.append(
                common_row(
                    source_type,
                    str(game_id),
                    pair_id,
                    "white",
                    white_source,
                    stable_key(source_type, str(game_id), int(white["actions_played"])),
                    white,
                )
            )

            stats_result = stats_results[target.digest]
            if stats_result["status"] != "ok":
                continue
            data = flatten_stats(stats_result["value"])
            count = second.get(target.digest) or first.get(target.digest)
            if count is None:
                data.update(
                    legal_actions="",
                    count_limit="",
                    capped="",
                    policy=args.policy,
                    count_status="error",
                    count_error="count failed; see journal",
                )
            else:
                legal_actions = int(count["legal_actions"])
                maximum = int(count["count_limit"])
                data.update(
                    legal_actions=legal_actions,
                    count_limit=maximum,
                    capped=str(legal_actions == maximum).lower(),
                    policy=args.policy,
                    count_status="ok",
                    count_error="",
                )
            black_row = common_row(
                source_type,
                str(game_id),
                pair_id,
                "black",
                target.source_log,
                stable_key(source_type, str(game_id), target.actions_played),
                data,
            )
            black_row["count_origin"] = "new"
            output_rows.append(black_row)

    output_rows.sort(
        key=lambda row: (
            row["source_type"],
            row["game_id"],
            row["pair_id"],
            int(row["actions_played"]),
        )
    )
    atomic_write(args.output, output_rows)

    autoplay_balanced = [row for row in output_rows if row["source_type"] == "autoplay"]
    human_balanced = [row for row in output_rows if row["source_type"] == "human"]
    autoplay_usable = complete_usable_pairs(autoplay_balanced)
    human_usable = complete_usable_pairs(human_balanced)
    autoplay_model = fit(autoplay_usable)
    model = {
        "response": "ln(legal_actions)",
        "predictor": "ln_hc_universe_volume",
        "filter": "White/Black 4k,4k+1 pairs where both counts are nonzero and uncapped",
        "autoplay_fit": autoplay_model,
        "autoplay_by_side": {
            side: fit([row for row in autoplay_usable if row["side_to_move"] == side])
            for side in ("white", "black")
        },
        "human_fit": fit(human_usable),
        "human_by_side": {
            side: fit([row for row in human_usable if row["side_to_move"] == side])
            for side in ("white", "black")
        },
        "autoplay_model_on_human": evaluate(autoplay_model, human_usable),
    }
    args.model_output.parent.mkdir(parents=True, exist_ok=True)
    args.model_output.write_text(json.dumps(model, indent=2) + "\n")

    summary = {
        "rows": len(output_rows),
        "pairs": len(output_rows) // 2,
        "white_rows": sum(row["side_to_move"] == "white" for row in output_rows),
        "black_rows": sum(row["side_to_move"] == "black" for row in output_rows),
        "new_black_counts": len(targets),
        "unique_new_black_histories": len(unique_hashes),
        "stats_failures": len(stats_failures),
        "count_failures": sum(row["count_status"] != "ok" for row in output_rows),
        "recounted": len(capped),
        "still_capped_new_black": sum(
            int(result["legal_actions"]) == args.recount_max for result in second.values()
        ),
        "output": str(args.output),
        "model_output": str(args.model_output),
    }
    print(json.dumps(summary))
    return 1 if summary["stats_failures"] or summary["count_failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
