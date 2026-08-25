"""Fail-closed input inventory for a multi-day factor pilot."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict, Optional, Sequence


def inspect_dates(dates: Sequence[str], *, factor_root: str, label_root: str) -> Dict[str, object]:
    normalized = [str(value) for value in dates]
    if len(set(normalized)) != len(normalized):
        raise ValueError("duplicate pilot dates are not allowed")
    if not normalized:
        raise ValueError("at least one pilot date is required")
    factors = Path(factor_root)
    labels = Path(label_root)
    rows = []
    for date in normalized:
        factor_path = factors / (date + ".arrow")
        label_path = labels / (date + ".arrow")
        if factor_path.is_file() and label_path.is_file():
            status = "ready"
        elif not factor_path.is_file():
            status = "factor_missing"
        else:
            status = "label_missing"
        rows.append({
            "date": date,
            "status": status,
            "factor_path": str(factor_path),
            "label_path": str(label_path),
        })
    ready_count = sum(row["status"] == "ready" for row in rows)
    return {
        "schema_version": 1,
        "factor_root": str(factors),
        "label_root": str(labels),
        "date_count": len(rows),
        "ready_count": ready_count,
        "ready": ready_count == len(rows),
        "dates": rows,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dates", required=True, help="comma-separated YYYYMMDD dates")
    parser.add_argument("--factor-root", required=True)
    parser.add_argument("--label-root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)

    dates = [value.strip() for value in args.dates.split(",") if value.strip()]
    report = inspect_dates(dates, factor_root=args.factor_root, label_root=args.label_root)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({
        "output": str(output),
        "ready": report["ready"],
        "ready_count": report["ready_count"],
        "date_count": report["date_count"],
    }, ensure_ascii=False))
    return 0 if report["ready"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
