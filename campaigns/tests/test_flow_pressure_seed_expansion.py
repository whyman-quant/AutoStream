import json
import unittest
from collections import Counter
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.contracts.consistency import candidate_hash, validate_idea_candidate_batch


ROOT = Path(__file__).parents[2]
CAMPAIGN = ROOT / "campaigns" / "sfm_stream_001"
CANDIDATE_DIR = CAMPAIGN / "candidates" / "flow_pressure"
BATCH_PATH = CAMPAIGN / "batches" / "flow_pressure_seed_v1.json"
IDEA_PATH = CAMPAIGN / "ideas" / "flow_pressure.json"
REPRESENTATIVE_ID = "flow_pressure_signed_trade_flow_w16"


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


class FlowPressureSeedExpansionTests(unittest.TestCase):
    def setUp(self):
        self.paths = sorted(CANDIDATE_DIR.glob("*.json"))
        self.candidates = [load_json(path) for path in self.paths]

    def test_batch_contains_twelve_valid_unique_candidates(self):
        self.assertEqual(len(self.paths), 12)
        batch = load_json(BATCH_PATH)
        validate_document("batch", batch)
        self.assertEqual(batch["search_policy"]["candidate_budget"], 12)
        self.assertEqual(set(batch["candidate_ids"]), {item["candidate_id"] for item in self.candidates})
        validate_idea_candidate_batch(self.paths, BATCH_PATH, IDEA_PATH)

        hashes = []
        for candidate in self.candidates:
            validate_document("candidate", candidate)
            self.assertEqual(candidate["output"]["factor_name"], candidate["candidate_id"])
            self.assertEqual(candidate["canonical_hash"], candidate_hash(candidate))
            hashes.append(candidate["canonical_hash"])
        self.assertEqual(len(set(hashes)), 12)

    def test_seed_grid_covers_declared_operators_and_event_windows(self):
        operator_counts = Counter(item["operator_id"] for item in self.candidates)
        self.assertEqual(operator_counts, {
            "signed_trade_flow": 4,
            "trade_flow_zscore": 4,
            "decayed_trade_flow": 4,
        })
        for operator_id in operator_counts:
            windows = {
                item["parameters"]["window_events"]
                for item in self.candidates
                if item["operator_id"] == operator_id
            }
            self.assertEqual(windows, {16, 32, 64, 128})

        normalizations = {item["parameters"]["normalization"] for item in self.candidates}
        self.assertEqual(normalizations, {"signed_total_volume", "rolling_zscore", "exponential_decay"})
        self.assertGreaterEqual(
            sum(item["availability"]["lag_events"] > 0 for item in self.candidates),
            3,
        )

    def test_all_implemented_seeds_claim_l3_evidence_after_grid_pilot(self):
        evidence = {item["candidate_id"]: item["evidence_level"] for item in self.candidates}
        self.assertEqual(evidence[REPRESENTATIVE_ID], "L3")
        self.assertEqual(set(evidence.values()), {"L3"})
        for candidate in self.candidates:
            if candidate["candidate_id"] != REPRESENTATIVE_ID:
                self.assertEqual(candidate["lineage"]["source_commit"], "2ad6d1f")


if __name__ == "__main__":
    unittest.main()
