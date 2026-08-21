#!/usr/bin/env python3
"""Automatically schedule and concurrently run Elo matches through autoplay."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import os
import sys
import traceback
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from types import SimpleNamespace
from typing import Sequence

import autoplay
import elo


@dataclass(frozen=True)
class WorkerJob:
    match_id: int
    white_id: str
    black_id: str
    white_name: str
    black_name: str
    white_command: str
    black_command: str
    white_rating: float
    black_rating: float
    module_dir: str
    movetime: int
    timeout: int
    max_actions: int
    game_file: str | None
    game_text: str | None
    match_dir: str


@dataclass(frozen=True)
class WorkerResult:
    match_id: int
    outcome: str
    summary: str
    pgn: str
    output_log: str
    metrics_csv: str


def _play_worker(job: WorkerJob) -> WorkerResult:
    """Run one autoplay game in an isolated process and return structured output."""

    match_dir = Path(job.match_dir)
    match_dir.mkdir(parents=True, exist_ok=True)
    output_log = (match_dir / "autoplay.log").resolve()
    metrics_csv = (match_dir / "go-metrics.csv").resolve()
    capture: dict[str, str] = {}
    outcome = "error"
    summary = "worker failed before autoplay returned a result"
    pgn = ""
    with output_log.open("w") as output, open(os.devnull) as null_input:
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            original_stdin = sys.stdin
            sys.stdin = null_input
            try:
                arguments = SimpleNamespace(
                    white=job.white_command,
                    black=job.black_command,
                    white_name=job.white_name,
                    black_name=job.black_name,
                    game_file=Path(job.game_file) if job.game_file else None,
                    game_text=job.game_text,
                    module_dir=Path(job.module_dir),
                    movetime=job.movetime,
                    timeout=job.timeout,
                    max_actions=job.max_actions,
                    games=None,
                    metrics_csv=metrics_csv,
                    log_dir=match_dir,
                )
                autoplay.initialize_metrics(metrics_csv)
                rules = autoplay.load_rules(arguments.module_dir)
                outcome = asyncio.run(
                    autoplay.play(
                        arguments, rules, game_number=job.match_id, capture=capture
                    )
                )
                summary = capture.get("summary", outcome)
                pgn = capture.get("pgn", "")
            except (
                BaseException
            ) as exc:  # A worker must turn even SystemExit into a report.
                summary = f"worker exception: {type(exc).__name__}: {exc}"
                traceback.print_exc()
            finally:
                sys.stdin = original_stdin
    return WorkerResult(
        match_id=job.match_id,
        outcome=outcome,
        summary=summary,
        pgn=pgn,
        output_log=str(output_log),
        metrics_csv=str(metrics_csv),
    )


def _reported_result(result: WorkerResult) -> tuple[str, str]:
    if result.outcome in {"white", "black", "draw"}:
        return result.outcome, ""
    if result.outcome == "cap":
        return "draw", "action limit reached"
    return "void", result.summary or result.outcome


def _print_completed(
    pairing: elo.Pairing, result: WorkerResult, completed: int, total: int
) -> None:
    print()
    print(
        f"{'=' * 18} Match {pairing.match_id} finished ({completed}/{total}) {'=' * 18}"
    )
    print(
        f"White:  {pairing.white_name} [{pairing.white_id}]  {pairing.white_rating:.1f}"
    )
    print(
        f"Black:  {pairing.black_name} [{pairing.black_id}]  {pairing.black_rating:.1f}"
    )
    print(f"Result: {result.summary}")
    print("Elo:    pending completion of this concurrent batch")
    print(f"Log:    {result.output_log}")
    print(f"Metrics:{' ' if result.metrics_csv else ''}{result.metrics_csv}")
    print("\nPGN:\n")
    print(result.pgn.rstrip() if result.pgn.strip() else "<no PGN was produced>")
    print(f"{'=' * 72}", flush=True)


def _print_batch_ratings(
    connection, batch_id: int, pairings: Sequence[elo.Pairing], rows
) -> None:
    by_match = {row["id"]: row for row in rows}
    print(f"\nBatch {batch_id} rating update:")
    for pairing in pairings:
        row = by_match[pairing.match_id]
        if row["status"] == "void":
            print(f"  Match {pairing.match_id}: void; no rating change")
            continue
        white_delta = float(row["white_delta"] or 0.0)
        black_delta = float(row["black_delta"] or 0.0)
        print(
            f"  Match {pairing.match_id}: {pairing.white_id} {white_delta:+.2f}, "
            f"{pairing.black_id} {black_delta:+.2f}"
        )
    print("\nLeaderboard:")
    elo._print_leaderboard(elo.leaderboard(connection))
    print(flush=True)


def _make_job(pairing: elo.Pairing, args, run_dir: Path) -> WorkerJob:
    assert pairing.match_id is not None
    match_dir = run_dir / f"match-{pairing.match_id:06d}"
    return WorkerJob(
        match_id=pairing.match_id,
        white_id=pairing.white_id,
        black_id=pairing.black_id,
        white_name=pairing.white_name,
        black_name=pairing.black_name,
        white_command=pairing.white_command,
        black_command=pairing.black_command,
        white_rating=pairing.white_rating,
        black_rating=pairing.black_rating,
        module_dir=str(args.module_dir.resolve()),
        movetime=args.movetime,
        timeout=args.timeout,
        max_actions=args.max_actions,
        game_file=str(args.game_file.resolve()) if args.game_file else None,
        game_text=args.game_text,
        match_dir=str(match_dir.resolve()),
    )


def run_scheduled_batch(
    connection, batch_id: int, pairings: Sequence[elo.Pairing], args, run_dir: Path
):
    """Claim, run, report, and finalize one scheduled concurrent wave."""

    runnable: list[elo.Pairing] = []
    for pairing in pairings:
        assert pairing.match_id is not None
        if elo.claim_match(
            connection, pairing.match_id, worker=f"matchmaker:{os.getpid()}"
        ):
            runnable.append(pairing)
    if not runnable:
        raise elo.EloError(f"batch {batch_id} has no scheduled matches to run")

    total = len(runnable)
    pairing_by_id = {pairing.match_id: pairing for pairing in runnable}
    jobs = [_make_job(pairing, args, run_dir) for pairing in runnable]
    completed = 0
    worker_tag = f"matchmaker:{os.getpid()}"
    try:
        with ProcessPoolExecutor(max_workers=min(args.jobs, len(jobs))) as executor:
            future_to_job = {executor.submit(_play_worker, job): job for job in jobs}
            for future in as_completed(future_to_job):
                job = future_to_job[future]
                try:
                    result = future.result()
                except BaseException as exc:
                    # Process-pool transport failures still become durable void results.
                    result = WorkerResult(
                        match_id=job.match_id,
                        outcome="error",
                        summary=f"worker process failure: {type(exc).__name__}: {exc}",
                        pgn="",
                        output_log=str(Path(job.match_dir) / "autoplay.log"),
                        metrics_csv=str(Path(job.match_dir) / "go-metrics.csv"),
                    )
                reported, reason = _reported_result(result)
                elo.report_result(
                    connection,
                    result.match_id,
                    reported,
                    reason=reason,
                    summary=result.summary,
                    pgn=result.pgn,
                    output_log=result.output_log,
                    metrics_csv=result.metrics_csv,
                    auto_finalize=False,
                )
                completed += 1
                _print_completed(
                    pairing_by_id[result.match_id], result, completed, total
                )
    finally:
        # A pool-construction failure or Ctrl+C must not strand claimed games.
        rows = connection.execute(
            "SELECT id FROM matches WHERE batch_id = ? AND status = 'running' AND worker = ?",
            (batch_id, worker_tag),
        ).fetchall()
        for row in rows:
            elo.reset_running_match(connection, row["id"])

    active = connection.execute(
        """
        SELECT COUNT(*) FROM matches
        WHERE batch_id = ? AND status IN ('scheduled', 'running')
        """,
        (batch_id,),
    ).fetchone()[0]
    if active:
        print(
            f"Batch {batch_id} is awaiting {active} match(es) claimed by another runner.",
            flush=True,
        )
        return None
    batch_status = connection.execute(
        "SELECT status FROM batches WHERE id = ?", (batch_id,)
    ).fetchone()
    if batch_status is None:
        raise elo.EloError(f"unknown batch {batch_id}")
    if batch_status["status"] == "finalized":
        rows = connection.execute(
            "SELECT * FROM matches WHERE batch_id = ? ORDER BY ordinal", (batch_id,)
        ).fetchall()
    else:
        rows = elo.finalize_batch(connection, batch_id)
    _print_batch_ratings(
        connection, batch_id, elo.batch_pairings(connection, batch_id), rows
    )
    return rows


def _validate_run_args(parser: argparse.ArgumentParser, args) -> None:
    if args.games <= 0 or args.jobs <= 0:
        parser.error("--games and --jobs must be positive")
    if args.batch_size is not None and args.batch_size <= 0:
        parser.error("--batch-size must be positive")
    if args.movetime <= 0 or args.timeout <= 0 or args.max_actions <= 0:
        parser.error("time and action limits must be positive")


def _run_new(connection, args) -> int:
    remaining = args.games
    wave = 0
    run_dir = args.logs_dir / datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir.mkdir(parents=True, exist_ok=True)
    print(f"Run logs: {run_dir.resolve()}")
    while remaining:
        wave += 1
        requested = min(args.batch_size or args.jobs, remaining)
        batch_id, pairings = elo.schedule_batch(
            connection,
            requested,
            focus_engine=args.focus_engine,
            seed=args.seed + wave - 1,
            rated=not args.unrated,
            k_factor=args.k_factor,
            note=f"automatic run wave {wave}",
        )
        print(
            f"\nStarting batch {batch_id}: {len(pairings)} match(es), "
            f"up to {min(args.jobs, len(pairings))} concurrently"
        )
        for pairing in pairings:
            print(
                f"  Match {pairing.match_id}: {pairing.white_id} (white) vs "
                f"{pairing.black_id} (black)"
            )
        rows = run_scheduled_batch(connection, batch_id, pairings, args, run_dir)
        if rows is None:
            raise elo.EloError(
                f"batch {batch_id} is being shared with another runner; "
                "resume after its remaining matches finish"
            )
        remaining -= len(pairings)
    return 0


def _resume(connection, args) -> int:
    if args.recover_running:
        rows = connection.execute(
            "SELECT id FROM matches WHERE batch_id = ? AND status = 'running'",
            (args.batch_id,),
        ).fetchall()
        for row in rows:
            elo.reset_running_match(connection, row["id"])
    statuses = {
        row["id"]: row["status"]
        for row in connection.execute(
            "SELECT id, status FROM matches WHERE batch_id = ?", (args.batch_id,)
        ).fetchall()
    }
    pairings = [
        pairing
        for pairing in elo.batch_pairings(connection, args.batch_id)
        if statuses.get(pairing.match_id) == "scheduled"
    ]
    if not pairings:
        active = sum(status == "running" for status in statuses.values())
        if active:
            raise elo.EloError(
                f"batch {args.batch_id} has {active} running match(es); "
                "use --recover-running if their workers are gone"
            )
        rows = elo.finalize_batch(connection, args.batch_id)
        all_pairings = elo.batch_pairings(connection, args.batch_id)
        _print_batch_ratings(connection, args.batch_id, all_pairings, rows)
        return 0
    run_dir = args.logs_dir / f"resume-batch-{args.batch_id}"
    run_dir.mkdir(parents=True, exist_ok=True)
    run_scheduled_batch(connection, args.batch_id, pairings, args, run_dir)
    return 0


def _add_game_options(parser: argparse.ArgumentParser, *, include_games: bool) -> None:
    if include_games:
        parser.add_argument("--games", type=int, required=True)
        parser.add_argument("--engine", dest="focus_engine")
        parser.add_argument("--seed", type=int, default=0)
        parser.add_argument("--unrated", action="store_true")
        parser.add_argument("--k-factor", type=float, default=elo.DEFAULT_K_FACTOR)
        parser.add_argument("--batch-size", type=int)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--module-dir", type=Path, default=Path("build"))
    parser.add_argument("--movetime", type=int, default=1000)
    parser.add_argument("--timeout", type=int, default=2000)
    parser.add_argument("--max-actions", type=int, default=500)
    parser.add_argument("--logs-dir", type=Path, default=Path("logs/elo-runs"))
    game_group = parser.add_mutually_exclusive_group()
    game_group.add_argument("-g", "--game", dest="game_file", type=Path)
    game_group.add_argument("-m", dest="game_text")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, default=elo.DEFAULT_DATABASE)
    subparsers = parser.add_subparsers(dest="action", required=True)
    run = subparsers.add_parser(
        "run", help="schedule and play new concurrent rating waves"
    )
    _add_game_options(run, include_games=True)
    resume = subparsers.add_parser(
        "resume", help="continue a previously interrupted batch"
    )
    resume.add_argument("batch_id", type=int)
    resume.add_argument("--recover-running", action="store_true")
    _add_game_options(resume, include_games=False)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.action == "resume":
        # Shared validation expects these fields, but resume does not schedule.
        args.games = 1
        args.batch_size = None
    _validate_run_args(parser, args)
    try:
        with elo.connect(args.database) as connection:
            if args.action == "run":
                return _run_new(connection, args)
            return _resume(connection, args)
    except (elo.EloError, OSError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
