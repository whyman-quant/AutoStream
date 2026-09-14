"""Resumable end-to-end controller for one factor-mining round.

The controller treats L2/L3 as gates inside a complete research loop rather
than user-visible stopping points.  Async production may return ``waiting`` or
``retryable``; completed stages are receipted and resume continues from the
first unfinished stage.  Promotion is never automatic.
"""
from __future__ import annotations

import math
import json
from pathlib import Path
from typing import Callable, Mapping, Sequence

from campaigns.research_round import atomic_write_json


PIPELINE_STAGES = (
    "logic_proposal",
    "factor_design",
    "code_implementation",
    "l2_technical",
    "l3_production",
    "l3_evaluation",
    "l4_production",
    "l4_evaluation",
    "portrait",
    "selection_freeze",
    "holdout_display",
    "effect_plots",
    "experience",
    "next_logic",
)


class PipelineBlocked(RuntimeError):
    """A non-retryable integrity, implementation, or leakage gate failed."""


def _state(value):
    return str(value or "").strip().upper().split("+")[0]


def classify_slurm_jobs(records: Sequence[Mapping[str, object]], *, memory_gb: int) -> dict:
    """Classify an async production wave and calculate an OOM retry tier."""
    if not records:
        raise PipelineBlocked("no Slurm jobs were supplied")
    retry = [int(item["job_id"]) for item in records
             if _state(item.get("state")) == "OUT_OF_MEMORY"]
    if retry:
        peaks = [float(item.get("max_rss_gb") or 0.0) for item in records
                 if int(item["job_id"]) in retry]
        required = max([float(memory_gb) * 2.0] + [peak * 1.25 for peak in peaks])
        next_memory = int(math.ceil(required / 8.0) * 8)
        return {
            "outcome": "retryable",
            "reason": "out_of_memory",
            "retry_job_ids": retry,
            "next_memory_gb": next_memory,
        }
    waiting_states = {"PENDING", "RUNNING", "CONFIGURING", "COMPLETING", "REQUEUED"}
    if any(_state(item.get("state")) in waiting_states for item in records):
        return {"outcome": "waiting", "job_ids": [int(item["job_id"]) for item in records]}
    failures = [item for item in records if _state(item.get("state")) != "COMPLETED"]
    if failures:
        summary = ", ".join("{}:{}".format(item.get("job_id"), item.get("state"))
                            for item in failures)
        raise PipelineBlocked("non-retryable Slurm failure: " + summary)
    return {"outcome": "complete", "job_ids": [int(item["job_id"]) for item in records]}


class RoundPipeline:
    """Run all research stages until complete or an async stage is waiting."""

    def __init__(self, round_id: str, runner: Callable[[str], Mapping[str, object]],
                 receipt_dir: Path):
        self.round_id = str(round_id)
        self.runner = runner
        self.receipt_dir = Path(receipt_dir)

    def _receipt(self, stage):
        return self.receipt_dir / (stage + ".json")

    def _is_complete(self, stage):
        path = self._receipt(stage)
        if not path.is_file():
            return False
        value = json.loads(path.read_text(encoding="utf-8"))
        if value.get("round_id") != self.round_id or value.get("status") != "complete":
            raise PipelineBlocked("invalid stage receipt: " + str(path))
        return True

    def run(self) -> dict:
        for stage in PIPELINE_STAGES:
            if self._is_complete(stage):
                continue
            result = dict(self.runner(stage))
            if result.get("promotion_allowed") is True:
                raise PipelineBlocked(stage + " attempted automatic promotion")
            outcome = result.get("outcome")
            if outcome == "complete":
                atomic_write_json(self._receipt(stage), {
                    "schema_version": 1,
                    "kind": "round_pipeline_stage_receipt",
                    "round_id": self.round_id,
                    "stage": stage,
                    "status": "complete",
                    "result": result,
                    "promotion_allowed": False,
                })
                continue
            if outcome in {"waiting", "retryable"}:
                return {
                    "round_id": self.round_id,
                    "status": outcome,
                    "stage": stage,
                    "detail": result,
                    "promotion_allowed": False,
                }
            raise PipelineBlocked("{} returned invalid outcome: {}".format(stage, outcome))
        return {
            "round_id": self.round_id,
            "status": "complete",
            "stage": PIPELINE_STAGES[-1],
            "promotion_allowed": False,
        }


__all__ = [
    "PIPELINE_STAGES", "PipelineBlocked", "RoundPipeline",
    "classify_slurm_jobs",
]
