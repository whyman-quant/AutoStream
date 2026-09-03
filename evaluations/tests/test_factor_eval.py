import math
from pathlib import Path
import unittest

from evaluations.factor_eval import (
    read_hdf5_factor_file,
    evaluate_columns,
    portfolio_metrics,
    quantile_group_returns,
    rank_ic,
)


class FactorEvalTests(unittest.TestCase):
    def test_rank_ic_is_one_for_monotonic_factor_and_return(self):
        self.assertAlmostEqual(rank_ic([1.0, 2.0, 3.0, 4.0], [0.1, 0.2, 0.3, 0.4]), 1.0)

    def test_rank_ic_ignores_non_finite_pairs(self):
        value = rank_ic([1.0, math.nan, 3.0, 4.0], [0.1, 0.2, math.inf, 0.4])
        self.assertAlmostEqual(value, 1.0)

    def test_quantile_groups_return_monotonic_spread(self):
        result = quantile_group_returns([1, 2, 3, 4, 5, 6], [1, 1, 2, 2, 3, 3], groups=3)
        self.assertEqual(result["group_count"], 3)
        self.assertEqual(result["group_returns"], [1.0, 2.0, 3.0])
        self.assertAlmostEqual(result["long_short"], 2.0)

    def test_quality_rejects_constant_and_post_warmup_missing_columns(self):
        result = evaluate_columns({
            "constant": [math.nan, 1.0, 1.0, 1.0],
            "missing": [math.nan, math.nan, 2.0, math.nan],
            "valid_zero": [math.nan, 0.0, 0.0, 1.0],
        }, warmup=1, min_post_warmup_coverage=0.5)
        self.assertEqual(result["constant"]["status"], "technical_reject")
        self.assertIn("constant_column", result["constant"]["reject_reasons"])
        self.assertEqual(result["missing"]["status"], "technical_reject")
        self.assertIn("post_warmup_coverage_below_threshold", result["missing"]["reject_reasons"])
        self.assertEqual(result["valid_zero"]["status"], "technical_pass")
        self.assertEqual(result["valid_zero"]["zero_count"], 2)

    def test_missing_labels_is_explicit(self):
        result = evaluate_columns({"factor": [1.0, 2.0]}, labels=None)
        self.assertEqual(result["factor"]["evaluation_status"], "data_missing")

    def test_event_cross_section_metrics_reject_degenerate_values(self):
        result = evaluate_columns(
            {"factor": [0.0, 0.0, 0.0, 0.0]},
            min_cross_section_std=0.01,
            min_unique_count=2,
            min_nonzero_ratio=0.25,
            min_effective_rank_count=2,
        )["factor"]
        self.assertEqual(result["cross_section_std"], 0.0)
        self.assertEqual(result["unique_count"], 1)
        self.assertEqual(result["nonzero_ratio"], 0.0)
        self.assertEqual(result["effective_rank_count"], 1)
        self.assertIn("cross_section_std_below_threshold", result["reject_reasons"])
        self.assertIn("cross_section_unique_count_below_threshold", result["reject_reasons"])
        self.assertIn("cross_section_nonzero_ratio_below_threshold", result["reject_reasons"])
        self.assertIn("cross_section_effective_rank_count_below_threshold", result["reject_reasons"])

    def test_readiness_mask_separates_not_ready_zero_from_real_zero(self):
        result = evaluate_columns(
            {"factor": [0.0, 0.0, 1.0, 2.0]},
            ready_masks={"factor": [False, True, True, True]},
            min_post_warmup_coverage=0.75,
        )["factor"]
        self.assertEqual(result["not_ready_count"], 1)
        self.assertEqual(result["not_ready_zero_count"], 1)
        self.assertEqual(result["ready_count"], 3)
        self.assertEqual(result["zero_count"], 1)
        self.assertAlmostEqual(result["nonzero_ratio"], 2.0 / 3.0)

    def test_reads_factor_hdf5_schema_without_python_hdf5_package(self):
        path = Path("/home/fangwei/mnt-ssd/AutoStream/data/sfm_autoresearch_001/acceptance/20251013/book_imbalance/20251013/092700.h5")
        if not path.exists():
            self.skipTest("acceptance fixture is not mounted")
        columns = read_hdf5_factor_file(str(path))
        self.assertEqual(len(columns), 12)
        self.assertEqual(len(next(iter(columns.values()))), 4964)

    def test_portfolio_metrics_include_risk_and_drawdown(self):
        metrics = portfolio_metrics([0.10, -0.05, 0.02], periods_per_year=3)
        self.assertAlmostEqual(metrics["cumulative_return"], 1.10 * 0.95 * 1.02 - 1.0)
        self.assertAlmostEqual(metrics["volatility"], 0.1061446, places=5)
        self.assertAlmostEqual(metrics["max_drawdown"], -0.05, places=8)
        self.assertGreater(metrics["sharpe"], 0.0)
        self.assertGreater(metrics["calmar"], 0.0)

    def test_portfolio_metrics_report_turnover_from_weights(self):
        metrics = portfolio_metrics(
            [0.01, 0.02],
            weights=[[1.0, 0.0], [0.5, 0.5], [0.5, 0.5]],
            periods_per_year=252,
        )
        self.assertAlmostEqual(metrics["average_turnover"], 0.25)


if __name__ == "__main__":
    unittest.main()
