import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
MANIFEST_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-dataset-v1.json"
DATE_LIST_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-dates-v1.txt"
MARKET_ROOT = Path("/mnt/beegfs_ssd_raid91/706_wgh_new/stock_open/basedata")
LABEL_ROOT = Path("/home/fangwei/beta_team_share/sfutils/factor_zoo/data/arrow_label_zoo/huyifan/atan_day_myrisk_neuted_ease_926")


def load_manifest():
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


class FormalHistoryDatasetTests(unittest.TestCase):
    def test_manifest_freezes_exact_intersection_and_hash(self):
        manifest = load_manifest()
        source_dates = sorted(
            {path.name for path in MARKET_ROOT.iterdir() if path.name[:8].isdigit()}
            & {path.stem for path in LABEL_ROOT.glob("*.arrow") if path.stem[:8].isdigit()}
        )
        dates = source_dates
        self.assertEqual(DATE_LIST_PATH.read_text(encoding="utf-8"), "\n".join(dates) + "\n")
        self.assertEqual(manifest["date_list_path"], "campaigns/sfm_stream_001/manifests/formal-history-dates-v1.txt")
        self.assertEqual(manifest["date_count"], len(dates))
        self.assertEqual(len(dates), 2171)
        self.assertEqual(manifest["date_start"], dates[0])
        self.assertEqual(manifest["date_end"], dates[-1])
        self.assertEqual(
            hashlib.sha256(("\n".join(dates) + "\n").encode()).hexdigest(),
            manifest["date_list_sha256"].removeprefix("sha256:"),
        )

    def test_splits_are_exact_non_overlapping_and_holdout_is_sealed(self):
        manifest = load_manifest()
        dates = {
            path.name
            for path in MARKET_ROOT.iterdir()
            if path.name[:8].isdigit()
        } & {
            path.stem
            for path in LABEL_ROOT.glob("*.arrow")
            if path.stem[:8].isdigit()
        }
        splits = manifest["splits"]
        sets = {
            name: {date for date in dates if spec["date_start"] <= date <= spec["date_end"]}
            for name, spec in splits.items()
        }
        self.assertEqual({name: len(value) for name, value in sets.items()}, {"training": 1459, "observation": 484, "holdout": 228})
        self.assertEqual(set().union(*sets.values()), dates)
        self.assertEqual(sum(len(value) for value in sets.values()), len(dates))
        self.assertEqual(splits["training"]["holdout_access"], "sealed")
        self.assertEqual(splits["observation"]["holdout_access"], "sealed")
        self.assertEqual(splits["holdout"]["holdout_access"], "sealed_until_l6")
        self.assertEqual(manifest["leakage_policy"]["holdout_may_read"], [])

    def test_events_labels_universes_evaluator_and_l4_stops_are_frozen(self):
        manifest = load_manifest()
        self.assertEqual(manifest["events"]["count"], 8)
        self.assertEqual(manifest["events"]["source_snapshots"], [92700000, 100000000, 103000000, 110000000, 113000000, 133000000, 140000000, 143000000])
        self.assertEqual(manifest["events"]["evaluation_events"][0], 92600000)
        self.assertEqual(manifest["labels"]["names"], ["raw926", "ease926"])
        self.assertEqual(manifest["universes"], ["000985", "003800", "000906"])
        self.assertEqual(manifest["evaluation"]["metrics"], ["RankIC", "IC", "D1-D10", "LS", "Monotonicity"])
        self.assertEqual(manifest["evaluation"]["evaluator_commit"], "8690c39333e30b3a868c33b2c307c0499f229770")
        self.assertFalse(manifest["formal_production_started"])
        self.assertFalse(manifest["promotion_allowed"])
        self.assertIn("required event missing", manifest["stop_conditions"]["hard_stop"])
        self.assertIn("using holdout to generate candidates", manifest["leakage_policy"]["forbidden"])


if __name__ == "__main__":
    unittest.main()
