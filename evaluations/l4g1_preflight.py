"""Preflight helpers for the 16 next-generation mixed-round factors."""
from __future__ import annotations

import json
from pathlib import Path

FAMILIES = ("book_imbalance", "flow_pressure", "liquidity_resilience", "impact_efficiency")


def load_l4g1_factor_names(campaign_root):
    root = Path(campaign_root)
    names = []
    for family in FAMILIES:
        path = root / "batches" / ("l4_formal_history_next_v1_" + family + ".json")
        payload = json.loads(path.read_text(encoding="utf-8"))
        ids = payload.get("candidate_ids")
        if not isinstance(ids, list) or len(ids) != 4:
            raise ValueError("each l4g1 family batch must contain exactly 4 candidates (expected 16 total)")
        names.extend(str(value) for value in ids)
    if len(names) != 16 or len(set(names)) != 16:
        raise ValueError("expected exactly 16 unique l4g1 factor names")
    return names


__all__ = ["FAMILIES", "load_l4g1_factor_names"]
