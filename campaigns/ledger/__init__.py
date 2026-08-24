"""Append-only experiment ledger with fail-closed validation."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Mapping

REQUIRED_FIELDS = ("experiment_id", "campaign_id", "family_id", "candidate_id", "status", "source_commit", "config_hash", "data_snapshot", "evaluator_version")
STATUSES = frozenset(("generated", "compiled", "technical_pass", "technical_reject", "pilot_pass", "pilot_reject", "backtest_pass", "backtest_reject", "holdout_pass", "promoted", "crash", "timeout", "data_missing", "lookahead_reject", "duplicate_reject"))


def append_event(path: Path, event: Mapping[str, object]) -> None:
    missing = [field for field in REQUIRED_FIELDS if not event.get(field)]
    if missing:
        raise ValueError("missing required ledger fields: " + ", ".join(missing))
    if event["status"] not in STATUSES:
        raise ValueError("unknown ledger status: " + str(event["status"]))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(dict(event), ensure_ascii=False, sort_keys=True) + "\n")
