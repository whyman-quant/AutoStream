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
CANDIDATE_DIR = CANDIDATE_PATH.parent
METADATA_PATH = ROOT / "base" / "hf-open5m-factor-demo" / "factors" / "liquidity_resilience" / "meta_config.h"

def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))

class LiquidityResilienceMigrationTests(unittest.TestCase):
    def setUp(self):
        self.paths = sorted(CANDIDATE_DIR.glob("*.json"))

    def test_representative_artifacts_form_one_consistent_vertical_slice(self):
        idea, batch, candidate = load_json(IDEA_PATH), load_json(BATCH_PATH), load_json(CANDIDATE_PATH)
        validate_document("idea_spec", idea)
        validate_document("batch", batch)
        validate_document("candidate", candidate)
        self.assertEqual(idea["representative_candidate_id"], candidate["candidate_id"])
        self.assertIn(candidate["candidate_id"], batch["candidate_ids"])
        self.assertEqual(candidate["operator_id"], "spread_adjusted_depth_recovery")
        self.assertEqual(candidate["source_streams"], ["quote"])
        self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
        validate_idea_candidate_batch(self.paths, BATCH_PATH, IDEA_PATH)

    def test_seed_batch_contains_twelve_schema_valid_unique_candidates(self):
        batch = load_json(BATCH_PATH)
        candidates = [load_json(path) for path in self.paths]
        self.assertEqual(len(candidates), 12)
        self.assertEqual(batch["search_policy"]["candidate_budget"], 12)
        self.assertEqual(set(batch["candidate_ids"]), {item["candidate_id"] for item in candidates})
        hashes = []
        for candidate in candidates:
            validate_document("candidate", candidate)
            self.assertEqual(candidate["output"]["factor_name"], candidate["candidate_id"])
            self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
            hashes.append(candidate["canonical_hash"])
        self.assertEqual(len(set(hashes)), 12)

    def test_seed_grid_has_explicit_mechanism_dimensions_and_honest_evidence(self):
        candidates = [load_json(path) for path in self.paths]
        representative_id = load_json(IDEA_PATH)["representative_candidate_id"]
        by_operator = {}
        for candidate in candidates:
            by_operator.setdefault(candidate["operator_id"], []).append(candidate)
            if candidate["candidate_id"] != representative_id:
                self.assertEqual(
                    candidate["parameters"]["lag_events"],
                    candidate["availability"]["lag_events"],
                )
                self.assertIn("normalization", candidate["parameters"])

        self.assertEqual(
            {key: len(value) for key, value in by_operator.items()},
            {
                "spread_adjusted_depth_recovery": 4,
                "multi_level_depth_recovery": 4,
                "shock_recovery_speed": 4,
            },
        )
        self.assertEqual(
            {item["parameters"]["window_events"] for item in candidates},
            {16, 32, 64, 128},
        )
        self.assertEqual(
            {item["availability"]["lag_events"] for item in candidates},
            {0, 1, 2},
        )

        evidence = {item["candidate_id"]: item["evidence_level"] for item in candidates}
        self.assertEqual(evidence[representative_id], "L2")
        self.assertEqual({level for key, level in evidence.items() if key != representative_id}, {"L2"})

    def test_candidate_name_matches_cpp_metadata(self):
        candidate = load_json(CANDIDATE_PATH)
        text = METADATA_PATH.read_text(encoding="utf-8")
        block = re.search(r"kFactorNames\s*=\s*\{(.*?)\};", text, re.S)
        self.assertIsNotNone(block)
        self.assertEqual(re.findall(r'"([^"]+)"', block.group(1)), load_json(BATCH_PATH)["candidate_ids"])

if __name__ == "__main__":
    unittest.main()
