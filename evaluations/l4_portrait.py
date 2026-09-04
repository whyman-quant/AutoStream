"""Strict L4 formal-history acceptance and one v2 portrait per factor."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Mapping, Optional, Sequence

import numpy as np
import pandas as pd
import pyarrow.feather as pa_feather

METRICS = tuple(["D{}".format(i) for i in range(1, 11)] + ["LS", "Monotonicity", "IC", "RankIC"])


def _sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""): h.update(chunk)
    return "sha256:" + h.hexdigest()


def _dates_sha(dates):
    return "sha256:" + hashlib.sha256(("\n".join(dates) + "\n").encode()).hexdigest()


def _stats(series):
    x = pd.Series(series, dtype="float64").dropna()
    if not np.isfinite(x.to_numpy()).all(): raise ValueError("non-finite metric value")
    mean = float(x.mean()) if len(x) else None
    std = float(x.std(ddof=1)) if len(x) > 1 else None
    return {"count": int(len(x)), "mean": mean, "std": std, "ir": mean / std if std and std > 0 else None, "positive_fraction": float((x > 0).mean()) if len(x) else None}


def _load_candidates(root, expected_factors):
    docs = {}
    for path in Path(root).glob("*/*.json"):
        doc = json.loads(path.read_text())
        if doc.get("candidate_id") in expected_factors: docs[doc["candidate_id"]] = (doc, path)
    if set(docs) != set(expected_factors): raise ValueError("candidate set mismatch")
    return docs


def _neighbors(factor, candidates):
    base = candidates[factor][0]
    result = []
    for peer, (doc, _) in candidates.items():
        if peer == factor or doc.get("family_id") != base.get("family_id"): continue
        a, b = base.get("parameters", {}), doc.get("parameters", {})
        if set(a) != set(b): continue
        changed = [key for key in a if a[key] != b[key]]
        if len(changed) == 1:
            result.append({"candidate_id": peer, "changed_parameter": changed[0], "from": a[changed[0]], "to": b[changed[0]]})
    return sorted(result, key=lambda x: x["candidate_id"])


def _strict_frames(result_root, split_dates, labels, universes, factors, events, threshold, allow_partial_metrics=False):
    frames, receipts = {}, {}
    columns = [factor + "|" + metric for factor in factors for metric in METRICS]
    for split in ("training", "observation"):
        dates = list(split_dates[split])
        if not dates or dates != sorted(set(dates)): raise ValueError("frozen date list invalid")
        if any(str(date) >= "20250101" for date in dates): raise ValueError("holdout dates are forbidden")
        expected_index = pd.MultiIndex.from_product([dates, events], names=["date", "event"])
        for label in labels:
            for universe in universes:
                path = Path(result_root) / split / label / universe / "res_full.parquet"
                if not path.is_file(): raise ValueError("missing result {}".format(path))
                frame = pd.read_parquet(path)
                frame.index = pd.MultiIndex.from_arrays([[str(x) for x in frame.index.get_level_values(0)], frame.index.get_level_values(1)], names=frame.index.names)
                if frame.index.names != ["date", "event"]: raise ValueError("index names mismatch")
                if frame.index.has_duplicates: raise ValueError("duplicate date/event")
                if not frame.index.equals(expected_index): raise ValueError("frozen date/event index mismatch")
                if frame.shape != (len(dates) * len(events), len(factors) * len(METRICS)): raise ValueError("shape mismatch")
                if list(frame.columns) != columns: raise ValueError("factor/metric column order mismatch")
                coverage = {}
                for column in columns:
                    values = frame[column]
                    finite = np.isfinite(values.dropna().to_numpy()).all()
                    if not finite: raise ValueError("non-finite {}".format(column))
                    fraction = float(values.notna().mean()); coverage[column] = fraction
                    if fraction < threshold and not allow_partial_metrics:
                        raise ValueError("coverage below 95 percent: {}".format(column))
                frames[(split, label, universe)] = frame
                receipts.setdefault(split, []).append({"label": label, "universe": universe, "path": str(path), "sha256": _sha(path), "rows": len(frame), "columns": len(frame.columns), "minimum_column_coverage": min(coverage.values())})
    return frames, receipts


VALIDITY_STATES = ("pass", "not_ready", "metric_undefined", "coverage_fail", "data_error", "review")


def classify_validity_cell(factor_values, metric_series, readiness=None, date_count=None,
                           coverage_threshold=.95, min_unique=2, min_rank_count=2,
                           min_nonzero_ratio=0.0, min_factor_std=0.0):
    """Classify one factor/event/universe/label/split cell.

    Readiness is intentionally explicit: zero-valued factors never imply ``not_ready``.
    ``metric_series`` is a mapping of metric name to a one-dimensional sequence.
    """
    if not hasattr(metric_series, "items"):
        return {"status": "data_error", "reason": "invalid_metric_series", "readiness_source": "provided" if readiness is not None else "not_provided"}
    try:
        factor_values = pd.Series(factor_values, dtype="float64")
    except (TypeError, ValueError):
        return {"status": "data_error", "reason": "invalid_factor_values", "readiness_source": "provided" if readiness is not None else "not_provided"}
    n = int(date_count if date_count is not None else len(factor_values))
    result = {"status": None, "readiness_source": "provided" if readiness is not None else "not_provided",
              "date_count": n, "factor_value_count": int(factor_values.notna().sum()),
              "factor_finite_count": int(np.isfinite(factor_values.to_numpy()).sum()),
              "factor_zero_count": int((factor_values == 0).sum()), "not_ready_zero_count": 0,
              "metric_counts": {}, "metric_finite_count": 0, "metric_coverage": 0.0, "reason": None}
    if len(factor_values) != n or n <= 0:
        result.update(status="data_error", reason="invalid_date_count")
        return result
    if not np.isfinite(factor_values.dropna().to_numpy()).all():
        result.update(status="data_error", reason="non_finite_factor_value")
        return result
    # Validate metric input before applying readiness so an explicit false mask
    # cannot hide a malformed/non-finite metric series.
    for values in metric_series.values():
        try:
            probe = pd.Series(values, dtype="float64")
        except (TypeError, ValueError):
            result.update(status="data_error", reason="invalid_metric_values")
            return result
        if not np.isfinite(probe.dropna().to_numpy()).all():
            result.update(status="data_error", reason="non_finite_metric")
            return result
    ready = None
    finite_factor = factor_values.dropna()
    result["factor_std"] = float(finite_factor.std(ddof=1)) if len(finite_factor) > 1 else None
    result["factor_unique_count"] = int(finite_factor.nunique())
    result["factor_nonzero_ratio"] = float((finite_factor != 0).mean()) if len(finite_factor) else 0.0
    result["effective_rank_count"] = int(finite_factor.rank(method="dense").nunique())
    if readiness is not None:
        ready = pd.Series(readiness)
        if len(ready) != n:
            result.update(status="data_error", reason="readiness_length_mismatch")
            return result
        # Accept bool/numeric masks, but reject missing or non-binary values.
        if ready.isna().any():
            result.update(status="data_error", reason="readiness_contains_null")
            return result
        if pd.api.types.is_bool_dtype(ready):
            ready = ready.astype(bool)
        elif pd.api.types.is_numeric_dtype(ready):
            numeric = ready.to_numpy(dtype="float64")
            if not np.isfinite(numeric).all() or not np.isin(numeric, [0.0, 1.0]).all():
                raise ValueError("readiness must contain only bool/0/1")
            ready = ready.astype(bool)
        else:
            raise ValueError("readiness must contain only bool/0/1")
        false_mask = ~ready
        result["not_ready_count"] = int(false_mask.sum())
        result["not_ready_zero_count"] = int((false_mask & factor_values.eq(0)).sum())
        if not false_mask.any() and factor_values.isna().any():
            result.update(status="data_error", reason="ready_factor_value_not_finite")
            return result
        if false_mask.any():
            result.update(status="not_ready", reason="explicit_readiness_false")
            return result

    metric_ok = True
    metric_defined = False
    result["metric_stats"] = {}
    for name, values in metric_series.items():
        series = pd.Series(values, dtype="float64")
        if len(series) != n:
            result.update(status="data_error", reason="metric_length_mismatch")
            return result
        finite = series.dropna()
        if not np.isfinite(finite.to_numpy()).all():
            result.update(status="data_error", reason="non_finite_metric")
            return result
        count = int(len(finite)); result["metric_counts"][name] = count
        result["metric_finite_count"] += count
        result["metric_stats"][name] = {"count": count,
                                         "mean": float(finite.mean()) if count else None,
                                         "std": float(finite.std(ddof=1)) if count > 1 else None,
                                         "std_ddof": 1,
                                         "ir": (float(finite.mean()) / float(finite.std(ddof=1))) if count > 1 and float(finite.std(ddof=1)) > 0 else None,
                                         "positive_fraction": float((finite > 0).mean()) if count else None}
        if count:
            metric_defined = True
            if int(finite.nunique()) < min_unique or int(finite.rank(method="dense").nunique()) < min_rank_count:
                metric_ok = False
        if count == 0:
            metric_ok = False
    if not metric_defined:
        result.update(status="metric_undefined", reason="no_finite_metric_values")
        return result
    # A cell is only as complete as its least-covered metric.  Using the minimum
    # prevents one populated metric from masking an IC/RankIC (or portfolio metric)
    # that is unavailable for most dates.
    best_count = min(result["metric_counts"].values())
    result["metric_coverage"] = float(best_count / float(n))
    if result["metric_coverage"] < coverage_threshold:
        result.update(status="coverage_fail", reason="metric_coverage_below_threshold")
        return result
    if not metric_ok:
        result.update(status="metric_undefined", reason="insufficient_metric_variation")
        return result
    if (result["factor_std"] is None or result["factor_std"] <= min_factor_std or
            result["factor_unique_count"] < min_unique or
            result["effective_rank_count"] < min_rank_count or
            result["factor_nonzero_ratio"] < min_nonzero_ratio):
        result.update(status="metric_undefined", reason="insufficient_cross_sectional_variation")
        return result
    # Without an explicit mask, a valid-looking cell requires human review rather than
    # silently claiming readiness. This preserves the distinction from zero values.
    if readiness is None:
        result.update(status="review", reason="readiness_not_provided")
    else:
        result.update(status="pass", reason="explicit_readiness_true")
    return result


def _readiness_column(frame, factor):
    for name in (factor + "|ready", factor + "|readiness", "ready_" + factor, "readiness_" + factor):
        if name in frame.columns:
            return name
    return None


def _normalize_readiness(values):
    """Normalize an Arrow readiness column to a strict boolean mask."""
    ready = pd.Series(values)
    if ready.isna().any():
        raise ValueError("readiness contains null")
    if pd.api.types.is_bool_dtype(ready):
        return ready.astype(bool)
    if pd.api.types.is_numeric_dtype(ready):
        numeric = ready.to_numpy(dtype="float64")
        if not np.isfinite(numeric).all() or not np.isin(numeric, [0.0, 1.0]).all():
            raise ValueError("readiness must contain only bool/0/1")
        return ready.astype(bool)
    raise ValueError("readiness must contain only bool/0/1")


def _cross_section_stats(values, readiness=None):
    """Summarize one date/event Arrow cross section without using evaluation panels."""
    try:
        series = pd.Series(values, dtype="float64")
    except (TypeError, ValueError):
        return {"status": "review", "reason": "raw_factor_values_not_numeric"}
    finite_mask = np.isfinite(series.to_numpy())
    finite = series[finite_mask]
    stats = {
        "status": "provided", "symbol_count": int(len(series)),
        "finite_count": int(finite_mask.sum()),
        "std": float(finite.std(ddof=1)) if len(finite) > 1 else None,
        "unique_count": int(finite.nunique()),
        "nonzero_ratio": float((finite != 0).mean()) if len(finite) else 0.0,
        "effective_rank_count": int(finite.rank(method="dense").nunique()),
    }
    if not finite_mask.all():
        stats.update(status="review", reason="raw_factor_nonfinite")
    if readiness is not None:
        ready = _normalize_readiness(readiness)
        ready_values = series[ready]
        ready_finite = np.isfinite(ready_values.to_numpy())
        stats.update({
            "readiness_provided": True,
            "ready_count": int(ready.sum()),
            "ready_false_count": int((~ready).sum()),
            "ready_finite_count": int(ready_finite.sum()),
            "ready_factor_nonfinite_count": int((~ready_finite).sum()),
        })
        if (~ready_finite).any():
            stats.update(status="review", reason="ready_factor_value_not_finite")
        elif stats.get("reason") == "raw_factor_nonfinite":
            # NaN values in explicitly not-ready rows are allowed and remain
            # distinguishable from a malformed ready value.
            not_ready_values = series[~ready]
            if not np.isinf(not_ready_values.to_numpy()).any():
                stats.update(status="provided", reason=None)
    else:
        stats["readiness_provided"] = False
    return stats


def _load_arrow_cross_section(arrow_root, dates, factors, events):
    """Load Arrow raw values and aggregate cross-sectional stats by event/factor.

    Arrow files contain symbols but no frozen index constituent mapping for each
    universe.  Consequently the returned summary is explicitly scoped to all
    symbols in the file; it must never be inferred from Parquet metric panels.
    """
    requested = [str(d) for d in dates]
    if requested != sorted(set(requested)):
        raise ValueError("Arrow date list invalid")
    if any(d >= "20250101" for d in requested):
        raise ValueError("holdout dates are forbidden")
    accum = {(factor, event): [] for factor in factors for event in events}
    available = {(factor, event): True for factor in factors for event in events}
    for date in requested:
        path = Path(arrow_root) / (date + ".arrow")
        if not path.is_file():
            raise ValueError("missing Arrow {}".format(path))
        # Inspect the IPC schema first, then materialize only the identifier,
        # requested factor and readiness columns.  This keeps the 969-day scan
        # bounded even when Arrow carries auxiliary payload columns.
        schema_names = set(pa_feather.read_table(path, columns=[]).schema.names)
        readiness_names = {name for factor in factors for name in
                           (factor + "|ready", factor + "|readiness", "ready_" + factor, "readiness_" + factor)
                           if name in schema_names}
        selected = [name for name in ("symbol", "date", "event") if name in schema_names]
        selected += [factor for factor in factors if factor in schema_names]
        selected += sorted(readiness_names)
        frame = pd.read_feather(path, columns=selected)
        required = {"symbol", "date", "event"}
        if not required.issubset(frame.columns):
            raise ValueError("Arrow columns missing")
        if set(str(x) for x in frame["date"].unique()) != {date}:
            raise ValueError("Arrow date mismatch")
        if set(frame["event"].unique()) != set(events):
            raise ValueError("Arrow event mismatch")
        if frame.duplicated(["symbol", "event"]).any():
            raise ValueError("Arrow duplicate symbol/event")
        event_groups = {event: group for event, group in frame.groupby("event", sort=False)}
        for factor in factors:
            readiness_name = _readiness_column(frame, factor)
            if factor not in frame.columns:
                for event in events:
                    available[(factor, event)] = False
                continue
            for event in events:
                event_frame = event_groups[event]
                readiness = event_frame[readiness_name] if readiness_name else None
                stats = _cross_section_stats(event_frame[factor], readiness)
                if stats.get("ready_factor_nonfinite_count", 0):
                    stats["status"] = "review"
                    stats["reason"] = "ready_factor_value_not_finite"
                stats["date"] = date
                accum[(factor, event)].append(stats)
    summaries = {}
    for key, rows in accum.items():
        factor, event = key
        if not rows or not available[key]:
            summaries[key] = {"status": "review", "reason": "raw_factor_column_missing",
                              "source": "arrow", "universe_scope": "all_symbols",
                              "date_count": len(requested), "factor_available": False}
            continue
        if any(row.get("status") != "provided" for row in rows):
            reason = next(row.get("reason", "raw_factor_invalid") for row in rows if row.get("status") != "provided")
            summaries[key] = {"status": "review", "reason": reason, "source": "arrow",
                              "universe_scope": "all_symbols", "date_count": len(requested),
                              "factor_available": True}
            continue
        def _mean(name):
            vals = [row[name] for row in rows if row.get(name) is not None]
            return float(np.mean(vals)) if vals else None
        summary = {
            "status": "provided", "source": "arrow", "universe_scope": "all_symbols",
            "factor_available": True, "date_count": len(requested),
            "by_date": rows,
            "dates_with_data": len(rows), "symbol_count": int(round(_mean("symbol_count") or 0)),
            "finite_count": int(sum(row["finite_count"] for row in rows)),
            "std_mean": _mean("std"), "std_min": min((row["std"] for row in rows if row["std"] is not None), default=None),
            "unique_count_min": min(row["unique_count"] for row in rows),
            "nonzero_ratio_mean": _mean("nonzero_ratio"),
            "effective_rank_count_min": min(row["effective_rank_count"] for row in rows),
            "readiness_provided": any(row.get("readiness_provided", False) for row in rows),
        }
        if summary["readiness_provided"]:
            summary["ready_count"] = int(sum(row.get("ready_count", 0) for row in rows))
            summary["ready_false_count"] = int(sum(row.get("ready_false_count", 0) for row in rows))
            summary["ready_finite_count"] = int(sum(row.get("ready_finite_count", 0) for row in rows))
            summary["ready_factor_nonfinite_count"] = int(sum(row.get("ready_factor_nonfinite_count", 0) for row in rows))
        summaries[key] = summary
    return summaries


def build_validity_matrix(frames, split_dates, factors, events, labels, universes, coverage_threshold=.95,
                          arrow_root=None, dates=None):
    """Build deterministic factor×event×universe×label×split validity cells."""
    cells = []
    training_dates = set(str(d) for d in split_dates.get("training", []))
    observation_dates = set(str(d) for d in split_dates.get("observation", []))
    if training_dates & observation_dates:
        raise ValueError("training/observation date overlap")
    arrow_summaries = {}
    if arrow_root is not None:
        if dates is None:
            arrow_dates = {split: list(split_dates.get(split, [])) for split in ("training", "observation")}
        elif hasattr(dates, "get"):
            arrow_dates = {split: list(dates.get(split, split_dates.get(split, []))) for split in ("training", "observation")}
        else:
            arrow_dates = {split: list(dates) for split in ("training", "observation")}
        for split in ("training", "observation"):
            arrow_summaries[split] = _load_arrow_cross_section(arrow_root, arrow_dates[split], factors, events) if arrow_dates[split] else {}
    for split in ("training", "observation"):
        expected_dates = list(split_dates[split])
        if any(str(d) >= "20250101" for d in expected_dates):
            raise ValueError("holdout dates are forbidden")
        for factor in factors:
            for event in events:
                for universe in universes:
                    for label in labels:
                        key = (split, label, universe)
                        if key not in frames:
                            raise ValueError("missing frame for {}".format(key))
                        frame = frames[key]
                        try:
                            event_frame = frame.xs(event, level="event")
                        except KeyError:
                            raise ValueError("missing event {}".format(event))
                        readiness_name = _readiness_column(frame, factor)
                        readiness = event_frame[readiness_name] if readiness_name else None
                        missing_metrics = [metric for metric in METRICS if factor + "|" + metric not in event_frame]
                        if missing_metrics:
                            cell = {"status": "data_error", "reason": "metric_column_missing", "missing_metrics": missing_metrics,
                                    "factor_value_source": "provided" if factor in event_frame else "not_provided",
                                    "split": split, "factor": factor, "event": event, "universe": universe, "label": label}
                            if arrow_root is not None:
                                cross = arrow_summaries.get(split, {}).get((factor, event))
                                if cross is not None:
                                    cell["cross_sectional"] = cross
                            cells.append(cell)
                            continue
                        metrics = {metric: event_frame[factor + "|" + metric] for metric in METRICS}
                        cross = arrow_summaries.get(split, {}).get((factor, event)) if arrow_root is not None else None
                        if factor not in event_frame:
                            cell = classify_validity_cell(np.full(len(event_frame), np.nan), metrics, readiness=readiness, date_count=len(expected_dates), coverage_threshold=coverage_threshold)
                            cell.update(status="review", reason="factor_values_not_provided", factor_value_source="not_provided")
                        else:
                            cell = classify_validity_cell(event_frame[factor], metrics, readiness=readiness, date_count=len(expected_dates), coverage_threshold=coverage_threshold)
                            cell["factor_value_source"] = "provided"
                        if cross is not None:
                            cell["cross_sectional"] = cross
                            if cross.get("factor_available"):
                                cell["factor_value_source"] = "arrow_cross_section"
                            else:
                                cell["factor_value_source"] = "not_provided"
                                cell["reason"] = "raw_factor_column_missing"
                                cell["status"] = "review"
                        cell.update({"split": split, "factor": factor, "event": event, "universe": universe, "label": label})
                        cells.append(cell)
    return cells


def summarize_validity_matrix(cells, min_pass_events=6):
    """Return pass-only aggregates and deterministic counts for a validity matrix."""
    if not cells:
        return {"cell_count": 0, "status_counts": {}, "pass_count": 0, "summaries": {}}
    status_counts = {state: sum(c.get("status") == state for c in cells) for state in VALIDITY_STATES}
    result = {"cell_count": len(cells), "status_counts": status_counts, "pass_count": status_counts["pass"], "summaries": {}}
    def aggregate_stats(subset):
        out = {}
        names = sorted(set(k for c in subset for k in c.get("metric_stats", {})))
        for name in names:
            vals = [c["metric_stats"][name].get("mean") for c in subset if c.get("status") == "pass" and c.get("metric_stats", {}).get(name, {}).get("mean") is not None]
            s = pd.Series(vals, dtype="float64")
            mean = float(s.mean()) if len(s) else None
            std = float(s.std(ddof=1)) if len(s) > 1 else None
            out[name] = {"count": int(len(s)), "mean": mean, "std": std, "ir": mean / std if std and std > 0 else None}
        return out
    for dimension in ("event", "universe", "label", "split"):
        values = sorted(set(c[dimension] for c in cells), key=str)
        result["summaries"][dimension] = {}
        for value in values:
            subset = [c for c in cells if c[dimension] == value]
            result["summaries"][dimension][str(value)] = {
                "cell_count": len(subset),
                "pass_count": sum(c["status"] == "pass" for c in subset),
                "status_counts": {state: sum(c["status"] == state for c in subset) for state in VALIDITY_STATES},
                "metric_stats": aggregate_stats(subset),
            }
    factor_summaries = {}
    for factor in sorted(set(c["factor"] for c in cells)):
        fcells = [c for c in cells if c["factor"] == factor]
        pass_cells = [c for c in fcells if c.get("status") == "pass"]
        split_summaries = {}
        for split in ("training", "observation"):
            scoped = [c for c in fcells if c["split"] == split]
            event_pass = {e: sum(c["status"] == "pass" for c in scoped if c["event"] == e) for e in sorted(set(c["event"] for c in scoped))}
            cells_per_event = 6  # frozen: three universes × two labels
            broad = sum(v >= int(math.ceil(cells_per_event / 2.0)) for v in event_pass.values())
            split_summaries[split] = {"event_pass_counts": event_pass, "broad_pass_event_count": broad,
                                      "broad_pass": broad >= min_pass_events, "pass_count": sum(c["status"] == "pass" for c in scoped)}
        training_broad = split_summaries["training"]["broad_pass"]
        observation_broad = split_summaries["observation"]["broad_pass"]
        status = "review" if training_broad and observation_broad else ("observation_failure" if training_broad and not observation_broad else "insufficient_scope")
        factor_summaries[factor] = {"status": status, "cell_count": len(fcells), "pass_count": len(pass_cells), "split_summaries": split_summaries,
                                    "event_pass_counts": {e: sum(c["status"] == "pass" for c in fcells if c["event"] == e) for e in sorted(set(c["event"] for c in fcells))},
                                         "pass_cells": [{k: c[k] for k in ("split", "event", "universe", "label")} for c in pass_cells]}
    result["summaries"]["factor"] = factor_summaries
    return result


def _value_correlations(arrow_root, dates, factors, events):
    accum = {(a, b): [] for i, a in enumerate(factors) for b in factors[i + 1:]}
    file_hashes = []
    for date in dates:
        path = Path(arrow_root) / (date + ".arrow")
        if not path.is_file(): raise ValueError("missing Arrow {}".format(date))
        frame = pd.read_feather(path, columns=["date", "event"] + list(factors))
        if set(str(x) for x in frame["date"].unique()) != {date}: raise ValueError("Arrow date mismatch")
        if set(frame["event"].unique()) != set(events): raise ValueError("Arrow event mismatch")
        file_hashes.append({"date": date, "sha256": _sha(path)})
        for _, cross in frame.groupby("event", sort=False):
            corr = cross[list(factors)].corr(method="spearman")
            for pair in accum: accum[pair].append(corr.loc[pair[0], pair[1]])
    peers = {factor: [] for factor in factors}
    for (a, b), values in accum.items():
        value = float(pd.Series(values).mean())
        peers[a].append({"candidate_id": b, "spearman_mean": value, "daily_event_observations": len(values), "measured": True})
        peers[b].append({"candidate_id": a, "spearman_mean": value, "daily_event_observations": len(values), "measured": True})
    return peers, file_hashes


def build_portraits(result_root: Path, split_dates: Mapping[str, Sequence[str]], labels: Sequence[str], universes: Sequence[str], expected_factors: Sequence[str], events: Sequence[int], arrow_root: Path, candidates_root: Path, coverage_threshold: float = .95, campaign_id: str = "sfm_stream_001", provenance: Optional[Mapping[str, str]] = None, return_validity_matrix: bool = False):
    factors, labels, universes, events = list(expected_factors), list(labels), list(universes), list(events)
    if len(set(factors)) != len(factors): raise ValueError("duplicate factors")
    frames, receipts = _strict_frames(result_root, split_dates, labels, universes, factors, events, coverage_threshold,
                                      allow_partial_metrics=return_validity_matrix)
    validity_matrix = build_validity_matrix(frames, split_dates, factors, events, labels, universes, coverage_threshold,
                                            arrow_root=arrow_root, dates=split_dates)
    candidates = _load_candidates(candidates_root, factors)
    all_dates = list(split_dates["training"]) + list(split_dates["observation"])
    value_peers, arrow_hashes = _value_correlations(arrow_root, all_dates, factors, events)

    # A factor's IC series is the mean RankIC across the 12 split/label/universe panels for each date/event.
    ic_series = {}
    for factor in factors:
        series = [frame[factor + "|RankIC"] for frame in frames.values()]
        ic_series[factor] = pd.concat(series, axis=1).mean(axis=1).sort_index()
    ic_corr = pd.DataFrame(ic_series).corr(method="spearman")

    provenance = dict(provenance or {})
    empty_hash = "sha256:" + ("0" * 64)
    for key in ("dataset_manifest", "binary", "config", "evaluator", "label_contract", "methodology", "evaluation_receipt"):
        provenance.setdefault(key + "_path", "fixture/" + key)
        provenance.setdefault(key + "_sha256", empty_hash)
    correlation_artifact = provenance.get("correlation_artifact_path", "correlations/value-spearman.json")
    correlation_hash = provenance.get("correlation_artifact_sha256", "sha256:" + hashlib.sha256(json.dumps(value_peers, sort_keys=True).encode()).hexdigest())

    docs = []
    for factor in factors:
        split_output, rolling_groups = {}, []
        split_means = {}
        for split in ("training", "observation"):
            combinations = []
            split_rank = []
            for label in labels:
                for universe in universes:
                    frame = frames[(split, label, universe)]
                    metric_stats = {metric: _stats(frame[factor + "|" + metric]) for metric in METRICS}
                    combinations.append({"label": label, "universe": universe, "metrics": metric_stats})
                    split_rank.append(frame[factor + "|RankIC"])
                    for event in events:
                        daily = frame.xs(event, level="event")[factor + "|RankIC"]
                        rolled = daily.rolling(60, min_periods=30).mean().dropna()
                        rolling_groups.append({"split": split, "label": label, "universe": universe, "event": event, "window": 60, "min_periods": 30, "observation_count": len(rolled), "positive_fraction": float((rolled > 0).mean()) if len(rolled) else None})
            merged = pd.concat(split_rank, axis=1).mean(axis=1)
            split_means[split] = float(merged.mean())
            dates = list(split_dates[split])
            split_output[split] = {"date_start": dates[0], "date_end": dates[-1], "date_count": len(dates), "date_list_sha256": _dates_sha(dates), "combinations": combinations, "event_slices": {str(e): _stats(merged.xs(e, level="event")) for e in events}, "year_slices": {str(k): _stats(v) for k, v in merged.groupby(merged.index.get_level_values(0).map(lambda d: str(d)[:4]))}, "quarter_slices": {str(k): _stats(v) for k, v in merged.groupby(merged.index.get_level_values(0).map(lambda d: str(d)[:4] + "Q" + str((int(str(d)[4:6]) - 1) // 3 + 1)))}}

        daily = ic_series[factor].groupby(level="date").mean()
        standardized = (daily - daily.mean()) / daily.std(ddof=1)
        top = standardized.abs().sort_values(ascending=False).head(5)
        peer_ic = [{"candidate_id": peer, "spearman": float(ic_corr.loc[factor, peer]), "measured": True} for peer in factors if peer != factor]
        candidate, candidate_path = candidates[factor]
        all_rank = pd.concat([frame[factor + "|RankIC"] for frame in frames.values()])
        rolling_positive = [g["positive_fraction"] for g in rolling_groups if g["positive_fraction"] is not None]
        disagreement = np.sign(split_means["training"]) != np.sign(split_means["observation"])
        docs.append({
            "schema_version": 2, "kind": "factor_portrait", "portrait_id": factor + "__formal_history_v2", "campaign_id": campaign_id, "family_id": candidate["family_id"], "candidate_id": factor, "factor": factor, "scope": "formal_history", "evidence_level": "L4",
            "dataset": {"dataset_id": "sfm_stream_001_formal_history_v2", "date_start": all_dates[0], "date_end": all_dates[-1], "date_count": len(all_dates), "events": events, "labels": labels, "universes": universes, "split_date_list_sha256": {key: _dates_sha(list(value)) for key, value in split_dates.items()}},
            "lineage": {"candidate_path": str(candidate_path), "candidate_hash": candidate["canonical_hash"], "candidate_source_commit": candidate["lineage"]["source_commit"], "dataset_manifest_path": provenance["dataset_manifest_path"], "dataset_manifest_sha256": provenance["dataset_manifest_sha256"], "binary_path": provenance["binary_path"], "binary_sha256": provenance["binary_sha256"], "config_path": provenance["config_path"], "config_sha256": provenance["config_sha256"], "evaluator_path": provenance["evaluator_path"], "evaluator_sha256": provenance["evaluator_sha256"], "label_contract_path": provenance["label_contract_path"], "label_contract_sha256": provenance["label_contract_sha256"], "methodology_path": provenance["methodology_path"], "methodology_sha256": provenance["methodology_sha256"], "evaluation_receipt_path": provenance["evaluation_receipt_path"], "evaluation_receipt_sha256": provenance["evaluation_receipt_sha256"], "evaluation_files": receipts, "correlation_artifact_path": correlation_artifact, "correlation_artifact_sha256": correlation_hash},
            "data_quality": {"coverage_threshold": coverage_threshold, "minimum_coverage": min(r["minimum_column_coverage"] for rows in receipts.values() for r in rows), "all_required_values_finite": True, "duplicate_date_event_count": 0, "matrix_complete": True},
            "direction": "raw_signed", "metrics": {"rank_ic": dict(_stats(all_rank), std_ddof=1, ir_formula="mean/std")}, "splits": split_output,
            "rolling_stability": {"window": 60, "min_periods": 30, "groups": rolling_groups, "positive_fraction": float(np.mean(rolling_positive)) if rolling_positive else None, "split_mean_sign_disagreement": bool(disagreement), "unstable": bool((rolling_positive and np.mean(rolling_positive) < .55) or disagreement)},
            "correlations": {"method": "Spearman", "redundancy_threshold_abs": .90, "ic_series": {"peers": peer_ic, "measured": True}, "factor_value": {"peers": value_peers[factor], "aggregation": "mean of per-date per-event cross-sectional Spearman", "measured": True}},
            "parameter_neighborhood": _neighbors(factor, candidates),
            "anomaly_days": [{"date": str(date), "standardized_rank_ic_contribution": float(standardized.loc[date]), "absolute_standardized_contribution": float(value)} for date, value in top.items()],
            "decision": {"status": "formal_observation", "promotion_allowed": False, "reasons": ["L4 creates portraits and experience evidence only"]}
        })
    return (docs, validity_matrix) if return_validity_matrix else docs


def write_portraits(documents, output_root):
    from campaigns.contracts import validate_document
    root = Path(output_root); root.mkdir(parents=True, exist_ok=True); paths = []
    for document in documents:
        validate_document("factor_portrait", document)
        path = root / (document["candidate_id"] + ".json")
        path.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n")
        paths.append(path)
    return paths


def main(argv: Optional[Sequence[str]] = None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result-root", type=Path, required=True); parser.add_argument("--arrow-root", type=Path, required=True); parser.add_argument("--candidates-root", type=Path, required=True); parser.add_argument("--dataset-manifest", type=Path, required=True); parser.add_argument("--date-list", type=Path, required=True); parser.add_argument("--factors", type=Path, required=True); parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args(argv)
    manifest = json.loads(args.dataset_manifest.read_text()); dates = [x for x in args.date_list.read_text().splitlines() if x]
    if _sha(args.date_list) != manifest["production_date_list_sha256"]: raise ValueError("frozen date-list SHA mismatch")
    splits = {name: [d for d in dates if spec["date_start"] <= d <= spec["date_end"]] for name, spec in manifest["splits"].items() if name in ("training", "observation")}
    factors = [x for x in args.factors.read_text().splitlines() if x]
    if len(factors) != 48: raise ValueError("formal L4 requires exactly 48 factors")
    docs = build_portraits(args.result_root, splits, manifest["labels"]["names"], manifest["universes"], factors, manifest["events"]["evaluation_events"], args.arrow_root, args.candidates_root)
    write_portraits(docs, args.output_root); return 0


if __name__ == "__main__": raise SystemExit(main())
