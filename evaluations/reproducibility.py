"""Compare current production HDF5 against historical checkpoint files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Optional, Sequence, Tuple


DEFAULT_EVENT_FILES = [
    (92700000, "092700"),
    (100000000, "100000"),
    (103000000, "103000"),
    (110000000, "110000"),
    (113000000, "113000"),
    (133000000, "133000"),
    (140000000, "140000"),
    (143000000, "143000"),
]


def _decode(values):
    return [value.decode().rstrip("\x00") if isinstance(value, bytes) else str(value) for value in values]


def compare_reproduction(current_path: Path, historical_dir: Path, event_files: Sequence[Tuple[int, str]] = DEFAULT_EVENT_FILES, *, rtol: float = 1e-12, atol: float = 1e-12) -> dict:
    import h5py
    import numpy as np

    events = []
    with h5py.File(str(current_path), "r") as current:
        current_names = _decode(current["factorlist"][:])
        for event, stem in event_files:
            historical_path = historical_dir / (stem + ".h5")
            with h5py.File(str(historical_path), "r") as historical:
                historical_names = _decode(historical["factorlist"][:])
                current_codes = _decode(current["codelist_" + str(event)][:])
                historical_codes = _decode(historical["codelist"][:])
                current_values = np.asarray(current[str(event)][:], dtype=np.float64)
                historical_values = np.asarray(historical["factordata"][:], dtype=np.float64)
                comparable = current_values.shape == historical_values.shape
                difference = np.abs(current_values - historical_values) if comparable else np.empty((0, 0))
                factor_errors = []
                if comparable:
                    for index, name in enumerate(current_names):
                        column = difference[:, index]
                        factor_errors.append({
                            "factor": name,
                            "max_abs_error": float(column.max(initial=0.0)),
                            "mean_abs_error": float(column.mean()) if column.size else 0.0,
                            "exact_equal_fraction": float(np.mean(current_values[:, index] == historical_values[:, index])),
                        })
                row = {
                    "source_event": event,
                    "historical_file": str(historical_path),
                    "factor_names_equal": current_names == historical_names,
                    "current_shape": list(current_values.shape),
                    "historical_shape": list(historical_values.shape),
                    "codes_equal": current_codes == historical_codes,
                    "code_set_equal": set(current_codes) == set(historical_codes),
                    "max_abs_error": float(difference.max(initial=0.0)) if comparable else None,
                    "allclose": bool(np.allclose(current_values, historical_values, rtol=rtol, atol=atol)) if comparable else False,
                    "factor_errors": factor_errors,
                }
                row["passed"] = row["factor_names_equal"] and row["codes_equal"] and row["allclose"]
                events.append(row)
    return {
        "schema_version": 1,
        "current": str(current_path),
        "historical_directory": str(historical_dir),
        "rtol": rtol,
        "atol": atol,
        "event_count": len(events),
        "factor_count": len(current_names),
        "passed": all(event["passed"] for event in events),
        "events": events,
    }


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", type=Path, required=True)
    parser.add_argument("--historical-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--rtol", type=float, default=1e-12)
    parser.add_argument("--atol", type=float, default=1e-12)
    args = parser.parse_args(argv)
    report = compare_reproduction(args.current, args.historical_dir, rtol=args.rtol, atol=args.atol)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({"passed": report["passed"], "events": report["event_count"], "factors": report["factor_count"]}))
    return 0 if report["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
