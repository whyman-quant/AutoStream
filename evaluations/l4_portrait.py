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


def _strict_frames(result_root, split_dates, labels, universes, factors, events, threshold):
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
                    if fraction < threshold: raise ValueError("coverage below 95 percent: {}".format(column))
                frames[(split, label, universe)] = frame
                receipts.setdefault(split, []).append({"label": label, "universe": universe, "path": str(path), "sha256": _sha(path), "rows": len(frame), "columns": len(frame.columns), "minimum_column_coverage": min(coverage.values())})
    return frames, receipts


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


def build_portraits(result_root: Path, split_dates: Mapping[str, Sequence[str]], labels: Sequence[str], universes: Sequence[str], expected_factors: Sequence[str], events: Sequence[int], arrow_root: Path, candidates_root: Path, coverage_threshold: float = .95, campaign_id: str = "sfm_stream_001", provenance: Optional[Mapping[str, str]] = None):
    factors, labels, universes, events = list(expected_factors), list(labels), list(universes), list(events)
    if len(set(factors)) != len(factors): raise ValueError("duplicate factors")
    frames, receipts = _strict_frames(result_root, split_dates, labels, universes, factors, events, coverage_threshold)
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
    return docs


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
