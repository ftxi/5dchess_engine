#!/usr/bin/env python3
"""Measure legal-action counts at sampled positions in human 5D chess games."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tomllib
from pathlib import Path


FIELDS = [
    "game_index",
    "turn",
    "actions_played",
    "side_to_move",
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
    timeout: float | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(tool), *arguments],
        input=pgn,
        text=True,
        capture_output=True,
        timeout=timeout,
        check=False,
    )


def read_json_result(process: subprocess.CompletedProcess[str]) -> dict:
    if process.returncode:
        message = process.stderr.strip() or process.stdout.strip()
        raise RuntimeError(f"exit {process.returncode}: {message}")
    return json.loads(process.stdout)


def stats_at(tool: Path, pgn: str, actions: int) -> dict:
    process = run_tool(
        tool,
        pgn,
        ["stats", "--at", turn_label(actions), "--format", "json"],
    )
    return read_json_result(process)


def final_stats(tool: Path, pgn: str) -> dict:
    return read_json_result(run_tool(tool, pgn, ["stats", "--format", "json"]))


def selected_actions(stats: list[dict], stride: int) -> list[int]:
    final = len(stats) - 1
    selected = set(range(0, final + 1, stride))
    selected.add(final)
    for actions in range(1, final + 1):
        before = stats[actions - 1]["timelines"]["total"]
        after = stats[actions]["timelines"]["total"]
        if before != after:
            selected.update((actions - 1, actions, actions + 1))
    return sorted(actions for actions in selected if 0 <= actions <= final)


def flatten_stats(game_index: int, actions: int, data: dict) -> dict:
    timelines = data["timelines"]
    hc = data["hc"]
    return {
        "game_index": game_index,
        "turn": turn_label(actions),
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


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--games", type=Path, default=Path("logs/games.toml"))
    parser.add_argument("--tool", type=Path, default=Path("build/5dtools"))
    parser.add_argument(
        "--output", type=Path, default=Path("logs/branching-human.csv")
    )
    parser.add_argument(
        "--audit-output", type=Path, default=Path("logs/branching-human-audit.json")
    )
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--max", type=int, default=10001, dest="maximum")
    parser.add_argument("--policy", default="balanced")
    parser.add_argument("--count-timeout", type=float, default=120.0)
    parser.add_argument("--limit-games", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.stride <= 0 or args.maximum <= 0:
        raise SystemExit("--stride and --max must be positive")

    games = tomllib.loads(args.games.read_text())["decks"]["5dc"]
    audit: dict[str, object] = {
        "source": str(args.games),
        "deck": "5dc",
        "board": "Standard",
        "total_games": len(games),
        "accepted": [],
        "rejected": [],
    }
    accepted: list[tuple[int, str, dict]] = []
    for game_index, movetext in enumerate(games):
        pgn = '[Board "Standard"]\n\n' + movetext
        try:
            data = final_stats(args.tool, pgn)
        except (RuntimeError, json.JSONDecodeError) as error:
            audit["rejected"].append({"game_index": game_index, "error": str(error)})
        else:
            audit["accepted"].append(
                {
                    "game_index": game_index,
                    "actions_played": data["actions_played"],
                    "final_timelines": data["timelines"]["total"],
                }
            )
            accepted.append((game_index, pgn, data))

    if args.limit_games is not None:
        accepted = accepted[: args.limit_games]
    args.audit_output.parent.mkdir(parents=True, exist_ok=True)
    args.audit_output.write_text(json.dumps(audit, indent=2) + "\n")
    args.output.parent.mkdir(parents=True, exist_ok=True)

    written = 0
    timeouts = 0
    with args.output.open("w", newline="") as output_file:
        writer = csv.DictWriter(output_file, fieldnames=FIELDS)
        writer.writeheader()
        for accepted_index, (game_index, pgn, last) in enumerate(accepted, start=1):
            final_actions = last["actions_played"]
            all_stats = []
            try:
                for actions in range(final_actions):
                    all_stats.append(stats_at(args.tool, pgn, actions))
            except (RuntimeError, json.JSONDecodeError) as error:
                print(
                    f"game {game_index}: intermediate stats failed: {error}",
                    file=sys.stderr,
                    flush=True,
                )
                continue
            all_stats.append(last)
            samples = selected_actions(all_stats, args.stride)
            print(
                f"game {accepted_index}/{len(accepted)} (source {game_index}): "
                f"{final_actions} actions, {len(samples)} samples",
                file=sys.stderr,
                flush=True,
            )
            for actions in samples:
                row = flatten_stats(game_index, actions, all_stats[actions])
                try:
                    process = run_tool(
                        args.tool,
                        pgn,
                        [
                            "count",
                            "--at",
                            turn_label(actions),
                            "--format",
                            "json",
                            args.policy,
                            str(args.maximum),
                        ],
                        timeout=args.count_timeout,
                    )
                    count = read_json_result(process)["legal_actions"]
                except subprocess.TimeoutExpired:
                    row.update(
                        legal_actions="",
                        count_limit=args.maximum,
                        capped="",
                        policy=args.policy,
                        count_status="timeout",
                        count_error=f"exceeded {args.count_timeout:g}s",
                    )
                    timeouts += 1
                except (RuntimeError, json.JSONDecodeError) as error:
                    row.update(
                        legal_actions="",
                        count_limit=args.maximum,
                        capped="",
                        policy=args.policy,
                        count_status="error",
                        count_error=str(error),
                    )
                else:
                    row.update(
                        legal_actions=count,
                        count_limit=args.maximum,
                        capped=str(count == args.maximum).lower(),
                        policy=args.policy,
                        count_status="ok",
                        count_error="",
                    )
                writer.writerow(row)
                output_file.flush()
                written += 1

    print(
        json.dumps(
            {
                "accepted_games": len(accepted),
                "rejected_games": len(audit["rejected"]),
                "rows": written,
                "timeouts": timeouts,
                "output": str(args.output),
                "audit_output": str(args.audit_output),
            }
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
