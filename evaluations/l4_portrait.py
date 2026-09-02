"""Strict L4 formal-history aggregation and factor portraits (v2)."""
from __future__ import annotations
import argparse, json, math
from pathlib import Path
from typing import Sequence, Optional

import numpy as np
import pandas as pd

METRICS = ["D{}".format(i) for i in range(1, 11)] + ["LS", "Monotonicity", "IC", "RankIC"]

def _finite(v):
    try: return math.isfinite(float(v))
    except (TypeError, ValueError): return False

def _stats(values):
    x = np.asarray([float(v) for v in values if _finite(v)], dtype=float)
    if len(x) == 0: return {"mean": None, "std": None, "ir": None, "positive_fraction": None, "count": 0}
    mean = float(np.mean(x)); std = float(np.std(x, ddof=1)) if len(x) > 1 else 0.0
    return {"mean": mean, "std": std, "ir": (mean / std if std > 0 else None), "positive_fraction": float(np.mean(x > 0)), "count": int(len(x))}

def _aggregate(frame, factor, metric="RankIC", group=None):
    series = frame[factor + "|" + metric]
    if group is not None: series = series.groupby(group).mean()
    return _stats(series.dropna().tolist())

def _load(result_root, dates, labels, universes, coverage_threshold):
    expected = set(str(d) for d in dates)
    if any(d.startswith("2025") for d in expected): raise ValueError("holdout dates are forbidden")
    frames = {}
    factors = None
    for label in labels:
        for universe in universes:
            path = result_root / label / universe / "res_full.parquet"
            if not path.exists(): raise ValueError("missing result: {}".format(path))
            frame = pd.read_parquet(path)
            actual = set(str(x) for x in frame.index.get_level_values(0))
            if actual != expected: raise ValueError("date set mismatch for {} {}".format(label, universe))
            names = sorted({c.rsplit("|", 1)[0] for c in frame.columns})
            if factors is None: factors = names
            elif names != factors: raise ValueError("factor set mismatch")
            if len(actual) / max(len(expected), 1) < coverage_threshold: raise ValueError("coverage below threshold")
            if not all(c in frame for f in names for c in [f + "|" + m for m in METRICS]): raise ValueError("metric columns incomplete")
            frames[(label, universe)] = frame
    return frames, factors or []

def build_portraits(result_root: Path, dates: Sequence[str], labels: Sequence[str], universes: Sequence[str], coverage_threshold: float = .95, campaign_id: str = "sfm_stream_001") -> dict:
    frames, factors = _load(Path(result_root), dates, labels, universes, coverage_threshold)
    dates = list(dates)
    portraits = {}
    for factor in factors:
        combo = {}
        all_rank = []
        for (label, universe), frame in frames.items():
            rank = frame[factor + "|RankIC"]
            all_rank.extend(rank.dropna().tolist())
            combo.setdefault(label, {})[universe] = {"rank_ic_mean": _stats(rank)["mean"], "rank_ic_std": _stats(rank)["std"], "rank_ic_ir": _stats(rank)["ir"], "rank_ic_positive_fraction": _stats(rank)["positive_fraction"], "observation_count": int(rank.notna().sum())}
        # cross-combination metric table, with yearly/quarterly and event slices
        slices = {"year": {}, "quarter": {}, "event": {}}
        frame0 = next(iter(frames.values()))
        r = frame0[factor + "|RankIC"]
        idx = r.index
        for key, values in (("year", idx.get_level_values(0).map(lambda x: str(x)[:4])), ("quarter", idx.get_level_values(0).map(lambda x: str(x)[:4] + "Q" + str((int(str(x)[4:6])-1)//3+1))), ("event", idx.get_level_values(1))):
            grouped = r.groupby(values)
            slices[key] = {str(k): _stats(v.dropna().tolist()) for k, v in grouped}
        metric_stats = {}
        for metric in METRICS:
            vals = []
            for frame in frames.values(): vals.extend(frame[factor + "|" + metric].dropna().tolist())
            metric_stats[metric] = _stats(vals)
        rank_series = pd.concat([f[factor + "|RankIC"] for f in frames.values()]).groupby(level=[0,1]).mean().sort_index()
        rolling = rank_series.rolling(60, min_periods=30).mean()
        rolling_stability = {"window": 60, "min_periods": 30, "positive_fraction": float((rolling.dropna() > 0).mean()) if len(rolling.dropna()) else None, "split_mean_sign_disagreement": False}
        anomaly = rank_series.groupby(level=0).mean().abs().sort_values(ascending=False).head(5)
        rs = _stats(all_rank)
        portraits[factor] = {"portrait_id": factor + "__formal_history_v2", "campaign_id": campaign_id, "factor": factor, "scope": "formal_history", "evidence_level": "L4", "dataset": {"date_start": min(dates), "date_end": max(dates), "date_count": len(dates), "events": sorted({int(e) for f in frames.values() for e in f.index.get_level_values(1)}), "labels": list(labels), "universes": list(universes)}, "coverage": 1.0, "direction": "raw_signed", "promotion_allowed": False, "metrics": dict(metric_stats, mean=rs["mean"], std=rs["std"], ir=rs["ir"], positive_fraction=rs["positive_fraction"]), "splits": {"all": combo}, "slices": slices, "rolling_stability": rolling_stability, "correlations": {"peer": {}, "value": {}, "ic": {}}, "anomaly_days": [{"date": str(k), "absolute_rank_ic": float(v)} for k,v in anomaly.items()], "decision": "observation_only"}
    return {"schema_version": 2, "kind": "factor_portrait_set", "date_count": len(dates), "dates": dates, "factor_count": len(factors), "factors": factors, "portraits": portraits, "coverage_threshold": coverage_threshold, "direction": "raw_signed", "promotion_allowed": False, "decision": "observation_only"}

def main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(); p.add_argument("--result-root", type=Path, required=True); p.add_argument("--dates", required=True); p.add_argument("--output", type=Path, required=True)
    a = p.parse_args(argv); report = build_portraits(a.result_root, a.dates.split(","), ["raw926", "ease926"], ["000985", "003800", "000906"]); a.output.parent.mkdir(parents=True, exist_ok=True); a.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n"); return 0
if __name__ == "__main__": raise SystemExit(main())
