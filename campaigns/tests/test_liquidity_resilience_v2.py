import json
import re
import unittest
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash, validate_idea_candidate_batch


ROOT = Path(__file__).resolve().parents[2]
CAMPAIGN = ROOT / "campaigns/sfm_stream_001"
BATCH = CAMPAIGN / "batches/liquidity_resilience_v2_seed_v1.json"
IDEA = CAMPAIGN / "ideas/liquidity_resilience_v2.json"
CANDIDATE_DIR = CAMPAIGN / "candidates/liquidity_resilience_v2"
METADATA = ROOT / "base/hf-open5m-factor-demo/factors/liquidity_resilience_v2/meta_config.h"


class LiquidityResilienceV2ContractTests(unittest.TestCase):
    def test_v2_batch_candidates_are_hashed_and_schema_valid(self):
        batch = json.loads(BATCH.read_text(encoding="utf-8"))
        idea = json.loads(IDEA.read_text(encoding="utf-8"))
        paths = [CANDIDATE_DIR / (candidate_id + ".json") for candidate_id in batch["candidate_ids"]]
        self.assertEqual(batch["status"], "proposed")
        self.assertEqual(len(paths), 12)
        for path in paths:
            candidate = json.loads(path.read_text(encoding="utf-8"))
            validate_document("candidate", candidate)
            self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
            self.assertEqual(candidate["availability"]["invalid_policy"], "unavailable")
            self.assertIn(
                candidate["availability"]["readiness_policy"],
                {"nan_until_full_window", "nan_until_shock_and_rebound"},
            )
        validate_idea_candidate_batch(paths, BATCH, IDEA)

    def test_metadata_names_match_v2_batch(self):
        text = METADATA.read_text(encoding="utf-8")
        names = re.findall(r'"(liquidity_resilience_v2_[a-z0-9_]+)"', text)
        batch = json.loads(BATCH.read_text(encoding="utf-8"))
        self.assertEqual(names, batch["candidate_ids"])


if __name__ == "__main__":
    unittest.main()
