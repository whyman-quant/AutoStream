"""Convert and strictly validate a production factor HDF5 into Arrow IPC."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Iterable, Optional, Sequence


EVALUATION_EVENT_BY_SOURCE_EVENT = {92700000: 92600000}
READINESS_REASON_READY = 0
READINESS_REASON_UNSPECIFIED = 255


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
    require_explicit_reason: bool = False,
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
        readiness_values = []
        reason_values = []
        date = _infer_date(input_path)
        for event in events:
            matrix = np.asarray(source[str(event)][:], dtype=np.float64)
            event_symbols = [_decode(value) for value in source["codelist_" + str(event)][:]]
            if matrix.shape != (expected_rows, expected_factor_count):
                raise ValueError("event {} shape {} does not match {}x{}".format(event, matrix.shape, expected_rows, expected_factor_count))
            if len(event_symbols) != expected_rows or len(set(event_symbols)) != expected_rows:
                raise ValueError("event {} codelist mismatch".format(event))
            readiness_key = "readiness_" + str(event)
            if readiness_key in source:
                readiness = np.asarray(source[readiness_key][:], dtype=np.uint8)
            elif "readiness" in source:
                readiness = np.asarray(source["readiness"][:], dtype=np.uint8)
            else:
                readiness = np.ones(matrix.shape, dtype=np.uint8)
            if readiness.shape != matrix.shape:
                raise ValueError("event {} readiness shape {} does not match {}".format(event, readiness.shape, matrix.shape))
            if np.any((readiness != 0) & (readiness != 1)):
                raise ValueError("event {} readiness values must be 0/1".format(event))
            # Non-finite values are permitted only where the producer marked
            # the factor unavailable.  Ready values must remain finite.
            if np.any((readiness != 0) & ~np.isfinite(matrix)):
                raise ValueError("event {} contains non-finite ready values".format(event))
            reason_key = "readiness_reason_" + str(event)
            if reason_key in source or "readiness_reason" in source:
                raw_reasons = np.asarray(source[reason_key if reason_key in source else "readiness_reason"][:])
                if raw_reasons.dtype.kind not in "ui" or np.any(raw_reasons < 0) or np.any(raw_reasons > 255):
                    raise ValueError("event {} readiness reason values must be uint8 integers".format(event))
                reasons = raw_reasons.astype(np.uint8)
                if reasons.shape != matrix.shape:
                    raise ValueError("event {} readiness reason shape {} does not match {}".format(event, reasons.shape, matrix.shape))
                if np.any((readiness != 0) & (reasons != READINESS_REASON_READY)):
                    raise ValueError("event {} readiness reason must be zero for ready values".format(event))
                if np.any((readiness == 0) & (reasons == READINESS_REASON_READY)):
                    raise ValueError("event {} readiness reason must be nonzero for not-ready values".format(event))
            else:
                if require_explicit_reason:
                    raise ValueError("event {} requires explicit readiness reason".format(event))
                # Preserve compatibility with historical readiness-only HDF5
                # while making the missing explanation explicit in Arrow.
                reasons = np.where(
                    readiness != 0, READINESS_REASON_READY,
                    READINESS_REASON_UNSPECIFIED,
                ).astype(np.uint8)
            symbols.extend(event_symbols)
            dates.extend([date] * expected_rows)
            evaluation_event = EVALUATION_EVENT_BY_SOURCE_EVENT.get(event, event)
            event_values.extend([evaluation_event] * expected_rows)
            factor_values.append(matrix)
            readiness_values.append(readiness)
            reason_values.append(reasons)
        if len(set(zip(symbols, event_values))) != expected_rows * len(events):
            raise ValueError("duplicate (symbol,event) rows")
        matrix = np.concatenate(factor_values, axis=0)
        readiness_matrix = np.concatenate(readiness_values, axis=0)
        reason_matrix = np.concatenate(reason_values, axis=0)
        table = pa.table({
            "symbol": pa.array(symbols, type=pa.string()),
            "date": pa.array(dates, type=pa.string()),
            "event": pa.array(event_values, type=pa.int64()),
            **{name: pa.array(matrix[:, index], type=pa.float64()) for index, name in enumerate(names)},
            **{"ready_" + name: pa.array(readiness_matrix[:, index].astype(bool), type=pa.bool_()) for index, name in enumerate(names)},
            **{"reason_" + name: pa.array(reason_matrix[:, index], type=pa.uint8()) for index, name in enumerate(names)},
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
    parser.add_argument("--factor-count", type=int)
    parser.add_argument("--factor-manifest", type=Path,
                        help="release factor manifest; derives the expected count")
    args = parser.parse_args(argv)
    events = [int(value) for value in args.events.split(",") if value]
    count = args.factor_count
    if args.factor_manifest is not None:
        from campaigns.release_factor_manifest import load_factor_manifest
        manifest_count = load_factor_manifest(args.factor_manifest)["factor_count"]
        if count is not None and count != manifest_count:
            raise ValueError("--factor-count disagrees with release factor manifest")
        count = manifest_count
    if count is None:
        raise ValueError("provide --factor-manifest or --factor-count")
    print(json.dumps(convert_hdf5(args.input, args.output, expected_rows=args.rows, expected_events=events, expected_factor_count=count), ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
