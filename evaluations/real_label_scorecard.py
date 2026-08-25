"""Normalize real-label evaluator outputs into an auditable scorecard."""

from __future__ import annotations

import math
import hashlib
from typing import Dict, Mapping, Optional, Sequence, Tuple


DEFAULT_LABELS = ("raw926", "ease926")
DEFAULT_UNIVERSES = ("000985", "003800", "000906")
SCORECARD_METRICS = frozenset(("RankIC", "IC", "LS", "Monotonicity"))
EXPECTED_EVENTS = (92600000, 100000000, 103000000, 110000000, 113000000, 133000000, 140000000, 143000000)


def _finite(values: Sequence[float]):
    return [float(value) for value in values if math.isfinite(float(value))]


def _mean(values: Sequence[float]) -> Optional[float]:
    finite = _finite(values)
    return sum(finite) / len(finite) if finite else None


def summarize_factor_metrics(metrics: Mapping[str, Sequence[float]]) -> Dict[str, object]:
    rank_ic = _finite(metrics.get("RankIC", ()))
    event_count = max((len(values) for values in metrics.values()), default=0)
    return {
        "event_count": event_count,
        "rank_ic_count": len(rank_ic),
        "mean_rank_ic": _mean(rank_ic),
        "mean_ic": _mean(metrics.get("IC", ())),
        "mean_long_short": _mean(metrics.get("LS", ())),
        "mean_monotonicity": _mean(metrics.get("Monotonicity", ())),
    }


def validate_result_frame(frame, *, expected_date: Optional[str] = None) -> None:
    if list(frame.index.names) != ["date", "event"]:
        raise ValueError("evaluator result index must be (date,event)")
    dates = {str(value) for value in frame.index.get_level_values("date")}
    if len(dates) != 1 or (expected_date is not None and dates != {str(expected_date)}):
        raise ValueError("evaluator result date does not match the requested acceptance date")
    events = tuple(sorted(int(value) for value in frame.index.get_level_values("event")))
    if events != EXPECTED_EVENTS:
        raise ValueError("evaluator result event set does not match the fixed eight checkpoints")


def load_result_parquet(path: str, *, expected_date: Optional[str] = None) -> Dict[str, Dict[str, Sequence[float]]]:
    """Read factor_eval_toolkit's flattened ``factor|metric`` parquet."""
    try:
        import pandas as pd
    except ImportError as error:
        raise RuntimeError("pandas and pyarrow are required to read evaluator parquet") from error
    frame = pd.read_parquet(path)
    validate_result_frame(frame, expected_date=expected_date)
    outputs: Dict[str, Dict[str, Sequence[float]]] = {}
    for column in frame.columns:
        if isinstance(column, tuple):
            factor, metric = str(column[0]), str(column[1])
        else:
            value = str(column)
            if "|" not in value:
                continue
            factor, metric = value.rsplit("|", 1)
        if metric in SCORECARD_METRICS:
            outputs.setdefault(factor, {})[metric] = [float(value) for value in frame[column].tolist()]
    if not outputs:
        raise ValueError("no supported factor metrics found in {}".format(path))
    for factor, metrics in outputs.items():
        missing = SCORECARD_METRICS.difference(metrics)
        if missing:
            raise ValueError("factor {} is missing required metrics: {}".format(factor, sorted(missing)))
    return outputs


def build_scorecard(
    matrix: Mapping[Tuple[str, str], Mapping[str, Mapping[str, Sequence[float]]]],
    *,
    labels: Sequence[str] = DEFAULT_LABELS,
    universes: Sequence[str] = DEFAULT_UNIVERSES,
    expected_factor_count: Optional[int] = None,
) -> Dict[str, object]:
    factor_sets = []
    for key, factors in matrix.items():
        if not factors:
            raise ValueError("empty factor result for {}/{}".format(*key))
        factor_sets.append(frozenset(factors))
    if factor_sets and any(value != factor_sets[0] for value in factor_sets[1:]):
        raise ValueError("factor set mismatch across label/universe combinations")
    if expected_factor_count is not None and any(len(value) != expected_factor_count for value in factor_sets):
        raise ValueError("factor count does not match expected acceptance contract")
    combinations = []
    for label in labels:
        for universe in universes:
            key = (label, universe)
            if key not in matrix:
                raise ValueError("missing evaluation result for {}/{}".format(label, universe))
            factors = {
                name: summarize_factor_metrics(metrics)
                for name, metrics in sorted(matrix[key].items())
            }
            combinations.append({
                "label": label,
                "universe": universe,
                "factor_count": len(factors),
                "factors": factors,
            })
    return {
        "schema_version": 1,
        "evaluation_scope": "single_day_acceptance",
        "promotion_allowed": False,
        "direction_policy": "no_direction_selection_from_acceptance_data",
        "labels": list(labels),
        "universes": list(universes),
        "combination_count": len(combinations),
        "factor_count": len(factor_sets[0]) if factor_sets else 0,
        "combinations": combinations,
    }


def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()
