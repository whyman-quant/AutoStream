from pathlib import Path
import unittest

from evaluations.run_campaign_technical import evaluate_directory


class CampaignTechnicalTests(unittest.TestCase):
    def test_evaluates_all_mounted_checkpoints(self):
        directory = Path(
            "/home/fangwei/mnt-ssd/AutoStream/data/sfm_autoresearch_001/acceptance/"
            "20251013/book_imbalance/20251013"
        )
        if not directory.exists():
            self.skipTest("acceptance fixture is not mounted")
        report = evaluate_directory(str(directory))
        self.assertEqual(report["snapshot_count"], 8)
        self.assertEqual(report["factor_count"], 12)
        self.assertEqual(report["technical_status"], "technical_pass")
        self.assertEqual(report["evaluation_status"], "data_missing")
        self.assertTrue(all(item["factor_count"] == 12 for item in report["snapshots"]))


if __name__ == "__main__":
    unittest.main()
