from __future__ import annotations

import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import iteration_ratio_match as tested


class IterationBudgetTest(unittest.TestCase):
    def test_budget_rounds_up_to_the_engine_depth_unit(self) -> None:
        requested, allowed = tested.iteration_budget(101, 0.75)
        self.assertEqual(requested, 76)
        self.assertEqual(allowed, 80)

    def test_time_cap_scales_inversely_with_ips_ratio(self) -> None:
        self.assertEqual(tested.target_time_cap_ms(400, 0.5, 2.0, 10_000), 1600)
        self.assertEqual(tested.target_time_cap_ms(400, 0.01, 2.0, 5000), 5000)


class ExactBinomialTest(unittest.TestCase):
    def test_extreme_tails(self) -> None:
        expected = 1.0 / 1024.0
        self.assertAlmostEqual(tested.binomial_upper_tail(10, 10, 0.5), expected)
        self.assertAlmostEqual(tested.binomial_lower_tail(0, 10, 0.5), expected)

    def test_ratio_decisions(self) -> None:
        better = tested.RatioResult(
            1.0, pair_wins=16, pair_losses=0, pair_score_sum=16.0
        )
        self.assertEqual(tested.evaluate_ratio(better, 0.01, 0.10), "better")

        inferior = tested.RatioResult(1.0, pair_wins=0, pair_losses=16)
        self.assertEqual(tested.evaluate_ratio(inferior, 0.01, 0.10), "inferior")

        equal = tested.RatioResult(
            1.0, pair_wins=128, pair_losses=128, pair_score_sum=128.0
        )
        self.assertEqual(tested.evaluate_ratio(equal, 0.026, 0.10), "noninferior")
        self.assertGreater(equal.superiority_p, 0.026)

        tied = tested.RatioResult(1.0, pair_ties=256, pair_score_sum=128.0)
        self.assertEqual(tested.evaluate_ratio(tied, 0.026, 0.10), "noninferior")


class AnytimeEvidenceTest(unittest.TestCase):
    def test_superiority_e_value_crosses_fixed_boundary(self) -> None:
        sixteen_wins = tested.bounded_mean_log_e([1.0] * 16, 0.5, 0.6)
        seventeen_wins = tested.bounded_mean_log_e([1.0] * 17, 0.5, 0.6)
        self.assertLess(math.exp(sixteen_wins), 20.0)
        self.assertGreater(math.exp(seventeen_wins), 20.0)

    def test_noninferiority_uses_pair_ties(self) -> None:
        result = tested.RatioResult(0.75)
        result.pair_scores = [0.5] * 74
        result.pair_ties = 74
        result.pair_score_sum = 37.0
        decision = tested.evaluate_anytime_ratio(result, 0.05, 0.10, 0.60)
        self.assertEqual(decision, "noninferior")
        self.assertGreaterEqual(result.noninferiority_max_log_e, math.log(20.0))

    def test_inferiority_uses_the_symmetric_lower_bet(self) -> None:
        result = tested.RatioResult(0.25)
        result.pair_scores = [0.0] * 17
        result.pair_losses = 17
        decision = tested.evaluate_anytime_ratio(result, 0.05, 0.10, 0.60)
        self.assertEqual(decision, "inferior")

    def test_anytime_p_retains_earlier_peak(self) -> None:
        result = tested.RatioResult(1.0)
        result.pair_scores = [1.0] * 17
        tested.update_anytime_evidence(result, 0.10, 0.60)
        crossed_p = result.superiority_p
        result.pair_scores.extend([0.0] * 10)
        tested.update_anytime_evidence(result, 0.10, 0.60)
        self.assertEqual(result.superiority_p, crossed_p)
        self.assertLessEqual(result.superiority_p, 0.05)

    def test_add_pair_records_normalized_score(self) -> None:
        result = tested.RatioResult(1.0)
        result.add_pair(
            [
                tested.GameResult("win", "", 1, ""),
                tested.GameResult("draw", "", 1, ""),
            ]
        )
        self.assertEqual(result.pair_scores, [0.75])
        self.assertEqual(
            (result.pair_wins, result.pair_ties, result.pair_losses),
            (1, 0, 0),
        )


class ThroughputTest(unittest.TestCase):
    def test_reports_target_over_zero(self) -> None:
        throughput = tested.Throughput()
        zero = tested.SearchSample(1000, 1.0)
        target = tested.SearchSample(500, 1.0)
        throughput.add_zero(zero)
        throughput.add_pair(target, zero)
        self.assertTrue(math.isclose(throughput.aggregate_ratio, 0.5))
        self.assertTrue(math.isclose(throughput.paired_geometric_ratio, 0.5))


if __name__ == "__main__":
    unittest.main()
