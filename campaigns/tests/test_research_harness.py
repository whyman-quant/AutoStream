import tempfile
import unittest
import json
from pathlib import Path

from campaigns.research_harness import ResearchHarness, HarnessError


class ResearchHarnessTests(unittest.TestCase):
    def _round(self, root):
        return {
            "round_id": "fixture-round",
            "stages": ["L2", "L3", "L4", "L6-display"],
            "stop_after": "next_batch_generated",
            "recursive": False,
            "promotion_allowed": False,
            "selection": {"allowed_years": [2021, 2022, 2023, 2024], "forbidden_years": [2025]},
            "date_lists": {"training": {"dates": ["20210104"]}, "observation": {"dates": ["20230103"]}, "holdout": {"dates": ["20250102"]}},
            "holdout_authorization": {"selection_frozen": True, "best_event_frozen": True},
            "artifacts_root": str(root),
        }

    def test_plan_is_read_only_and_start_runs_research_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []
            def runner(stage, context):
                calls.append((stage, dict(context)))
                return {"stage": stage, "output": stage}

            harness = ResearchHarness(self._round(tmp), runner=runner, receipt_dir=Path(tmp) / "receipts")
            preview = harness.plan()
            self.assertEqual(preview["stages"], ["L2", "L3", "L4", "selection", "L6-display", "experience", "next_batch"])
            self.assertEqual(calls, [])
            result = harness.start()
            self.assertEqual(result["status"], "complete")
            self.assertEqual([item[0] for item in calls], ["L2", "L3", "L4", "selection", "L6-display", "experience", "next_batch"])
            self.assertFalse(any("2025" in str(item[1].get("selection_input", "")) for item in calls))

    def test_holdout_cannot_run_before_selection_receipt(self):
        with tempfile.TemporaryDirectory() as tmp:
            def runner(stage, context):
                if stage == "selection":
                    raise RuntimeError("selection failed")
                return {"stage": stage}
            harness = ResearchHarness(self._round(tmp), runner=runner, receipt_dir=Path(tmp) / "receipts")
            with self.assertRaises(HarnessError):
                harness.start()
            self.assertFalse((Path(tmp) / "receipts" / "L6-display.json").exists())

    def test_resume_skips_valid_completed_stage(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []
            def runner(stage, context):
                calls.append(stage)
                return {"stage": stage, "output": stage}
            receipt_dir = Path(tmp) / "receipts"
            harness = ResearchHarness(self._round(tmp), runner=runner, receipt_dir=receipt_dir)
            harness.start(stop_after="L4")
            calls[:] = []
            result = harness.resume()
            self.assertEqual(result["status"], "complete")
            self.assertNotIn("L2", calls)
            self.assertNotIn("L3", calls)
            self.assertIn("selection", calls)

    def test_date_list_paths_are_resolved_for_real_round_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lists = root / "campaigns/sfm_stream_001/manifests"
            lists.mkdir(parents=True)
            (lists / "training.txt").write_text("20210104\n20210105\n")
            (lists / "observation.txt").write_text("20230103\n")
            (lists / "holdout.txt").write_text("20250102\n")
            round_data = self._round(root)
            round_data["date_lists"] = {
                "training": {"path": "campaigns/sfm_stream_001/manifests/training.txt"},
                "observation": {"path": "campaigns/sfm_stream_001/manifests/observation.txt"},
                "holdout": {"path": "campaigns/sfm_stream_001/manifests/holdout.txt"},
            }
            harness = ResearchHarness(round_data, receipt_dir=root / "receipts", round_root=root)
            self.assertEqual(harness._context("L4")["training_dates"], ["20210104", "20210105"])
            self.assertEqual(harness._context("L6-display")["holdout_dates"], ["20250102"])

    def test_mixed_round_loads_sealed_authorization_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            auth = root / "campaigns/sfm_stream_001/manifests/l4g1-mixed-holdout-authorization.json"
            auth.parent.mkdir(parents=True)
            auth.write_text(json.dumps({"selection_frozen": True, "best_event_frozen": True}))
            data = self._round(root)
            data["round_id"] = "l4g1_mixed_v1"
            data["date_lists"]["holdout"] = {"dates": ["20250102"]}
            harness = ResearchHarness(data, round_root=root)
            self.assertTrue(harness._context("L6-display")["authorization"]["selection_frozen"])


if __name__ == "__main__":
    unittest.main()
