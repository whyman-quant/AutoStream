import json
import tempfile
import unittest
from pathlib import Path

from campaigns.research_round import (
    STATES,
    advance,
    atomic_write_json,
    load_round,
    plan_transition,
    resume_point,
    sha256,
    validate_receipt,
)


class ResearchRoundStateTests(unittest.TestCase):
    def test_legal_sequence_is_strictly_ordered(self):
        for left, right in zip(STATES, STATES[1:]):
            kwargs = {"holdout_authorized": right == "holdout_displayed"}
            self.assertEqual(plan_transition(left, right, **kwargs), right)
        self.assertEqual(plan_transition("selected", "holdout_displayed", holdout_authorized=True), "holdout_displayed")

    def test_rejects_skip_backwards_and_recursive_completion(self):
        for left, right in (("planned", "l2_complete"), ("l3_produced", "l4_evaluated"), ("selected", "l4_produced"), ("complete", "planned")):
            with self.assertRaises(ValueError):
                plan_transition(left, right)
        with self.assertRaises(ValueError):
            plan_transition("selected", "holdout_displayed")
        with self.assertRaises(ValueError):
            plan_transition("next_batch_generated", "complete", recursive=True)

    def test_receipts_bind_hashes_and_reject_duplicate_submission(self):
        receipt = advance("planned", "released", input_data={"release": 1}, output_data={"jobs": [7]}, job_ids=[7])
        self.assertEqual(receipt["input_hash"], sha256({"release": 1}))
        self.assertEqual(receipt["output_hash"], sha256({"jobs": [7]}))
        validate_receipt(receipt)
        with self.assertRaises(ValueError):
            advance(receipt, "released", input_data={"release": 1}, output_data={"jobs": [7]}, job_ids=[7])
        tampered = dict(receipt, output_hash=sha256({"jobs": [8]}))
        with self.assertRaises(ValueError):
            validate_receipt(tampered, expected_output_hash=receipt["output_hash"])

    def test_holdout_access_requires_authorization_and_selection(self):
        with self.assertRaises(ValueError):
            advance("l4_evaluated", "holdout_displayed", holdout_access=True)
        selected = advance("l4_evaluated", "selected", output_data={"selection": "frozen"})
        displayed = advance(selected, "holdout_displayed", holdout_authorized=True, holdout_access=True)
        self.assertTrue(displayed["holdout_access"])
        self.assertEqual(displayed["stage"], "holdout_displayed")

    def test_resume_uses_receipts_not_file_presence_and_detects_artifact_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact = root / "artifact.json"
            artifact.write_text('{"ok": true}', encoding="utf-8")
            receipt = advance("planned", "released", output_path=artifact)
            atomic_write_json(root / "released.json", receipt)
            (root / "fake_l4_complete.json").write_text("{}", encoding="utf-8")
            self.assertEqual(resume_point(root), "released")
            artifact.write_text('{"ok": false}', encoding="utf-8")
            with self.assertRaises(ValueError):
                resume_point(root)

    def test_resume_rejects_a_receipt_that_skips_stages(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "l4.json"
            atomic_write_json(path, advance("l3_evaluated", "l4_produced"))
            with self.assertRaises(ValueError):
                resume_point(tmp)

    def test_atomic_write_and_load_round(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "round.json"
            atomic_write_json(path, {"round_id": "r1"})
            self.assertEqual(load_round(path)["round_id"], "r1")


if __name__ == "__main__":
    unittest.main()
