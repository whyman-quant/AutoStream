import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).parents[2]
MANIFEST_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-dataset-v1.json"
DATE_LIST_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-dates-v1.txt"
V2_MANIFEST_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-dataset-v2.json"
V2_PRODUCTION_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-production-dates-v2.txt"
V2_HOLDOUT_PATH = ROOT / "campaigns" / "sfm_stream_001" / "manifests" / "formal-history-holdout-dates-v2.txt"
CAMPAIGN_PATH = ROOT / "campaigns" / "sfm_stream_001" / "campaign.json"
MARKET_ROOT = Path("/mnt/beegfs_ssd_raid91/706_wgh_new/stock_open/basedata")
LABEL_ROOT = Path("/home/fangwei/beta_team_share/sfutils/factor_zoo/data/arrow_label_zoo/huyifan/atan_day_myrisk_neuted_ease_926")


def load_manifest():
    return json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))


def load_v2_manifest():
    return json.loads(V2_MANIFEST_PATH.read_text(encoding="utf-8"))


class FormalHistoryDatasetTests(unittest.TestCase):
    def test_v2_is_strict_recent_history_subset_with_frozen_split_hashes(self):
        v1 = load_manifest()
        v2 = load_v2_manifest()
        v1_dates = DATE_LIST_PATH.read_text(encoding="utf-8").splitlines()
        production = V2_PRODUCTION_PATH.read_text(encoding="utf-8")
        holdout = V2_HOLDOUT_PATH.read_text(encoding="utf-8")
        production_dates = production.splitlines()
        holdout_dates = holdout.splitlines()

        self.assertEqual(v2["parent_dataset_id"], v1["dataset_id"])
        self.assertEqual(v2["parent_manifest"], "campaigns/sfm_stream_001/manifests/formal-history-dataset-v1.json")
        self.assertEqual(v2["parent_date_list_path"], v1["date_list_path"])
        self.assertEqual(v2["parent_date_list_sha256"], v1["date_list_sha256"])
        self.assertEqual(v2["production_date_list_path"], "campaigns/sfm_stream_001/manifests/formal-history-production-dates-v2.txt")
        self.assertEqual(v2["holdout_date_list_path"], "campaigns/sfm_stream_001/manifests/formal-history-holdout-dates-v2.txt")
        self.assertEqual(v2["date_list_path"], v2["production_date_list_path"])
        self.assertEqual(v2["date_list_sha256"], v2["production_date_list_sha256"])
        self.assertEqual(production, "\n".join(production_dates) + "\n")
        self.assertEqual(holdout, "\n".join(holdout_dates) + "\n")
        self.assertTrue(all(len(d) == 8 and d.isdigit() for d in production_dates + holdout_dates))
        self.assertEqual(production_dates, [d for d in v1_dates if "20210104" <= d <= "20241231"])
        self.assertEqual(holdout_dates, [d for d in v1_dates if "20250102" <= d <= "20251210"])
        self.assertEqual(len(production_dates), 969)
        self.assertEqual(len(holdout_dates), 228)
        self.assertEqual(v2["date_count"], 969)
        self.assertEqual(v2["date_start"], "20210104")
        self.assertEqual(v2["date_end"], "20241231")
        self.assertEqual(v2["splits"]["training"]["date_count"], 485)
        self.assertEqual(v2["splits"]["observation"]["date_count"], 484)
        self.assertEqual(v2["splits"]["holdout"]["date_count"], 228)
        self.assertLess(set(production_dates) | set(holdout_dates), set(v1_dates))
        self.assertEqual(set(production_dates) & set(holdout_dates), set())
        training = {d for d in production_dates if "20210104" <= d <= "20221230"}
        observation = {d for d in production_dates if "20230103" <= d <= "20241231"}
        self.assertEqual((len(training), len(observation)), (485, 484))
        self.assertEqual(training & observation, set())
        self.assertEqual(training | observation, set(production_dates))
        self.assertEqual(v2["date_list_sha256"].removeprefix("sha256:"), hashlib.sha256(production.encode()).hexdigest())
        self.assertEqual(v2["holdout_date_list_sha256"].removeprefix("sha256:"), hashlib.sha256(holdout.encode()).hexdigest())

    def test_v2_preserves_frozen_contract_and_campaign_pointer(self):
        v1 = load_manifest()
        v2 = load_v2_manifest()
        campaign = json.loads(CAMPAIGN_PATH.read_text(encoding="utf-8"))
        self.assertEqual(campaign["formal_history_dataset_manifest"], "manifests/formal-history-dataset-v2.json")
        self.assertEqual(campaign["formal_history_dataset_id"], v2["dataset_id"])
        self.assertEqual(campaign["formal_history_status"], "frozen_definition_only")
        self.assertTrue((ROOT / "campaigns/sfm_stream_001/manifests/formal-history-dataset-v1.json").is_file())
        self.assertEqual(v2["events"], v1["events"])
        self.assertEqual(v2["labels"], v1["labels"])
        self.assertEqual(v2["universes"], v1["universes"])
        self.assertEqual(v2["evaluation"], v1["evaluation"])
        self.assertEqual(v2["data_quality"], v1["data_quality"])
        self.assertEqual(v2["leakage_policy"], v1["leakage_policy"])
        self.assertEqual(v2["stop_conditions"], v1["stop_conditions"])
        self.assertFalse(v2["formal_production_started"])
        self.assertFalse(v2["promotion_allowed"])
        self.assertEqual(v2["splits"]["holdout"]["holdout_access"], "sealed_until_l6")

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
