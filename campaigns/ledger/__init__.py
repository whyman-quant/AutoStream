"""Append-only experiment ledger with fail-closed validation."""

from __future__ import annotations

import json
from pathlib import Path
from typing import List, Mapping

REQUIRED_FIELDS = ("experiment_id", "campaign_id", "family_id", "candidate_id", "status", "source_commit", "config_hash", "data_snapshot", "evaluator_version")
STATUSES = frozenset((
    "generated", "compiled", "technical_pass", "technical_reject",
    "pilot_pass", "pilot_reject", "observation_only",
    "formal_observation", "backtest_pass", "backtest_reject",
    "holdout_pass", "promoted", "crash", "timeout",
    "data_missing", "lookahead_reject", "duplicate_reject",
))
PROMOTION_STATUSES = frozenset(("promoted",))


def validate_event(event: Mapping[str, object]) -> None:
    missing = [field for field in REQUIRED_FIELDS if not event.get(field)]
    if missing:
        raise ValueError("missing required ledger fields: " + ", ".join(missing))
    status = str(event["status"])
    if status not in STATUSES:
        raise ValueError("unknown ledger status: " + status)
    promotion_allowed = event.get("promotion_allowed", False)
    if not isinstance(promotion_allowed, bool):
        raise ValueError("promotion_allowed must be boolean when present")
    if status in PROMOTION_STATUSES and not promotion_allowed:
        raise ValueError("promotion_allowed must be true for status {}".format(status))
    if status not in PROMOTION_STATUSES and promotion_allowed:
        raise ValueError("promotion_allowed must be false for status {}".format(status))


def validate_ledger(path: Path) -> List[dict]:
    events = []
    with path.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
                validate_event(event)
            except (json.JSONDecodeError, ValueError) as error:
                raise ValueError("invalid ledger event at line {}: {}".format(line_number, error)) from error
            events.append(event)
    return events


def append_event(path: Path, event: Mapping[str, object]) -> None:
    validate_event(event)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(dict(event), ensure_ascii=False, sort_keys=True) + "\n")


__all__ = [
    "PROMOTION_STATUSES", "REQUIRED_FIELDS", "STATUSES", "append_event",
    "validate_event", "validate_ledger",
]
