"""Generate mechanism-level experience and a wide next-logic search plan."""
from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path

from campaigns.contracts import validate_document
from campaigns.research_round import atomic_write_json


def allocate_wide_search(mechanism_ids, per_mechanism=16):
    return {str(value): int(per_mechanism) for value in mechanism_ids}


def generate(selection_path, logic_path, blueprint_path, output_root, next_logic_path):
    selection = json.loads(Path(selection_path).read_text(encoding="utf-8"))
    logic = json.loads(Path(logic_path).read_text(encoding="utf-8"))
    blueprint = json.loads(Path(blueprint_path).read_text(encoding="utf-8"))
    proposal_by_idea = defaultdict(list)
    for variant in blueprint["variants"]:
        proposal_by_idea[variant["idea_id"]].append(variant["proposal_id"])
    cell_by_factor = defaultdict(list)
    for cell in selection["cells"]:
        cell_by_factor[cell["factor"]].append(cell)
    output = Path(output_root)
    output.mkdir(parents=True, exist_ok=True)
    records = []
    next_hypotheses = []
    dimension_by_idea = {
        "counterfactual_depth_fragility_001": "shock_shape_x_depth_curve_convexity",
        "trade_pair_parent_size_mismatch_001": "counterparty_breadth_x_parent_size_quantile",
        "resting_order_commitment_survival_001": "lifecycle_outcome_x_age_kernel_x_price_distance",
        "cancel_execution_divergence_001": "withdrawal_execution_horizon_x_activity_regime",
        "execution_shock_replenishment_elasticity_001": "adaptive_shock_threshold_x_response_horizon",
        "distinct_aggressor_run_surprisal_001": "state_order_x_run_definition_x_size_surprise",
    }
    for idea in logic["ideas"]:
        idea_id = idea["idea_id"]
        factors = proposal_by_idea[idea_id]
        cells = [cell for factor in factors for cell in cell_by_factor[factor]]
        counts = Counter(cell.get("status") for cell in cells)
        representative = max(factors, key=lambda factor: sum(
            cell.get("status") == "supported" for cell in cell_by_factor[factor]))
        portrait_ids = [factor + "__market_microstructure_l4_v1" for factor in factors]
        supported = counts.get("supported", 0)
        not_evaluable = counts.get("not_evaluable", 0)
        action_type = "request_data" if supported == 0 and not_evaluable == len(cells) else "generate_variants"
        dimension = dimension_by_idea[idea_id]
        record = {
            "schema_version": 1, "kind": "experience_record",
            "experience_id": "experience_sfm_stream_002_round_001_" + idea_id,
            "campaign_id": "sfm_stream_002", "family_id": "market_microstructure",
            "source_portrait_ids": portrait_ids,
            "fact": {
                "statement": "{}: supported={}, promising={}, unsupported={}, not_evaluable={} at factor×event×universe×label granularity; no holdout result was used.".format(
                    idea["research_question"], supported, counts.get("promising", 0),
                    counts.get("unsupported", 0), not_evaluable),
                "evidence": [{
                    "portrait_id": representative + "__market_microstructure_l4_v1",
                    "metric_path": "selection.cells.status.supported_count",
                    "observed_value": supported,
                }],
            },
            "interpretation": {
                "statement": ("The mechanism has repeatable pre-holdout cells and should be expanded structurally."
                              if supported else
                              "The current implementation did not produce evaluable repeated evidence; measurement and event formation must be changed before judging the mechanism."),
                "falsifiable_by": "Reject the next mechanism branch if no variant clears the same frozen split, coverage, magnitude and directional-coherence gates.",
            },
            "action": {
                "type": action_type, "changed_dimension": dimension,
                "target_family_id": "market_microstructure",
                "proposal": "Explore 16 controlled structures over {} without a blind window grid.".format(dimension),
                "falsifiable_test": {
                    "expected_observation": "At least one variant adds supported cells without duplicating its parent panel.",
                    "reject_condition": "All variants are not evaluable, split-unstable, direction-incoherent, or exactly redundant.",
                    "dataset_scope": "next_round_training_and_observation_only",
                },
            },
            "status": "proposed",
            "generated_batch_ids": ["market_microstructure_round_002_wide_search"],
        }
        validate_document("experience_record", record)
        atomic_write_json(output / (record["experience_id"] + ".json"), record)
        records.append(record)
        next_hypotheses.append({
            "parent_idea_id": idea_id,
            "parent_portrait_ids": portrait_ids,
            "parent_experience_id": record["experience_id"],
            "research_question": "Within {}, which causal representation survives regime, event and stock-pool changes?".format(dimension),
            "search_dimensions": dimension.split("_x_"),
            "candidate_budget": 16,
            "controls": ["raw_vs_conditioned", "linear_vs_nonlinear", "local_vs_path_dependent", "single_scale_vs_multiscale"],
            "stop_condition": record["action"]["falsifiable_test"]["reject_condition"],
        })
    allocation = allocate_wide_search([idea["idea_id"] for idea in logic["ideas"]])
    next_logic = {
        "schema_version": 1, "kind": "next_logic_proposal",
        "round_id": "sfm_stream_002_round_002",
        "parent_round_id": "sfm_stream_002_round_001",
        "source_selection": str(selection_path),
        "holdout_feedback_used": False,
        "search_policy": {
            "target_candidate_count": sum(allocation.values()),
            "candidate_count_range": [80, 140],
            "per_mechanism": allocation,
            "structural_search_first": True,
            "blind_window_grid_forbidden": True,
        },
        "hypotheses": next_hypotheses,
        "promotion_allowed": False,
        "elimination_allowed": False,
    }
    atomic_write_json(next_logic_path, next_logic)
    return records, next_logic


__all__ = ["allocate_wide_search", "generate"]
