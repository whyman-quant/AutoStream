"""Versioned, fail-closed contracts for factor research artifacts."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Mapping

from jsonschema import Draft7Validator


CONTRACT_FILES = {
    "idea_spec": "idea_spec.schema.json",
    "candidate": "candidate.schema.json",
    "batch": "batch.schema.json",
    "factor_portrait": "factor_portrait.schema.json",
    "experience_record": "experience_record.schema.json",
}


def load_schema(kind: str, schema_version=None) -> dict:
    try:
        filename = CONTRACT_FILES[kind]
    except KeyError as error:
        raise ValueError("unknown contract kind: {}".format(kind)) from error
    if kind == "factor_portrait" and schema_version == 2:
        filename = "factor_portrait_v2.schema.json"
    path = Path(__file__).with_name(filename)
    schema = json.loads(path.read_text(encoding="utf-8"))
    Draft7Validator.check_schema(schema)
    return schema


def validate_document(kind: str, document: Mapping[str, object]) -> None:
    version = document.get("schema_version") if isinstance(document, Mapping) else None
    validator = Draft7Validator(load_schema(kind, version))
    errors = sorted(validator.iter_errors(dict(document)), key=lambda item: list(item.path))
    if not errors:
        return
    messages = []
    for error in errors:
        location = ".".join(str(value) for value in error.absolute_path) or "<root>"
        messages.append("{}: {}".format(location, error.message))
    raise ValueError("{} contract violation: {}".format(kind, "; ".join(messages)))


__all__ = ["CONTRACT_FILES", "load_schema", "validate_document"]
