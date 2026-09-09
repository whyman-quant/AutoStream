"""Materialize deterministic L4 evaluation denominators for preflight dates.

The materializer is intentionally read-only: it reads the frozen universe,
tradability and label snapshots and emits one record for each
date x event x universe x label cell.  Holdout dates are rejected.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Mapping, Sequence

import pandas as pd

EXPECTED_EVENTS = (92600000, 100000000, 103000000, 110000000, 113000000, 133000000, 140000000, 143000000)
EXPECTED_UNIVERSES = ("000985", "003800", "000906")
EXPECTED_LABELS = ("raw926", "ease926")
EXPECTED_LABEL_COLUMNS = {
    "raw926": "v_1D_v_demean",
    "ease926": "v_1D_v_neuted",
}
PRODUCTION_START = "20210104"
PRODUCTION_END = "20241231"
HOLDOUT_START = "20250102"
HOLDOUT_END = "20251210"


def _symbol(value) -> str:
    value = str(value)
    return value.split(".", 1)[0]


def _symbols_hash(symbols: Sequence[str]) -> str:
    payload = "".join(str(s) + "\n" for s in sorted(set(symbols))).encode("utf-8")
    return "sha256:" + hashlib.sha256(payload).hexdigest()


def _label_columns(frame: pd.DataFrame, label_name: str = None):
    if "symbol" not in frame.columns or "event" not in frame.columns:
        raise ValueError("label frame must contain symbol and event columns")
    expected = EXPECTED_LABEL_COLUMNS.get(label_name)
    if expected is not None:
        if expected not in frame.columns:
            raise ValueError(
                "exact label column missing for {}: {}".format(label_name, expected)
            )
        return expected
    value_columns = [c for c in frame.columns if c not in ("symbol", "event", "date")]
    if not value_columns:
        raise ValueError("label frame must contain a label value column")
    return value_columns[0]


def materialize_date(
    date: str,
    universe: pd.DataFrame,
    tradability: pd.DataFrame,
    labels: Mapping[str, pd.DataFrame],
    *,
    events: Sequence[int] = EXPECTED_EVENTS,
    universes: Sequence[str] = EXPECTED_UNIVERSES,
) -> list:
    """Return denominator records for one production date.

    ``universe`` and ``tradability`` are indexed by exchange-suffixed symbol;
    labels use unsuffixed symbols and ``symbol,event`` keys.  A symbol enters
    a cell iff it is a member of that universe, has ``isdt1 + iszt1 == 0`` and
    has a non-null label value for the event.
    """
    date = str(date)
    if HOLDOUT_START <= date <= HOLDOUT_END or date > PRODUCTION_END:
        raise ValueError("holdout date is not allowed: {}".format(date))
    if date < PRODUCTION_START:
        raise ValueError("date precedes frozen production range: {}".format(date))
    for name in universes:
        if name not in universe.columns:
            raise ValueError("universe column missing: {}".format(name))
    for name in EXPECTED_LABELS:
        if name not in labels:
            raise ValueError("label missing: {}".format(name))
        if "date" in labels[name].columns:
            label_dates = {str(value) for value in labels[name]["date"].dropna().unique()}
            if label_dates and label_dates != {date}:
                raise ValueError("label date mismatch for {}: {}".format(name, sorted(label_dates)))
        _label_columns(labels[name], name)
    if "isdt1" not in tradability.columns or "iszt1" not in tradability.columns:
        raise ValueError("tradability must contain isdt1 and iszt1")

    # Apply the evaluator's exact non-limit rule and normalize exchange suffixes.
    tradable = tradability[(tradability["isdt1"] + tradability["iszt1"]) == 0].copy()
    tradable_symbols = {_symbol(value) for value in tradable.index}
    member_symbols = {
        name: {_symbol(index) for index, value in universe[name].items() if value == 1 and _symbol(index) in tradable_symbols}
        for name in universes
    }
    labels_by_key = {}
    for label_name, frame in labels.items():
        value_column = _label_columns(frame, label_name)
        keys = [(_symbol(row["symbol"]), int(row["event"])) for _, row in frame.iterrows()]
        if len(keys) != len(set(keys)):
            raise ValueError("duplicate symbol,event keys in label {}".format(label_name))
        labels_by_key[label_name] = {
            (_symbol(row["symbol"]), int(row["event"])): row[value_column]
            for _, row in frame.iterrows()
            if pd.notna(row[value_column])
        }
    rows = []
    for event in events:
        for universe_name in universes:
            base = member_symbols[universe_name]
            for label_name in EXPECTED_LABELS:
                selected = sorted(symbol for symbol in base if (symbol, int(event)) in labels_by_key[label_name])
                rows.append({
                    "date": date,
                    "event": int(event),
                    "universe": universe_name,
                    "label": label_name,
                    "symbols": selected,
                    "symbol_count": len(selected),
                    "symbols_sha256": _symbols_hash(selected),
                    "label_nonnull_count": len(selected),
                    "non_limit_exclusion_count": len({_symbol(i) for i in universe[universe_name].index if universe[universe_name].get(i) == 1}) - len(base),
                })
    return rows


def load_date_inputs(date: str, *, universe_root: str, label_roots: Mapping[str, str], tradability_loader=None):
    """Load snapshots using the same utility paths as the evaluator."""
    universe_path = Path(universe_root.format(YYYYMMDD=date))
    universe = pd.read_parquet(universe_path)
    if tradability_loader is None:
        import sys
        data_root = "/home/fangwei/mnt-ssd/fwm/calculation/data"
        if data_root not in sys.path:
            sys.path.insert(0, data_root)
        from utils_data import get_dbar_data
        tradability = get_dbar_data(date)
    else:
        tradability = tradability_loader(date)
    labels = {}
    for name, root in label_roots.items():
        value_column = EXPECTED_LABEL_COLUMNS[name]
        labels[name] = pd.read_feather(
            Path(root) / (date + ".arrow"),
            columns=["symbol", "date", "event", value_column],
        )
    return universe, tradability, labels


def materialize_dates(dates: Sequence[str], *, universe_root: str, label_roots: Mapping[str, str], tradability_loader=None):
    rows = []
    for date in dates:
        universe, tradability, labels = load_date_inputs(date, universe_root=universe_root, label_roots=label_roots, tradability_loader=tradability_loader)
        rows.extend(materialize_date(date, universe, tradability, labels))
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dates", required=True, help="comma-separated production dates")
    parser.add_argument("--output", required=True)
    parser.add_argument("--universe-root", default="/mnt/beegfs_dev/storage_r/706_wgh/app_working_dir/data/AlphaFW/fwm01/basedata/{YYYYMMDD}/universe.parquet")
    parser.add_argument("--raw-label-root", required=True)
    parser.add_argument("--ease-label-root", required=True)
    args = parser.parse_args()
    dates = tuple(item for item in args.dates.split(",") if item)
    rows = materialize_dates(dates, universe_root=args.universe_root, label_roots={"raw926": args.raw_label_root, "ease926": args.ease_label_root})
    # Keep the committed manifest compact; the sorted symbol list is represented
    # by its count and content hash while ``materialize_date`` remains useful to
    # callers that need the concrete symbols for a local check.
    manifest_rows = [{key: value for key, value in row.items() if key != "symbols"} for row in rows]
    report = {"schema_version": 1, "dates": list(dates), "events": list(EXPECTED_EVENTS), "universes": list(EXPECTED_UNIVERSES), "labels": list(EXPECTED_LABELS), "rows": manifest_rows, "holdout_read": False}
    destination = Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"date_count": len(dates), "cell_count": len(rows), "holdout_read": False}, sort_keys=True))


if __name__ == "__main__":
    main()
