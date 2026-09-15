"""Run the non-holdout half of one L4 round after production completes.

This job is submitted with an ``afterok`` dependency on every terminal L4
production lane.  It deliberately stops before 2025: selection and holdout
display are separate, auditable stages.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))


SOURCE_EVENTS = (92700000, 100000000, 103000000, 110000000, 113000000, 133000000, 140000000, 143000000)
EVAL_EVENTS = (92600000, 100000000, 103000000, 110000000, 113000000, 133000000, 140000000, 143000000)
LABELS = ("raw926", "ease926")
UNIVERSES = ("000985", "003800", "000906")
METRICS = tuple(["D{}".format(i) for i in range(1, 11)] + ["LS", "Monotonicity", "IC", "RankIC"])


def _sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return "sha256:" + h.hexdigest()


def _next_day(value):
    return (datetime.strptime(value, "%Y%m%d") + timedelta(days=1)).strftime("%Y%m%d")


def split_dates(date_list):
    dates = [str(x) for x in date_list if str(x)]
    return {
        "training": [x for x in dates if "20210104" <= x <= "20221230"],
        "observation": [x for x in dates if "20230103" <= x <= "20241231"],
    }


def _factor_arrow_is_valid(path, date, factor_names):
    try:
        import pyarrow.ipc as ipc
        with ipc.open_file(str(path)) as reader:
            table = reader.read_all()
        expected = ["symbol", "date", "event"] + list(factor_names)
        return (table.column_names == expected and table.num_rows > 0 and
                set(table["date"].to_pylist()) == {str(date)} and
                set(table["event"].to_pylist()) == set(EVAL_EVENTS))
    except Exception:
        return False


def convert_all(hdf5_root, arrow_root, dates, factor_manifest, repo_root, workers=8):
    from evaluations.convert_production_hdf5 import convert_hdf5
    manifest = json.loads(Path(factor_manifest).read_text(encoding="utf-8"))
    count = int(manifest["factor_count"])
    factor_names = list(manifest["factor_names"])

    def one(date):
        source = Path(hdf5_root) / date / "all_families" / "factors.h5"
        target = Path(arrow_root) / (date + ".arrow")
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and _factor_arrow_is_valid(target, date, factor_names):
            return {"date": date, "path": str(target), "sha256": _sha(target), "reused": True}
        import h5py
        with h5py.File(str(source), "r") as handle:
            expected_rows = len(handle["codelist_92700000"])
        result = convert_hdf5(source, target, expected_rows=expected_rows,
                              expected_events=SOURCE_EVENTS,
                              expected_factor_count=count,
                              require_explicit_reason=True,
                              factor_only=True)
        result["date"] = date
        result["sha256"] = _sha(target)
        result["reused"] = False
        return result

    with ThreadPoolExecutor(max_workers=int(workers)) as pool:
        return list(pool.map(one, dates))


def materialize_factor_views(source_root, output_root, dates, workers=8):
    """Create clean evaluator inputs; readiness is not emitted."""
    from evaluations.pilot_postprocess import write_factor_only_view
    output_root = Path(output_root)
    def one(date):
        source = Path(source_root) / (date + ".arrow")
        target = output_root / (date + ".arrow")
        target.parent.mkdir(parents=True, exist_ok=True)
        result = write_factor_only_view(source, target)
        result.update({"date": date, "reused": False})
        return result
    with ThreadPoolExecutor(max_workers=int(workers)) as pool:
        return list(pool.map(one, dates))


def resolve_candidates_root(path):
    """Accept either the campaign candidates root or one family directory."""
    root = Path(path)
    if any(root.glob("*/*.json")):
        return root
    if any(root.glob("*.json")):
        return root.parent
    raise ValueError("candidate directory has no Candidate JSON files: {}".format(root))


def valid_evaluation_result(path, dates, factors, events=EVAL_EVENTS):
    try:
        import pandas as pd
        frame = pd.read_parquet(path)
        expected_index = pd.MultiIndex.from_product(
            [[str(x) for x in dates], list(events)], names=["date", "event"])
        expected_columns = [factor + "|" + metric for factor in factors for metric in METRICS]
        frame.index = pd.MultiIndex.from_arrays(
            [[str(x) for x in frame.index.get_level_values(0)],
             frame.index.get_level_values(1)], names=frame.index.names)
        return (frame.index.equals(expected_index) and
                list(frame.columns) == expected_columns)
    except Exception:
        return False


def evaluate(result_root, arrow_root, split_dates, factor_group, toolkit, factors, workers=8):
    result = {}
    for split, dates in split_dates.items():
        if not dates:
            raise ValueError("empty split: " + split)
        split_root = Path(result_root) / split
        split_root.mkdir(parents=True, exist_ok=True)
        for label in LABELS:
            for universe in UNIVERSES:
                output = split_root / label / universe
                command = [
                    "/usr/local/python3.8.10/bin/python3", str(toolkit),
                    "--sdate", dates[0], "--edate", _next_day(dates[-1]),
                    "--label", label, "--universe", universe,
                    "--factor_group", factor_group,
                    "--factor_path", str(arrow_root),
                    "--workers", str(int(workers)), "--output_dir", str(output),
                ]
                path = output / "res_full.parquet"
                reused = valid_evaluation_result(path, dates, factors)
                if not reused:
                    subprocess.run(command, check=True)
                if not path.is_file():
                    raise RuntimeError("evaluator did not produce " + str(path))
                if not valid_evaluation_result(path, dates, factors):
                    raise RuntimeError("evaluator result violates frozen contract: " + str(path))
                result[split + "/" + label + "/" + universe] = {
                    "path": str(path), "sha256": _sha(path),
                    "rows": int(len(__import__("pandas").read_parquet(path))),
                    "reused": reused,
                }
    return result


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--hdf5-root", required=True)
    parser.add_argument("--arrow-root", required=True)
    parser.add_argument("--factor-arrow-root", "--evaluator-arrow-root", dest="factor_arrow_root")
    parser.add_argument("--result-root", required=True)
    parser.add_argument("--factor-manifest", required=True)
    parser.add_argument("--candidates-root", required=True)
    parser.add_argument("--portrait-root", required=True)
    parser.add_argument("--receipt", required=True)
    parser.add_argument("--submission-manifest", default="campaigns/sfm_stream_002/manifests/market-microstructure-l4-submission-20260914.json")
    parser.add_argument("--date-list", required=True)
    parser.add_argument("--toolkit", default="/mnt/beegfs_ssd_raid91/10513_fangwei/factor_eval_toolkit/scripts/evaluate_factors.py")
    parser.add_argument("--factor-group", default="market_microstructure_round_001")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args(argv)

    dates = [x.strip() for x in Path(args.date_list).read_text(encoding="utf-8").splitlines() if x.strip()]
    splits = split_dates(dates)
    all_dates = splits["training"] + splits["observation"]
    manifest = json.loads(Path(args.factor_manifest).read_text(encoding="utf-8"))
    factors = list(manifest["factor_names"])
    factor_arrow_root = (args.factor_arrow_root or
                         (str(args.arrow_root).rstrip("/") + "-factor-only"))
    conversion = convert_all(args.hdf5_root, factor_arrow_root, all_dates,
                             args.factor_manifest, Path.cwd(), workers=args.workers)
    evaluation = evaluate(args.result_root, factor_arrow_root, splits,
                          args.factor_group, args.toolkit, factors, workers=args.workers)

    from evaluations.l4_portrait import build_portraits, write_portraits
    submission = json.loads(Path(args.submission_manifest).read_text(encoding="utf-8"))
    evaluation_receipt = Path(args.receipt).with_name(Path(args.receipt).stem + "-evaluation.json")
    evaluation_payload = {
        "schema_version": 1, "kind": "l4_evaluation_receipt",
        "status": "complete", "decision": "observation_only",
        "promotion_allowed": False, "holdout_read": False,
        "dates": splits, "factor_count": len(factors), "events": list(EVAL_EVENTS),
        "factor_arrow_root": str(Path(factor_arrow_root).resolve()),
        "factor_arrow_columns": ["symbol", "date", "event"] + factors,
        "status_columns_emitted": False,
        "results": evaluation,
    }
    evaluation_receipt.parent.mkdir(parents=True, exist_ok=True)
    evaluation_receipt.write_text(json.dumps(evaluation_payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    release_root = Path(submission["release_root"])
    dataset_manifest = REPOSITORY_ROOT / "campaigns/sfm_stream_001/manifests/formal-history-dataset-v2.json"
    provenance = {
        "dataset_manifest_path": str(dataset_manifest.resolve()),
        "dataset_manifest_sha256": _sha(dataset_manifest),
        "binary_path": str(release_root / "factor_main"), "binary_sha256": _sha(release_root / "factor_main"),
        "config_path": str(release_root / "config_factor.json"), "config_sha256": _sha(release_root / "config_factor.json"),
        "evaluator_path": str(Path(args.toolkit).resolve()), "evaluator_sha256": _sha(args.toolkit),
        "label_contract_path": str((REPOSITORY_ROOT / "evaluations/label_contract.json").resolve()), "label_contract_sha256": _sha(REPOSITORY_ROOT / "evaluations/label_contract.json"),
        "methodology_path": str((REPOSITORY_ROOT / "evaluations/README.md").resolve()), "methodology_sha256": _sha(REPOSITORY_ROOT / "evaluations/README.md"),
        "evaluation_receipt_path": str(evaluation_receipt.resolve()), "evaluation_receipt_sha256": _sha(evaluation_receipt),
    }
    portraits = build_portraits(Path(args.result_root), splits, LABELS, UNIVERSES, factors, EVAL_EVENTS,
                                Path(factor_arrow_root), resolve_candidates_root(args.candidates_root),
                                campaign_id="sfm_stream_002",
                                provenance=provenance, dataset_id="sfm_stream_002_market_microstructure_formal_v1",
                                portrait_suffix="market_microstructure_l4_v1", return_validity_matrix=True)
    documents, validity = portraits
    portrait_paths = write_portraits(documents, args.portrait_root)
    receipt = {
        "schema_version": 1, "kind": "l4_postprocess_receipt",
        "status": "complete", "decision": "observation_only",
        "promotion_allowed": False, "holdout_read": False,
        "dates": splits, "factor_count": len(factors), "events": list(EVAL_EVENTS),
        "factor_arrow_root": str(Path(factor_arrow_root).resolve()),
        "factor_arrow_columns": ["symbol", "date", "event"] + factors,
        "status_columns_emitted": False,
        "result_root": str(Path(args.result_root).resolve()),
        "conversion_count": len(conversion), "conversion": conversion,
        "evaluation": evaluation, "portrait_count": len(portrait_paths),
        "portrait_root": str(Path(args.portrait_root).resolve()),
        "validity_status_counts": {state: sum(x.get("status") == state for x in validity)
                                   for state in ("pass", "not_ready", "metric_undefined", "coverage_fail", "data_error", "review")},
    }
    Path(args.receipt).parent.mkdir(parents=True, exist_ok=True)
    Path(args.receipt).write_text(json.dumps(receipt, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": "complete", "converted": len(conversion), "evaluations": len(evaluation), "portraits": len(portrait_paths), "holdout_read": False}))


if __name__ == "__main__":
    main()
