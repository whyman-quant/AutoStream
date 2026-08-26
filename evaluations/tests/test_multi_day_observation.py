import unittest

from evaluations.multi_day_observation import summarize_metric_frame


class MultiDayObservationTests(unittest.TestCase):
    def test_reports_mean_and_sign_stability(self):
        rows = [
            {"factor": "f", "RankIC": 0.2, "IC": 0.1, "LS": 0.03, "Monotonicity": 0.4},
            {"factor": "f", "RankIC": -0.1, "IC": 0.0, "LS": -0.02, "Monotonicity": 0.2},
        ]
        report = summarize_metric_frame(rows)
        self.assertAlmostEqual(report["f"]["mean_rank_ic"], 0.05)
        self.assertAlmostEqual(report["f"]["rank_ic_positive_fraction"], 0.5)
        self.assertAlmostEqual(report["f"]["mean_long_short"], 0.005)


if __name__ == "__main__":
    unittest.main()
