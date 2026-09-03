"""Directory-level technical acceptance for a factor campaign."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict

from .factor_eval import evaluate_columns, read_hdf5_factor_file


def evaluate_directory(
    directory: str,
    *,
    warmup: int = 0,
    min_post_warmup_coverage: float = 0.8,
    min_cross_section_std: float = 0.0,
    min_unique_count: int = 2,
    min_nonzero_ratio: float = 0.0,
    min_effective_rank_count: int = 2,
) -> Dict[str, object]:
    paths = sorted(Path(directory).glob("*.h5"))
    if not paths:
        raise ValueError(f"no HDF5 checkpoints found in {directory}")
    snapshots = []
    factor_names = None
    for path in paths:
        columns = read_hdf5_factor_file(str(path))
        names = list(columns)
        if factor_names is None:
            factor_names = names
        elif names != factor_names:
            raise ValueError(f"factorlist mismatch in {path.name}")
        results = evaluate_columns(
            columns,
            labels=None,
            warmup=warmup,
            min_post_warmup_coverage=min_post_warmup_coverage,
            min_cross_section_std=min_cross_section_std,
            min_unique_count=min_unique_count,
            min_nonzero_ratio=min_nonzero_ratio,
            min_effective_rank_count=min_effective_rank_count,
        )
        rejected = {name: value["reject_reasons"] for name, value in results.items() if value["status"] != "technical_pass"}
        snapshots.append({
            "file": str(path),
            "row_count": len(next(iter(columns.values()))) if columns else 0,
            "factor_count": len(columns),
            "technical_status": "technical_reject" if rejected else "technical_pass",
            "rejected_columns": rejected,
        })
    technical_status = "technical_pass" if all(item["technical_status"] == "technical_pass" for item in snapshots) else "technical_reject"
    return {
        "schema_version": 1,
        "directory": str(Path(directory)),
        "snapshot_count": len(snapshots),
        "factor_count": len(factor_names or []),
        "technical_status": technical_status,
        "evaluation_status": "data_missing",
        "promotion_allowed": False,
        "cross_section_thresholds": {
            "min_cross_section_std": min_cross_section_std,
            "min_unique_count": min_unique_count,
            "min_nonzero_ratio": min_nonzero_ratio,
            "min_effective_rank_count": min_effective_rank_count,
        },
        "snapshots": snapshots,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--warmup", type=int, default=0)
    parser.add_argument("--min-post-warmup-coverage", type=float, default=0.8)
    parser.add_argument("--min-cross-section-std", type=float, default=0.0)
    parser.add_argument("--min-unique-count", type=int, default=2)
    parser.add_argument("--min-nonzero-ratio", type=float, default=0.0)
    parser.add_argument("--min-effective-rank-count", type=int, default=2)
    args = parser.parse_args()
    report = evaluate_directory(
        args.directory,
        warmup=args.warmup,
        min_post_warmup_coverage=args.min_post_warmup_coverage,
        min_cross_section_std=args.min_cross_section_std,
        min_unique_count=args.min_unique_count,
        min_nonzero_ratio=args.min_nonzero_ratio,
        min_effective_rank_count=args.min_effective_rank_count,
    )
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    print(f"{report['technical_status']} {report['snapshot_count']} snapshots {report['factor_count']} factors")


if __name__ == "__main__":
    main()
