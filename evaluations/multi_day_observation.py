"""Aggregate multi-day factor observations without promotion decisions."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Dict, Iterable, Optional, Sequence


def summarize_metric_frame(rows: Iterable[dict]) -> Dict[str, dict]:
    grouped = {}
    for row in rows:
        grouped.setdefault(row["factor"], []).append(row)
    report = {}
    for factor, values in grouped.items():
        def metric(name):
            return [float(value[name]) for value in values if value.get(name) is not None]
        def mean(name):
            values_for_metric = metric(name)
            return sum(values_for_metric) / len(values_for_metric) if values_for_metric else None
        def positive_fraction(name):
            values_for_metric = metric(name)
            return sum(value > 0 for value in values_for_metric) / len(values_for_metric) if values_for_metric else None
        report[factor] = {
            "observation_count": len(values),
            "mean_rank_ic": mean("RankIC"),
            "rank_ic_positive_fraction": positive_fraction("RankIC"),
            "mean_ic": mean("IC"),
            "ic_positive_fraction": positive_fraction("IC"),
            "mean_long_short": mean("LS"),
            "long_short_positive_fraction": positive_fraction("LS"),
            "mean_monotonicity": mean("Monotonicity"),
        }
    return report


def _rows_from_frame(frame):
    rows = []
    for index, values in frame.iterrows():
        for column, value in values.items():
            factor, metric = column.rsplit("|", 1)
            row = next((item for item in rows if item["factor"] == factor and item["date"] == str(index[0]) and item["event"] == int(index[1])), None)
            if row is None:
                row = {"factor": factor, "date": str(index[0]), "event": int(index[1])}
                rows.append(row)
            row[metric] = None if value != value else float(value)
    return rows


def build_observation(result_root: Path, dates: Sequence[str], labels: Sequence[str], universes: Sequence[str]) -> dict:
    try:
        import pandas as pd
    except ImportError as error:
        raise RuntimeError("pandas is required for multi-day observation") from error
    combinations = {}
    expected_dates = set(dates)
    factor_sets = None
    for label in labels:
        for universe in universes:
            path = result_root / label / universe / "res_full.parquet"
            frame = pd.read_parquet(path)
            actual_dates = {str(value) for value in frame.index.get_level_values(0)}
            if actual_dates != expected_dates:
                raise ValueError("{} {} date set mismatch".format(label, universe))
            names = sorted({column.rsplit("|", 1)[0] for column in frame.columns})
            if factor_sets is None:
                factor_sets = names
            elif names != factor_sets:
                raise ValueError("factor set mismatch across combinations")
            combinations["{}:{}".format(label, universe)] = summarize_metric_frame(_rows_from_frame(frame))
    return {
        "schema_version": 1,
        "date_count": len(dates),
        "dates": list(dates),
        "event_count": 8,
        "labels": list(labels),
        "universes": list(universes),
        "factor_count": len(factor_sets or []),
        "combinations": combinations,
        "promotion_allowed": False,
        "decision": "observation_only",
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result-root", type=Path, required=True)
    parser.add_argument("--dates", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    dates = [value for value in args.dates.split(",") if value]
    report = build_observation(args.result_root, dates, ["raw926", "ease926"], ["000985", "003800", "000906"])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"date_count": report["date_count"], "factor_count": report["factor_count"], "promotion_allowed": False}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
