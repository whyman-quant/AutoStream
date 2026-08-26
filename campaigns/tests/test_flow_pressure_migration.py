import json
import re
import unittest
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash, validate_idea_candidate_batch


ROOT = Path(__file__).parents[2]
CAMPAIGN = ROOT / "campaigns" / "sfm_stream_001"
IDEA_PATH = CAMPAIGN / "ideas" / "flow_pressure.json"
BATCH_PATH = CAMPAIGN / "batches" / "flow_pressure_seed_v1.json"
CANDIDATE_DIR = CAMPAIGN / "candidates" / "flow_pressure"
CANDIDATE_PATH = CANDIDATE_DIR / "flow_pressure_signed_trade_flow_w16.json"
METADATA_PATH = ROOT / "base" / "hf-open5m-factor-demo" / "factors" / "flow_pressure" / "meta_config.h"
RECEIPT_PATH = CAMPAIGN / "manifests" / "flow-pressure-vertical-slice.json"
SMOKE_RECEIPT_PATH = CAMPAIGN / "manifests" / "flow-pressure-l3-smoke-20251014-000001.json"
FULL_DAY_RECEIPT_PATH = CAMPAIGN / "manifests" / "flow-pressure-l3-single-day-20251014.json"
PILOT_RECEIPT_PATH = CAMPAIGN / "manifests" / "flow-pressure-l3-pilot-20251009-20251031.json"


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


class FlowPressureMigrationTests(unittest.TestCase):
    def test_representative_artifacts_form_one_consistent_vertical_slice(self):
        idea = load_json(IDEA_PATH)
        batch = load_json(BATCH_PATH)
        candidate = load_json(CANDIDATE_PATH)
        validate_document("idea_spec", idea)
        validate_document("batch", batch)
        validate_document("candidate", candidate)

        self.assertEqual(idea["representative_candidate_id"], candidate["candidate_id"])
        self.assertIn(candidate["candidate_id"], batch["candidate_ids"])
        self.assertEqual(batch["search_policy"]["candidate_budget"], len(batch["candidate_ids"]))
        self.assertEqual(candidate["operator_id"], "signed_trade_flow")
        self.assertEqual(candidate["source_streams"], ["trade"])
        self.assertEqual(candidate["parameters"], {
            "window_events": 16,
            "normalization": "signed_total_volume",
            "include_cancels": False,
        })
        self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
        self.assertEqual(candidate_hash(candidate), candidate_hash(candidate))
        self.assertEqual(candidate["output"]["research_direction"], "raw_signed")
        validate_idea_candidate_batch(sorted(CANDIDATE_DIR.glob("*.json")), BATCH_PATH, IDEA_PATH)

    def test_candidate_name_matches_cpp_metadata(self):
        candidate = load_json(CANDIDATE_PATH)
        text = METADATA_PATH.read_text(encoding="utf-8")
        names_block = re.search(r"kFactorNames\s*=\s*\{(.*?)\};", text, re.S)
        self.assertIsNotNone(names_block)
        names = re.findall(r'"([^"]+)"', names_block.group(1))
        self.assertEqual(names, [candidate["candidate_id"]])

    def test_l2_receipt_does_not_claim_real_data_pilot(self):
        candidate = load_json(CANDIDATE_PATH)
        batch = load_json(BATCH_PATH)
        receipt = load_json(RECEIPT_PATH)
        self.assertEqual(candidate["evidence_level"], "L2")
        self.assertEqual(candidate["lineage"]["source_commit"], "0322592")
        self.assertEqual(batch["status"], "technical_complete")
        self.assertEqual(receipt["evidence_level"], "L2")
        self.assertEqual(receipt["candidate_hash"], candidate["canonical_hash"])
        self.assertTrue(receipt["synthetic_semantic_tests_passed"])
        self.assertTrue(receipt["prefix_causality_test_passed"])
        self.assertFalse(receipt["real_data_pilot_run"])
        self.assertFalse(receipt["promotion_allowed"])

    def test_real_data_smoke_receipt_stays_below_l3_completion(self):
        receipt = load_json(SMOKE_RECEIPT_PATH)
        self.assertEqual(receipt["scope"], "single_stock_real_data_smoke")
        self.assertEqual(receipt["trading_date"], "20251014")
        self.assertEqual(receipt["stock"], "000001")
        self.assertEqual(receipt["checkpoint_count"], 8)
        self.assertEqual(receipt["factor_count"], 1)
        self.assertEqual(receipt["arrow_rows"], 8)
        self.assertTrue(receipt["all_values_finite"])
        self.assertFalse(receipt["all_values_zero"])
        self.assertFalse(receipt["constant_across_checkpoints"])
        self.assertEqual(receipt["decision"], "continue_to_full_market_single_day")
        self.assertFalse(receipt["l3_complete"])
        self.assertFalse(receipt["promotion_allowed"])

    def test_full_day_receipt_requires_six_label_universe_results(self):
        receipt = load_json(FULL_DAY_RECEIPT_PATH)
        self.assertEqual(receipt["scope"], "single_day_full_market_vertical_slice")
        self.assertEqual(receipt["trading_date"], "20251014")
        self.assertEqual(receipt["stock_count"], 4968)
        self.assertEqual(receipt["checkpoint_count"], 8)
        self.assertEqual(receipt["arrow_rows"], 39744)
        self.assertEqual(receipt["factor_count"], 1)
        self.assertEqual(receipt["evaluation_combination_count"], 6)
        self.assertEqual(set(receipt["labels"]), {"raw926", "ease926"})
        self.assertEqual(set(receipt["universes"]), {"000985", "003800", "000906"})
        self.assertTrue(receipt["strict_hdf5_passed"])
        self.assertTrue(receipt["strict_arrow_passed"])
        self.assertTrue(receipt["scorecard_passed"])
        self.assertFalse(receipt["l3_complete"])
        self.assertFalse(receipt["promotion_allowed"])

    def test_full_pilot_receipt_requires_seventeen_days_and_observation_only(self):
        receipt = load_json(PILOT_RECEIPT_PATH)
        self.assertEqual(receipt["scope"], "seventeen_day_real_data_pilot")
        self.assertEqual(receipt["date_count"], 17)
        self.assertEqual(receipt["checkpoint_count"], 8)
        self.assertEqual(receipt["factor_count"], 1)
        self.assertEqual(receipt["evaluation_combination_count"], 6)
        self.assertTrue(receipt["all_dates_produced"])
        self.assertTrue(receipt["all_arrow_strict_checks_passed"])
        self.assertTrue(receipt["all_evaluations_completed"])
        self.assertEqual(receipt["decision"], "observation_only")
        self.assertFalse(receipt["promotion_allowed"])


if __name__ == "__main__":
    unittest.main()
