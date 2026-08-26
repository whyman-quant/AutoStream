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
CANDIDATE_PATH = CAMPAIGN / "candidates" / "flow_pressure" / "flow_pressure_signed_trade_flow_w16.json"
METADATA_PATH = ROOT / "base" / "hf-open5m-factor-demo" / "factors" / "flow_pressure" / "meta_config.h"
RECEIPT_PATH = CAMPAIGN / "manifests" / "flow-pressure-vertical-slice.json"


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
        self.assertEqual(batch["candidate_ids"], [candidate["candidate_id"]])
        self.assertEqual(batch["search_policy"]["candidate_budget"], 1)
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
        validate_idea_candidate_batch([CANDIDATE_PATH], BATCH_PATH, IDEA_PATH)

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


if __name__ == "__main__":
    unittest.main()
