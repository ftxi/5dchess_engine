#!/usr/bin/env python3
"""Recount capped branching observations at a higher enumeration limit."""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
import time
import tomllib
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("logs/branching-human.csv"))
    parser.add_argument("--games", type=Path, default=Path("logs/games.toml"))
    parser.add_argument("--tool", type=Path, default=Path("build/5dtools"))
    parser.add_argument(
        "--output", type=Path, default=Path("logs/branching-human-100001.csv")
    )
    parser.add_argument(
        "--recounts",
        type=Path,
        default=Path("logs/branching-human-recount-100001.csv"),
    )
    parser.add_argument("--max", type=int, default=100001, dest="maximum")
    parser.add_argument("--timeout", type=float, default=300.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    with args.input.open(newline="") as source:
        reader = csv.DictReader(source)
        rows = list(reader)
        fieldnames = reader.fieldnames
    if fieldnames is None:
        raise SystemExit("input CSV has no header")
    games = tomllib.loads(args.games.read_text())["decks"]["5dc"]
    capped_rows = [row for row in rows if row["capped"].lower() == "true"]
    if not capped_rows:
        raise SystemExit("input CSV has no capped rows")
    for row in capped_rows:
        old_limit = int(row["count_limit"])
        if int(row["legal_actions"]) != old_limit:
            raise SystemExit(
                f"capped row {row['game_index']}:{row['turn']} does not equal "
                "its count limit"
            )
        if args.maximum <= old_limit:
            raise SystemExit(
                f"--max must exceed the count limit for {row['game_index']}:{row['turn']}"
            )
    recount_fields = [
        "game_index",
        "turn",
        "old_limit",
        "new_limit",
        "legal_actions",
        "capped",
        "status",
        "error",
        "elapsed_seconds",
    ]
    results: dict[tuple[str, str], dict[str, str]] = {}
    args.recounts.parent.mkdir(parents=True, exist_ok=True)
    with args.recounts.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=recount_fields)
        writer.writeheader()
        for index, row in enumerate(capped_rows, start=1):
            game_index = int(row["game_index"])
            old_limit = int(row["count_limit"])
            pgn = '[Board "Standard"]\n\n' + games[game_index]
            command = [
                str(args.tool),
                "count",
                "--at",
                row["turn"],
                "--format",
                "json",
                row["policy"],
                str(args.maximum),
            ]
            print(
                f"recount {index}/{len(capped_rows)}: game {game_index}, turn {row['turn']}",
                file=sys.stderr,
                flush=True,
            )
            started = time.monotonic()
            try:
                process = subprocess.run(
                    command,
                    input=pgn,
                    text=True,
                    capture_output=True,
                    timeout=args.timeout,
                    check=False,
                )
                elapsed = time.monotonic() - started
                if process.returncode:
                    raise RuntimeError(
                        process.stderr.strip()
                        or process.stdout.strip()
                        or f"exit {process.returncode}"
                    )
                count = int(json.loads(process.stdout)["legal_actions"])
            except subprocess.TimeoutExpired:
                result = {
                    "game_index": str(game_index),
                    "turn": row["turn"],
                    "old_limit": str(old_limit),
                    "new_limit": str(args.maximum),
                    "legal_actions": "",
                    "capped": "",
                    "status": "timeout",
                    "error": f"exceeded {args.timeout:g}s",
                    "elapsed_seconds": f"{args.timeout:.6f}",
                }
            except (RuntimeError, json.JSONDecodeError, KeyError, ValueError) as error:
                result = {
                    "game_index": str(game_index),
                    "turn": row["turn"],
                    "old_limit": str(old_limit),
                    "new_limit": str(args.maximum),
                    "legal_actions": "",
                    "capped": "",
                    "status": "error",
                    "error": str(error),
                    "elapsed_seconds": f"{time.monotonic() - started:.6f}",
                }
            else:
                result = {
                    "game_index": str(game_index),
                    "turn": row["turn"],
                    "old_limit": str(old_limit),
                    "new_limit": str(args.maximum),
                    "legal_actions": str(count),
                    "capped": str(count == args.maximum).lower(),
                    "status": "ok",
                    "error": "",
                    "elapsed_seconds": f"{elapsed:.6f}",
                }
            results[(str(game_index), row["turn"])] = result
            writer.writerow(result)
            output.flush()

    failures = [result for result in results.values() if result["status"] != "ok"]
    successful = [result for result in results.values() if result["status"] == "ok"]
    summary = {
        "requested": len(capped_rows),
        "successful": len(successful),
        "still_capped": sum(result["capped"] == "true" for result in successful),
        "timeouts": sum(result["status"] == "timeout" for result in results.values()),
        "errors": sum(result["status"] == "error" for result in results.values()),
        "output": str(args.output),
        "recounts": str(args.recounts),
    }
    if failures:
        summary["output"] = None
        print(json.dumps(summary))
        return 1

    for row in rows:
        key = (row["game_index"], row["turn"])
        if key not in results:
            continue
        result = results[key]
        row["legal_actions"] = result["legal_actions"]
        row["count_limit"] = str(args.maximum)
        row["capped"] = result["capped"]
        row["count_status"] = "ok"
        row["count_error"] = ""

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", newline="", dir=args.output.parent, prefix=f".{args.output.name}.", delete=False
    ) as output:
        temporary_output = Path(output.name)
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary_output, args.output)

    print(json.dumps(summary))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
