"""Holdout-safe deterministic planner for L4 formal-history production."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import re
import secrets
import shlex
import subprocess
import tempfile
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
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
MYBATCH_INPUT_MEMORY_GB = 256
EFFECTIVE_SLURM_MEMORY_GB = 243
SLURM_RESOURCES = ("-c12", "-m256G", "-p", "cpu_wgh", "-t2:00:00")
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
RUNNER_RELATIVE_FILES = (
    "l4_runner_bootstrap.py",
    "campaigns/__init__.py",
    "campaigns/l4_production.py",
    "evaluations/__init__.py",
    "evaluations/l4_preflight.py",
    "evaluations/pilot_postprocess.py",
    "evaluations/convert_production_hdf5.py",
)


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        while True:
            block = source.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def _sha256_bytes(value):
    return "sha256:" + hashlib.sha256(value).hexdigest()


def _resolve_recorded_path(recorded_path, campaign_root):
    recorded = Path(recorded_path)
    if recorded.is_absolute():
        return recorded.resolve()
    campaign = Path(campaign_root).resolve()
    candidates = [campaign / recorded]
    if campaign.parent.name == "campaigns":
        candidates.append(campaign.parents[1] / recorded)
    candidates.append(REPOSITORY_ROOT / recorded)
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
    holdout_start = manifest.get("splits", {}).get("holdout", {}).get("date_start")
    holdout_end = manifest.get("splits", {}).get("holdout", {}).get("date_end")
    if len(holdout_dates) != HOLDOUT_COUNT or holdout_manifest_count != HOLDOUT_COUNT:
        raise ValueError("active v2 holdout list must contain 228 dates")
    if (holdout_start, holdout_end) != ("20250102", "20251210"):
        raise ValueError("active v2 holdout boundaries must remain frozen")
    if (
        holdout_dates[0] != holdout_start
        or holdout_dates[-1] != holdout_end
        or any(not holdout_start <= date <= holdout_end for date in holdout_dates)
    ):
        raise ValueError("holdout list must equal the frozen 2025 range")
    parent_recorded = manifest.get("parent_date_list_path")
    parent_sha256 = manifest.get("parent_date_list_sha256")
    if not isinstance(parent_recorded, str) or not isinstance(parent_sha256, str):
        raise ValueError("active v2 must record the frozen parent date list")
    parent_path = _resolve_recorded_path(parent_recorded, root)
    parent_dates = _read_frozen_lines(
        parent_path, parent_sha256, "parent date list"
    )
    expected_holdout = [
        date for date in parent_dates if holdout_start <= date <= holdout_end
    ]
    if holdout_dates != expected_holdout:
        raise ValueError("holdout list must equal the parent v1 frozen subset")
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


def validate_dates_against_frozen_list(requested, verified_dates):
    """Validate requested dates only against an already verified snapshot."""
    dates = [str(date) for date in requested]
    if not dates:
        raise ValueError("at least one production date is required")
    if len(set(dates)) != len(dates):
        raise ValueError("requested dates must not contain duplicates")
    frozen = set(verified_dates)
    for date in dates:
        if len(date) != 8 or not date.isdigit():
            raise ValueError("invalid date: {}".format(date))
        if date >= "20250101":
            raise ValueError("2025 and holdout dates are forbidden: {}".format(date))
        if date not in frozen:
            raise ValueError("date is not in the verified production list: {}".format(date))
    return dates


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


def _is_within(path, parent):
    try:
        Path(path).resolve().relative_to(Path(parent).resolve())
        return True
    except ValueError:
        return False


def _has_git_ancestor(path):
    candidate = Path(path).resolve()
    return any((ancestor / ".git").exists() for ancestor in (candidate, *candidate.parents))


def _require_provenance_file(path, runner_root, description):
    root = Path(runner_root).resolve()
    candidate = Path(path)
    try:
        relative = candidate.relative_to(root)
    except ValueError as error:
        raise ValueError("{} must be inside runner_root".format(description)) from error
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError("{} must not use symlink paths".format(description))
    resolved = candidate.resolve()
    if not _is_within(resolved, root) or not resolved.is_file():
        raise ValueError("{} must be a regular file inside runner_root".format(description))
    return resolved


def _validate_runner_root(runner_root, campaign_root):
    raw_root = Path(runner_root)
    if raw_root.is_symlink():
        raise ValueError("runner_root must not be a symlink")
    root = _require_absolute_directory_path(raw_root, "runner_root")
    if not root.is_dir():
        raise ValueError("runner_root must be an existing directory")
    if _has_git_ancestor(root):
        raise ValueError("runner_root must be a frozen release, not a git checkout")
    campaign = Path(campaign_root).resolve()
    if not _is_within(campaign, root):
        raise ValueError("campaign_root must be contained in runner_root release")
    campaign_json = campaign / "campaign.json"
    manifest_path = campaign / "manifests" / "formal-history-dataset-v2.json"
    required_contract_files = [campaign_json, manifest_path]
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for key in (
            "production_date_list_path",
            "parent_date_list_path",
        ):
            required_contract_files.append(
                _resolve_recorded_path(manifest[key], campaign)
            )
    except (OSError, KeyError, json.JSONDecodeError) as error:
        raise ValueError("runner release has an invalid v2 manifest") from error
    for path in required_contract_files:
        if not Path(path).is_file() or not _is_within(path, root):
            raise ValueError("runner release is missing {}".format(path))
    return root


def _validate_submission_workdir(path, runner_root):
    workdir = _require_absolute_directory_path(path, "submission_workdir")
    if not workdir.is_dir():
        raise ValueError("submission_workdir must be an existing directory")
    if not os.access(str(workdir), os.W_OK | os.X_OK):
        raise ValueError("submission_workdir must be writable and searchable")
    if _is_within(workdir, runner_root):
        raise ValueError("submission_workdir must be outside the frozen runner release")
    persistent_root = Path("/mnt").resolve()
    if workdir == persistent_root or not _is_within(workdir, persistent_root):
        raise ValueError("submission_workdir must resolve beneath /mnt")
    descriptor, probe_name = tempfile.mkstemp(
        prefix=".l4-submit-probe.", dir=str(workdir)
    )
    probe = Path(probe_name)
    try:
        os.write(descriptor, b"l4")
        os.fsync(descriptor)
    finally:
        os.close(descriptor)
        if probe.exists():
            probe.unlink()
        _fsync_directory(workdir)
    return workdir


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


def collect_runner_provenance(runner_root, campaign_root):
    """Freeze runner modules and four Batch JSON files by absolute path/hash."""
    root = _validate_runner_root(runner_root, campaign_root)
    campaign = Path(campaign_root).resolve()
    entries = []
    for relative in RUNNER_RELATIVE_FILES:
        path = _require_provenance_file(
            root / relative, root, "runner file"
        )
        entries.append({
            "kind": "runner",
            "name": relative,
            "path": str(path),
            "sha256": _sha256(path),
        })
    for family in FAMILIES:
        path = _require_provenance_file(
            campaign / "batches" / (family + "_seed_v1.json"),
            root,
            "Batch JSON",
        )
        entries.append({
            "kind": "batch",
            "name": family,
            "path": str(path),
            "sha256": _sha256(path),
        })
    return entries


def validate_runner_provenance(runner_root, campaign_root, provenance):
    """Recompute the complete runner provenance before run-chunk work."""
    expected = collect_runner_provenance(runner_root, campaign_root)
    if provenance != expected:
        raise ValueError("runner or Batch provenance hash changed")
    return expected


def _verify_frozen_inputs(binary_path, config_path, binary_sha256, config_sha256):
    if _sha256(binary_path) != binary_sha256:
        raise ValueError("binary hash changed")
    if _sha256(config_path) != config_sha256:
        raise ValueError("config hash changed")


def build_plan(
    campaign_root,
    binary,
    config,
    output_root,
    runner_root,
    submission_workdir=None,
    *,
    chunk_size=CHUNK_SIZE,
    lane_count=LANE_COUNT,
    expected_binary_sha256=None,
    expected_config_sha256=None,
    _verified_contract=None,
):
    """Build a deterministic JSON-serializable plan from the active v2 list."""
    if chunk_size != CHUNK_SIZE or lane_count != LANE_COUNT:
        raise ValueError("L4 production requires chunk_size=5 and lane_count=4")
    binary_path = _require_absolute_file(binary, "binary")
    config_path = _require_absolute_file(config, "config")
    output_path = _require_absolute_directory_path(output_root, "output_root")
    runner_path = _validate_runner_root(runner_root, campaign_root)
    submission_path = _validate_submission_workdir(
        runner_path / "slurm"
        if submission_workdir is None
        else submission_workdir,
        runner_path,
    )
    binary_hash_before = _sha256(binary_path)
    config_hash_before = _sha256(config_path)
    if expected_binary_sha256 is not None and binary_hash_before != expected_binary_sha256:
        raise ValueError("binary hash changed from frozen input")
    if expected_config_sha256 is not None and config_hash_before != expected_config_sha256:
        raise ValueError("config hash changed from frozen input")
    _load_and_validate_config(config_path, output_path)
    contract = (
        load_active_v2_contract(campaign_root)
        if _verified_contract is None
        else _verified_contract
    )
    runner_provenance = collect_runner_provenance(
        runner_path, contract["campaign_root"]
    )
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
    plan = {
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
        "runner_root": str(runner_path),
        "runner_provenance": runner_provenance,
        "submission_workdir": str(submission_path),
        "submission_resources": {
            "cpus": 12,
            "mybatch_input_memory_gb": MYBATCH_INPUT_MEMORY_GB,
            "effective_slurm_memory_gb": EFFECTIVE_SLURM_MEMORY_GB,
            "partition": "cpu_wgh",
            "time": "2:00:00",
        },
        "chunk_size": chunk_size,
        "lane_count": lane_count,
        "date_count": len(contract["production_dates"]),
        "chunk_count": len(planned_chunks),
        "chunks": planned_chunks,
    }
    validate_runner_provenance(
        runner_path, contract["campaign_root"], runner_provenance
    )
    return plan


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
        _fsync_directory(target.parent)
    finally:
        if temporary.exists():
            temporary.unlink()


def _json_bytes(payload):
    return (json.dumps(payload, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def _fsync_directory(path):
    descriptor = os.open(str(path), os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def write_plan_once(path, plan):
    """Atomically create a plan, or reuse only byte-identical contents."""
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    desired = _json_bytes(plan)
    lock_path = target.with_name(target.name + ".lock")
    with lock_path.open("a+") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        if target.is_symlink():
            raise ValueError("plan output must not be a symlink")
        if target.exists():
            if target.read_bytes() != desired:
                raise ValueError("existing plan bytes differ; refusing overwrite")
            return
        descriptor, temporary_name = tempfile.mkstemp(
            prefix="." + target.name + ".", suffix=".tmp", dir=str(target.parent)
        )
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output:
                output.write(desired)
                output.flush()
                os.fsync(output.fileno())
            os.replace(str(temporary), str(target))
            _fsync_directory(target.parent)
        finally:
            if temporary.exists():
                temporary.unlink()


def read_plan_once(path):
    """Bind JSON decoding and SHA-256 to one immutable bytes snapshot."""
    plan_path = Path(path)
    try:
        raw = plan_path.read_bytes()
        plan = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("plan file is not valid UTF-8 JSON") from error
    return plan, raw, _sha256_bytes(raw)


def submission_receipt_path(plan_output):
    path = Path(plan_output)
    return path.with_name(path.stem + "-submission.json")


def submission_lock_path(receipt_path):
    receipt = Path(receipt_path)
    return receipt.with_name(receipt.name + ".lock")


@contextmanager
def _submission_lock(receipt_path):
    lock_path = submission_lock_path(receipt_path)
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("a+") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("another submission holds the receipt lock") from error
        try:
            yield
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def plan_provenance(plan):
    return {
        "dataset_id": plan["dataset_id"],
        "date_list_sha256": plan["date_list_sha256"],
        "binary": plan["binary"],
        "binary_sha256": plan["binary_sha256"],
        "config": plan["config"],
        "config_sha256": plan["config_sha256"],
        "output_root": plan["output_root"],
        "runner_root": plan["runner_root"],
        "runner_provenance": plan["runner_provenance"],
        "submission_workdir": plan["submission_workdir"],
        "submission_resources": plan["submission_resources"],
    }


def _chunk_run_command(plan, chunk):
    arguments = [
        PYTHON38,
        "-I",
        str(Path(plan["runner_root"]) / "l4_runner_bootstrap.py"),
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
        "--runner-root",
        plan["runner_root"],
        "--runner-provenance-json",
        json.dumps(plan["runner_provenance"], separators=(",", ":")),
    ]
    return shlex.join(arguments)


def recover_job_id(job_name, submitted_at, now_fn=None):
    """Recover one job id by exact unique Slurm job name."""
    try:
        submitted = datetime.fromisoformat(str(submitted_at))
    except ValueError as error:
        raise ValueError("receipt submitted_at is not ISO-8601") from error
    if submitted.tzinfo is None:
        submitted = submitted.replace(tzinfo=timezone.utc)
    submitted_utc = submitted.astimezone(timezone.utc)
    recovered_at = (
        datetime.now(timezone.utc) if now_fn is None else now_fn()
    )
    if recovered_at.tzinfo is None:
        recovered_at = recovered_at.replace(tzinfo=timezone.utc)
    recovered_utc = recovered_at.astimezone(timezone.utc)
    start = (submitted_utc - timedelta(hours=1)).strftime(
        "%Y-%m-%dT%H:%M:%S"
    )
    end = max(recovered_utc, submitted_utc).strftime(
        "%Y-%m-%dT%H:%M:%S"
    )
    commands = (
        ["squeue", "-h", "-n", job_name, "-o", "%i|%j"],
        [
            "sacct", "-n", "-X", "--name", job_name,
            "-S", start, "-E", end,
            "--format=JobIDRaw,JobName", "--parsable2",
        ],
    )
    failures = []
    matches = set()
    for command in commands:
        try:
            completed = subprocess.run(command, capture_output=True, text=True)
        except (FileNotFoundError, OSError) as error:
            failures.append("{}: {}".format(command[0], error))
            continue
        if completed.returncode != 0:
            failures.append(
                "{} exited {}: {}".format(
                    command[0], completed.returncode, completed.stderr.strip()
                )
            )
            continue
        for line in completed.stdout.splitlines():
            fields = [field.strip() for field in line.split("|")]
            if len(fields) >= 2 and fields[1] == job_name and fields[0].isdigit():
                matches.add(fields[0])
        if len(matches) > 1:
            raise RuntimeError("multiple jobs match exact name {}".format(job_name))
    if failures:
        raise RuntimeError("job recovery query failed: {}".format("; ".join(failures)))
    return next(iter(matches)) if matches else None


def _next_chunk_id(plan, jobs_by_chunk):
    for chunk in plan["chunks"]:
        record = jobs_by_chunk.get(chunk["chunk_id"])
        if record is None or record.get("status") != "submitted":
            return chunk["chunk_id"]
    return None


def _submission_job_name(submission_id, chunk_id):
    return "{}-{}".format(submission_id, chunk_id)


def _submission_command(plan, chunk, job_name, dependency_job):
    run_command = _chunk_run_command(plan, chunk)
    command = ["mybatch"] + list(SLURM_RESOURCES) + ["-J", job_name]
    if dependency_job is not None:
        command.extend(["-d", "afterok:" + dependency_job])
    command.extend(["-s", run_command])
    return run_command, command


def _job_resources():
    return {
        "cpus": 12,
        "memory": "243G",
        "mybatch_input_memory_gb": MYBATCH_INPUT_MEMORY_GB,
        "effective_slurm_memory_gb": EFFECTIVE_SLURM_MEMORY_GB,
        "partition": "cpu_wgh",
        "time": "2:00:00",
    }


def _validate_receipt_jobs(plan, receipt):
    submission_id = receipt.get("submission_id")
    if not isinstance(submission_id, str) or re.fullmatch(r"[0-9a-f]+", submission_id) is None:
        raise ValueError("submission receipt has invalid submission_id")
    try:
        datetime.fromisoformat(str(receipt["submitted_at"]))
    except (KeyError, ValueError) as error:
        raise ValueError("submission receipt has invalid submitted_at") from error
    jobs = receipt.get("jobs")
    if not isinstance(jobs, list) or len(jobs) > len(plan["chunks"]):
        raise ValueError("submission receipt jobs must be a plan prefix")
    jobs_by_chunk = {}
    for index, record in enumerate(jobs):
        if not isinstance(record, dict):
            raise ValueError("submission receipt job must be an object")
        chunk = plan["chunks"][index]
        chunk_id = chunk["chunk_id"]
        if record.get("chunk_id") != chunk_id or chunk_id in jobs_by_chunk:
            raise ValueError("submission receipt job order/chunk mismatch")
        dependency_chunk = chunk["depends_on_chunk_id"]
        dependency_job = None
        if dependency_chunk is not None:
            dependency_record = jobs_by_chunk.get(dependency_chunk)
            if dependency_record is None or dependency_record.get("status") != "submitted":
                raise ValueError("submission receipt dependency is not submitted")
            dependency_job = dependency_record["job_id"]
        expected_name = _submission_job_name(submission_id, chunk_id)
        run_command, command = _submission_command(
            plan, chunk, expected_name, dependency_job
        )
        expected = {
            "lane": chunk["lane"],
            "dates": list(chunk["dates"]),
            "job_name": expected_name,
            "depends_on_chunk_id": dependency_chunk,
            "depends_on_job_id": dependency_job,
            "command": command,
            "run_command": run_command,
            "resources": _job_resources(),
        }
        for key, value in expected.items():
            if record.get(key) != value:
                raise ValueError("submission receipt job {} mismatch".format(key))
        status = record.get("status")
        if status == "submitted":
            if not str(record.get("job_id", "")).isdigit():
                raise ValueError("submitted receipt job_id must be numeric")
        elif status == "submitting":
            if "job_id" in record:
                raise ValueError("submitting receipt job must not have job_id")
            if index != len(jobs) - 1:
                raise ValueError("only the final receipt job may be submitting")
        else:
            raise ValueError("submission receipt job has invalid status")
        jobs_by_chunk[chunk_id] = record
    return jobs_by_chunk


def submit_plan(
    plan,
    plan_path,
    receipt_path=None,
    *,
    _verified_contract=None,
    recover_job_id=None,
):
    """Submit/resume chunks with a locked, crash-recoverable receipt."""
    plan_file = Path(plan_path)
    output_receipt = (
        submission_receipt_path(plan_file)
        if receipt_path is None
        else Path(receipt_path)
    )
    recover = globals()["recover_job_id"] if recover_job_id is None else recover_job_id
    with _submission_lock(output_receipt):
        recorded_plan, _, plan_hash = read_plan_once(plan_file)
        if recorded_plan != plan:
            raise ValueError("submitted plan payload does not match frozen plan bytes")
        contract = (
            load_active_v2_contract(plan["campaign_root"])
            if _verified_contract is None
            else _verified_contract
        )
        expected_plan = build_plan(
            plan["campaign_root"],
            plan["binary"],
            plan["config"],
            plan["output_root"],
            plan["runner_root"],
            plan["submission_workdir"],
            expected_binary_sha256=plan["binary_sha256"],
            expected_config_sha256=plan["config_sha256"],
            _verified_contract=contract,
        )
        if expected_plan != plan:
            raise ValueError("plan does not match deterministic active v2 production")
        validate_runner_provenance(
            plan["runner_root"], plan["campaign_root"], plan["runner_provenance"]
        )
        _verify_frozen_inputs(
            Path(plan["binary"]),
            Path(plan["config"]),
            plan["binary_sha256"],
            plan["config_sha256"],
        )
        if output_receipt.exists():
            try:
                receipt = json.loads(output_receipt.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                raise ValueError("submission receipt is invalid JSON") from error
            if receipt.get("plan_sha256") != plan_hash:
                raise ValueError("submission receipt belongs to a different plan")
            if receipt.get("plan_provenance") != plan_provenance(plan):
                raise ValueError("submission receipt provenance differs from plan")
            if receipt.get("status") == "submitted":
                raise ValueError("plan already has a completed submission receipt")
            if receipt.get("status") not in {"failed", "submitting"}:
                raise ValueError("submission receipt has invalid status")
        else:
            receipt = {
                "schema_version": 2,
                "status": "submitting",
                "submission_id": secrets.token_hex(12),
                "submitted_at": datetime.now(timezone.utc).isoformat(),
                "plan_path": str(plan_file.resolve()),
                "plan_sha256": plan_hash,
                "plan_provenance": plan_provenance(plan),
                "next_chunk_id": plan["chunks"][0]["chunk_id"],
                "jobs": [],
            }
            _write_json_atomic(output_receipt, receipt)

        jobs_by_chunk = _validate_receipt_jobs(plan, receipt)
        receipt["status"] = "submitting"
        receipt.pop("error", None)
        receipt["next_chunk_id"] = _next_chunk_id(plan, jobs_by_chunk)
        _write_json_atomic(output_receipt, receipt)

        for chunk in plan["chunks"]:
            chunk_id = chunk["chunk_id"]
            record = jobs_by_chunk.get(chunk_id)
            if record is not None and record.get("status") == "submitted":
                continue
            dependency_chunk = chunk["depends_on_chunk_id"]
            dependency_job = None
            if dependency_chunk is not None:
                dependency_record = jobs_by_chunk.get(dependency_chunk)
                if dependency_record is None or dependency_record.get("status") != "submitted":
                    raise ValueError("missing submitted dependency {}".format(dependency_chunk))
                dependency_job = dependency_record["job_id"]
            if record is None:
                record = {
                    "chunk_id": chunk_id,
                    "lane": chunk["lane"],
                    "dates": list(chunk["dates"]),
                    "job_name": _submission_job_name(receipt["submission_id"], chunk_id),
                    "status": "submitting",
                    "depends_on_chunk_id": dependency_chunk,
                    "depends_on_job_id": dependency_job,
                }
                run_command, command = _submission_command(
                    plan, chunk, record["job_name"], dependency_job
                )
                record.update({
                    "command": command,
                    "run_command": run_command,
                    "resources": _job_resources(),
                })
                receipt["jobs"].append(record)
                jobs_by_chunk[chunk_id] = record
            else:
                if record.get("status") != "submitting" or record.get("job_id") is not None:
                    raise ValueError("invalid resumable job state for {}".format(chunk_id))
                record["depends_on_job_id"] = dependency_job
                run_command, command = _submission_command(
                    plan, chunk, record["job_name"], dependency_job
                )
                record["command"] = command
                record["run_command"] = run_command
                record["resources"] = _job_resources()
                recovered = recover(
                    record["job_name"], receipt["submitted_at"]
                )
                if recovered is not None:
                    if not str(recovered).isdigit():
                        raise RuntimeError("recovered job id is not numeric")
                    record["job_id"] = str(recovered)
                    record["status"] = "submitted"
                    receipt["next_chunk_id"] = _next_chunk_id(plan, jobs_by_chunk)
                    _write_json_atomic(output_receipt, receipt)
                    continue

            receipt["next_chunk_id"] = chunk_id
            _write_json_atomic(output_receipt, receipt)
            try:
                completed = subprocess.run(
                    command,
                    capture_output=True,
                    text=True,
                    cwd=plan["submission_workdir"],
                )
                if completed.returncode != 0:
                    raise RuntimeError(
                        "mybatch failed for {}: {}".format(
                            chunk_id, completed.stderr.strip()
                        )
                    )
                match = re.fullmatch(r"Jobid:(\d+)", completed.stdout.strip())
                if match is None:
                    raise RuntimeError(
                        "invalid mybatch output for {}: {!r}".format(
                            chunk_id, completed.stdout
                        )
                    )
            except Exception as error:
                receipt["status"] = "failed"
                receipt["error"] = {
                    "chunk_id": chunk_id,
                    "type": type(error).__name__,
                    "message": str(error),
                }
                receipt["next_chunk_id"] = chunk_id
                _write_json_atomic(output_receipt, receipt)
                raise
            record.update({
                "job_id": match.group(1),
                "status": "submitted",
            })
            receipt["next_chunk_id"] = _next_chunk_id(plan, jobs_by_chunk)
            _write_json_atomic(output_receipt, receipt)

        receipt["status"] = "submitted"
        receipt["job_count"] = len(receipt["jobs"])
        receipt["next_chunk_id"] = None
        receipt.pop("error", None)
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
    runner_root,
    runner_provenance,
):
    """Produce and HDF5-only validate one frozen five-date chunk."""
    binary_path = _require_absolute_file(binary, "binary")
    config_path = _require_absolute_file(config, "config")
    output_path = _require_absolute_directory_path(output_root, "output_root")
    runner_path = _require_absolute_directory_path(runner_root, "runner_root")
    validate_runner_provenance(runner_path, campaign_root, runner_provenance)
    from evaluations.l4_preflight import validate_hdf5_only

    contract = _load_active_v2_production_contract(campaign_root)
    if contract["production_date_list_sha256"] != date_list_sha256:
        raise ValueError("production date-list hash changed")
    _verify_frozen_inputs(
        binary_path, config_path, binary_sha256, config_sha256
    )
    _load_and_validate_config(config_path, output_path)
    requested = validate_dates_against_frozen_list(
        dates, contract["production_dates"]
    )
    if len(requested) > CHUNK_SIZE:
        raise ValueError("run-chunk accepts at most five frozen dates")
    results = []
    for date in requested:
        _verify_frozen_inputs(
            binary_path, config_path, binary_sha256, config_sha256
        )
        target = output_path / date / "all_families" / "factors.h5"
        if target.exists():
            inspection = validate_hdf5_only(target, campaign_root)
            _verify_frozen_inputs(
                binary_path, config_path, binary_sha256, config_sha256
            )
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
            _verify_frozen_inputs(
                binary_path, config_path, binary_sha256, config_sha256
            )
            if not target.is_file():
                raise RuntimeError("factor binary did not create {}".format(target))
            inspection = validate_hdf5_only(target, campaign_root)
            _verify_frozen_inputs(
                binary_path, config_path, binary_sha256, config_sha256
            )
            status = "produced_valid"
        results.append({
            "date": date,
            "path": str(target),
            "status": status,
            "stock_count": inspection["stock_count"],
            "event_count": inspection["event_count"],
            "factor_count": inspection["factor_count"],
        })
    _verify_frozen_inputs(
        binary_path, config_path, binary_sha256, config_sha256
    )
    return results


def _parser():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    plan_parser = subparsers.add_parser("plan")
    plan_parser.add_argument("--campaign-root", type=Path, required=True)
    plan_parser.add_argument("--binary", type=Path, required=True)
    plan_parser.add_argument("--config", type=Path, required=True)
    plan_parser.add_argument("--output-root", type=Path, required=True)
    plan_parser.add_argument("--runner-root", type=Path, required=True)
    plan_parser.add_argument("--submission-workdir", type=Path, required=True)
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
    chunk_parser.add_argument("--runner-root", type=Path, required=True)
    chunk_parser.add_argument("--runner-provenance-json", required=True)
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = _parser().parse_args(argv)
    if args.command == "plan":
        contract = load_active_v2_contract(args.campaign_root)
        plan = build_plan(
            args.campaign_root,
            args.binary,
            args.config,
            args.output_root,
            args.runner_root,
            args.submission_workdir,
            _verified_contract=contract,
        )
        write_plan_once(args.plan_output, plan)
        result = plan
        if args.submit:
            result = submit_plan(
                plan,
                args.plan_output,
                _verified_contract=contract,
            )
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
        args.runner_root,
        json.loads(args.runner_provenance_json),
    )
    print(json.dumps(results, ensure_ascii=False))
    return 0


__all__ = [
    "FAMILIES",
    "assign_lanes",
    "build_plan",
    "chunk_dates",
    "collect_runner_provenance",
    "load_active_v2_contract",
    "main",
    "run_chunk",
    "submission_receipt_path",
    "submission_lock_path",
    "submit_plan",
    "validate_dates_against_frozen_list",
    "validate_runner_provenance",
    "write_plan_once",
    "read_plan_once",
    "plan_provenance",
]


if __name__ == "__main__":
    raise SystemExit(main())
