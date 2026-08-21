from __future__ import annotations

import contextlib
import io
import sqlite3
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import elo
import elo_matchmaker


class EloDatabaseTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.database = Path(self.temporary.name) / "ratings.sqlite3"
        self.connection = elo.connect(self.database)

    def tearDown(self) -> None:
        self.connection.close()
        self.temporary.cleanup()

    def register(self, engine_id: str, rating: float = 1500.0, **options):
        return elo.register_engine(
            self.connection,
            engine_id,
            f"engine --id {engine_id}",
            rating=rating,
            **options,
        )

    def test_existing_database_gains_strategy_column(self) -> None:
        self.connection.close()
        self.database.unlink()
        legacy = sqlite3.connect(self.database)
        legacy.execute(
            """
            CREATE TABLE batches (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                created_at TEXT NOT NULL,
                finalized_at TEXT,
                status TEXT NOT NULL,
                rated INTEGER NOT NULL,
                k_factor REAL NOT NULL,
                seed INTEGER NOT NULL,
                focus_engine TEXT,
                note TEXT NOT NULL DEFAULT ''
            )
            """
        )
        legacy.commit()
        legacy.close()

        self.connection = elo.connect(self.database)
        columns = {
            row["name"]
            for row in self.connection.execute("PRAGMA table_info(batches)").fetchall()
        }
        self.assertIn("strategy", columns)

    def test_existing_matches_table_gains_aborted_state(self) -> None:
        schema = self.connection.execute(
            "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'matches'"
        ).fetchone()[0]
        old_schema = schema.replace(", 'aborted'", "").replace(
            "\n            aborted_at TEXT,", ""
        )
        self.connection.execute("DROP TABLE matches")
        self.connection.execute(old_schema)
        self.connection.commit()
        self.connection.close()

        self.connection = elo.connect(self.database)
        columns = {
            row["name"]
            for row in self.connection.execute("PRAGMA table_info(matches)").fetchall()
        }
        migrated_schema = self.connection.execute(
            "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'matches'"
        ).fetchone()[0]
        self.assertIn("aborted_at", columns)
        self.assertIn("'aborted'", migrated_schema)

    def test_registration_is_immutable_and_clone_has_separate_identity(self) -> None:
        self.register("experimental-v1", rating=1612.0)
        with self.assertRaises(elo.EloError):
            self.register("experimental-v1")
        clone = elo.clone_engine(
            self.connection,
            "experimental-v1",
            "experimental-v2",
            command="engine --id experimental-v2",
            inherit_rating=True,
        )
        self.assertEqual(clone.parent_id, "experimental-v1")
        self.assertEqual(clone.rating, 1612.0)
        self.assertEqual(
            elo.get_engine(self.connection, "experimental-v1").command,
            "engine --id experimental-v1",
        )

    def test_two_engine_wave_alternates_colors(self) -> None:
        self.register("a")
        self.register("b")
        pairings = elo.suggest_pairings(self.connection, 4, seed=4)
        colors = [(item.white_id, item.black_id) for item in pairings]
        self.assertEqual(colors.count(("a", "b")), 2)
        self.assertEqual(colors.count(("b", "a")), 2)

    def test_scheduled_pairings_have_batch_relative_ordinals(self) -> None:
        self.register("a")
        self.register("b")
        batch_id, pairings = elo.schedule_batch(self.connection, 2)
        self.assertEqual([item.batch_id for item in pairings], [batch_id, batch_id])
        self.assertEqual([item.ordinal for item in pairings], [1, 2])
        self.assertEqual(
            [item.ordinal for item in elo.batch_pairings(self.connection, batch_id)],
            [1, 2],
        )

    def test_focused_scheduler_spreads_games_across_opponents(self) -> None:
        for engine_id in ("new", "anchor-a", "anchor-b"):
            self.register(engine_id)
        pairings = elo.suggest_pairings(self.connection, 2, focus_engine="new", seed=7)
        opponents = {
            item.black_id if item.white_id == "new" else item.white_id
            for item in pairings
        }
        self.assertEqual(opponents, {"anchor-a", "anchor-b"})

    def test_adaptive_focus_prefers_close_repeated_opponent_over_unused_far_one(
        self,
    ) -> None:
        self.register("focus", rating=1500.0)
        self.register("close", rating=1520.0)
        batch_id, history = elo.schedule_batch(
            self.connection,
            16,
            focus_engine="focus",
            strategy="coverage",
        )
        for pairing in history:
            elo.report_result(
                self.connection,
                pairing.match_id,
                "draw",
                auto_finalize=False,
            )
        elo.finalize_batch(self.connection, batch_id)
        self.register("far", rating=1900.0)

        adaptive = elo.suggest_pairings(
            self.connection,
            1,
            focus_engine="focus",
            strategy="adaptive",
            seed=3,
        )[0]
        coverage = elo.suggest_pairings(
            self.connection,
            1,
            focus_engine="focus",
            strategy="coverage",
            seed=3,
        )[0]

        def opponent(pairing: elo.Pairing) -> str:
            return pairing.black_id if pairing.white_id == "focus" else pairing.white_id

        self.assertEqual(opponent(adaptive), "close")
        self.assertEqual(opponent(coverage), "far")

    def test_focused_strategy_defaults_to_adaptive_and_is_persisted(self) -> None:
        self.register("focus")
        self.register("opponent")
        batch_id, _ = elo.schedule_batch(
            self.connection,
            1,
            focus_engine="focus",
        )
        strategy = self.connection.execute(
            "SELECT strategy FROM batches WHERE id = ?", (batch_id,)
        ).fetchone()[0]
        self.assertEqual(strategy, "adaptive")

    def test_adaptive_focus_uses_every_tenth_game_for_exploration(self) -> None:
        self.register("focus", rating=1500.0)
        self.register("close", rating=1520.0)
        batch_id, history = elo.schedule_batch(
            self.connection,
            9,
            focus_engine="focus",
            strategy="coverage",
        )
        for pairing in history:
            elo.report_result(
                self.connection,
                pairing.match_id,
                "draw",
                auto_finalize=False,
            )
        elo.finalize_batch(self.connection, batch_id)
        self.register("untried", rating=1600.0)

        pairing = elo.suggest_pairings(
            self.connection,
            1,
            focus_engine="focus",
            strategy="adaptive",
        )[0]
        opponent = pairing.black_id if pairing.white_id == "focus" else pairing.white_id
        self.assertEqual(opponent, "untried")

    def test_strategy_is_rejected_without_focus_engine(self) -> None:
        self.register("a")
        self.register("b")
        with self.assertRaisesRegex(elo.EloError, "requires --engine"):
            elo.suggest_pairings(self.connection, 1, strategy="coverage")

    def test_stateful_engine_limits_a_concurrent_wave(self) -> None:
        self.register("learner", training=True, stateful=True)
        self.register("anchor")
        pairings = elo.suggest_pairings(
            self.connection,
            8,
            focus_engine="learner",
            rated=False,
        )
        self.assertEqual(len(pairings), 1)

    def test_training_engine_is_excluded_from_rated_matches(self) -> None:
        self.register("learner", training=True, stateful=True)
        self.register("anchor")
        with self.assertRaisesRegex(elo.EloError, "unrated"):
            elo.schedule_batch(self.connection, 1, focus_engine="learner")
        batch_id, pairings = elo.schedule_batch(
            self.connection, 1, focus_engine="learner", rated=False
        )
        self.assertEqual(batch_id, 1)
        self.assertEqual(len(pairings), 1)

    def test_single_game_elo_update_and_pgn_persistence(self) -> None:
        self.register("a")
        self.register("b")
        batch_id, pairings = elo.schedule_batch(self.connection, 1, seed=2)
        match = pairings[0]
        elo.report_result(
            self.connection,
            match.match_id,
            "white",
            summary="white wins",
            pgn='[Result "1-0"]\n',
        )
        white = elo.get_engine(self.connection, match.white_id)
        black = elo.get_engine(self.connection, match.black_id)
        self.assertAlmostEqual(white.rating, 1516.0)
        self.assertAlmostEqual(black.rating, 1484.0)
        row = self.connection.execute(
            "SELECT * FROM matches WHERE batch_id = ?", (batch_id,)
        ).fetchone()
        self.assertEqual(row["status"], "completed")
        self.assertEqual(row["pgn"], '[Result "1-0"]\n')

    def test_batch_updates_do_not_depend_on_report_order(self) -> None:
        def play(order: tuple[int, int]) -> tuple[float, float]:
            with tempfile.TemporaryDirectory() as directory:
                connection = elo.connect(Path(directory) / "db.sqlite3")
                elo.register_engine(connection, "a", "a")
                elo.register_engine(connection, "b", "b")
                _, matches = elo.schedule_batch(connection, 2, seed=3)
                for index in order:
                    elo.report_result(
                        connection,
                        matches[index].match_id,
                        "white",
                        auto_finalize=False,
                    )
                elo.finalize_batch(connection, matches[0].batch_id)
                ratings = (
                    elo.get_engine(connection, "a").rating,
                    elo.get_engine(connection, "b").rating,
                )
                connection.close()
                return ratings

        self.assertEqual(play((0, 1)), play((1, 0)))

    def test_only_one_rated_batch_may_be_open(self) -> None:
        self.register("a")
        self.register("b")
        elo.schedule_batch(self.connection, 1)
        with self.assertRaisesRegex(elo.EloError, "still open"):
            elo.schedule_batch(self.connection, 1)

    def test_artifact_change_blocks_rated_schedule(self) -> None:
        artifact = Path(self.temporary.name) / "weights.bin"
        artifact.write_bytes(b"first")
        self.register("a", artifacts=[artifact])
        self.register("b")
        artifact.write_bytes(b"second")
        with self.assertRaisesRegex(elo.EloError, "artifact changed"):
            elo.schedule_batch(self.connection, 1)

    def test_claim_is_atomic(self) -> None:
        self.register("a")
        self.register("b")
        _, matches = elo.schedule_batch(self.connection, 1)
        self.assertTrue(elo.claim_match(self.connection, matches[0].match_id, "one"))
        self.assertFalse(elo.claim_match(self.connection, matches[0].match_id, "two"))

    def test_runner_pool_failure_returns_claimed_match_to_schedule(self) -> None:
        self.register("a")
        self.register("b")
        batch_id, matches = elo.schedule_batch(self.connection, 1)
        arguments = SimpleNamespace(
            jobs=1,
            module_dir=Path("build"),
            movetime=10,
            timeout=10,
            max_actions=1,
            game_file=None,
            game_text=None,
        )
        with mock.patch.object(
            elo_matchmaker, "ProcessPoolExecutor", side_effect=OSError("not permitted")
        ):
            with self.assertRaises(OSError):
                elo_matchmaker.run_scheduled_batch(
                    self.connection,
                    batch_id,
                    matches,
                    arguments,
                    Path(self.temporary.name) / "run",
                )
        status = self.connection.execute(
            "SELECT status FROM matches WHERE id = ?", (matches[0].match_id,)
        ).fetchone()[0]
        self.assertEqual(status, "scheduled")

    def test_ctrl_c_before_first_result_cancels_batch(self) -> None:
        self.register("a")
        self.register("b")
        batch_id, matches = elo.schedule_batch(self.connection, 2)
        arguments = SimpleNamespace(
            jobs=1,
            module_dir=Path("build"),
            movetime=10,
            timeout=10,
            max_actions=1,
            game_file=None,
            game_text=None,
        )

        class Future:
            def __init__(self) -> None:
                self.cancelled = False

            def cancel(self) -> bool:
                self.cancelled = True
                return True

        class Executor:
            def __init__(self, **_kwargs) -> None:
                self.futures: list[Future] = []
                self.shutdown_calls: list[tuple[bool, bool]] = []

            def submit(self, _function, _job) -> Future:
                future = Future()
                self.futures.append(future)
                return future

            def shutdown(self, wait=True, *, cancel_futures=False) -> None:
                self.shutdown_calls.append((wait, cancel_futures))

        executor = Executor()
        with mock.patch.object(
            elo_matchmaker, "ProcessPoolExecutor", return_value=executor
        ), mock.patch.object(
            elo_matchmaker, "as_completed", side_effect=KeyboardInterrupt
        ):
            with self.assertRaises(KeyboardInterrupt):
                elo_matchmaker.run_scheduled_batch(
                    self.connection,
                    batch_id,
                    matches,
                    arguments,
                    Path(self.temporary.name) / "run",
                )

        self.assertTrue(all(future.cancelled for future in executor.futures))
        self.assertEqual(executor.shutdown_calls, [(True, True)])
        rows = self.connection.execute(
            "SELECT status, result, summary FROM matches WHERE batch_id = ? ORDER BY id",
            (batch_id,),
        ).fetchall()
        self.assertEqual([row["status"] for row in rows], ["aborted", "aborted"])
        self.assertEqual([row["result"] for row in rows], [None, None])
        self.assertEqual([row["summary"] for row in rows], ["", ""])
        batch_status = self.connection.execute(
            "SELECT status FROM batches WHERE id = ?", (batch_id,)
        ).fetchone()[0]
        self.assertEqual(batch_status, "cancelled")

        # A cancelled rated batch no longer blocks a fresh rated run.
        new_batch_id, _ = elo.schedule_batch(self.connection, 1)
        self.assertNotEqual(new_batch_id, batch_id)

    def test_ctrl_c_after_result_retains_it_and_reschedules_unfinished_game(
        self,
    ) -> None:
        self.register("a")
        self.register("b")
        batch_id, matches = elo.schedule_batch(self.connection, 2)
        arguments = SimpleNamespace(
            jobs=1,
            module_dir=Path("build"),
            movetime=10,
            timeout=10,
            max_actions=1,
            game_file=None,
            game_text=None,
        )

        class Future:
            def __init__(self, match_id: int, finishes: bool) -> None:
                self.match_id = match_id
                self.finishes = finishes

            def cancel(self) -> bool:
                return False

            def result(self) -> elo_matchmaker.WorkerResult:
                if not self.finishes:
                    raise KeyboardInterrupt
                return elo_matchmaker.WorkerResult(
                    match_id=self.match_id,
                    outcome="draw",
                    summary="finished draw",
                    pgn="pgn",
                    output_log="match.log",
                    metrics_csv="metrics.csv",
                )

        class Executor:
            def __init__(self) -> None:
                self.submitted = 0

            def submit(self, _function, job) -> Future:
                self.submitted += 1
                return Future(job.match_id, finishes=self.submitted == 1)

            def shutdown(self, wait=True, *, cancel_futures=False) -> None:
                pass

        def one_then_interrupt(future_to_job):
            yield next(iter(future_to_job))
            streamed_status = self.connection.execute(
                "SELECT status FROM matches WHERE id = ?", (matches[0].match_id,)
            ).fetchone()[0]
            self.assertEqual(streamed_status, "reported")
            raise KeyboardInterrupt

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            with mock.patch.object(
                elo_matchmaker, "ProcessPoolExecutor", return_value=Executor()
            ), mock.patch.object(
                elo_matchmaker, "as_completed", side_effect=one_then_interrupt
            ):
                with self.assertRaises(KeyboardInterrupt):
                    elo_matchmaker.run_scheduled_batch(
                        self.connection,
                        batch_id,
                        matches,
                        arguments,
                        Path(self.temporary.name) / "run",
                    )
        self.assertIn("finished draw", output.getvalue())

        rows = self.connection.execute(
            "SELECT status, result FROM matches WHERE batch_id = ? ORDER BY id",
            (batch_id,),
        ).fetchall()
        self.assertEqual(rows[0]["status"], "reported")
        self.assertEqual(rows[0]["result"], "draw")
        self.assertEqual(rows[1]["status"], "scheduled")
        batch_status = self.connection.execute(
            "SELECT status FROM batches WHERE id = ?", (batch_id,)
        ).fetchone()[0]
        self.assertEqual(batch_status, "open")

    def test_cancelling_batch_discards_reported_results_and_rating_effects(
        self,
    ) -> None:
        self.register("a", rating=1400.0)
        self.register("b", rating=1600.0)
        batch_id, matches = elo.schedule_batch(self.connection, 2)
        elo.report_result(
            self.connection,
            matches[0].match_id,
            "white",
            summary="result that must be discarded",
            auto_finalize=False,
        )

        elo.cancel_batch(self.connection, batch_id, reason="wrong parameters")

        rows = self.connection.execute(
            "SELECT status, result, summary FROM matches WHERE batch_id = ? ORDER BY id",
            (batch_id,),
        ).fetchall()
        self.assertEqual([row["status"] for row in rows], ["aborted", "aborted"])
        self.assertEqual([row["result"] for row in rows], [None, None])
        self.assertEqual(
            [row["summary"] for row in rows],
            ["result that must be discarded", ""],
        )
        self.assertEqual(elo.get_engine(self.connection, "a").rating, 1400.0)
        self.assertEqual(elo.get_engine(self.connection, "b").rating, 1600.0)

    def test_aborting_one_match_preserves_and_finalizes_other_results(self) -> None:
        self.register("a", rating=1500.0)
        self.register("b", rating=1500.0)
        batch_id, matches = elo.schedule_batch(self.connection, 2)
        elo.abort_matches(
            self.connection, [matches[0].match_id], reason="bad configuration"
        )
        elo.report_result(self.connection, matches[1].match_id, "draw")

        rows = self.connection.execute(
            "SELECT status, result FROM matches WHERE batch_id = ? ORDER BY ordinal",
            (batch_id,),
        ).fetchall()
        self.assertEqual(rows[0]["status"], "aborted")
        self.assertIsNone(rows[0]["result"])
        self.assertEqual(rows[1]["status"], "completed")
        batch_status = self.connection.execute(
            "SELECT status FROM batches WHERE id = ?", (batch_id,)
        ).fetchone()[0]
        self.assertEqual(batch_status, "finalized")

    def test_aborting_engine_matches_cancels_focused_batch(self) -> None:
        self.register("broken")
        self.register("anchor")
        batch_id, matches = elo.schedule_batch(
            self.connection, 2, focus_engine="broken"
        )
        rows = elo.abort_engine_matches(
            self.connection, "broken", reason="wrong weights"
        )
        self.assertEqual(
            {row["id"] for row in rows}, {item.match_id for item in matches}
        )
        self.assertTrue(all(row["status"] == "aborted" for row in rows))
        batch_status = self.connection.execute(
            "SELECT status FROM batches WHERE id = ?", (batch_id,)
        ).fetchone()[0]
        self.assertEqual(batch_status, "cancelled")

    def test_aborting_engine_across_multiple_batches_requires_explicit_scope(
        self,
    ) -> None:
        self.register("broken")
        self.register("anchor")
        first, first_matches = elo.schedule_batch(
            self.connection, 1, focus_engine="broken", rated=False
        )
        second, second_matches = elo.schedule_batch(
            self.connection, 1, focus_engine="broken", rated=False
        )
        with self.assertRaisesRegex(elo.EloError, "specify --batch or --all-open"):
            elo.abort_engine_matches(self.connection, "broken")

        rows = elo.abort_engine_matches(
            self.connection, "broken", all_open=True, reason="broken"
        )
        expected = {
            first_matches[0].match_id,
            second_matches[0].match_id,
        }
        self.assertEqual({row["id"] for row in rows}, expected)
        statuses = self.connection.execute(
            "SELECT id, status FROM batches WHERE id IN (?, ?) ORDER BY id",
            (first, second),
        ).fetchall()
        self.assertEqual(
            [row["status"] for row in statuses], ["cancelled", "cancelled"]
        )

    def test_disabling_engine_does_not_change_scheduled_matches(self) -> None:
        self.register("broken")
        self.register("anchor")
        self.register("spare")
        _, matches = elo.schedule_batch(self.connection, 1, rated=False)
        elo.update_engine(self.connection, "broken", enabled=False)
        status = self.connection.execute(
            "SELECT status FROM matches WHERE id = ?", (matches[0].match_id,)
        ).fetchone()[0]
        self.assertEqual(status, "scheduled")
        suggestions = elo.suggest_pairings(self.connection, 1, rated=False)
        self.assertNotIn("broken", {suggestions[0].white_id, suggestions[0].black_id})

    def test_disable_abort_open_combines_both_operations(self) -> None:
        self.register("broken")
        self.register("anchor")
        batch_id, _ = elo.schedule_batch(self.connection, 2, focus_engine="broken")
        with contextlib.redirect_stdout(io.StringIO()):
            result = elo.main(
                [
                    "--database",
                    str(self.database),
                    "disable",
                    "broken",
                    "--abort-open",
                    "--reason",
                    "bad engine",
                ]
            )
        self.assertEqual(result, 0)
        self.assertFalse(elo.get_engine(self.connection, "broken").enabled)
        rows = self.connection.execute(
            "SELECT status FROM matches WHERE batch_id = ?", (batch_id,)
        ).fetchall()
        self.assertTrue(all(row["status"] == "aborted" for row in rows))

    def test_finalized_match_cannot_be_aborted(self) -> None:
        self.register("a")
        self.register("b")
        _, matches = elo.schedule_batch(self.connection, 1)
        elo.report_result(self.connection, matches[0].match_id, "draw")
        with self.assertRaisesRegex(elo.EloError, "cannot abort finalized"):
            elo.abort_matches(self.connection, [matches[0].match_id])

    def test_resume_without_id_selects_only_open_batch(self) -> None:
        self.register("a")
        self.register("b")
        batch_id, _ = elo.schedule_batch(self.connection, 1)
        self.assertEqual(
            elo_matchmaker._resolve_resume_batch_id(self.connection, None), batch_id
        )

    def test_resume_without_id_rejects_multiple_open_batches(self) -> None:
        self.register("a")
        self.register("b")
        first, _ = elo.schedule_batch(self.connection, 1, rated=False)
        second, _ = elo.schedule_batch(self.connection, 1, rated=False)
        with self.assertRaisesRegex(
            elo.EloError, rf"multiple batches are open \({first}, {second}\)"
        ):
            elo_matchmaker._resolve_resume_batch_id(self.connection, None)

    def test_resume_without_id_rejects_missing_open_batch(self) -> None:
        with self.assertRaisesRegex(elo.EloError, "no open batch"):
            elo_matchmaker._resolve_resume_batch_id(self.connection, None)


class MatchmakerOutputTest(unittest.TestCase):
    def test_elo_parser_accepts_abort_targets(self) -> None:
        parser = elo.build_parser()
        batch = parser.parse_args(["abort", "batch"])
        matches = parser.parse_args(["abort", "match", "4", "9"])
        engine = parser.parse_args(["abort", "engine", "broken", "--all-open"])
        self.assertIsNone(batch.batch_id)
        self.assertEqual(matches.match_ids, [4, 9])
        self.assertTrue(engine.all_open)

    def test_matchmaker_accepts_resume_without_batch_id(self) -> None:
        args = elo_matchmaker.build_parser().parse_args(["resume", "--jobs", "2"])
        self.assertIsNone(args.batch_id)
        self.assertEqual(args.jobs, 2)

    def test_matchmaker_rejects_strategy_without_engine(self) -> None:
        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                elo_matchmaker.main(["run", "--games", "1", "--strategy", "coverage"])

    def test_matchmaker_accepts_event_but_not_site(self) -> None:
        parser = elo_matchmaker.build_parser()
        args = parser.parse_args(["run", "--games", "1", "--event", "Experiment A"])
        self.assertEqual(args.event, "Experiment A")
        self.assertFalse(hasattr(args, "site"))

    def test_matchmaker_accepts_focused_strategy(self) -> None:
        parser = elo_matchmaker.build_parser()
        args = parser.parse_args(
            ["run", "--games", "1", "--engine", "new", "--strategy", "coverage"]
        )
        self.assertEqual(args.strategy, "coverage")

    def test_worker_job_contains_batch_pgn_identity(self) -> None:
        pairing = elo.Pairing(
            match_id=42,
            batch_id=7,
            ordinal=2,
            white_id="new",
            black_id="anchor",
            white_name="New Engine",
            black_name="Anchor",
            white_command="new",
            black_command="anchor",
            white_rating=1500.0,
            black_rating=1510.0,
        )
        arguments = SimpleNamespace(
            module_dir=Path("build"),
            movetime=10,
            timeout=20,
            max_actions=30,
            game_file=None,
            game_text=None,
            event="Experiment A",
        )
        job = elo_matchmaker._make_job(pairing, arguments, Path("logs"))
        self.assertEqual(job.batch_id, 7)
        self.assertEqual(job.round_number, 2)
        self.assertEqual(job.match_id, 42)
        self.assertEqual(job.event, "Experiment A")

    def test_completed_summary_contains_pgn(self) -> None:
        pairing = elo.Pairing(
            match_id=12,
            batch_id=3,
            white_id="new",
            black_id="anchor",
            white_name="New Engine",
            black_name="Anchor",
            white_command="new",
            black_command="anchor",
            white_rating=1500.0,
            black_rating=1510.0,
        )
        result = elo_matchmaker.WorkerResult(
            match_id=12,
            outcome="white",
            summary="white(New Engine) wins",
            pgn='[Result "1-0"]\n1. test',
            output_log="match.log",
            metrics_csv="metrics.csv",
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            elo_matchmaker._print_completed(pairing, result, 1, 4)
        rendered = output.getvalue()
        self.assertIn("Match 12 finished (1/4)", rendered)
        self.assertIn("White:  New Engine [new]", rendered)
        self.assertIn("Black:  Anchor [anchor]", rendered)
        self.assertIn("white(New Engine) wins", rendered)
        self.assertIn('[Result "1-0"]', rendered)
        self.assertNotIn("pending completion", rendered)

    def test_completed_summary_suppresses_duplicate_name_and_id(self) -> None:
        pairing = elo.Pairing(
            match_id=13,
            batch_id=3,
            white_id="mcts",
            black_id="linear-trained",
            white_name="mcts",
            black_name="linear-trained",
            white_command="mcts",
            black_command="linear-trained",
            white_rating=1517.0,
            black_rating=1511.7,
        )
        result = elo_matchmaker.WorkerResult(
            match_id=13,
            outcome="black",
            summary="black wins",
            pgn="pgn",
            output_log="match.log",
            metrics_csv="metrics.csv",
        )
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            elo_matchmaker._print_completed(pairing, result, 1, 5)
        rendered = output.getvalue()
        self.assertIn("White:  [mcts]  1517.0", rendered)
        self.assertIn("Black:  [linear-trained]  1511.7", rendered)
        self.assertNotIn("mcts [mcts]", rendered)


if __name__ == "__main__":
    unittest.main()
