from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "base/hf-open5m-factor-demo/factors/book_imbalance"


class BookImbalanceModuleTests(unittest.TestCase):
    def test_module_declares_at_least_the_seed_campaign_factors(self):
        metadata = (MODULE / "meta_config.h").read_text()
        import json
        seed = json.loads((ROOT / "campaigns/sfm_stream_001/batches/book_imbalance_seed_v1.json").read_text())
        expected = len(seed["candidate_ids"])
        self.assertRegex(metadata, r"kFactorSize = [0-9]+")
        self.assertGreaterEqual(metadata.count('"book_imbalance_'), expected)
        self.assertTrue((MODULE / "factor_entry.cpp").is_file())
        self.assertTrue((MODULE / "CMakeLists.txt").is_file())

    def test_production_config_uses_the_same_eight_checkpoints(self):
        import json

        config = json.loads((MODULE.parents[1] / "config_factor_sfm_stream_001.json").read_text())
        factors = config["factors_config"]
        self.assertEqual(
            factors["ev"],
            "/mnt/beegfs_ssd_raid91/706_wgh_new/stock_open/basedata",
        )
        self.assertEqual(
            factors["ev_code_file"],
            "[DATE]/per1day/lab200005_codelist.h5",
        )
        self.assertEqual(factors["factor_sets"], [{"name": "book_imbalance", "enabled": True}])
        self.assertEqual(factors["save_info"]["save_times"], [
            92700, 100000, 103000, 110000, 113000, 133000, 140000, 143000,
        ])


if __name__ == "__main__":
    unittest.main()
