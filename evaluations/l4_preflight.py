"""Strict factor-only preflight for L4 formal-history production dates."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Optional, Sequence

from .convert_production_hdf5 import (
    EVALUATION_EVENT_BY_SOURCE_EVENT,
    convert_hdf5,
)
from .pilot_postprocess import inspect_hdf5, validate_arrow


FAMILIES = (
    "book_imbalance",
    "flow_pressure",
    "liquidity_resilience",
    "impact_efficiency",
)
DEFAULT_SOURCE_EVENTS = (
    92700000,
    100000000,
    103000000,
    110000000,
    113000000,
    133000000,
    140000000,
    143000000,
)
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CAMPAIGN_ROOT = REPOSITORY_ROOT / "campaigns" / "sfm_stream_001"


def _batch_paths(campaign_root_or_batch_paths):
    if isinstance(campaign_root_or_batch_paths, (str, bytes, Path)):
        campaign_root = Path(campaign_root_or_batch_paths)
        return [
            campaign_root / "batches" / (family + "_seed_v1.json")
            for family in FAMILIES
        ]
    paths = [Path(path) for path in campaign_root_or_batch_paths]
    if len(paths) != len(FAMILIES):
        raise ValueError("exactly four Batch paths are required")
    by_name = {path.name: path for path in paths}
    expected_names = [family + "_seed_v1.json" for family in FAMILIES]
    if set(by_name) != set(expected_names):
        raise ValueError("Batch paths must name the four frozen family Batches")
    return [by_name[name] for name in expected_names]


def load_expected_factor_names(campaign_root=None, batch_paths=None):
    """Load the frozen 48 factor names in all-families HDF5 column order."""
    if (campaign_root is None) == (batch_paths is None):
        raise ValueError("provide either campaign_root or batch_paths")
    source = campaign_root if campaign_root is not None else batch_paths
    names = []
    for path in _batch_paths(source):
        batch = json.loads(path.read_text(encoding="utf-8"))
        candidate_ids = batch.get("candidate_ids")
        if not isinstance(candidate_ids, list):
            raise ValueError("Batch candidate_ids must be a list: {}".format(path))
        names.extend(str(name) for name in candidate_ids)
    if len(names) != 48 or len(set(names)) != 48:
        raise ValueError("expected exactly 48 unique factor names")
    return names


def validate_requested_dates(requested, production_date_list_path):
    """Return requested dates after enforcing the frozen v2 production list."""
    dates = [str(date) for date in requested]
    if not dates:
        raise ValueError("at least one production date is required")
    if len(set(dates)) != len(dates):
        raise ValueError("requested dates must not contain duplicates")
    production_dates = {
        line.strip()
        for line in Path(production_date_list_path).read_text(
            encoding="utf-8"
        ).splitlines()
        if line.strip()
    }
    for date in dates:
        if len(date) != 8 or not date.isdigit():
            raise ValueError("invalid date: {}".format(date))
        if date >= "20250101":
            raise ValueError("2025 and holdout dates are forbidden: {}".format(date))
        if date not in production_dates:
            raise ValueError("date is not in the v2 production list: {}".format(date))
    return dates


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return "sha256:" + digest.hexdigest()


def _validate_exact_hdf5_event_keys(path, expected_events):
    import h5py

    expected = {str(event) for event in expected_events}
    with h5py.File(str(path), "r") as source:
        event_keys = {key for key in source.keys() if key.isdigit()}
        codelist_keys = {
            key[len("codelist_") :]
            for key in source.keys()
            if key.startswith("codelist_")
            and key[len("codelist_") :].isdigit()
        }
        non_dataset_events = sorted(
            key
            for key in event_keys
            if not isinstance(source[key], h5py.Dataset)
        )
        non_dataset_codelists = sorted(
            "codelist_" + key
            for key in codelist_keys
            if not isinstance(source["codelist_" + key], h5py.Dataset)
        )
    if non_dataset_events:
        raise ValueError(
            "event keys must be datasets: {}".format(non_dataset_events)
        )
    if non_dataset_codelists:
        raise ValueError(
            "codelist keys must be datasets: {}".format(non_dataset_codelists)
        )
    missing_events = sorted(expected - event_keys)
    unexpected_events = sorted(event_keys - expected)
    missing_codelists = sorted(expected - codelist_keys)
    unexpected_codelists = sorted(codelist_keys - expected)
    if missing_events:
        raise ValueError("missing event datasets: {}".format(missing_events))
    if unexpected_events:
        raise ValueError(
            "unexpected event datasets: {}".format(unexpected_events)
        )
    if missing_codelists:
        raise ValueError(
            "missing codelist datasets: {}".format(missing_codelists)
        )
    if unexpected_codelists:
        raise ValueError(
            "unexpected codelist datasets: {}".format(unexpected_codelists)
        )


def compose_date_summary(
    date,
    hdf5_path,
    arrow_path,
    inspection,
    arrow_validation,
    *,
    expected_events,
):
    """Compose a date receipt from completed strict validation evidence."""
    event_count = len(expected_events)
    expected_rows = inspection["stock_count"] * event_count
    if arrow_validation["rows"] != expected_rows:
        raise ValueError("row count mismatch")
    if sorted(arrow_validation["events"]) != sorted(expected_events):
        raise ValueError("event set mismatch")
    if arrow_validation["factor_count"] != inspection["factor_count"]:
        raise ValueError("factor count mismatch")
    if arrow_validation["factor_names"] != inspection["factor_names"]:
        raise ValueError("factor names mismatch")
    if not arrow_validation["all_values_finite"]:
        raise ValueError("non-finite factor values")
    if not arrow_validation["unique_symbol_event"]:
        raise ValueError("duplicate (symbol,event) rows")
    return {
        "date": str(date),
        "stock_count": inspection["stock_count"],
        "rows": arrow_validation["rows"],
        "event_count": event_count,
        "factor_count": inspection["factor_count"],
        "factor_names": list(inspection["factor_names"]),
        "all_values_finite": True,
        "unique_symbol_event": True,
        "hdf5_sha256": _sha256(hdf5_path),
        "arrow_sha256": _sha256(arrow_path),
    }


def preflight_dates(
    dates,
    hdf5_root,
    arrow_root,
    campaign_root,
    production_date_list_path,
    *,
    expected_source_events=DEFAULT_SOURCE_EVENTS,
    expected_factor_names=None,
):
    """Validate and convert factor HDF5 files without reading labels or results."""
    requested_dates = validate_requested_dates(dates, production_date_list_path)
    factor_names = (
        load_expected_factor_names(campaign_root=campaign_root)
        if expected_factor_names is None
        else [str(name) for name in expected_factor_names]
    )
    if not factor_names or len(set(factor_names)) != len(factor_names):
        raise ValueError("expected factor names must be non-empty and unique")
    source_events = [int(event) for event in expected_source_events]
    if not source_events or len(set(source_events)) != len(source_events):
        raise ValueError("expected source events must be non-empty and unique")
    evaluation_events = [
        EVALUATION_EVENT_BY_SOURCE_EVENT.get(event, event)
        for event in source_events
    ]
    if len(set(evaluation_events)) != len(evaluation_events):
        raise ValueError("mapped evaluation events must be unique")

    summaries = []
    for date in requested_dates:
        hdf5_path = (
            Path(hdf5_root) / date / "all_families" / "factors.h5"
        )
        _validate_exact_hdf5_event_keys(hdf5_path, source_events)
        inspection = inspect_hdf5(
            hdf5_path, expected_events=source_events
        )
        if inspection["factor_names"] != factor_names:
            raise ValueError("factor names mismatch for {}".format(date))
        arrow_path = Path(arrow_root) / (date + ".arrow")
        converted = convert_hdf5(
            hdf5_path,
            arrow_path,
            expected_rows=inspection["stock_count"],
            expected_events=source_events,
            expected_factor_count=len(factor_names),
        )
        checked = validate_arrow(
            arrow_path,
            expected_stock_count=inspection["stock_count"],
            expected_events=evaluation_events,
            expected_factor_names=factor_names,
        )
        checked["all_values_finite"] = True
        checked["unique_symbol_event"] = True
        if converted["rows"] != checked["rows"]:
            raise ValueError("converted and validated Arrow row counts differ")
        summaries.append(
            compose_date_summary(
                date,
                hdf5_path,
                arrow_path,
                inspection,
                checked,
                expected_events=evaluation_events,
            )
        )
    return {
        "dates": summaries,
        "aggregate": {
            "date_count": len(summaries),
            "total_rows": sum(summary["rows"] for summary in summaries),
            "factor_count": len(factor_names),
            "event_count": len(evaluation_events),
            "decision": "continue_to_bulk_l4",
        },
    }


def _write_json_atomic(path, payload):
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = output_path.with_name("." + output_path.name + ".tmp")
    try:
        temporary.write_text(
            json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        temporary.replace(output_path)
    finally:
        if temporary.exists():
            temporary.unlink()


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dates", required=True, help="comma-separated dates")
    parser.add_argument("--hdf5-root", type=Path, required=True)
    parser.add_argument("--arrow-root", type=Path, required=True)
    parser.add_argument(
        "--campaign-root", type=Path, default=DEFAULT_CAMPAIGN_ROOT
    )
    parser.add_argument("--production-date-list", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    dates = args.dates.split(",")
    result = preflight_dates(
        dates,
        args.hdf5_root,
        args.arrow_root,
        args.campaign_root,
        args.production_date_list,
    )
    if args.output is not None:
        _write_json_atomic(args.output, result)
    print(json.dumps(result, ensure_ascii=False))
    return 0


__all__ = [
    "DEFAULT_SOURCE_EVENTS",
    "compose_date_summary",
    "load_expected_factor_names",
    "preflight_dates",
    "validate_requested_dates",
]


if __name__ == "__main__":
    raise SystemExit(main())
