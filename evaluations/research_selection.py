"""Pre-registered cell evidence classification and holdout selection.

This module never reads 2025.  It turns L4 training/observation metrics into
explicit cell evidence, then freezes directions and a best event per
``universe × label`` before the separate holdout-display stage may start.
"""
from __future__ import annotations

from collections import defaultdict
from typing import Iterable, Mapping, Sequence


EVIDENCE_STATES = ("supported", "promising", "unsupported", "not_evaluable", "error")


def _number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def classify_cell(cell: Mapping[str, object]) -> dict:
    """Classify one factor/event/universe/label evidence unit.

    Stable raw-signed evidence requires same nonzero sign in the two frozen
    splits, coverage and positive parent increment.  A negative sign can pass
    equally: raw direction is preserved rather than optimized.
    """
    result = dict(cell)
    if result.get("data_error"):
        result.update(status="error", reason="data_error")
        return result
    if not result.get("ready", True):
        result.update(status="not_evaluable", reason="not_ready")
        return result
    if not result.get("metric_defined", True):
        result.update(status="not_evaluable", reason="metric_undefined")
        return result
    coverage = result.get("coverage", 0.0)
    if not _number(coverage) or coverage < .95:
        result.update(status="not_evaluable", reason="insufficient_coverage")
        return result
    train = result.get("training_rank_ic")
    observe = result.get("observation_rank_ic")
    if not _number(train) or not _number(observe):
        result.update(status="not_evaluable", reason="rank_ic_undefined")
        return result
    parent = result.get("parent_rank_ic")
    if _number(parent):
        result["parent_delta_rank_ic"] = float((train + observe) / 2.0 - parent)
    same_sign = train * observe > 0.0
    ls_train, ls_obs = result.get("training_ls"), result.get("observation_ls")
    mono_train, mono_obs = result.get("training_monotonicity"), result.get("observation_monotonicity")
    coherent = all(_number(value) and value > 0.0 for value in (ls_train, ls_obs, mono_train, mono_obs))
    increment = result.get("parent_delta_rank_ic")
    if same_sign and coherent and (increment is None or increment > 0.0):
        result.update(status="supported", reason="split_stable_incremental_evidence")
    elif same_sign:
        result.update(status="promising", reason="split_sign_agrees_but_evidence_incomplete")
    else:
        result.update(status="unsupported", reason="split_direction_does_not_replicate")
    return result


def classify_cells(cells: Iterable[Mapping[str, object]]) -> list:
    return [classify_cell(cell) for cell in cells]


def freeze_holdout_selection(cells: Iterable[Mapping[str, object]], *,
                             allowed_years: Sequence[int] = (2021, 2022, 2023, 2024)) -> dict:
    """Select display-only factors/events using frozen pre-holdout evidence."""
    values = [dict(cell) for cell in cells]
    errors = [cell for cell in values if cell.get("status") == "error"]
    if errors:
        raise ValueError("data_error cells block holdout selection")
    by_factor = defaultdict(list)
    for cell in values:
        if cell.get("status") == "supported":
            by_factor[str(cell.get("factor"))].append(cell)
    selected = sorted(factor for factor, items in by_factor.items() if items)
    directions = {}
    best_events = {}
    for factor in selected:
        groups = defaultdict(list)
        for cell in by_factor[factor]:
            groups[(str(cell.get("universe")), str(cell.get("label")))].append(cell)
        signs = [cell["training_rank_ic"] + cell["observation_rank_ic"] for cell in by_factor[factor]]
        directions[factor] = "positive" if sum(signs) > 0.0 else "negative"
        for (universe, label), group in groups.items():
            best = max(group, key=lambda cell: (
                float(cell["training_rank_ic"] + cell["observation_rank_ic"]),
                float(cell.get("parent_delta_rank_ic", float("-inf"))),
                -int(cell.get("event", 0)),
            ))
            best_events[universe + "|" + label] = best.get("event")
    return {
        "schema_version": 1,
        "kind": "holdout_display_selection",
        "selection_years": list(allowed_years),
        "holdout_read": False,
        "direction_policy": "raw_signed",
        "selected_for_holdout_display": selected,
        "directions": directions,
        "best_events": best_events,
        "cells": values,
        "promotion_allowed": False,
    }


__all__ = ["EVIDENCE_STATES", "classify_cell", "classify_cells", "freeze_holdout_selection"]
