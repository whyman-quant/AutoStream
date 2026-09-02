import tempfile
import unittest
from pathlib import Path

import pandas as pd

from evaluations.l4_portrait import build_portraits
from campaigns.contracts import validate_document


class L4PortraitTests(unittest.TestCase):
    def _write(self, root, dates=("20210104", "20210105")):
        factors = ["f1", "f2"]
        index = pd.MultiIndex.from_product([dates, [1, 2]], names=["date", "event"])
        frame = pd.DataFrame(index=index)
        for factor in factors:
            for metric in (["D{}".format(i) for i in range(1, 11)] + ["RankIC", "IC", "LS", "Monotonicity"]):
                frame[factor + "|" + metric] = [0.1, 0.2, -0.1, 0.0]
        for label in ("raw926",):
            for universe in ("000985",):
                path = Path(root) / label / universe
                path.mkdir(parents=True)
                frame.to_parquet(path / "res_full.parquet")
        return factors

    def test_builds_complete_portrait_and_aggregates(self):
        with tempfile.TemporaryDirectory() as tmp:
            factors = self._write(tmp)
            report = build_portraits(Path(tmp), ["20210104", "20210105"], ["raw926"], ["000985"], coverage_threshold=0.95)
        self.assertEqual(report["factor_count"], 2)
        self.assertEqual(report["date_count"], 2)
        self.assertAlmostEqual(report["portraits"]["f1"]["splits"]["all"]["raw926"]["000985"]["rank_ic_mean"], 0.05)
        self.assertIn("rolling_stability", report["portraits"]["f1"])
        self.assertEqual(set(report["portraits"]["f1"]["metrics"]) >= {"std", "ir", "positive_fraction"}, True)
        self.assertEqual(set(report["factors"]), set(factors))

    def test_rejects_holdout_dates(self):
        with tempfile.TemporaryDirectory() as tmp:
            self._write(tmp, dates=("20210104", "20210105"))
            with self.assertRaises(ValueError):
                build_portraits(Path(tmp), ["20210104", "20250102"], ["raw926"], ["000985"])

    def test_v2_schema_is_dispatched(self):
        document = {"schema_version": 2, "kind": "factor_portrait", "portrait_id": "p", "campaign_id": "c", "factor": "f", "scope": "formal_history", "evidence_level": "L4", "dataset": {"date_start": "20210104", "date_end": "20210105", "date_count": 2, "events": [1, 2], "labels": ["raw926"], "universes": ["000985"]}, "coverage": 1.0, "direction": "raw_signed", "promotion_allowed": False, "metrics": {}, "decision": "observation_only"}
        validate_document("factor_portrait", document)


if __name__ == "__main__":
    unittest.main()
