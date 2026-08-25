import math
import importlib.util
from pathlib import Path
import tempfile
import unittest

from evaluations.real_label_scorecard import (
    build_scorecard,
    load_result_parquet,
    summarize_factor_metrics,
    validate_result_frame,
)


class RealLabelScorecardTests(unittest.TestCase):
    def test_summarizes_signed_metrics_without_choosing_direction(self):
        summary = summarize_factor_metrics({
            "RankIC": [0.10, 0.20, math.nan],
            "IC": [0.05, 0.15, 0.25],
            "LS": [-0.01, 0.02, 0.03],
            "Monotonicity": [0.4, 0.6, 0.8],
        })
        self.assertEqual(summary["event_count"], 3)
        self.assertEqual(summary["rank_ic_count"], 2)
        self.assertAlmostEqual(summary["mean_rank_ic"], 0.15)
        self.assertAlmostEqual(summary["mean_ic"], 0.15)
        self.assertAlmostEqual(summary["mean_long_short"], 0.04 / 3.0)
        self.assertNotIn("direction", summary)

    def test_missing_metric_is_json_null_not_nonstandard_nan(self):
        summary = summarize_factor_metrics({"RankIC": [0.1]})
        self.assertIsNone(summary["mean_ic"])
        self.assertIsNone(summary["mean_long_short"])

    def test_scorecard_rejects_incomplete_label_universe_matrix(self):
        with self.assertRaisesRegex(ValueError, "missing evaluation result"):
            build_scorecard(
                {("raw926", "000985"): {"factor_a": {"RankIC": [0.1]}}},
                labels=("raw926", "ease926"),
                universes=("000985",),
            )

    def test_scorecard_is_acceptance_only(self):
        matrix = {
            (label, universe): {"factor_a": {"RankIC": [0.1], "IC": [0.2], "LS": [0.01], "Monotonicity": [0.3]}}
            for label in ("raw926", "ease926")
            for universe in ("000985", "003800", "000906")
        }
        scorecard = build_scorecard(matrix)
        self.assertEqual(scorecard["evaluation_scope"], "single_day_acceptance")
        self.assertFalse(scorecard["promotion_allowed"])
        self.assertEqual(scorecard["combination_count"], 6)

    @unittest.skipUnless(importlib.util.find_spec("pandas") and importlib.util.find_spec("pyarrow"), "pandas/pyarrow unavailable")
    def test_loads_flattened_toolkit_parquet(self):
        import pandas as pd

        events = [92600000, 100000000, 103000000, 110000000, 113000000, 133000000, 140000000, 143000000]
        frame = pd.DataFrame({
            "factor_a|RankIC": [0.1] * 8,
            "factor_a|IC": [0.3] * 8,
            "factor_a|LS": [0.01] * 8,
            "factor_a|Monotonicity": [0.5] * 8,
        }, index=pd.MultiIndex.from_product([["20251013"], events], names=["date", "event"]))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "res_full.parquet"
            frame.to_parquet(path)
            result = load_result_parquet(str(path))
        self.assertEqual(result["factor_a"]["RankIC"], [0.1] * 8)
        self.assertEqual(result["factor_a"]["LS"], [0.01] * 8)

    @unittest.skipUnless(importlib.util.find_spec("pandas") and importlib.util.find_spec("pyarrow"), "pandas/pyarrow unavailable")
    def test_rejects_result_with_wrong_event_set(self):
        import pandas as pd

        frame = pd.DataFrame(
            {
                "factor_a|RankIC": [0.1],
                "factor_a|IC": [0.2],
                "factor_a|LS": [0.01],
                "factor_a|Monotonicity": [0.3],
            },
            index=pd.MultiIndex.from_tuples([('20251013', 93000000)], names=['date', 'event']),
        )
        with self.assertRaisesRegex(ValueError, "event set"):
            validate_result_frame(frame)


if __name__ == "__main__":
    unittest.main()
