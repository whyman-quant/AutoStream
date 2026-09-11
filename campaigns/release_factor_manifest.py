"""Build and verify the ordered factor contract of a frozen release."""
from __future__ import annotations

import hashlib
import json
from collections import OrderedDict
from pathlib import Path


def _canonical(document):
    return json.dumps(document, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def _sha256_bytes(value):
    return "sha256:" + hashlib.sha256(value).hexdigest()


def _manifest_hash(document):
    payload = dict(document)
    payload.pop("manifest_sha256", None)
    return _sha256_bytes(_canonical(payload))


def build_factor_manifest(release_id, batch_paths):
    """Derive exact factor order from ordered Batch paths.

    The first Batch encountered for a family is its control Batch; later
    Batches are research candidates.  Counts are observations, never policy
    constants.  The caller freezes ordering by freezing this document.
    """
    families = OrderedDict()
    factors = []
    batches = []
    seen = set()
    for source in batch_paths:
        path = Path(source)
        payload = json.loads(path.read_text(encoding="utf-8"))
        family = str(payload.get("family_id", ""))
        batch_id = str(payload.get("batch_id", path.stem))
        ids = payload.get("candidate_ids")
        if not family or not isinstance(ids, list) or not ids:
            raise ValueError("Batch must declare family_id and non-empty candidate_ids: {}".format(path))
        names = [str(value) for value in ids]
        duplicates = [name for name in names if name in seen]
        if len(set(names)) != len(names) or duplicates:
            raise ValueError("duplicate factor identity in release: {}".format(duplicates or names))
        seen.update(names)
        factors.extend(names)
        family_entry = families.setdefault(family, {
            "family_id": family, "batch_ids": [], "factor_names": [],
            "control_count": 0, "candidate_count": 0,
        })
        role = "control" if not family_entry["batch_ids"] else "candidate"
        family_entry["batch_ids"].append(batch_id)
        family_entry["factor_names"].extend(names)
        family_entry[role + "_count"] += len(names)
        raw = path.read_bytes()
        batches.append({"batch_id": batch_id, "family_id": family, "role": role,
                        "path": str(path), "sha256": _sha256_bytes(raw),
                        "factor_names": names, "factor_count": len(names)})
    if not factors:
        raise ValueError("release must contain at least one factor")
    document = {
        "schema_version": 1,
        "kind": "release_factor_manifest",
        "release_id": str(release_id),
        "factor_count": len(factors),
        "factor_names": factors,
        "family_count": len(families),
        "families": list(families.values()),
        "batches": batches,
    }
    document["manifest_sha256"] = _manifest_hash(document)
    return document


def load_factor_manifest(path):
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    recorded = document.get("manifest_sha256")
    if recorded != _manifest_hash(document):
        raise ValueError("release factor manifest hash mismatch")
    names = document.get("factor_names")
    if not isinstance(names, list) or len(names) != document.get("factor_count") or len(set(names)) != len(names):
        raise ValueError("release factor manifest factor list is invalid")
    flattened = [name for family in document.get("families", [])
                 for name in family.get("factor_names", [])]
    if flattened != names:
        raise ValueError("release factor manifest family order mismatch")
    return document


def write_factor_manifest(path, document):
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    return destination


__all__ = ["build_factor_manifest", "load_factor_manifest", "write_factor_manifest"]
