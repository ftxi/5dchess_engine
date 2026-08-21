#!/usr/bin/env python3
"""Automatically schedule and concurrently run Elo matches through autoplay."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import os
import signal
import sqlite3
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
    batch_id: int
    round_number: int
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
    event: str
    database: str


@dataclass(frozen=True)
class WorkerResult:
    match_id: int
    outcome: str
    summary: str
    pgn: str
    output_log: str
    metrics_csv: str


class WorkerAborted(Exception):
    """The database no longer marks this worker's match as running."""


def _initialize_worker() -> None:
    # The parent owns terminal interrupts and changes match state to request
    # cooperative cancellation. Letting every worker raise KeyboardInterrupt
    # produces noisy ProcessPool remote tracebacks and races pool shutdown.
    signal.signal(signal.SIGINT, signal.SIG_IGN)


async def _play_until_released(job: WorkerJob, arguments, rules, capture) -> str:
    async def wait_until_released() -> None:
        connection = sqlite3.connect(job.database, timeout=5.0)
        try:
            while True:
                row = connection.execute(
                    "SELECT status FROM matches WHERE id = ?", (job.match_id,)
                ).fetchone()
                if row is None or row[0] != "running":
                    return
                await asyncio.sleep(0.1)
        finally:
            connection.close()

    play_task = asyncio.create_task(
        autoplay.play(arguments, rules, game_number=job.match_id, capture=capture)
    )
    release_task = asyncio.create_task(wait_until_released())
    try:
        done, _ = await asyncio.wait(
            (play_task, release_task), return_when=asyncio.FIRST_COMPLETED
        )
        if play_task in done:
            return await play_task
        try:
            await release_task
        except BaseException:
            play_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await play_task
            raise
        play_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await play_task
        raise WorkerAborted
    finally:
        release_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await release_task


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
                    event=job.event,
                    site="Local",
                    pgn_site=f"Local/Batch {job.batch_id}",
                    round_number=job.round_number,
                    match_id=job.match_id,
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
                    _play_until_released(job, arguments, rules, capture)
                )
                summary = capture.get("summary", outcome)
                pgn = capture.get("pgn", "")
            except WorkerAborted:
                outcome = "aborted"
                summary = "match aborted"
                pgn = capture.get("pgn", "")
            except (Exception, SystemExit) as exc:
                # Report ordinary worker failures without swallowing process
                # termination primitives such as GeneratorExit.
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
    def engine_label(name: str, engine_id: str) -> str:
        return f"[{engine_id}]" if name == engine_id else f"{name} [{engine_id}]"

    print()
    print(
        f"{'=' * 18} Match {pairing.match_id} finished ({completed}/{total}) {'=' * 18}"
    )
    print(
        f"White:  {engine_label(pairing.white_name, pairing.white_id)}  "
        f"{pairing.white_rating:.1f}"
    )
    print(
        f"Black:  {engine_label(pairing.black_name, pairing.black_id)}  "
        f"{pairing.black_rating:.1f}"
    )
    print(f"Result: {result.summary}")
    print(f"Log:    {result.output_log}")
    print(f"Metrics:{' ' if result.metrics_csv else ''}{result.metrics_csv}")
    print("\nPGN:\n")
    print(result.pgn.rstrip() if result.pgn.strip() else "<no PGN was produced>")
    print(f"{'=' * 72}", flush=True)


def _report_completed(connection, result: WorkerResult) -> bool:
    status = connection.execute(
        "SELECT status FROM matches WHERE id = ?", (result.match_id,)
    ).fetchone()
    if status is None or status["status"] in {
        "aborted",
        "reported",
        "completed",
        "void",
    }:
        return False
    reported, reason = _reported_result(result)
    try:
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
    except elo.EloError:
        latest = connection.execute(
            "SELECT status FROM matches WHERE id = ?", (result.match_id,)
        ).fetchone()
        if latest is not None and latest["status"] == "aborted":
            return False
        raise
    return True


def _print_batch_ratings(
    connection, batch_id: int, pairings: Sequence[elo.Pairing], rows
) -> None:
    by_match = {row["id"]: row for row in rows}
    print(f"\nBatch {batch_id} rating update:")
    for pairing in pairings:
        row = by_match[pairing.match_id]
        if row["status"] in {"void", "aborted"}:
            print(f"  Match {pairing.match_id}: {row['status']}; no rating change")
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
    assert pairing.batch_id is not None
    assert pairing.ordinal is not None
    match_dir = run_dir / f"match-{pairing.match_id:06d}"
    return WorkerJob(
        match_id=pairing.match_id,
        batch_id=pairing.batch_id,
        round_number=pairing.ordinal,
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
        event=getattr(args, "event", "Autoplay"),
        database=str(getattr(args, "database", elo.DEFAULT_DATABASE)),
    )


def _quiet_pool_shutdown(executor: ProcessPoolExecutor, *, cancel: bool) -> None:
    previous_handler = signal.signal(signal.SIGINT, signal.SIG_IGN)
    try:
        executor.shutdown(wait=True, cancel_futures=cancel)
    finally:
        signal.signal(signal.SIGINT, previous_handler)


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
    executor: ProcessPoolExecutor | None = None
    futures = []
    completed_results: list[WorkerResult] = []
    try:
        executor = ProcessPoolExecutor(
            max_workers=min(args.jobs, len(jobs)), initializer=_initialize_worker
        )
        future_to_job = {executor.submit(_play_worker, job): job for job in jobs}
        futures = list(future_to_job)
        for future in as_completed(future_to_job):
            job = future_to_job[future]
            try:
                result = future.result()
            except Exception as exc:
                # Process-pool transport failures still become durable void results.
                result = WorkerResult(
                    match_id=job.match_id,
                    outcome="error",
                    summary=f"worker process failure: {type(exc).__name__}: {exc}",
                    pgn="",
                    output_log=str(Path(job.match_dir) / "autoplay.log"),
                    metrics_csv=str(Path(job.match_dir) / "go-metrics.csv"),
                )
            completed_results.append(result)
            if _report_completed(connection, result):
                completed += 1
                _print_completed(
                    pairing_by_id[result.match_id], result, completed, total
                )
        _quiet_pool_shutdown(executor, cancel=False)
        executor = None
    except KeyboardInterrupt:
        # Include results already settled at the instant Ctrl+C arrived.
        known_result_ids = {result.match_id for result in completed_results}
        for future in futures:
            done = getattr(future, "done", lambda: False)()
            if not done:
                continue
            try:
                result = future.result()
            except BaseException:
                continue
            if result.match_id not in known_result_ids:
                completed_results.append(result)
                known_result_ids.add(result.match_id)
                if _report_completed(connection, result):
                    completed += 1
                    _print_completed(
                        pairing_by_id[result.match_id], result, completed, total
                    )
        finished = connection.execute(
            """
            SELECT COUNT(*) FROM matches
            WHERE batch_id = ? AND status IN ('reported', 'completed', 'void')
            """,
            (batch_id,),
        ).fetchone()[0]
        if not finished:
            elo.cancel_batch(
                connection,
                batch_id,
                reason="automatic run interrupted before its first game finished",
            )
            print(
                f"\nCancelled batch {batch_id} before its first result; "
                "it has no rating or matchmaking effect.",
                file=sys.stderr,
                flush=True,
            )
        else:
            rows = connection.execute(
                """
                SELECT id FROM matches
                WHERE batch_id = ? AND status = 'running' AND worker = ?
                """,
                (batch_id, worker_tag),
            ).fetchall()
            for row in rows:
                elo.reset_running_match(connection, row["id"])
            print(
                f"\nInterrupted batch {batch_id} after {finished} finished "
                "match(es); results were retained and unfinished matches can be resumed.",
                file=sys.stderr,
                flush=True,
            )
        if executor is not None:
            for future in futures:
                future.cancel()
            _quiet_pool_shutdown(executor, cancel=True)
            executor = None
        raise
    finally:
        if executor is not None:
            _quiet_pool_shutdown(executor, cancel=True)
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
    if batch_status["status"] in {"finalized", "cancelled"}:
        rows = connection.execute(
            "SELECT * FROM matches WHERE batch_id = ? ORDER BY ordinal", (batch_id,)
        ).fetchall()
        if batch_status["status"] == "cancelled":
            return rows
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
            strategy=args.strategy,
            note=f"automatic run wave {wave}",
        )
        strategy_label = (
            f", {args.strategy or 'adaptive'} strategy" if args.focus_engine else ""
        )
        print(
            f"\nStarting batch {batch_id}: {len(pairings)} match(es), "
            f"up to {min(args.jobs, len(pairings))} concurrently{strategy_label}"
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


def _resolve_resume_batch_id(connection, batch_id: int | None) -> int:
    return elo.resolve_open_batch_id(connection, batch_id)


def _resume(connection, args) -> int:
    requested_batch_id = args.batch_id
    args.batch_id = _resolve_resume_batch_id(connection, requested_batch_id)
    if requested_batch_id is None:
        print(f"Resuming the only open batch: {args.batch_id}")
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
    disabled = sorted(
        {
            engine_id
            for pairing in pairings
            for engine_id in (pairing.white_id, pairing.black_id)
            if not elo.get_engine(connection, engine_id).enabled
        }
    )
    if disabled:
        print(
            "Warning: this batch still contains disabled engine(s): "
            + ", ".join(disabled),
            file=sys.stderr,
        )
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
        parser.add_argument(
            "--strategy",
            choices=sorted(elo.FOCUSED_STRATEGIES),
            help="focused matchmaking strategy; requires --engine (default: adaptive)",
        )
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
    parser.add_argument("--event", default="Autoplay")
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
    resume.add_argument(
        "batch_id",
        type=int,
        nargs="?",
        help="batch to resume; omit when exactly one batch is open",
    )
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
    elif args.strategy is not None and args.focus_engine is None:
        parser.error("--strategy requires --engine")
    _validate_run_args(parser, args)
    try:
        with elo.connect(args.database) as connection:
            if args.action == "run":
                return _run_new(connection, args)
            return _resume(connection, args)
    except (elo.EloError, OSError) as exc:
        parser.error(str(exc))
    except KeyboardInterrupt:
        print("Interrupted.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
