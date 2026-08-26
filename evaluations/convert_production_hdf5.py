"""Convert and strictly validate a production factor HDF5 into Arrow IPC."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Iterable, Optional, Sequence


EVALUATION_EVENT_BY_SOURCE_EVENT = {92700000: 92600000}


def _decode(value: object) -> str:
    return value.decode().rstrip("\x00") if isinstance(value, bytes) else str(value)


def _infer_date(input_path: Path) -> str:
    for parent in input_path.parents:
        if re.fullmatch(r"[0-9]{8}", parent.name):
            return parent.name
    return ""


def convert_hdf5(
    input_path: Path,
    output_path: Path,
    *,
    expected_rows: int,
    expected_events: Sequence[int],
    expected_factor_count: int,
) -> dict:
    try:
        import h5py
        import numpy as np
        import pyarrow as pa
        import pyarrow.ipc as ipc
    except ImportError as error:
        raise RuntimeError("python3.8 with h5py, numpy and pyarrow is required") from error

    with h5py.File(str(input_path), "r") as source:
        names = [_decode(value) for value in source["factorlist"][:]]
        if len(names) != expected_factor_count or len(set(names)) != len(names):
            raise ValueError("factorlist count or uniqueness mismatch")
        events = [int(value) for value in expected_events]
        missing = [event for event in events if str(event) not in source]
        if missing:
            raise ValueError("missing events: {}".format(missing))
        symbols = []
        dates = []
        event_values = []
        factor_values = []
        date = _infer_date(input_path)
        for event in events:
            matrix = np.asarray(source[str(event)][:], dtype=np.float64)
            event_symbols = [_decode(value) for value in source["codelist_" + str(event)][:]]
            if matrix.shape != (expected_rows, expected_factor_count):
                raise ValueError("event {} shape {} does not match {}x{}".format(event, matrix.shape, expected_rows, expected_factor_count))
            if len(event_symbols) != expected_rows or len(set(event_symbols)) != expected_rows:
                raise ValueError("event {} codelist mismatch".format(event))
            if not np.isfinite(matrix).all():
                raise ValueError("event {} contains non-finite factor values".format(event))
            symbols.extend(event_symbols)
            dates.extend([date] * expected_rows)
            evaluation_event = EVALUATION_EVENT_BY_SOURCE_EVENT.get(event, event)
            event_values.extend([evaluation_event] * expected_rows)
            factor_values.append(matrix)
        if len(set(zip(symbols, event_values))) != expected_rows * len(events):
            raise ValueError("duplicate (symbol,event) rows")
        matrix = np.concatenate(factor_values, axis=0)
        table = pa.table({
            "symbol": pa.array(symbols, type=pa.string()),
            "date": pa.array(dates, type=pa.string()),
            "event": pa.array(event_values, type=pa.int64()),
            **{name: pa.array(matrix[:, index], type=pa.float64()) for index, name in enumerate(names)},
        })
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name("." + output_path.name + ".tmp")
    try:
        with pa.OSFile(str(temporary), "wb") as sink:
            with ipc.RecordBatchFileWriter(sink, table.schema) as writer:
                writer.write_table(table)
        temporary.replace(output_path)
    finally:
        if temporary.exists():
            temporary.unlink()
    evaluation_events = [EVALUATION_EVENT_BY_SOURCE_EVENT.get(event, event) for event in events]
    return {"path": str(output_path), "rows": table.num_rows, "columns": table.num_columns, "source_events": events, "events": evaluation_events, "factor_count": len(names)}


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rows", type=int, default=4968)
    parser.add_argument("--events", default="92700000,100000000,103000000,110000000,113000000,133000000,140000000,143000000")
    parser.add_argument("--factor-count", type=int, default=12)
    args = parser.parse_args(argv)
    events = [int(value) for value in args.events.split(",") if value]
    print(json.dumps(convert_hdf5(args.input, args.output, expected_rows=args.rows, expected_events=events, expected_factor_count=args.factor_count), ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
