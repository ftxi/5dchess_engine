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
