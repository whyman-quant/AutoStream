"""Cross-artifact consistency checks beyond individual JSON schemas."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Mapping, Sequence

from campaigns.contracts import validate_document


CANONICAL_CANDIDATE_FIELDS = (
    "family_id", "hypothesis_id", "operator_id", "formula",
    "source_streams", "parameters", "state", "availability", "output",
)


def canonical_candidate_payload(document: Mapping[str, object]) -> bytes:
    payload = {field: document[field] for field in CANONICAL_CANDIDATE_FIELDS}
    return json.dumps(
        payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
    ).encode("utf-8")


def candidate_hash(document: Mapping[str, object]) -> str:
    return "sha256:" + hashlib.sha256(canonical_candidate_payload(document)).hexdigest()


def _load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _metadata_names(path: Path):
    text = path.read_text(encoding="utf-8")
    match = re.search(r"kFactorNames\s*=\s*\{(.*?)\};", text, re.S)
    if match is None:
        raise ValueError("kFactorNames not found in {}".format(path))
    return re.findall(r'"([^"]+)"', match.group(1))


def validate_candidate_batch(
    candidate_paths: Sequence[Path],
    batch_path: Path,
    idea_path: Path,
    metadata_path: Path,
) -> None:
    candidates = [_load(path) for path in candidate_paths]
    batch = _load(batch_path)
    idea = _load(idea_path)
    for candidate in candidates:
        validate_document("candidate", candidate)
        if candidate["canonical_hash"] != candidate_hash(candidate):
            raise ValueError("candidate hash mismatch: {}".format(candidate["candidate_id"]))
        if candidate["batch_id"] != batch["batch_id"]:
            raise ValueError("candidate batch mismatch: {}".format(candidate["candidate_id"]))
        if candidate["family_id"] != idea["family"]:
            raise ValueError("candidate family mismatch: {}".format(candidate["candidate_id"]))
        if not idea_path.as_posix().endswith(candidate["lineage"]["idea_path"]):
            raise ValueError("candidate idea lineage mismatch: {}".format(candidate["candidate_id"]))
        if set(candidate["source_streams"]) != set(idea["source_streams"]):
            raise ValueError("candidate source stream mismatch: {}".format(candidate["candidate_id"]))

    validate_document("batch", batch)
    ids = [candidate["candidate_id"] for candidate in candidates]
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate candidate identity")
    if set(ids) != set(batch["candidate_ids"]):
        raise ValueError("batch candidate set mismatch")
    if batch["search_policy"]["candidate_budget"] != len(ids):
        raise ValueError("batch candidate budget does not match candidate count")
    if len(ids) != idea["candidate_quota"]:
        raise ValueError("candidate count does not match family quota")
    if set(ids) != set(_metadata_names(metadata_path)):
        raise ValueError("candidate identities do not match C++ metadata")


__all__ = [
    "candidate_hash", "canonical_candidate_payload", "validate_candidate_batch",
]
