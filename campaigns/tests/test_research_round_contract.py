import copy
import json
from pathlib import Path
import unittest

from campaigns.contracts import validate_document


ROOT = Path(__file__).parents[1]
ROUND_PATH = ROOT / "sfm_stream_001" / "rounds" / "l4g1_mixed_v1.json"
AUTH_PATH = ROOT / "sfm_stream_001" / "manifests" / "l4g1-mixed-holdout-authorization.json"


class ResearchRoundContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.round = json.loads(ROUND_PATH.read_text()) if ROUND_PATH.exists() else None
        cls.authorization = json.loads(AUTH_PATH.read_text()) if AUTH_PATH.exists() else None

    def test_round_is_registered_and_freezes_scope(self):
        self.assertIsNotNone(self.round, "frozen research round fixture is required")
        validate_document("research_round", self.round)
        self.assertEqual(self.round["counts"], {"batch_count": 4, "candidate_count": 16, "control_count": 48})
        self.assertEqual(self.round["stages"], ["L2", "L3", "L4", "L6-display"])
        self.assertEqual(len(self.round["events"]), 8)
        self.assertEqual(len(self.round["universes"]), 3)
        self.assertEqual(len(self.round["labels"]), 2)
        self.assertFalse(self.round["promotion_allowed"])
        self.assertEqual(self.round["stop_after"], "next_batch_generated")
        self.assertFalse(self.round["recursive"])
        self.assertEqual(len(self.round["parent_portrait_ids"]), 48)
        self.assertEqual(set(self.round["date_lists"]), {"training", "observation", "holdout"})
        self.assertEqual(set(self.round["families"]), {"book_imbalance", "flow_pressure", "impact_efficiency", "liquidity_resilience"})

    def test_each_family_preregisters_question_controls_and_rules(self):
        self.assertIsNotNone(self.round, "frozen research round fixture is required")
        for family, prereg in self.round["families"].items():
            with self.subTest(family=family):
                for key in ("research_question", "single_variable_control", "parent_candidate_ids", "expected_supported_events", "judgement_rules"):
                    self.assertIn(key, prereg)
                self.assertEqual(len(prereg["parent_candidate_ids"]), 12)
                self.assertEqual(set(prereg["judgement_rules"]), {"supported", "promising", "unsupported", "not_evaluable", "error"})

    def test_rejects_missing_parent_portraits(self):
        self.assertIsNotNone(self.round, "frozen research round fixture is required")
        bad = copy.deepcopy(self.round)
        del bad["parent_portrait_ids"]
        with self.assertRaises(ValueError):
            validate_document("research_round", bad)

    def test_rejects_recursive_or_l4_holdout_or_2025_selection(self):
        self.assertIsNotNone(self.round, "frozen research round fixture is required")
        for mutate in (
            lambda d: d.update(recursive=True),
            lambda d: d["stages_readable_splits"]["L4"].append("holdout"),
            lambda d: d["selection"]["allowed_years"].append(2025),
        ):
            bad = copy.deepcopy(self.round)
            mutate(bad)
            with self.assertRaises(ValueError):
                validate_document("research_round", bad)

    def test_holdout_authorization_is_frozen_and_non_feedback(self):
        self.assertIsNotNone(self.authorization, "holdout authorization fixture is required")
        self.assertEqual(self.authorization["kind"], "holdout_authorization")
        self.assertEqual(self.authorization["confirmed_at"], "2026-09-10")
        self.assertTrue(self.authorization["selection_frozen"])
        self.assertTrue(self.authorization["best_event_frozen"])
        self.assertEqual(self.authorization["allowed_use"], "display_only")
        self.assertFalse(self.authorization["feedback_allowed"])
        self.assertEqual(self.authorization["readable_split"], "holdout")


if __name__ == "__main__":
    unittest.main()
