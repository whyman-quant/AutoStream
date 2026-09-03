"""CLI for technical factor evaluation and optional label evaluation."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from .factor_eval import evaluate_columns, read_hdf5_factor_file, read_label_csv


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--factor-file", required=True)
    parser.add_argument("--labels-csv")
    parser.add_argument("--label-column", default="label")
    parser.add_argument("--output", required=True)
    parser.add_argument("--warmup", type=int, default=0)
    parser.add_argument("--min-post-warmup-coverage", type=float, default=0.8)
    parser.add_argument("--min-cross-section-std", type=float, default=0.0)
    parser.add_argument("--min-unique-count", type=int, default=2)
    parser.add_argument("--min-nonzero-ratio", type=float, default=0.0)
    parser.add_argument("--min-effective-rank-count", type=int, default=2)
    parser.add_argument("--groups", type=int, default=10)
    args = parser.parse_args()
    columns = read_hdf5_factor_file(args.factor_file)
    labels = read_label_csv(args.labels_csv, args.label_column) if args.labels_csv else None
    if labels is not None and len(labels) != len(next(iter(columns.values()))):
        raise SystemExit("labels row count does not match factordata row count")
    results = evaluate_columns(
        columns,
        labels,
        warmup=args.warmup,
        min_post_warmup_coverage=args.min_post_warmup_coverage,
        groups=args.groups,
        min_cross_section_std=args.min_cross_section_std,
        min_unique_count=args.min_unique_count,
        min_nonzero_ratio=args.min_nonzero_ratio,
        min_effective_rank_count=args.min_effective_rank_count,
    )
    payload = {
        "schema_version": 1,
        "factor_file": str(Path(args.factor_file).resolve()),
        "row_count": len(next(iter(columns.values()))) if columns else 0,
        "factor_count": len(columns),
        "evaluation_status": "data_missing" if labels is None else "evaluated",
        "columns": results,
    }
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    Path(args.output).write_text(json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
