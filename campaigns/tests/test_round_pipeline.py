import tempfile
import unittest
import json
from pathlib import Path

from campaigns.round_pipeline import (
    PIPELINE_STAGES,
    PipelineBlocked,
    RoundPipeline,
    classify_slurm_jobs,
)


class RoundPipelineTests(unittest.TestCase):
    def test_round_one_pipeline_covers_complete_research_loop(self):
        spec = json.loads(Path(
            "campaigns/sfm_stream_002/rounds/round_001_pipeline.json"
        ).read_text())
        self.assertEqual(spec["stages"], list(PIPELINE_STAGES))
        self.assertFalse(spec["automatic_promotion"])
        self.assertEqual(spec["l4_dataset"]["holdout_access"],
                         "sealed_until_selection_and_best_event_are_frozen")
        self.assertFalse(spec["l4_dataset"]["reuse_prior_factor_conclusions"])

    def test_complete_stage_immediately_continues_to_next_stage(self):
        calls = []

        def runner(stage):
            calls.append(stage)
            return {"outcome": "complete", "artifact": stage}

        with tempfile.TemporaryDirectory() as tmp:
            result = RoundPipeline("r1", runner, Path(tmp)).run()
        self.assertEqual(calls, list(PIPELINE_STAGES))
        self.assertEqual(result["status"], "complete")
        self.assertFalse(result["promotion_allowed"])

    def test_waiting_stage_pauses_without_running_later_stages(self):
        calls = []

        def runner(stage):
            calls.append(stage)
            if stage == "l3_production":
                return {"outcome": "waiting", "job_ids": [1, 2]}
            return {"outcome": "complete"}

        with tempfile.TemporaryDirectory() as tmp:
            result = RoundPipeline("r1", runner, Path(tmp)).run()
        self.assertEqual(result["status"], "waiting")
        self.assertEqual(result["stage"], "l3_production")
        self.assertNotIn("l3_evaluation", calls)

    def test_resume_skips_receipted_stages(self):
        calls = []

        def first(stage):
            calls.append(stage)
            if stage == "l3_production":
                return {"outcome": "waiting"}
            return {"outcome": "complete"}

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            RoundPipeline("r1", first, root).run()
            calls.clear()
            result = RoundPipeline(
                "r1", lambda stage: calls.append(stage) or {"outcome": "complete"}, root
            ).run()
        self.assertEqual(calls[0], "l3_production")
        self.assertEqual(result["status"], "complete")

    def test_oom_is_retryable_and_memory_scales_above_observed_peak(self):
        decision = classify_slurm_jobs(
            [
                {"job_id": 1, "state": "COMPLETED", "max_rss_gb": 80},
                {"job_id": 2, "state": "OUT_OF_MEMORY", "max_rss_gb": 92},
            ],
            memory_gb=64,
        )
        self.assertEqual(decision["outcome"], "retryable")
        self.assertEqual(decision["retry_job_ids"], [2])
        self.assertGreaterEqual(decision["next_memory_gb"], 115)

    def test_non_retryable_failure_blocks_pipeline(self):
        with self.assertRaises(PipelineBlocked):
            classify_slurm_jobs(
                [{"job_id": 3, "state": "FAILED", "max_rss_gb": 1}], memory_gb=64
            )


if __name__ == "__main__":
    unittest.main()
