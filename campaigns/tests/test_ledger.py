import json
import tempfile
import unittest
from pathlib import Path

from campaigns.ledger import append_event, validate_event, validate_ledger


class LedgerTests(unittest.TestCase):
    def test_appends_valid_event_as_jsonl(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "events.jsonl"
            event = {
                "experiment_id": "exp-001",
                "campaign_id": "sfm_stream_001",
                "family_id": "book_imbalance",
                "candidate_id": "candidate-001",
                "status": "data_missing",
                "source_commit": "abc1234",
                "config_hash": "sha256:config",
                "data_snapshot": "20251013",
                "evaluator_version": "1",
            }
            append_event(path, event)
            self.assertEqual(json.loads(path.read_text())["status"], "data_missing")

    def test_rejects_unknown_status_and_missing_lineage(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "events.jsonl"
            with self.assertRaises(ValueError):
                append_event(path, {"status": "promoted"})
            with self.assertRaises(ValueError):
                append_event(path, {"experiment_id": "x", "campaign_id": "c", "family_id": "f", "candidate_id": "x", "status": "unknown", "source_commit": "a", "config_hash": "b", "data_snapshot": "d", "evaluator_version": "1"})

    def test_accepts_observation_only_without_promotion(self):
        event = {
            "experiment_id": "pilot-001",
            "campaign_id": "sfm_stream_001",
            "family_id": "book_imbalance",
            "candidate_id": "book-batch",
            "status": "observation_only",
            "source_commit": "abc1234",
            "config_hash": "sha256:config",
            "data_snapshot": "20251009-20251031",
            "evaluator_version": "1",
            "promotion_allowed": False,
        }
        validate_event(event)

    def test_rejects_promotion_flag_state_contradictions(self):
        base = {
            "experiment_id": "exp-001",
            "campaign_id": "sfm_stream_001",
            "family_id": "book_imbalance",
            "candidate_id": "book-batch",
            "source_commit": "abc1234",
            "config_hash": "sha256:config",
            "data_snapshot": "20251009-20251031",
            "evaluator_version": "1",
        }
        with self.assertRaisesRegex(ValueError, "promotion_allowed"):
            validate_event(dict(base, status="observation_only", promotion_allowed=True))
        with self.assertRaisesRegex(ValueError, "promotion_allowed"):
            validate_event(dict(base, status="promoted", promotion_allowed=False))

    def test_validates_all_existing_campaign_events(self):
        path = Path(__file__).parents[1] / "sfm_stream_001" / "events.jsonl"
        events = validate_ledger(path)
        self.assertGreaterEqual(len(events), 1)
        self.assertEqual(events[-1]["status"], "observation_only")


if __name__ == "__main__":
    unittest.main()
