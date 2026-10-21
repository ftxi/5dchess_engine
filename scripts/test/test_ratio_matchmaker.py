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
from unittest.mock import patch

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

        with patch.object(matcher, "play_pair", play_pair), contextlib.redirect_stdout(io.StringIO()):
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

    async def test_cancellation_waits_for_all_worker_cleanup(self):
        experiment = self.experiment()
        started, cleaned = set(), set()
        both_started = asyncio.Event()

        async def play_pair(exp, result, pair, cpu):
            started.add(pair)
            if len(started) == 2:
                both_started.set()
            try:
                await asyncio.Event().wait()
            finally:
                await asyncio.sleep(0)
                cleaned.add(pair)

        with patch.object(matcher, "play_pair", play_pair), contextlib.redirect_stdout(io.StringIO()):
            task = asyncio.create_task(matcher.run_matches(experiment))
            await asyncio.wait_for(both_started.wait(), 3)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
        self.assertEqual(cleaned, started)
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
