"""Generate human-readable round summaries and factor effect plots."""

from __future__ import annotations

import json
from collections import defaultdict
from pathlib import Path
from typing import Mapping


EVIDENCE_STATES = ("supported", "promising", "unsupported", "not_evaluable", "error")


def _read_selection_receipt(receipt) -> dict:
    if receipt is None:
        raise ValueError("selection receipt is required for an evidence report")
    if isinstance(receipt, (str, Path)):
        receipt = json.loads(Path(receipt).read_text())
    if not isinstance(receipt, Mapping):
        raise ValueError("selection receipt must be a mapping or JSON path")
    return dict(receipt)


def _receipt_cells(receipt: Mapping[str, object]):
    for key in ("cells", "cell_classifications", "evidence_cells"):
        value = receipt.get(key)
        if isinstance(value, list):
            return [dict(cell) for cell in value if isinstance(cell, Mapping)]
    return []


def _supported_cell(cell: Mapping[str, object]) -> dict:
    result = {key: cell.get(key) for key in ("factor", "event", "universe", "label")}
    for key in ("parent_delta", "parent_increment", "parent_delta_rank_ic"):
        if key in cell:
            result["parent_delta"] = cell[key]
            break
    return result


def build_portrait_effect_report(portrait_root, round_id: str, selection_receipt) -> dict:
    receipt = _read_selection_receipt(selection_receipt)
    cells = _receipt_cells(receipt)
    portraits = []
    for path in sorted(Path(portrait_root).glob("*.json")):
        document = json.loads(path.read_text())
        decision = document.get("decision", {})
        if decision.get("promotion_allowed", False):
            raise ValueError("portrait unexpectedly allows promotion: {}".format(path))
        rank = document.get("metrics", {}).get("rank_ic", {})
        portraits.append({
            "factor": document.get("candidate_id"),
            "family_id": document.get("family_id"),
            "portrait_id": document.get("portrait_id"),
            "rank_ic_mean": rank.get("mean"),
            "rank_ic_ir": rank.get("ir"),
            "positive_fraction": rank.get("positive_fraction"),
            "status": "observation_only",
        })
    families = defaultdict(list)
    for item in portraits:
        families[item["family_id"]].append(item)
    counts = {state: sum(cell.get("status") == state for cell in cells) for state in EVIDENCE_STATES}
    supported = [_supported_cell(cell) for cell in cells if cell.get("status") == "supported"]
    selected = receipt.get("selected_for_holdout_display", receipt.get("selected_factors", []))
    if not isinstance(selected, list):
        raise ValueError("selection receipt selected_for_holdout_display must be a list")
    return {
        "schema_version": 1,
        "kind": "round_effect_report",
        "round_id": round_id,
        "portrait_count": len(portraits),
        "selected_for_holdout_display": list(selected),
        "selected_for_holdout_display_count": len(selected),
        "status_counts": counts,
        "supported_cells": supported,
        "promotion_allowed": False,
        "portraits": portraits,
        "families": {key: {"factor_count": len(value), "factors": value} for key, value in sorted(families.items())},
    }


def write_effect_plots(portrait_root, output_root, round_id: str, selection_receipt):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    report = build_portrait_effect_report(portrait_root, round_id, selection_receipt)
    root = Path(output_root)
    root.mkdir(parents=True, exist_ok=True)
    paths = []
    for family, block in report["families"].items():
        factors = block["factors"]
        names = [item["factor"] for item in factors]
        values = [item["rank_ic_mean"] if item["rank_ic_mean"] is not None else 0.0 for item in factors]
        colors = ["#147d92" if value >= 0 else "#d95f59" for value in values]
        fig, ax = plt.subplots(figsize=(12, max(4, len(names) * .32)))
        ax.barh(names, values, color=colors)
        ax.axvline(0.0, color="#333333", linewidth=.8)
        ax.set_title("{} — raw-signed mean RankIC (observation only)".format(family))
        ax.set_xlabel("mean RankIC")
        fig.tight_layout()
        path = root / ("{}-rankic.png".format(family))
        fig.savefig(path, dpi=150)
        plt.close(fig)
        paths.append(str(path))
    return paths


__all__ = ["build_portrait_effect_report", "write_effect_plots"]
