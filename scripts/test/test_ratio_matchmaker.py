"""Adaptive allocation, ordered asynchronous evidence, and cancellation contracts."""

import argparse
import asyncio
import contextlib
import io
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import ANY, AsyncMock, Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ratio_matchmaker as matcher


def settings(**overrides):
    args = matcher.build_parser().parse_args(["--target", "unused"])
    args.schedule = "adaptive"
    for key, value in overrides.items():
        setattr(args, key, value)
    if "worker_cores" not in overrides:
        args.worker_cores = [None] * args.jobs
    if "test_alpha" not in overrides:
        args.test_alpha = args.alpha / (3 * len(args.ratios))
    return args


def outcomes(outcome):
    return [matcher.GameResult(outcome, "test", 0, "") for _ in range(2)]


class AdaptivePolicyTests(unittest.TestCase):
    def setUp(self):
        self.args = settings(ratios=[1, .75, .5], switch_min_pairs=8)
        self.states = [matcher.RatioState(matcher.RatioResult(ratio)) for ratio in self.args.ratios]
        self.policy = matcher.AdaptivePolicy(self.states, self.args)
        self.output = contextlib.redirect_stdout(io.StringIO())
        self.output.__enter__()
        self.addCleanup(self.output.__exit__, None, None, None)

    def signal(self, index, acceptable=True):
        result = self.states[index].result
        result.pair_wins = 8
        if acceptable:
            result.noninferiority_log_e = math.log(3)
        else:
            result.inferiority_log_e = math.log(3)

    def test_provisional_descent_needs_current_evidence_and_minimum_pairs(self):
        result = self.states[0].result
        result.noninferiority_max_log_e = math.log(100)
        self.assertEqual(self.policy.choose(), 0)
        result.noninferiority_log_e = math.log(3)
        self.assertEqual(self.policy.choose(), 0)
        self.signal(0)
        self.assertEqual(self.policy.choose(), 1)
        self.assertEqual(result.decision, "running")

    def test_setback_refines_without_bouncing_on_old_evidence(self):
        self.signal(0)
        self.assertEqual(self.policy.choose(), 1)
        self.signal(1, acceptable=False)
        self.assertEqual([self.policy.choose() for _ in range(8)], [0, 0, 0, 1] * 2)
        self.assertEqual(self.policy.bracket, (0, 1))
        # Certification retires the upper endpoint, leaving all new work for
        # the lower endpoint. Revisiting retained the original sample counts.
        self.states[0].result.decision = "noninferior"
        self.assertEqual([self.policy.choose() for _ in range(4)], [1] * 4)
        self.assertEqual(self.states[0].result.valid_pairs, 8)
        self.states[1].result.decision = "noninferior"
        self.assertEqual(self.policy.choose(), 2)

    def test_formal_rejection_moves_boundary_up_and_resolved_boundary_stops(self):
        self.signal(0)
        self.signal(1)
        self.assertEqual(self.policy.choose(), 2)
        self.signal(2, acceptable=False)
        self.assertEqual(self.policy.choose(), 1)
        self.states[1].result.decision = "inferior"
        self.assertEqual(self.policy.choose(), 0)
        self.assertEqual(self.policy.bracket, (0, 1))
        self.states[0].result.decision = "noninferior"
        self.assertIsNone(self.policy.choose())
        self.assertTrue(self.policy.finished)
        self.assertEqual(self.states[2].result.decision, "running")

    def test_exhausted_neighbors_stop_without_manufacturing_certification(self):
        self.signal(0)
        self.policy.choose()
        self.signal(1, acceptable=False)
        self.policy.choose()
        for state in self.states[:2]:
            state.next_pair = self.args.max_pairs + 1
        self.assertIsNone(self.policy.choose())
        self.assertTrue(self.policy.finished)
        self.assertTrue(all(state.result.decision == "running" for state in self.states))

    def test_superiority_mode_does_not_descend_on_noninferiority_alone(self):
        self.args.advance_on = "better"
        self.signal(0)
        self.states[0].result.decision = "noninferior"
        self.assertEqual(self.policy.choose(), 0)
        self.states[0].result.superiority_log_e = math.log(3)
        self.assertEqual(self.policy.choose(), 1)


class EvidenceTests(unittest.TestCase):
    def test_safety_cap_respects_an_absolute_limit_below_baseline_time(self):
        self.assertEqual(matcher.target_time_cap_ms(400, .5, 2, 100), 100)
        self.assertEqual(matcher.target_time_cap_ms(400, 2, 2, 10000), 400)

    def test_below_equal_strength_can_still_be_acceptable(self):
        result = matcher.RatioResult(1, pair_scores=[.45] * 500)
        decision = matcher.evaluate_anytime_ratio(result, .01, .10, .60)
        self.assertEqual(decision, "noninferior")
        self.assertLess(result.inferiority_log_e, 0)
        self.assertLess(result.superiority_log_e, 0)

    def test_historical_crossing_survives_later_weaker_evidence(self):
        result = matcher.RatioResult(1, pair_scores=[1] * 30)
        matcher.update_anytime_evidence(result, .10, .60)
        previous = result.noninferiority_max_log_e
        result.pair_scores.extend([0] * 30)
        matcher.update_anytime_evidence(result, .10, .60)
        self.assertLess(result.noninferiority_log_e, previous)
        self.assertEqual(result.noninferiority_max_log_e, previous)

    def test_nonfinite_ratios_are_rejected(self):
        for text in ("nan", "inf", "1,nan", "0"):
            with self.subTest(text=text), self.assertRaises(argparse.ArgumentTypeError):
                matcher.parse_float_list(text)


class ProgressOutputTests(unittest.TestCase):
    def reporter(self, state, **overrides):
        args = settings(**overrides)
        experiment = matcher.Experiment(args, None, [], Path("unused"))
        experiment.calibration[None] = matcher.SearchSample(3301, .2)
        experiment.throughput = matcher.Throughput(
            target_iterations=1013, target_seconds=1,
            baseline_iterations=1000, baseline_seconds=1,
        )
        with patch.object(matcher.time, "monotonic", return_value=0):
            reporter = matcher.ProgressReporter(experiment, [state])
        return experiment, reporter

    def test_exact_agreed_format_coalesces_the_example_catchup(self):
        result = matcher.RatioResult(1, pair_wins=3, pair_ties=22, pair_losses=2)
        state = matcher.RatioState(result)
        state.completed = {pair: outcomes("draw") for pair in range(28, 52)}
        _, reporter = self.reporter(state)
        result.game_wins, result.game_draws, result.game_losses = 48, 7, 47
        result.pair_wins, result.pair_ties, result.pair_losses = 9, 34, 8
        result.pair_score_sum = 25.75
        result.noninferiority_log_e = math.log(6.364)
        result.noninferiority_max_log_e = math.log(8.798)
        state.completed.clear()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            reporter.flush(now=5)
        self.assertEqual(output.getvalue(),
            "[m=1] games=102 W:D:L=48:7:47 (void 0) ips-ratio=1.013\n"
            "[m=1] pairs=51 W:T:L=9:34:8 score=50.5% buffered=0 "
            "| noninferior-E current=6.364 peak=8.798/240 (added 24 pairs)\n")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result.pair_ties += 1
            result.pair_score_sum += .5
            reporter.flush(now=10)
        self.assertIn("[m=1] pairs=52", output.getvalue())
        self.assertIn("(added 1 pairs)", output.getvalue())
        self.assertNotIn("games=", output.getvalue())

    def test_routine_updates_are_throttled_without_losing_pair_deltas(self):
        state = matcher.RatioState(matcher.RatioResult(1))
        experiment, reporter = self.reporter(state)
        state.result.add_game("draw")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            reporter.flush(now=4.99)
            self.assertEqual(output.getvalue(), "")
            reporter.flush(now=5)
            state.result.add_game("draw")
            state.completed[1] = outcomes("draw")
            state.commit(experiment, reporter)
            reporter.flush(now=9.99)
            self.assertEqual(len(output.getvalue().splitlines()), 1)
            reporter.flush(now=10)
            reporter.flush(now=15)  # No new data: no duplicate report.
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 3)
        self.assertIn("games=1 W:D:L=0:1:0", lines[0])
        self.assertIn("games=2 W:D:L=0:2:0", lines[1])
        self.assertIn("pairs=1 W:T:L=0:1:0", lines[2])
        self.assertIn("(added 1 pairs)", lines[2])
        self.assertNotIn("pairs=", lines[0])
        self.assertNotIn("games=", lines[2])

    def test_buffered_pairs_are_counted_and_drained_in_one_report(self):
        state = matcher.RatioState(matcher.RatioResult(1, game_draws=4))
        experiment, reporter = self.reporter(state)
        state.completed[2] = outcomes("draw")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            reporter.flush(now=5)
            state.completed[1] = outcomes("draw")
            state.commit(experiment, reporter)
            reporter.flush(now=10)
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertIn("score=n/a buffered=1", lines[0])
        self.assertIn("score=50.0% buffered=0", lines[1])
        self.assertIn("(added 2 pairs)", lines[1])

    def test_decisions_print_immediately_with_peak_evidence(self):
        state = matcher.RatioState(matcher.RatioResult(1))
        experiment, reporter = self.reporter(state, test_alpha=.9)
        state.completed[1] = outcomes("win")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            state.commit(experiment, reporter)
        self.assertIn("decision: better", output.getvalue())
        self.assertIn("noninferior-E current=", output.getvalue())
        self.assertIn("peak=", output.getvalue())
        self.assertNotIn("Finished games:", output.getvalue())
        self.assertNotIn("games=", output.getvalue())

    def test_fixed_looks_report_p_values_without_fabricating_evidence(self):
        state = matcher.RatioState(matcher.RatioResult(1))
        experiment, reporter = self.reporter(state, stat_method="fixed-looks", look_pairs=[1])
        state.completed[1] = outcomes("draw")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            state.commit(experiment, reporter)
        self.assertIn("Fixed look", output.getvalue())
        self.assertIn("noninferior-p", reporter.pairs(state))
        self.assertNotIn("Current E", output.getvalue())
        self.assertNotIn("noninferior-E", reporter.pairs(state))


    def test_void_outcomes_remain_visible_on_each_relevant_line(self):
        state = matcher.RatioState(matcher.RatioResult(1))
        experiment, reporter = self.reporter(state)
        state.result.add_game("void")
        state.completed[1] = outcomes("void")
        state.commit(experiment, reporter)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            reporter.flush(now=5)
        games, pairs = output.getvalue().splitlines()
        self.assertIn("games=1 W:D:L=0:0:0 (void 1)", games)
        self.assertIn("pairs=0 W:T:L=0:0:0", pairs)
        self.assertIn("(void 1)", pairs)
        self.assertIn("(added 0 pairs)", pairs)

    def test_final_summary_has_one_line_per_ratio_and_no_panels(self):
        state = matcher.RatioState(matcher.RatioResult(1, game_draws=2, decision="inconclusive"))
        experiment, reporter = self.reporter(state)
        state.result.add_pair(outcomes("draw"))
        experiment.results = [state.result]
        experiment.progress = reporter
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            matcher.print_final_summary(experiment)
        summary = [line for line in output.getvalue().splitlines() if line.startswith("m=")]
        self.assertEqual(len(summary), 1)
        self.assertIn("m=1: inconclusive; games=2 W:D:L=0:2:0 (void 0); pairs=1 W:T:L=0:1:0", summary[0])
        self.assertIn("noninferior-E current=", summary[0])
        self.assertIn("anytime p(superior/noninferior/unacceptable)=", summary[0])
        self.assertNotIn("Finished games:", output.getvalue())
        self.assertIn("Startpos target-iters: 3301", output.getvalue())


class CalibrationTests(unittest.IsolatedAsyncioTestCase):
    async def test_uses_startpos_and_preserves_actual_counts_separately_from_game_searches(self):
        args = settings(baseline_movetime=200)
        rules = Mock()
        experiment = matcher.Experiment(args, rules, ["a selected non-startpos opening"], Path("unused"))
        for cpu, count in ((0, 3301), (1, 3303)):
            baseline, target = Mock(), Mock()
            for player, iterations in ((baseline, 1000), (target, count)):
                player.choose = AsyncMock()
                player.close = AsyncMock()
                player.last_engine_stats = {"iterations": str(iterations), "elapsed_seconds": "0.2"}
            output = io.StringIO()
            with (patch.object(experiment, "engine", side_effect=[baseline, target]),
                  patch.object(matcher, "start_engines", new_callable=AsyncMock),
                  patch.object(matcher.autoplay, "uci_position", return_value="startpos"),
                  contextlib.redirect_stdout(output)):
                await matcher.calibrate_on(experiment, cpu)
            rules.game.from_pgn.assert_called_with(matcher.STARTPOS_PGN)
            baseline.choose.assert_awaited_once_with("startpos", [], 200, ANY)
            target.choose.assert_awaited_once_with("startpos", [], 200, ANY)
            self.assertEqual(experiment.calibration[cpu].iterations, count)
            self.assertIn(f"target-iters={count} (startpos)", output.getvalue())
        self.assertEqual(experiment.startpos_target_iterations, 3302)
        experiment.add_pair(0, matcher.SearchSample(5, .2), matcher.SearchSample(10, .2))
        self.assertEqual(experiment.startpos_target_iterations, 3302)


class CPUAllocationTests(unittest.TestCase):
    def test_default_and_explicit_controller_pool_leave_distinct_engine_cpus(self):
        parser = matcher.build_parser()
        with patch.object(matcher.cpu_affinity, "available_cpus", return_value={0, 1, 2, 3}):
            args = settings(jobs=2)
            matcher.configure_cpus(parser, args)
            self.assertEqual(args.worker_cores, [0, 1])
            self.assertEqual(args.controller_cpus, [2, 3])
            args = settings(jobs=2, controller_cores=[0, 1])
            matcher.configure_cpus(parser, args)
            self.assertEqual(args.worker_cores, [2, 3])
            self.assertEqual(args.controller_cpus, [0, 1])


class SchedulerTests(unittest.IsolatedAsyncioTestCase):
    def experiment(self, **overrides):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        options = dict(ratios=[1], schedule="sequential", max_pairs=3, jobs=2)
        options.update(overrides)
        args = settings(**options)
        return matcher.Experiment(args, None, ["opening"], Path(directory.name))

    async def test_refills_before_slow_pair_finishes_but_commits_in_order(self):
        experiment = self.experiment()
        release_first, release_third, third_started = (asyncio.Event() for _ in range(3))
        assignments = []

        async def play_pair(exp, result, pair, cpu):
            assignments.append((pair, cpu))
            if pair == 1:
                await release_first.wait()
                return outcomes("win")
            if pair == 2:
                return outcomes("loss")
            third_started.set()
            await release_third.wait()
            return outcomes("draw")

        output = io.StringIO()
        with patch.object(matcher, "play_pair", play_pair), contextlib.redirect_stdout(output):
            task = asyncio.create_task(matcher.run_matches(experiment))
            try:
                await asyncio.wait_for(third_started.wait(), 3)
                self.assertFalse(release_first.is_set())
                self.assertEqual(experiment.results[0].valid_pairs, 0)
                release_first.set()
                release_third.set()
                await asyncio.wait_for(task, 3)
            finally:
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
        self.assertEqual(experiment.results[0].pair_scores, [1, 0, .5])
        self.assertEqual(len(assignments), 3)
        self.assertEqual(output.getvalue().count("Ratio allocation changed"), 1)
        self.assertNotIn("games=", output.getvalue())

    async def test_cancellation_waits_for_all_worker_cleanup(self):
        experiment = self.experiment()
        started, cleaned = set(), set()
        both_started = asyncio.Event()
        reporter_cleaned = asyncio.Event()

        async def report_progress(reporter):
            try:
                await asyncio.Event().wait()
            finally:
                await asyncio.sleep(0)
                reporter_cleaned.set()

        async def play_pair(exp, result, pair, cpu):
            started.add(pair)
            if len(started) == 2:
                both_started.set()
            try:
                await asyncio.Event().wait()
            finally:
                await asyncio.sleep(0)
                cleaned.add(pair)

        with (patch.object(matcher, "play_pair", play_pair),
              patch.object(matcher.ProgressReporter, "run", report_progress),
              contextlib.redirect_stdout(io.StringIO())):
            task = asyncio.create_task(matcher.run_matches(experiment))
            await asyncio.wait_for(both_started.wait(), 3)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        self.assertEqual(cleaned, started)
        self.assertTrue(reporter_cleaned.is_set())
        self.assertEqual(experiment.results[0].valid_pairs, 0)

    async def test_formal_stop_cancels_unfinished_suffix(self):
        experiment = self.experiment(test_alpha=.9)
        second_started, cancelled = asyncio.Event(), asyncio.Event()

        async def play_pair(exp, result, pair, cpu):
            if pair == 1:
                await second_started.wait()
                return outcomes("win")
            second_started.set()
            try:
                await asyncio.Event().wait()
            finally:
                cancelled.set()

        with patch.object(matcher, "play_pair", play_pair), contextlib.redirect_stdout(io.StringIO()):
            await asyncio.wait_for(matcher.run_matches(experiment), 3)
        self.assertTrue(cancelled.is_set())
        self.assertEqual(experiment.results[0].pair_scores, [1])
        self.assertEqual(experiment.results[0].decision, "better")

    async def test_adaptive_run_descends_then_certifies_the_boundary(self):
        experiment = self.experiment(ratios=[1, .75, .5], schedule="adaptive",
                                     max_pairs=80, test_alpha=.005)
        assignments = []

        async def play_pair(exp, result, pair, cpu):
            assignments.append((result.ratio, pair))
            await asyncio.sleep(0)
            return outcomes("win" if result.ratio >= .75 else "loss")

        output = io.StringIO()
        with patch.object(matcher, "play_pair", play_pair), contextlib.redirect_stdout(output):
            await asyncio.wait_for(matcher.run_matches(experiment), 3)
        by_ratio = {result.ratio: result for result in experiment.results}
        self.assertIn(by_ratio[.75].decision, ("noninferior", "better"))
        self.assertEqual(by_ratio[.5].decision, "inferior")
        # The successful lower ratio did not certify the untested higher
        # ratio by inference. Its own small sample remains inconclusive.
        self.assertEqual(by_ratio[1].decision, "inconclusive")
        self.assertIn("Provisional descent", output.getvalue())
        self.assertIn("Adaptive refinement", output.getvalue())
        for ratio in by_ratio:
            self.assertEqual([pair for m, pair in assignments if m == ratio],
                             list(range(1, 1 + sum(m == ratio for m, _ in assignments))))


if __name__ == "__main__":
    unittest.main()
