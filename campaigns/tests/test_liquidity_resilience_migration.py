import json
import re
import unittest
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash, validate_idea_candidate_batch

ROOT = Path(__file__).parents[2]
CAMPAIGN = ROOT / "campaigns" / "sfm_stream_001"
IDEA_PATH = CAMPAIGN / "ideas" / "liquidity_resilience.json"
BATCH_PATH = CAMPAIGN / "batches" / "liquidity_resilience_seed_v1.json"
CANDIDATE_PATH = CAMPAIGN / "candidates" / "liquidity_resilience" / "liquidity_resilience_spread_adjusted_depth_recovery_w16.json"
METADATA_PATH = ROOT / "base" / "hf-open5m-factor-demo" / "factors" / "liquidity_resilience" / "meta_config.h"

def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))

class LiquidityResilienceMigrationTests(unittest.TestCase):
    def test_representative_artifacts_form_one_consistent_vertical_slice(self):
        idea, batch, candidate = load_json(IDEA_PATH), load_json(BATCH_PATH), load_json(CANDIDATE_PATH)
        validate_document("idea_spec", idea)
        validate_document("batch", batch)
        validate_document("candidate", candidate)
        self.assertEqual(idea["representative_candidate_id"], candidate["candidate_id"])
        self.assertEqual(batch["candidate_ids"], [candidate["candidate_id"]])
        self.assertEqual(candidate["operator_id"], "spread_adjusted_depth_recovery")
        self.assertEqual(candidate["source_streams"], ["quote"])
        self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
        validate_idea_candidate_batch([CANDIDATE_PATH], BATCH_PATH, IDEA_PATH)

    def test_candidate_name_matches_cpp_metadata(self):
        candidate = load_json(CANDIDATE_PATH)
        text = METADATA_PATH.read_text(encoding="utf-8")
        block = re.search(r"kFactorNames\s*=\s*\{(.*?)\};", text, re.S)
        self.assertIsNotNone(block)
        self.assertEqual(re.findall(r'"([^"]+)"', block.group(1)), [candidate["candidate_id"]])

if __name__ == "__main__":
    unittest.main()
