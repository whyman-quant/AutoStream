"""Build pre-holdout evidence cells from frozen evaluator outputs.

Availability is derived from finite metric coverage.  No readiness columns or
sidecar artifacts are required or emitted.
"""
from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd


CORE_METRICS = ("RankIC", "IC", "LS", "Monotonicity")
METRIC_KEYS = {"RankIC": "rank_ic", "IC": "ic", "LS": "ls",
               "Monotonicity": "monotonicity"}


def _event_series(frame, factor, event, metric):
    try:
        values = frame.xs(event, level="event")[factor + "|" + metric]
    except (KeyError, TypeError):
        return pd.Series(dtype="float64")
    return pd.Series(values, dtype="float64")


def build_evidence_cells(result_root, split_dates, factors, events, labels, universes):
    """Return factor×event×universe×label cells joining both frozen splits."""
    root = Path(result_root)
    frames = {}
    for split in ("training", "observation"):
        expected_dates = [str(value) for value in split_dates[split]]
        if any(value >= "20250101" for value in expected_dates):
            raise ValueError("holdout dates are forbidden while building evidence cells")
        for label in labels:
            for universe in universes:
                path = root / split / str(label) / str(universe) / "res_full.parquet"
                frame = pd.read_parquet(path)
                frame.index = pd.MultiIndex.from_arrays(
                    [[str(x) for x in frame.index.get_level_values("date")],
                     frame.index.get_level_values("event")],
                    names=["date", "event"],
                )
                if frame.index.has_duplicates:
                    raise ValueError("duplicate date/event in {}".format(path))
                frames[(split, str(label), str(universe))] = frame

    cells = []
    for factor in factors:
        for event in events:
            for universe in universes:
                for label in labels:
                    payload = {
                        "factor": str(factor), "event": int(event),
                        "universe": str(universe), "label": str(label),
                        "availability_source": "finite_metrics",
                        "data_error": False,
                    }
                    coverages = []
                    for split in ("training", "observation"):
                        frame = frames[(split, str(label), str(universe))]
                        date_count = len(split_dates[split])
                        series = {metric: _event_series(frame, factor, event, metric)
                                  for metric in CORE_METRICS}
                        if any(len(values) != date_count for values in series.values()):
                            payload["data_error"] = True
                        if any(not np.isfinite(values.dropna().to_numpy()).all()
                               for values in series.values()):
                            payload["data_error"] = True
                        metric_coverage = min(
                            (float(values.notna().mean()) for values in series.values()),
                            default=0.0,
                        )
                        coverages.append(metric_coverage)
                        for metric, values in series.items():
                            key = split + "_" + METRIC_KEYS[metric]
                            payload[key] = float(values.mean()) if values.notna().any() else None
                            if metric == "RankIC":
                                finite = values.dropna()
                                std = float(finite.std(ddof=1)) if len(finite) > 1 else None
                                payload[split + "_rank_ic_tstat"] = (
                                    float(finite.mean() / std * np.sqrt(len(finite)))
                                    if std and std > 0.0 else None)
                        payload[split + "_metric_count"] = {
                            metric: int(values.notna().sum()) for metric, values in series.items()}
                    payload["coverage"] = min(coverages) if coverages else 0.0
                    payload["ready"] = payload["coverage"] > 0.0
                    payload["metric_defined"] = all(
                        payload.get(split + "_" + METRIC_KEYS[metric]) is not None
                        for split in ("training", "observation")
                        for metric in CORE_METRICS
                    )
                    cells.append(payload)
    return cells


__all__ = ["CORE_METRICS", "METRIC_KEYS", "build_evidence_cells"]
