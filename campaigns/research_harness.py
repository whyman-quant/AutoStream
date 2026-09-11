"""Resumable, research-first orchestration for one mixed factor round.

The harness owns ordering and leakage gates; stage implementations are injected
so the same controller can run a local fixture or the production adapters.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Callable, Mapping, Optional

from .research_round import atomic_write_json


ORDER = ("L2", "L3", "L4", "selection", "L6-display", "experience", "next_batch")
_INDEX = {name: i for i, name in enumerate(ORDER)}


class HarnessError(RuntimeError):
    """A stage failed or a research boundary was violated."""


def _sha(value) -> str:
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True, default=str).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


class ResearchHarness:
    def __init__(self, round_data: Mapping[str, object], *, runner: Optional[Callable] = None,
                 receipt_dir: Optional[Path] = None, round_root: Optional[Path] = None):
        self.round = dict(round_data)
        self.runner = runner or self._unconfigured_runner
        root = self.round.get("artifacts_root") or ".autostream-research"
        self.receipt_dir = Path(receipt_dir or Path(root) / "receipts")
        self.round_root = Path(round_root or self.round.get("round_root") or ".").resolve()

    @staticmethod
    def _unconfigured_runner(stage, context):
        raise HarnessError("no stage runner configured for {}".format(stage))

    def plan(self) -> dict:
        """Return a command-free preview; this method never writes receipts."""
        return {
            "round_id": self.round.get("round_id"),
            "stages": list(ORDER),
            "promotion_allowed": False,
            "recursive": bool(self.round.get("recursive", False)),
            "selection_years": list(self.round.get("selection", {}).get("allowed_years", [])),
            "holdout_years": list(self.round.get("selection", {}).get("forbidden_years", [])),
            "receipt_dir": str(self.receipt_dir),
        }

    def _receipt_path(self, stage):
        return self.receipt_dir / (stage.replace("-", "_") + ".json")

    def _dates(self, split):
        """Read an inline date list or the frozen path used by a release contract."""
        entry = self.round.get("date_lists", {}).get(split, {}) or {}
        inline = entry.get("dates")
        if inline is not None:
            return [str(value) for value in inline]
        recorded = entry.get("path")
        if not isinstance(recorded, str) or not recorded:
            return []
        candidates = [Path(recorded)]
        if not candidates[0].is_absolute():
            candidates.extend((self.round_root / recorded, Path.cwd() / recorded))
        for path in candidates:
            if path.is_file():
                values = [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]
                if len(values) != len(set(values)):
                    raise HarnessError("{} date list contains duplicates".format(split))
                return values
        raise HarnessError("{} date list not found: {}".format(split, recorded))

    def _holdout_authorization(self):
        authorization = self.round.get("holdout_authorization")
        if authorization:
            return authorization
        recorded = self.round.get("holdout_authorization_path")
        if not recorded and self.round.get("round_id") == "l4g1_mixed_v1":
            recorded = "campaigns/sfm_stream_001/manifests/l4g1-mixed-holdout-authorization.json"
        if isinstance(recorded, str):
            path = Path(recorded)
            if not path.is_absolute():
                path = self.round_root / path
            if path.is_file():
                try:
                    return json.loads(path.read_text(encoding="utf-8"))
                except (OSError, ValueError) as exc:
                    raise HarnessError("invalid holdout authorization") from exc
        return None

    def _completed(self):
        result = []
        for stage in ORDER:
            path = self._receipt_path(stage)
            if not path.is_file():
                continue
            try:
                value = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError) as exc:
                raise HarnessError("invalid receipt for {}".format(stage)) from exc
            if value.get("stage") != stage or value.get("status") != "complete":
                raise HarnessError("stage {} has no complete receipt".format(stage))
            result.append(stage)
        return result

    def _context(self, stage):
        lists = self.round.get("date_lists", {})
        if stage == "L6-display":
            authorization = self._holdout_authorization()
            if not authorization:
                raise HarnessError("holdout display requires explicit authorization")
            if not authorization.get("selection_frozen") or not authorization.get("best_event_frozen"):
                raise HarnessError("holdout display requires frozen selection and best event")
            return {"holdout_dates": self._dates("holdout"),
                    "authorization": authorization, "selection_receipt": str(self._receipt_path("selection"))}
        if stage == "selection":
            # Selection is deliberately limited to pre-holdout dates.
            dates = self._dates("training") + self._dates("observation")
            if any(str(date) >= "20250101" for date in dates):
                raise HarnessError("selection input contains holdout date")
            return {"selection_input": dates, "holdout_dates": []}
        return {"training_dates": self._dates("training"),
                "observation_dates": self._dates("observation"),
                "holdout_dates": []}

    def _run_stage(self, stage):
        context = self._context(stage)
        try:
            output = self.runner(stage, context)
        except Exception as exc:
            raise HarnessError("{} failed: {}".format(stage, exc)) from exc
        receipt = {"stage": stage, "status": "complete", "input_hash": _sha(context),
                   "output_hash": _sha(output), "holdout_access": stage == "L6-display",
                   "promotion_allowed": False, "recursive": False}
        atomic_write_json(self._receipt_path(stage), receipt)
        return receipt

    def start(self, *, stop_after: Optional[str] = None) -> dict:
        if self.round.get("promotion_allowed") is not False:
            raise HarnessError("promotion is forbidden in research harness")
        if self.round.get("recursive"):
            raise HarnessError("recursive execution is forbidden")
        completed = set(self._completed())
        stop_index = _INDEX[stop_after] if stop_after else len(ORDER) - 1
        for stage in ORDER:
            if _INDEX[stage] > stop_index:
                break
            if stage in completed:
                continue
            # A stage can only run after its predecessor has a complete receipt.
            predecessor = ORDER[_INDEX[stage] - 1] if _INDEX[stage] else None
            if predecessor and predecessor not in completed:
                raise HarnessError("stage {} is missing predecessor {}".format(stage, predecessor))
            self._run_stage(stage)
            completed.add(stage)
        final = ORDER[max((_INDEX[x] for x in completed), default=0)] if completed else "planned"
        status = "complete" if final == ORDER[-1] else "paused"
        return {"round_id": self.round.get("round_id"), "status": status, "stage": final,
                "promotion_allowed": False, "recursive": False}

    def resume(self) -> dict:
        return self.start()


def _load_round(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "start", "resume", "status"))
    parser.add_argument("--round", required=True, dest="round_path")
    parser.add_argument("--receipt-dir", type=Path)
    parser.add_argument("--stop-after", choices=ORDER)
    args = parser.parse_args(argv)
    harness = ResearchHarness(_load_round(args.round_path), receipt_dir=args.receipt_dir,
                              round_root=Path(args.round_path).resolve().parents[3])
    if args.command == "plan":
        result = harness.plan()
    elif args.command == "status":
        result = {"completed": harness._completed(), "plan": harness.plan()}
    elif args.command == "resume":
        result = harness.resume()
    else:
        result = harness.start(stop_after=args.stop_after)
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


__all__ = ["HarnessError", "ORDER", "ResearchHarness"]
