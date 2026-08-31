"""Holdout-safe deterministic planner for L4 formal-history production."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path
from typing import Optional, Sequence


DATASET_ID = "sfm_stream_001_formal_history_v2"
FAMILIES = (
    "book_imbalance",
    "flow_pressure",
    "liquidity_resilience",
    "impact_efficiency",
)
CHUNK_SIZE = 5
LANE_COUNT = 4
DATE_COUNT = 969
HOLDOUT_COUNT = 228
PYTHON38 = "/usr/local/python3.8.10/bin/python3"
SLURM_RESOURCES = ("-c12", "-m243G", "-p", "cpu_wgh", "-t2:00:00")
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def _resolve_recorded_path(recorded_path, campaign_root):
    recorded = Path(recorded_path)
    if recorded.is_absolute():
        return recorded.resolve()
    candidates = (
        REPOSITORY_ROOT / recorded,
        Path(campaign_root) / recorded,
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    raise ValueError("recorded campaign path does not exist: {}".format(recorded_path))


def _read_frozen_lines(path, expected_sha256, description):
    before = _sha256(path)
    raw = Path(path).read_bytes()
    after = _sha256(path)
    if before != after:
        raise ValueError("{} changed while being read".format(description))
    if before != expected_sha256:
        raise ValueError("{} hash does not match active v2 manifest".format(description))
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("{} is not UTF-8".format(description)) from error
    if not text.endswith("\n"):
        raise ValueError("{} must end with a newline".format(description))
    lines = text.splitlines()
    if not lines or any(not value for value in lines):
        raise ValueError("{} must contain non-empty lines".format(description))
    return lines


def _validate_dates(dates, description):
    if len(set(dates)) != len(dates):
        raise ValueError("{} must not contain duplicate dates".format(description))
    for date in dates:
        if len(date) != 8 or not date.isdigit():
            raise ValueError("invalid {} date: {}".format(description, date))


def _load_active_v2_manifest(campaign_root):
    root = Path(campaign_root).resolve()
    campaign_path = root / "campaign.json"
    try:
        campaign = json.loads(campaign_path.read_text(encoding="utf-8"))
        active_id = campaign["formal_history_dataset_id"]
        manifest_path = _resolve_recorded_path(
            campaign["formal_history_dataset_manifest"], root
        )
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, KeyError, json.JSONDecodeError) as error:
        raise ValueError("invalid active campaign contract") from error
    if active_id != DATASET_ID or manifest.get("dataset_id") != active_id:
        raise ValueError("active formal-history dataset must be v2")
    if manifest.get("schema_version") != 2:
        raise ValueError("active formal-history manifest must use schema v2")
    production_recorded = manifest.get("production_date_list_path")
    holdout_recorded = manifest.get("holdout_date_list_path")
    if not isinstance(production_recorded, str) or not isinstance(holdout_recorded, str):
        raise ValueError("v2 date-list paths must be recorded")
    if manifest.get("date_list_path") != production_recorded:
        raise ValueError("date_list_path must equal production_date_list_path")
    if manifest.get("date_list_sha256") != manifest.get("production_date_list_sha256"):
        raise ValueError("date_list_sha256 must equal production date-list hash")
    if manifest.get("leakage_policy", {}).get("holdout_may_read") != []:
        raise ValueError("holdout_may_read must remain empty")
    return root, manifest_path, manifest


def _load_active_v2_production_contract(campaign_root):
    root, manifest_path, manifest = _load_active_v2_manifest(campaign_root)
    production_recorded = manifest["production_date_list_path"]
    production_path = _resolve_recorded_path(production_recorded, root)
    production_dates = _read_frozen_lines(
        production_path,
        manifest.get("production_date_list_sha256"),
        "production date list",
    )
    _validate_dates(production_dates, "production")
    if len(production_dates) != DATE_COUNT or manifest.get("date_count") != DATE_COUNT:
        raise ValueError("active v2 production list must contain 969 dates")
    if any(date >= "20250101" for date in production_dates):
        raise ValueError("2025 dates are forbidden in production")
    return {
        "campaign_root": str(root),
        "manifest_path": str(manifest_path),
        "dataset_id": manifest["dataset_id"],
        "production_date_list_path": str(production_path),
        "production_date_list_sha256": manifest["production_date_list_sha256"],
        "production_dates": production_dates,
    }


def load_active_v2_contract(campaign_root):
    """Load and validate the active sealed v2 production/holdout contract."""
    contract = _load_active_v2_production_contract(campaign_root)
    root = Path(contract["campaign_root"])
    manifest = json.loads(
        Path(contract["manifest_path"]).read_text(encoding="utf-8")
    )
    holdout_path = _resolve_recorded_path(
        manifest["holdout_date_list_path"], root
    )
    holdout_dates = _read_frozen_lines(
        holdout_path,
        manifest.get("holdout_date_list_sha256"),
        "holdout date list",
    )
    _validate_dates(holdout_dates, "holdout")
    holdout_manifest_count = (
        manifest.get("splits", {}).get("holdout", {}).get("date_count")
    )
    if len(holdout_dates) != HOLDOUT_COUNT or holdout_manifest_count != HOLDOUT_COUNT:
        raise ValueError("active v2 holdout list must contain 228 dates")
    if any(date < "20250101" for date in holdout_dates):
        raise ValueError("holdout list must contain only sealed 2025 dates")
    if set(contract["production_dates"]) & set(holdout_dates):
        raise ValueError("production and holdout lists must not overlap")
    return {
        **contract,
        "holdout_date_list_path": str(holdout_path),
        "holdout_date_list_sha256": manifest["holdout_date_list_sha256"],
        "holdout_dates": holdout_dates,
    }


def chunk_dates(dates, size=5):
    """Split a non-empty ordered date sequence into fixed-size chunks."""
    values = list(dates)
    if not values:
        raise ValueError("dates must not be empty")
    if size <= 0:
        raise ValueError("chunk size must be positive")
    if len(set(values)) != len(values):
        raise ValueError("dates must not contain duplicates")
    return [values[index : index + size] for index in range(0, len(values), size)]


def assign_lanes(chunks, lane_count=4):
    """Assign ordered chunks to deterministic round-robin dependency lanes."""
    values = list(chunks)
    if not values:
        raise ValueError("chunks must not be empty")
    if lane_count <= 0:
        raise ValueError("lane_count must be positive")
    if lane_count > LANE_COUNT:
        raise ValueError("lane_count must not exceed four")
    lanes = [[] for _ in range(min(lane_count, len(values)))]
    for index, chunk in enumerate(values):
        lanes[index % len(lanes)].append(chunk)
    return lanes


def _require_absolute_file(path, description):
    candidate = Path(path)
    if not candidate.is_absolute():
        raise ValueError("{} path must be absolute".format(description))
    if not candidate.is_file():
        raise ValueError("{} path must be an existing file".format(description))
    return candidate.resolve()


def _require_absolute_directory_path(path, description):
    candidate = Path(path)
    if not candidate.is_absolute():
        raise ValueError("{} path must be absolute".format(description))
    return candidate.resolve()


def _load_and_validate_config(config, output_root):
    try:
        payload = json.loads(config.read_text(encoding="utf-8"))
        configured = payload["factors_config"]["save_info"]["dir"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError("invalid frozen factor config") from error
    if not isinstance(configured, str) or configured.count("[DATE]") != 1:
        raise ValueError("config output directory must contain one [DATE]")
    marker = "__L4_FROZEN_DATE__"
    configured_path = Path(configured.replace("[DATE]", marker))
    if not configured_path.is_absolute():
        raise ValueError("config output directory must be absolute")
    expected_path = output_root / marker / "all_families"
    if configured_path.resolve() != expected_path.resolve():
        raise ValueError("config output directory does not match output_root")


def build_plan(
    campaign_root,
    binary,
    config,
    output_root,
    *,
    chunk_size=CHUNK_SIZE,
    lane_count=LANE_COUNT,
    expected_binary_sha256=None,
    expected_config_sha256=None,
):
    """Build a deterministic JSON-serializable plan from the active v2 list."""
    if chunk_size != CHUNK_SIZE or lane_count != LANE_COUNT:
        raise ValueError("L4 production requires chunk_size=5 and lane_count=4")
    binary_path = _require_absolute_file(binary, "binary")
    config_path = _require_absolute_file(config, "config")
    output_path = _require_absolute_directory_path(output_root, "output_root")
    binary_hash_before = _sha256(binary_path)
    config_hash_before = _sha256(config_path)
    if expected_binary_sha256 is not None and binary_hash_before != expected_binary_sha256:
        raise ValueError("binary hash changed from frozen input")
    if expected_config_sha256 is not None and config_hash_before != expected_config_sha256:
        raise ValueError("config hash changed from frozen input")
    _load_and_validate_config(config_path, output_path)
    contract = load_active_v2_contract(campaign_root)
    chunks = chunk_dates(contract["production_dates"], size=chunk_size)
    lanes = assign_lanes(chunks, lane_count=lane_count)
    lane_by_identity = {}
    for lane_index, lane in enumerate(lanes):
        for chunk in lane:
            lane_by_identity[id(chunk)] = lane_index
    previous_by_lane = {}
    planned_chunks = []
    for index, dates in enumerate(chunks):
        lane = lane_by_identity[id(dates)]
        chunk_id = "l4-v2-{:04d}".format(index)
        planned_chunks.append({
            "chunk_id": chunk_id,
            "lane": lane,
            "dates": list(dates),
            "job_name": "l4-v2-{:04d}".format(index),
            "depends_on_chunk_id": previous_by_lane.get(lane),
        })
        previous_by_lane[lane] = chunk_id
    binary_hash_after = _sha256(binary_path)
    config_hash_after = _sha256(config_path)
    if binary_hash_after != binary_hash_before:
        raise ValueError("binary changed while building plan")
    if config_hash_after != config_hash_before:
        raise ValueError("config changed while building plan")
    return {
        "schema_version": 1,
        "dataset_id": contract["dataset_id"],
        "campaign_root": contract["campaign_root"],
        "date_list_path": contract["production_date_list_path"],
        "date_list_sha256": contract["production_date_list_sha256"],
        "binary": str(binary_path),
        "binary_sha256": binary_hash_before,
        "config": str(config_path),
        "config_sha256": config_hash_before,
        "output_root": str(output_path),
        "chunk_size": chunk_size,
        "lane_count": lane_count,
        "date_count": len(contract["production_dates"]),
        "chunk_count": len(planned_chunks),
        "chunks": planned_chunks,
    }


def _write_json_atomic(path, payload):
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix="." + target.name + ".", suffix=".tmp", dir=str(target.parent)
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(str(temporary), str(target))
    finally:
        if temporary.exists():
            temporary.unlink()


def submission_receipt_path(plan_output):
    path = Path(plan_output)
    return path.with_name(path.stem + "-submission.json")


def _chunk_run_command(plan, chunk):
    arguments = [
        PYTHON38,
        "-m",
        "campaigns.l4_production",
        "run-chunk",
        "--dates",
        ",".join(chunk["dates"]),
        "--campaign-root",
        plan["campaign_root"],
        "--binary",
        plan["binary"],
        "--config",
        plan["config"],
        "--output-root",
        plan["output_root"],
        "--binary-sha256",
        plan["binary_sha256"],
        "--config-sha256",
        plan["config_sha256"],
        "--date-list-sha256",
        plan["date_list_sha256"],
    ]
    return shlex.join(arguments)


def submit_plan(plan, plan_path, receipt_path=None):
    """Submit every chunk with bounded lane dependencies and atomic receipt."""
    plan_file = Path(plan_path)
    output_receipt = (
        submission_receipt_path(plan_file)
        if receipt_path is None
        else Path(receipt_path)
    )
    if output_receipt.exists():
        raise ValueError("submission receipt already exists")
    plan_hash = _sha256(plan_file)
    try:
        recorded_plan = json.loads(plan_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("plan file is not valid JSON") from error
    if recorded_plan != plan:
        raise ValueError("submitted plan payload does not match frozen plan file")
    expected_plan = build_plan(
        plan["campaign_root"],
        plan["binary"],
        plan["config"],
        plan["output_root"],
        expected_binary_sha256=plan["binary_sha256"],
        expected_config_sha256=plan["config_sha256"],
    )
    if expected_plan != plan:
        raise ValueError("plan does not match deterministic active v2 production")
    contract = load_active_v2_contract(plan["campaign_root"])
    if contract["production_date_list_sha256"] != plan["date_list_sha256"]:
        raise ValueError("production date-list hash changed before submission")
    if _sha256(plan["binary"]) != plan["binary_sha256"]:
        raise ValueError("binary hash changed before submission")
    if _sha256(plan["config"]) != plan["config_sha256"]:
        raise ValueError("config hash changed before submission")
    job_id_by_chunk = {}
    jobs = []
    for chunk in plan["chunks"]:
        dependency_chunk = chunk["depends_on_chunk_id"]
        dependency_job = (
            None if dependency_chunk is None else job_id_by_chunk[dependency_chunk]
        )
        run_command = _chunk_run_command(plan, chunk)
        command = ["mybatch"] + list(SLURM_RESOURCES) + ["-J", chunk["job_name"]]
        if dependency_job is not None:
            command.extend(["-d", "afterok:" + dependency_job])
        command.extend(["-s", run_command])
        completed = subprocess.run(command, capture_output=True, text=True)
        if completed.returncode != 0:
            raise RuntimeError(
                "mybatch failed for {}: {}".format(
                    chunk["chunk_id"], completed.stderr.strip()
                )
            )
        match = re.fullmatch(r"Jobid:(\d+)", completed.stdout.strip())
        if match is None:
            raise RuntimeError(
                "invalid mybatch output for {}: {!r}".format(
                    chunk["chunk_id"], completed.stdout
                )
            )
        job_id = match.group(1)
        job_id_by_chunk[chunk["chunk_id"]] = job_id
        jobs.append({
            "chunk_id": chunk["chunk_id"],
            "lane": chunk["lane"],
            "dates": list(chunk["dates"]),
            "job_name": chunk["job_name"],
            "depends_on_chunk_id": dependency_chunk,
            "depends_on_job_id": dependency_job,
            "job_id": job_id,
            "command": command,
            "run_command": run_command,
            "resources": {
                "cpus": 12,
                "memory": "243G",
                "partition": "cpu_wgh",
                "time": "2:00:00",
            },
        })
    receipt = {
        "schema_version": 1,
        "status": "submitted",
        "plan_path": str(plan_file.resolve()),
        "plan_sha256": plan_hash,
        "job_count": len(jobs),
        "jobs": jobs,
    }
    _write_json_atomic(output_receipt, receipt)
    return receipt


def run_chunk(
    dates,
    campaign_root,
    binary,
    config,
    output_root,
    binary_sha256,
    config_sha256,
    date_list_sha256,
):
    """Produce and HDF5-only validate one frozen five-date chunk."""
    from evaluations.l4_preflight import validate_hdf5_only, validate_requested_dates

    binary_path = _require_absolute_file(binary, "binary")
    config_path = _require_absolute_file(config, "config")
    output_path = _require_absolute_directory_path(output_root, "output_root")
    contract = _load_active_v2_production_contract(campaign_root)
    if contract["production_date_list_sha256"] != date_list_sha256:
        raise ValueError("production date-list hash changed")
    if _sha256(binary_path) != binary_sha256:
        raise ValueError("binary hash changed")
    if _sha256(config_path) != config_sha256:
        raise ValueError("config hash changed")
    _load_and_validate_config(config_path, output_path)
    requested = validate_requested_dates(
        dates, contract["production_date_list_path"]
    )
    if len(requested) > CHUNK_SIZE:
        raise ValueError("run-chunk accepts at most five frozen dates")
    results = []
    for date in requested:
        if _sha256(binary_path) != binary_sha256 or _sha256(config_path) != config_sha256:
            raise ValueError("frozen binary or config changed during chunk")
        target = output_path / date / "all_families" / "factors.h5"
        if target.exists():
            inspection = validate_hdf5_only(target, campaign_root)
            status = "existing_valid"
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            command = [
                str(binary_path),
                "date=" + date,
                "thread_num=8",
                "config_file=" + str(config_path),
                "stock=all",
            ]
            completed = subprocess.run(command, capture_output=True, text=True)
            if completed.returncode != 0:
                raise RuntimeError(
                    "factor binary failed for {} with {}: {}".format(
                        date, completed.returncode, completed.stderr.strip()
                    )
                )
            if not target.is_file():
                raise RuntimeError("factor binary did not create {}".format(target))
            inspection = validate_hdf5_only(target, campaign_root)
            status = "produced_valid"
        results.append({
            "date": date,
            "path": str(target),
            "status": status,
            "stock_count": inspection["stock_count"],
            "event_count": inspection["event_count"],
            "factor_count": inspection["factor_count"],
        })
    return results


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan_parser = subparsers.add_parser("plan")
    plan_parser.add_argument("--campaign-root", type=Path, required=True)
    plan_parser.add_argument("--binary", type=Path, required=True)
    plan_parser.add_argument("--config", type=Path, required=True)
    plan_parser.add_argument("--output-root", type=Path, required=True)
    plan_parser.add_argument("--plan-output", type=Path, required=True)
    plan_parser.add_argument("--submit", action="store_true")
    chunk_parser = subparsers.add_parser("run-chunk")
    chunk_parser.add_argument("--dates", required=True)
    chunk_parser.add_argument("--campaign-root", type=Path, required=True)
    chunk_parser.add_argument("--binary", type=Path, required=True)
    chunk_parser.add_argument("--config", type=Path, required=True)
    chunk_parser.add_argument("--output-root", type=Path, required=True)
    chunk_parser.add_argument("--binary-sha256", required=True)
    chunk_parser.add_argument("--config-sha256", required=True)
    chunk_parser.add_argument("--date-list-sha256", required=True)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = _parser().parse_args(argv)
    if args.command == "plan":
        plan = build_plan(
            args.campaign_root,
            args.binary,
            args.config,
            args.output_root,
        )
        _write_json_atomic(args.plan_output, plan)
        result = plan
        if args.submit:
            result = submit_plan(plan, args.plan_output)
        print(json.dumps(result, ensure_ascii=False))
        return 0
    results = run_chunk(
        [value for value in args.dates.split(",") if value],
        args.campaign_root,
        args.binary,
        args.config,
        args.output_root,
        args.binary_sha256,
        args.config_sha256,
        args.date_list_sha256,
    )
    print(json.dumps(results, ensure_ascii=False))
    return 0


__all__ = [
    "FAMILIES",
    "assign_lanes",
    "build_plan",
    "chunk_dates",
    "load_active_v2_contract",
    "main",
    "run_chunk",
    "submission_receipt_path",
    "submit_plan",
]


if __name__ == "__main__":
    raise SystemExit(main())
