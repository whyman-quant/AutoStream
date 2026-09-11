import unittest

from campaigns.agent_harness import (
    AGENT_RESEARCH_STAGES,
    AgentResearchHarness,
    AgentIdeaError,
    build_task_packet,
    build_coverage_matrix,
    load_task_packet,
    structure_signature,
    validate_agent_idea,
)


class AgentHarnessTests(unittest.TestCase):
    def _task(self):
        return build_task_packet(
            {"round_id": "r1", "campaign_id": "c1", "events": [926, 1000]},
            "book_imbalance",
            {"operators": [{"operator_id": "depth_slope"}, {"operator_id": "rolling_slope"}]},
            [{"cell_id": "book.space", "status": "unexplored"}],
        )

    def _idea(self):
        return {
            "idea_id": "book_queue_depletion_001",
            "family_id": "book_imbalance",
            "research_question": "Does queue depletion followed by same-side replenishment predict the next move?",
            "mechanism": "Executed ask volume removes sell queue while new bid volume replenishes buy queue.",
            "inputs": ["ask_volume", "signed_trade_volume", "new_bid_volume"],
            "operators": ["depth_slope", "rolling_slope"],
            "formula": "rolling_slope(new_bid_volume - ask_consumption, window=16)",
            "readiness": "at least 3 quote events and one identified queue change",
            "falsification": "After controlling for spread and turnover, no incremental rank IC versus parent.",
            "novelty_claim": "Uses replenishment after executed queue depletion; parent uses displayed depth only.",
            "supported_events": [926, 1000],
        }

    def _proposal(self, idea=None):
        idea = idea or self._idea()
        value = {
            "schema_version": 1, "kind": "candidate_proposal", "proposal_id": "p1",
            "idea_id": idea["idea_id"], "family_id": idea["family_id"],
            "mechanism": idea["mechanism"], "formula": idea["formula"],
            "inputs": ["quote", "order", "trade"], "operators": idea["operators"],
            "readiness": idea["readiness"], "falsification": idea["falsification"],
            "novelty_claim": idea["novelty_claim"], "supported_events": idea["supported_events"],
        }
        value["structure_signature"] = structure_signature(value)
        return value

    def test_task_packet_exposes_constraints_and_coverage_without_formula(self):
        packet = self._task()
        self.assertEqual(packet["family_id"], "book_imbalance")
        self.assertIn("coverage_gaps", packet)
        self.assertNotIn("formula", packet)
        self.assertIn("must_be_falsifiable", packet["constraints"])

    def test_agent_idea_requires_mechanism_novelty_and_falsification(self):
        idea = self._idea()
        validate_agent_idea(idea, self._task())
        for field in ("mechanism", "novelty_claim", "falsification"):
            bad = dict(idea); bad.pop(field)
            with self.assertRaises(AgentIdeaError):
                validate_agent_idea(bad, self._task())

    def test_duplicate_structure_is_rejected_but_new_mechanism_is_allowed(self):
        idea = self._idea()
        old = {"inputs": idea["inputs"], "operators": idea["operators"], "formula": idea["formula"]}
        with self.assertRaises(AgentIdeaError):
            validate_agent_idea(idea, self._task(), existing_signatures={structure_signature(old)})

    def test_agent_may_propose_a_new_causal_operator_with_readiness(self):
        idea = self._idea()
        idea["operators"] = ["queue_replenishment_velocity"]
        idea["new_operator_specs"] = [{
            "operator_id": "queue_replenishment_velocity", "category": "temporal",
            "description": "Causal replenishment rate after observed depletion.",
            "input_streams": ["quote", "order", "trade"], "causal": True,
            "readiness_requirement": "observed depletion and one later replenishment",
        }]
        validate_agent_idea(idea, self._task())

    def test_unknown_operator_without_definition_is_rejected(self):
        idea = self._idea(); idea["operators"] = ["magic_operator"]
        with self.assertRaises(AgentIdeaError):
            validate_agent_idea(idea, self._task())

    def test_coverage_matrix_builds_full_axis_product_as_unexplored(self):
        cells = build_coverage_matrix(
            "book_imbalance", ["depletion", "replenishment"],
            ["spatial", "temporal"], ["quote", "quote_order_trade"],
        )
        self.assertEqual(len(cells), 8)
        self.assertEqual({cell["status"] for cell in cells}, {"unexplored"})

    def test_supported_events_are_minute_codes(self):
        self.assertEqual(self._task()["events"], [926, 1000])

    def test_task_normalizes_timestamp_events_to_minute_codes(self):
        round_data = {"round_id": "r1", "campaign_id": "c1", "events": [92600000, 100000000]}
        packet = build_task_packet(round_data, "book_imbalance", {"operators": []}, [])
        self.assertEqual(packet["events"], [926, 1000])

    def test_real_round_task_publishes_requested_opening_event_set(self):
        import json
        from pathlib import Path
        round_data = json.loads(Path("campaigns/sfm_stream_001/rounds/l4g1_mixed_v1.json").read_text())
        packet = build_task_packet(round_data, "book_imbalance", {"operators": []}, [])
        self.assertEqual(packet["events"], [926, 1000, 1030, 1100, 1130, 1330, 1400, 1430])

    def test_agent_research_harness_runs_seven_research_stages_in_order(self):
        import tempfile
        from pathlib import Path
        calls = []
        with tempfile.TemporaryDirectory() as tmp:
            idea = self._idea()
            def runner(stage, context):
                calls.append(stage)
                if stage == "logic_proposal": return {"ideas": [idea]}
                if stage == "factor_design": return {"proposals": [self._proposal(idea)]}
                return {"artifact": stage}
            harness = AgentResearchHarness(
                {"round_id": "r1", "promotion_allowed": False, "recursive": False},
                runner=runner, receipt_dir=Path(tmp), task_packet=self._task(),
            )
            result = harness.start()
        self.assertEqual(tuple(calls), AGENT_RESEARCH_STAGES)
        self.assertEqual(result["stage"], "next_logic")
        self.assertFalse(result["promotion_allowed"])

    def test_agent_research_harness_does_not_run_after_failed_idea_stage(self):
        import tempfile
        from pathlib import Path
        calls = []
        def runner(stage, context):
            calls.append(stage)
            if stage == "logic_proposal":
                raise RuntimeError("no valid idea")
            return {"artifact": stage}
        with tempfile.TemporaryDirectory() as tmp:
            harness = AgentResearchHarness(
                {"round_id": "r1", "promotion_allowed": False, "recursive": False},
                runner=runner, receipt_dir=Path(tmp),
            )
            with self.assertRaises(AgentIdeaError):
                harness.start()
        self.assertEqual(calls, ["logic_proposal"])

    def test_new_round_loads_extensible_catalog_and_full_coverage_space(self):
        from pathlib import Path
        packet = load_task_packet(
            Path("campaigns/sfm_stream_002/rounds/round_001.json"), Path(".")
        )
        self.assertEqual(packet["events"], [926, 1000, 1030, 1100, 1130, 1330, 1400, 1430])
        self.assertTrue(packet["operator_catalog"]["policy"]["catalog_is_extensible"])
        self.assertEqual(len(packet["coverage_gaps"]), 80)
        self.assertFalse(packet["reuse_policy"]["prior_research_conclusions"])

    def test_logic_output_is_validated_and_passed_to_design(self):
        import tempfile
        from pathlib import Path
        seen = {}
        task = self._task()
        idea = self._idea()
        def runner(stage, context):
            if stage == "logic_proposal":
                return {"ideas": [idea]}
            if stage == "factor_design":
                seen[stage] = context.get("previous_output")
                return {"proposals": [self._proposal(idea)]}
            seen[stage] = context.get("previous_output")
            return {"artifact": stage}
        with tempfile.TemporaryDirectory() as tmp:
            harness = AgentResearchHarness(
                {"round_id": "r1", "promotion_allowed": False, "recursive": False},
                runner=runner, receipt_dir=Path(tmp), task_packet=task,
            )
            harness.start(stop_after="factor_design")
            self.assertEqual(seen["factor_design"]["ideas"][0]["idea_id"], idea["idea_id"])

    def test_invalid_logic_output_does_not_create_receipt(self):
        import tempfile
        from pathlib import Path
        bad = self._idea(); del bad["falsification"]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            harness = AgentResearchHarness(
                {"round_id": "r1", "promotion_allowed": False, "recursive": False},
                runner=lambda stage, context: {"ideas": [bad]},
                receipt_dir=root, task_packet=self._task(),
            )
            with self.assertRaises(AgentIdeaError):
                harness.start(stop_after="logic_proposal")
            self.assertFalse((root / "logic_proposal.json").exists())

    def test_factor_design_requires_valid_proposals_and_matching_signature(self):
        import tempfile
        from pathlib import Path
        idea = self._idea()
        proposal = {
            "schema_version": 1, "kind": "candidate_proposal", "proposal_id": "p1",
            "idea_id": idea["idea_id"], "family_id": idea["family_id"],
            "mechanism": idea["mechanism"], "formula": idea["formula"],
            "inputs": ["quote", "order", "trade"], "operators": idea["operators"],
            "readiness": idea["readiness"], "falsification": idea["falsification"],
            "novelty_claim": idea["novelty_claim"], "supported_events": idea["supported_events"],
            "structure_signature": "sha256:" + "0" * 64,
        }
        def runner(stage, context):
            return {"ideas": [idea]} if stage == "logic_proposal" else {"proposals": [proposal]}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            harness = AgentResearchHarness(
                {"round_id": "r1", "promotion_allowed": False, "recursive": False},
                runner=runner, receipt_dir=root, task_packet=self._task(),
            )
            with self.assertRaises(AgentIdeaError):
                harness.start(stop_after="factor_design")
            self.assertFalse((root / "factor_design.json").exists())


if __name__ == "__main__":
    unittest.main()
