"""Small, deterministic state machine for a resumable research round.

The module deliberately keeps orchestration out of the state machine.  A stage is
advanced only by a validated receipt; the presence of an output file is never
treated as evidence that a stage completed.
"""

from __future__ import annotations

import hashlib
import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping, Optional, Union


STATES = (
    "planned",
    "released",
    "l2_complete",
    "l3_produced",
    "l3_evaluated",
    "l4_produced",
    "l4_evaluated",
    "selected",
    "holdout_displayed",
    "experience_generated",
    "next_batch_generated",
    "complete",
)
_STATE_INDEX = {state: i for i, state in enumerate(STATES)}
_HEX64 = set("0123456789abcdef")


def _canonical(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), default=str).encode("utf-8")


def sha256(value: Any) -> str:
    """Return a stable SHA-256 for bytes, a file, or canonical JSON data."""
    if isinstance(value, (str, os.PathLike)):
        path = Path(value)
        if path.exists() and path.is_file():
            data = path.read_bytes()
        else:
            data = str(value).encode("utf-8")
    elif isinstance(value, bytes):
        data = value
    else:
        data = _canonical(value)
    return hashlib.sha256(data).hexdigest()


def load_round(path: Union[str, os.PathLike]) -> dict:
    """Load a frozen round document without mutating it."""
    with Path(path).open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("research round must be a JSON object")
    return value


def atomic_write_json(path: Union[str, os.PathLike], value: Mapping[str, Any]) -> None:
    """Write JSON using a same-directory temporary file followed by replace."""
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".%s." % destination.name, dir=str(destination.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(dict(value), stream, ensure_ascii=False, sort_keys=True, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
        try:
            directory_fd = os.open(str(destination.parent), os.O_DIRECTORY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        except (AttributeError, OSError):
            pass
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _state(value: Any) -> str:
    if isinstance(value, Mapping):
        value = value.get("stage", value.get("state"))
    if value not in _STATE_INDEX:
        raise ValueError("unknown research round stage: %r" % (value,))
    return str(value)


def plan_transition(current: Any, target: str, *, holdout_authorized: bool = False, recursive: bool = False, round_data: Optional[Mapping[str, Any]] = None) -> str:
    """Purely validate and return an adjacent stage transition."""
    source = _state(current)
    target = _state(target)
    if round_data is not None:
        recursive = bool(round_data.get("recursive", recursive))
    if _STATE_INDEX[target] != _STATE_INDEX[source] + 1:
        raise ValueError("research round cannot skip or repeat stages: %s -> %s" % (source, target))
    if target == "holdout_displayed" and not holdout_authorized:
        raise ValueError("holdout display requires explicit authorization")
    if recursive and target in {"next_batch_generated", "complete"}:
        raise ValueError("recursive round completion is forbidden")
    return target


def _iso_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _hash_input(value: Any, explicit: Optional[str]) -> str:
    return explicit if explicit is not None else sha256(value)


def advance(current: Any, target: str, *, input_data: Any = None, output_data: Any = None,
            input_hash: Optional[str] = None, output_hash: Optional[str] = None,
            output_path: Optional[Union[str, os.PathLike]] = None,
            job_ids: Optional[Iterable[Any]] = None, retries: int = 0,
            error_category: Optional[str] = None, holdout_access: bool = False,
            holdout_authorized: bool = False, recursive: bool = False,
            round_data: Optional[Mapping[str, Any]] = None,
            started_at: Optional[str] = None, ended_at: Optional[str] = None,
            receipt_path: Optional[Union[str, os.PathLike]] = None) -> dict:
    """Create (and optionally persist) one immutable stage receipt."""
    prior = current if isinstance(current, Mapping) else None
    authorized = holdout_authorized or bool(prior and prior.get("holdout_authorized"))
    stage = plan_transition(current, target, holdout_authorized=authorized, recursive=recursive, round_data=round_data)
    if holdout_access and stage != "holdout_displayed":
        raise ValueError("holdout access is only permitted for holdout_displayed")
    if stage == "holdout_displayed" and not authorized:
        raise ValueError("holdout display requires explicit authorization")
    if retries < 0 or int(retries) != retries:
        raise ValueError("retries must be a non-negative integer")
    jobs = list(job_ids or [])
    if len(set(map(str, jobs))) != len(jobs):
        raise ValueError("duplicate Job IDs in receipt")
    started = started_at or _iso_now()
    ended = ended_at or started
    if output_path is not None and output_data is None:
        output_data = Path(output_path)
    receipt = {
        "stage": stage,
        "input_hash": _hash_input(input_data, input_hash),
        "output_hash": _hash_input(output_data, output_hash),
        "started_at": started,
        "ended_at": ended,
        "job_ids": jobs,
        "retries": int(retries),
        "error_category": error_category,
        "holdout_access": bool(holdout_access),
        "holdout_authorized": bool(authorized),
    }
    if output_path is not None:
        receipt["output_path"] = str(output_path)
    validate_receipt(receipt)
    if receipt_path is not None:
        atomic_write_json(receipt_path, receipt)
    return receipt


def validate_receipt(receipt: Mapping[str, Any], *, expected_input_hash: Optional[str] = None,
                     expected_output_hash: Optional[str] = None,
                     round_data: Optional[Mapping[str, Any]] = None) -> None:
    """Fail closed on malformed, tampered, or unauthorized stage evidence."""
    if not isinstance(receipt, Mapping):
        raise ValueError("stage receipt must be an object")
    required = ("stage", "input_hash", "output_hash", "started_at", "ended_at", "job_ids", "retries", "holdout_access")
    missing = [name for name in required if name not in receipt]
    if missing:
        raise ValueError("receipt missing fields: " + ", ".join(missing))
    stage = _state(receipt["stage"])
    for name in ("input_hash", "output_hash"):
        value = receipt[name]
        if not isinstance(value, str) or len(value) != 64 or any(char not in _HEX64 for char in value):
            raise ValueError("receipt %s is not a SHA-256" % name)
    if expected_input_hash is not None and receipt["input_hash"] != expected_input_hash:
        raise ValueError("receipt input hash drift")
    if expected_output_hash is not None and receipt["output_hash"] != expected_output_hash:
        raise ValueError("receipt output hash drift")
    if not isinstance(receipt["job_ids"], list) or len(set(map(str, receipt["job_ids"]))) != len(receipt["job_ids"]):
        raise ValueError("receipt Job IDs must be a unique list")
    if not isinstance(receipt["retries"], int) or receipt["retries"] < 0:
        raise ValueError("receipt retries must be non-negative")
    if not isinstance(receipt["holdout_access"], bool):
        raise ValueError("receipt holdout_access must be boolean")
    authorized = bool(receipt.get("holdout_authorized", False))
    if stage == "holdout_displayed" and not authorized:
        raise ValueError("holdout receipt lacks authorization")
    if stage != "holdout_displayed" and receipt["holdout_access"]:
        raise ValueError("holdout access before holdout_displayed")
    if round_data is not None and bool(round_data.get("recursive")) and stage in {"next_batch_generated", "complete"}:
        raise ValueError("recursive round completion is forbidden")
    output_path = receipt.get("output_path")
    if output_path is not None:
        path = Path(output_path)
        if not path.is_file() or sha256(path) != receipt["output_hash"]:
            raise ValueError("receipt output artifact hash drift")


def resume_point(source: Union[str, os.PathLike, Iterable[Mapping[str, Any]]], *, round_data: Optional[Mapping[str, Any]] = None) -> str:
    """Return the latest stage backed by a valid receipt, defaulting to planned."""
    receipts = []
    if isinstance(source, (str, os.PathLike)):
        path = Path(source)
        paths = [path] if path.is_file() else sorted(path.glob("*.json"))
        for candidate in paths:
            try:
                value = json.loads(candidate.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                raise ValueError("invalid receipt JSON: %s" % candidate) from error
            if isinstance(value, Mapping) and "stage" in value:
                validate_receipt(value, round_data=round_data)
                receipts.append(value)
    else:
        for value in source:
            validate_receipt(value, round_data=round_data)
            receipts.append(value)
    if not receipts:
        return "planned"
    ordered = sorted(receipts, key=lambda value: _STATE_INDEX[_state(value)])
    indices = [_STATE_INDEX[_state(value)] for value in ordered]
    if indices[0] != 1 or indices != list(range(1, indices[-1] + 1)):
        raise ValueError("resume receipts are not a contiguous accepted prefix")
    return ordered[-1]["stage"]


__all__ = ["STATES", "advance", "atomic_write_json", "load_round", "plan_transition", "resume_point", "sha256", "validate_receipt"]
