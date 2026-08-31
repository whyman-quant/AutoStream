"""Strict, date-aware post-processing for multi-day factor Pilots."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Optional, Sequence

from .convert_production_hdf5 import convert_hdf5


def _decode(value):
    return value.decode().rstrip("\x00") if isinstance(value, bytes) else str(value)


def inspect_hdf5(path: Path, *, expected_events: Sequence[int]) -> dict:
    import h5py
    import numpy as np

    with h5py.File(str(path), "r") as source:
        factor_names = [_decode(value) for value in source["factorlist"][:]]
        if not factor_names or len(set(factor_names)) != len(factor_names):
            raise ValueError("factorlist must be non-empty and unique")
        stock_count = None
        for event in expected_events:
            key = str(event)
            code_key = "codelist_" + key
            if key not in source or code_key not in source:
                raise ValueError("missing event or codelist {}".format(event))
            values = np.asarray(source[key][:], dtype=np.float64)
            symbols = [_decode(value) for value in source[code_key][:]]
            if values.ndim != 2 or values.shape[1] != len(factor_names):
                raise ValueError("event {} shape mismatch".format(event))
            if values.shape[0] != len(symbols):
                raise ValueError("event {} value/symbol row count mismatch".format(event))
            if stock_count is None:
                stock_count = len(symbols)
            if len(symbols) != stock_count:
                raise ValueError("inconsistent codelist stock counts")
            if len(set(symbols)) != len(symbols):
                raise ValueError("duplicate symbols in event {}".format(event))
            if not np.isfinite(values).all():
                raise ValueError("event {} contains non-finite values".format(event))
    return {"path": str(path), "stock_count": stock_count or 0, "factor_count": len(factor_names), "factor_names": factor_names, "events": list(expected_events)}


def validate_arrow(path: Path, *, expected_stock_count: int, expected_events: Sequence[int], expected_factor_names: Sequence[str]) -> dict:
    import numpy as np
    import pyarrow.ipc as ipc

    with ipc.open_file(str(path)) as reader:
        table = reader.read_all()
    names = list(table.column_names)
    factors = [name for name in names if name not in {"symbol", "date", "event"}]
    if factors != list(expected_factor_names):
        raise ValueError("factor names mismatch")
    rows = table.num_rows
    expected_rows = expected_stock_count * len(expected_events)
    if rows != expected_rows:
        raise ValueError("row count mismatch")
    events = sorted(set(table["event"].to_pylist()))
    if events != sorted(expected_events):
        raise ValueError("event set mismatch")
    symbols = table["symbol"].to_pylist()
    keys = list(zip(symbols, table["event"].to_pylist()))
    if len(set(keys)) != rows:
        raise ValueError("duplicate (symbol,event) rows")
    for name in factors:
        if not np.isfinite(np.asarray(table[name].to_numpy(zero_copy_only=False), dtype=float)).all():
            raise ValueError("non-finite factor values")
    return {"path": str(path), "rows": rows, "events": events, "factor_count": len(factors), "factor_names": factors}


def postprocess_pilot(family: str, dates: Sequence[str], hdf5_root: Path, arrow_root: Path, *, expected_events: Sequence[int]) -> list[dict]:
    results = []
    for date in dates:
        input_path = hdf5_root / str(date) / family / "factors.h5"
        inspection = inspect_hdf5(input_path, expected_events=expected_events)
        output_path = arrow_root / (str(date) + ".arrow")
        converted = convert_hdf5(input_path, output_path, expected_rows=inspection["stock_count"], expected_events=expected_events, expected_factor_count=inspection["factor_count"])
        checked = validate_arrow(output_path, expected_stock_count=inspection["stock_count"], expected_events=converted["events"], expected_factor_names=inspection["factor_names"])
        results.append({**inspection, **converted, **checked})
    return results


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", required=True)
    parser.add_argument("--dates", required=True, help="comma-separated YYYYMMDD values")
    parser.add_argument("--hdf5-root", type=Path, required=True)
    parser.add_argument("--arrow-root", type=Path, required=True)
    parser.add_argument("--events", default="92700000,100000000,103000000,110000000,113000000,133000000,140000000,143000000")
    args = parser.parse_args(argv)
    results = postprocess_pilot(
        args.family,
        [value for value in args.dates.split(",") if value],
        args.hdf5_root,
        args.arrow_root,
        expected_events=[int(value) for value in args.events.split(",") if value],
    )
    print(json.dumps(results, ensure_ascii=False))
    return 0


__all__ = ["inspect_hdf5", "validate_arrow", "postprocess_pilot"]


if __name__ == "__main__":
    raise SystemExit(main())
