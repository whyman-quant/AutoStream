"""Build a raw926/ease926 × universe scorecard from evaluator outputs."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Dict, Optional, Sequence, Tuple

from .real_label_scorecard import (
    DEFAULT_LABELS,
    DEFAULT_UNIVERSES,
    build_scorecard,
    load_result_parquet,
    sha256_file,
    validate_result_frame,
)


def _load_result(path: Path, input_format: str):
    if input_format == "json":
        value = json.loads(path.read_text(encoding="utf-8"), parse_constant=lambda value: (_ for _ in ()).throw(ValueError("non-standard JSON number: {}".format(value))))
        if not isinstance(value, dict):
            raise ValueError(f"result JSON must be an object: {path}")
        return value
    return load_result_parquet(str(path))


def run_scorecard(
    result_root: str,
    *,
    output: Optional[str] = None,
    labels: Sequence[str] = DEFAULT_LABELS,
    universes: Sequence[str] = DEFAULT_UNIVERSES,
    input_format: str = "parquet",
    expected_date: Optional[str] = None,
    expected_factor_count: Optional[int] = None,
    factor_input: Optional[str] = None,
    label_input: Optional[str] = None,
    label_contract: Optional[str] = None,
    evaluator_source: Optional[str] = None,
) -> Dict[str, object]:
    root = Path(result_root)
    matrix = {}
    inputs = []
    common_factor_names = None
    common_dates = None
    suffix = "res_full.json" if input_format == "json" else "res_full.parquet"
    for label in labels:
        for universe in universes:
            path = root / label / universe / suffix
            if not path.is_file():
                raise ValueError(f"missing evaluation result for {label}/{universe}: {path}")
            matrix[(label, universe)] = _load_result(path, input_format)
            if input_format == "parquet":
                import pandas as pd
                frame = pd.read_parquet(path)
                validate_result_frame(frame, expected_date=expected_date)
                dates = sorted({str(value) for value in frame.index.get_level_values("date")})
                factor_names = sorted({str(str(column).rsplit("|", 1)[0]) for column in frame.columns if "|" in str(column)})
                if common_factor_names is None:
                    common_factor_names = factor_names
                elif factor_names != common_factor_names:
                    raise ValueError("factor set mismatch across label/universe result files")
                if common_dates is None:
                    common_dates = dates
                elif dates != common_dates:
                    raise ValueError("date mismatch across label/universe result files")
                inputs.append({
                    "label": label,
                    "universe": universe,
                    "path": str(path),
                    "sha256": sha256_file(str(path)),
                    "row_count": int(len(frame)),
                    "date": dates[0],
                    "event_keys": [int(value) for value in frame.index.get_level_values("event")],
                    "factor_names": factor_names,
                })
    report = build_scorecard(matrix, labels=labels, universes=universes, expected_factor_count=expected_factor_count)
    report["result_root"] = str(root)
    report["input_format"] = input_format
    report["trading_dates"] = common_dates or []
    report["inputs"] = inputs
    artifacts = {}
    for name, path in (("factor_input", factor_input), ("label_input", label_input), ("label_contract", label_contract)):
        if path:
            artifact = Path(path)
            if not artifact.is_file():
                raise ValueError("artifact does not exist: {}".format(path))
            artifacts[name] = {"path": str(artifact), "sha256": sha256_file(str(artifact))}
    report["artifacts"] = artifacts
    if evaluator_source:
        source = Path(evaluator_source)
        if not source.is_file():
            raise ValueError("evaluator source does not exist: {}".format(evaluator_source))
        report["evaluator"] = {"path": str(source), "sha256": sha256_file(str(source))}
    if output:
        destination = Path(output)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--result-root", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--input-format", choices=("parquet", "json"), default="parquet")
    parser.add_argument("--date", dest="expected_date")
    parser.add_argument("--expected-factor-count", type=int, default=12)
    parser.add_argument("--factor-input")
    parser.add_argument("--label-input")
    parser.add_argument("--label-contract")
    parser.add_argument("--evaluator-source")
    parser.add_argument("--labels", default=",".join(DEFAULT_LABELS))
    parser.add_argument("--universes", default=",".join(DEFAULT_UNIVERSES))
    args = parser.parse_args()
    labels = tuple(item for item in args.labels.split(",") if item)
    universes = tuple(item for item in args.universes.split(",") if item)
    report = run_scorecard(
        args.result_root,
        output=args.output,
        labels=labels,
        universes=universes,
        input_format=args.input_format,
        expected_date=args.expected_date,
        expected_factor_count=args.expected_factor_count if args.input_format == "parquet" else None,
        factor_input=args.factor_input,
        label_input=args.label_input,
        label_contract=args.label_contract,
        evaluator_source=args.evaluator_source,
    )
    print(f"{report['evaluation_scope']} {report['combination_count']} combinations promotion_allowed={report['promotion_allowed']}")


if __name__ == "__main__":
    main()
