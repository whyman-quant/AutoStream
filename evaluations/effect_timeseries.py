"""Build frozen, readiness-aware factor effect time series.

The module deliberately accepts a dataframe rather than a particular evaluator
artifact format.  Columns are ``date,event,universe,label,ready,rank_ic`` and
``long_return`` (or ``q1_return`` … ``q10_return``).  Missing/not-ready dates
are omitted from cumulative products; they are represented by coverage fields.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np
import pandas as pd

EVENTS = tuple(str(i) for i in range(8))
UNIVERSES = ("000906", "003800", "000985")
LABELS = ("raw926", "ease926")


def _col(frame, *names):
    for name in names:
        if name in frame.columns:
            return name
    return None


def _frozen_selection(receipt, factor, key):
    if isinstance(receipt, (str, Path)):
        receipt = json.loads(Path(receipt).read_text())
    if not isinstance(receipt, dict):
        raise ValueError("selection receipt is required")
    selected = receipt.get("selected_for_holdout_display", receipt.get("selected_factors", []))
    if selected and factor not in selected:
        raise ValueError("factor is not selected for holdout display")
    directions = receipt.get("directions", receipt.get("factor_directions", {}))
    direction = directions.get(factor, receipt.get("direction", "positive")) if isinstance(directions, dict) else "positive"
    events = receipt.get("best_events", receipt.get("frozen_best_events", {}))
    event = events.get(key) if isinstance(events, dict) else None
    return str(direction), (str(event) if event is not None else None)


def _check_holdout(frame, selection_receipt, authorization, factor):
    dates = frame["date"].astype(str)
    has_holdout = bool((dates >= "20250101").any())
    if not has_holdout:
        return
    if isinstance(authorization, (str, Path)):
        authorization = json.loads(Path(authorization).read_text())
    if isinstance(selection_receipt, (str, Path)):
        selection_receipt = json.loads(Path(selection_receipt).read_text())
    if selection_receipt is None or not isinstance(authorization, dict):
        raise ValueError("2025 holdout requires selection receipt and authorization")
    if not authorization.get("selection_frozen") or not authorization.get("best_event_frozen"):
        raise ValueError("holdout authorization must freeze selection and best event")
    if authorization.get("readable_split") != "holdout" or authorization.get("allowed_use") != "display_only":
        raise ValueError("holdout authorization is not display-only")
    if authorization.get("feedback_allowed", True):
        raise ValueError("holdout feedback is forbidden")
    selected = selection_receipt.get("selected_for_holdout_display", selection_receipt.get("selected_factors", []))
    if selected and factor not in selected:
        raise ValueError("factor is not selected for holdout display")


def build_effect_timeseries(frame, factor, selection_receipt=None, authorization=None,
                            output_path=None):
    """Return cumulative IC/long/decile rows and an auditable receipt.

    Event selection is supplied by the frozen selection receipt.  If no best
    event is supplied, it is left ``None`` (plotting can still render all
    events); this avoids accidentally selecting on 2025 data.
    """
    if not isinstance(frame, pd.DataFrame):
        frame = pd.DataFrame(frame)
    required = {"date", "event", "universe", "label"}
    missing = required.difference(frame.columns)
    if missing:
        raise ValueError("missing columns: {}".format(",".join(sorted(missing))))
    frame = frame.copy()
    frame["date"] = frame["date"].astype(str)
    frame["event"] = frame["event"].astype(str)
    frame["universe"] = frame["universe"].astype(str).str.zfill(6)
    frame["label"] = frame["label"].astype(str)
    _check_holdout(frame, selection_receipt, authorization, factor)
    receipt_obj = json.loads(Path(selection_receipt).read_text()) if isinstance(selection_receipt, (str, Path)) else selection_receipt
    rows = []
    for (universe, label, event), group in frame.groupby(["universe", "label", "event"], sort=False):
        direction, best_event = _frozen_selection(receipt_obj or {}, factor, universe + "|" + label)
        # Once holdout display is authorized, only the frozen event may be read
        # from 2025; all pre-holdout events remain available for comparison.
        if best_event is not None and (group["date"] >= "20250101").any() and event != best_event:
            group = group[group["date"] < "20250101"]
            if group.empty:
                continue
        group = group.sort_values("date")
        ready_col = _col(group, "ready", "readiness")
        ready = group[ready_col].astype(bool) if ready_col else pd.Series(True, index=group.index)
        total = 0; ready_count = 0; ic = 0.0; long = 1.0; q = dict((i, 1.0) for i in range(1, 11))
        for idx, (_, record) in enumerate(group.iterrows()):
            total += 1
            if not bool(ready.loc[idx] if idx in ready.index else record.get(ready_col, True)):
                continue
            ready_count += 1
            rank_name = _col(group, "rank_ic", "rankic", "RankIC")
            rank = record[rank_name] if rank_name else np.nan
            if pd.notna(rank):
                ic += float(rank)
            long_name = _col(group, "long_return", "long", "long_top_return")
            if long_name is None:
                preferred = 10 if direction != "negative" else 1
                long_name = _col(group, "q%d_return" % preferred, "q%d" % preferred)
            lr = record[long_name] if long_name else np.nan
            if pd.notna(lr):
                long *= (1.0 + float(lr))
            for decile in range(1, 11):
                name = _col(group, "q%d_return" % decile, "q%d" % decile)
                value = record[name] if name else np.nan
                if pd.notna(value):
                    q[decile] *= (1.0 + float(value))
            payload = {"date": record["date"], "event": event, "universe": universe,
                       "label": label, "factor": factor, "ready": True,
                       "ic_cumulative": ic, "long_cumulative": long - 1.0,
                       "coverage": float(ready_count / total) if total else 0.0,
                       "best_event": best_event}
            payload.update({"q%d_cumulative" % i: q[i] - 1.0 for i in range(1, 11)})
            rows.append(payload)
    digest = hashlib.sha256(json.dumps(rows, sort_keys=True, default=str).encode()).hexdigest()
    result = {"schema_version": 1, "kind": "factor_effect_timeseries", "factor": factor,
              "rows": rows, "selection_receipt": receipt_obj,
              "selection_receipt_sha256": None,
              "coverage": {"total_dates": int(frame["date"].nunique()),
                           "ready_dates": int(frame[_col(frame, "ready", "readiness")].astype(bool).sum()) if _col(frame, "ready", "readiness") else int(len(frame)),
                           "dates": sorted(frame["date"].unique().tolist())},
              "output_sha256": "sha256:" + digest,
              "holdout_read": bool((frame["date"] >= "20250101").any())}
    if receipt_obj is not None:
        result["selection_receipt_sha256"] = "sha256:" + hashlib.sha256(json.dumps(receipt_obj, sort_keys=True).encode()).hexdigest()
    if output_path:
        Path(output_path).write_text(json.dumps(result, indent=2, sort_keys=True, default=str))
    return result


__all__ = ["build_effect_timeseries", "EVENTS", "UNIVERSES", "LABELS"]
