"""Freeze pre-holdout cell evidence and display selection without human input."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import pandas as pd

from campaigns.research_round import atomic_write_json
from campaigns.sfm_stream_002.l4_postprocess import EVAL_EVENTS, LABELS, UNIVERSES, split_dates
from evaluations.l4_evidence_cells import build_evidence_cells
from evaluations.research_selection import classify_cells, freeze_holdout_selection


def _metric_signature(result_root, factor):
    hashes = []
    root = Path(result_root)
    for split in ("training", "observation"):
        for label in LABELS:
            for universe in UNIVERSES:
                frame = pd.read_parquet(root / split / label / universe / "res_full.parquet")
                columns = [column for column in frame.columns if column.startswith(factor + "|")]
                hashes.append(pd.util.hash_pandas_object(frame[columns], index=True).values.tobytes())
    import hashlib
    digest = hashlib.sha256()
    for value in hashes:
        digest.update(value)
    return digest.hexdigest()


def deduplicate_exact_results(selection, result_root, factor_order):
    """Keep the first frozen candidate when evaluator panels are byte-identical."""
    selected = set(selection["selected_for_holdout_display"])
    seen = {}
    retained = []
    redundant = {}
    for factor in factor_order:
        if factor not in selected:
            continue
        signature = _metric_signature(result_root, factor)
        if signature in seen:
            redundant[factor] = seen[signature]
        else:
            seen[signature] = factor
            retained.append(factor)
    selection["selected_for_holdout_display"] = retained
    selection["exact_result_redundancy"] = redundant
    selection["deduplication_policy"] = "first_in_frozen_factor_manifest"
    return selection


def freeze(result_root, date_list, factor_manifest, output):
    dates = [value for value in Path(date_list).read_text(encoding="utf-8").splitlines() if value]
    manifest = json.loads(Path(factor_manifest).read_text(encoding="utf-8"))
    factors = list(manifest["factor_names"])
    splits = split_dates(dates)
    cells = build_evidence_cells(
        result_root, splits, factors, EVAL_EVENTS, LABELS, UNIVERSES)
    selection = freeze_holdout_selection(classify_cells(cells))
    selection = deduplicate_exact_results(selection, result_root, factors)
    selection.update({
        "round_id": "sfm_stream_002_round_001",
        "status": "complete",
        "decision": "holdout_display_only",
        "elimination_allowed": False,
        "manual_review_required": False,
    })
    atomic_write_json(output, selection)
    return selection


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result-root", required=True)
    parser.add_argument("--date-list", required=True)
    parser.add_argument("--factor-manifest", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    result = freeze(args.result_root, args.date_list, args.factor_manifest, args.output)
    print(json.dumps({"status": "complete",
                      "selected": result["selected_for_holdout_display"],
                      "redundant": result["exact_result_redundancy"],
                      "holdout_read": False}, ensure_ascii=False))


if __name__ == "__main__":
    main()
