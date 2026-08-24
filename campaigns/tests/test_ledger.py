import json
import tempfile
import unittest
from pathlib import Path

from campaigns.ledger import append_event


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


if __name__ == "__main__":
    unittest.main()
