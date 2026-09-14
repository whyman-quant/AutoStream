"""Materialize reviewed Agent proposals into Candidate and Batch contracts."""
from __future__ import annotations

import json
from pathlib import Path

from campaigns.contracts.consistency import candidate_hash
from campaigns.research_round import atomic_write_json


ALL_EVENTS = (926, 1000, 1030, 1100, 1130, 1330, 1400, 1430)
WARMUP_BY_POSITION = (0, 0, 20, 20, 4, 4, 20, 20, 31, 31, 30, 30)


def materialize_agent_batch(logic_path: Path, blueprint_path: Path, output_root: Path,
                            *, campaign_id: str, batch_id: str, source_commit: str,
                            evidence_level: str = "L2",
                            batch_status: str = "technical_complete") -> dict:
    logic = json.loads(Path(logic_path).read_text(encoding="utf-8"))
    blueprint = json.loads(Path(blueprint_path).read_text(encoding="utf-8"))
    ideas = {item["idea_id"]: item for item in logic["ideas"]}
    root = Path(output_root)
    candidate_root = root / "candidates" / "market_microstructure"
    candidate_ids = []
    candidate_paths = []
    for position, proposal in enumerate(blueprint["variants"]):
        idea = ideas[proposal["idea_id"]]
        candidate_id = proposal["proposal_id"]
        streams = list(dict.fromkeys(proposal["input_streams"]))
        quote_only = streams == ["quote"]
        supported = [int(value) for value in proposal.get(
            "supported_events", idea["supported_events"]
        )]
        document = {
            "schema_version": 1,
            "kind": "candidate",
            "candidate_id": candidate_id,
            "campaign_id": campaign_id,
            "family_id": "market_microstructure",
            "batch_id": batch_id,
            "generation": 0,
            "parent_candidate_ids": [],
            "hypothesis_id": proposal["idea_id"],
            "operator_id": proposal["operators"][0],
            "formula": proposal["formula"],
            "source_streams": streams,
            "parameters": {
                "design_variant": candidate_id,
                "supported_events": supported,
            },
            "state": {
                "window_type": "latest_event" if quote_only else "event_count",
                "window_events": None if quote_only else 4096,
                "warmup_events": WARMUP_BY_POSITION[position],
                "reset_policy": "trading_day",
            },
            "availability": {
                "update_on": streams,
                "output_at": "scheduled_snapshot",
                "lag_events": 0,
                "invalid_policy": "unavailable",
                "unsupported_events": [event for event in ALL_EVENTS if event not in supported],
                "readiness_policy": proposal.get("readiness", idea["readiness"]),
            },
            "output": {
                "factor_name": candidate_id,
                "dtype": "float64",
                "research_direction": "raw_signed",
            },
            "lineage": {
                "idea_path": str(logic_path),
                "implementation_path": "base/hf-open5m-factor-demo/factors/market_microstructure/factor_entry.cpp",
                "source_commit": source_commit,
            },
            "evidence_level": evidence_level,
            "canonical_hash": "",
        }
        document["canonical_hash"] = candidate_hash(document)
        path = candidate_root / (candidate_id + ".json")
        atomic_write_json(path, document)
        candidate_ids.append(candidate_id)
        candidate_paths.append(str(path))
    batch = {
        "schema_version": 1,
        "kind": "batch",
        "batch_id": batch_id,
        "campaign_id": campaign_id,
        "family_id": "market_microstructure",
        "generation": 0,
        "parent_batch_ids": [],
        "parent_experience_ids": [],
        "hypothesis_id": "market_microstructure_causal_structure_round_001",
        "objective": "Test six causal market-microstructure mechanisms with one representative and one single-variable control each.",
        "change_dimension": "mechanism_and_single_variable_control",
        "candidate_ids": candidate_ids,
        "search_policy": {"method": "mechanism_grid", "candidate_budget": len(candidate_ids)},
        "created_at": "2026-09-13",
        "source_commit": source_commit,
        "status": batch_status,
    }
    batch_path = root / "batches" / (batch_id + ".json")
    atomic_write_json(batch_path, batch)
    return {
        "candidate_ids": candidate_ids,
        "candidate_paths": candidate_paths,
        "batch_path": str(batch_path),
    }


__all__ = ["materialize_agent_batch"]
