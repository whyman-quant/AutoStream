"""Boundary helpers for agent-driven factor research.

The Harness publishes data/coverage constraints; an Agent supplies the
mechanism, formula and falsification claim.  This module intentionally does
not contain a factor generator or a pre-selected formula grid.
"""
from __future__ import annotations

import hashlib
import json
import os
import tempfile
import itertools
from pathlib import Path
from typing import Iterable, Mapping, Optional


class AgentIdeaError(ValueError):
    """An agent proposal violates the research boundary."""


AGENT_RESEARCH_STAGES = (
    "logic_proposal",
    "factor_design",
    "code_implementation",
    "factor_calculation",
    "backtest",
    "experience",
    "next_logic",
)


def _atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="." + path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


class AgentResearchHarness:
    """Resumable controller for one complete Agent-driven research batch."""

    def __init__(self, round_data, *, runner, receipt_dir, task_packet=None):
        self.round = dict(round_data)
        self.runner = runner
        self.receipt_dir = Path(receipt_dir)
        self.task_packet = None if task_packet is None else dict(task_packet)

    def _receipt(self, stage):
        return self.receipt_dir / (stage + ".json")

    def _artifact(self, stage):
        return self.receipt_dir / "artifacts" / (stage + ".json")

    def _validate_output(self, stage, output):
        if not isinstance(output, Mapping):
            raise AgentIdeaError(stage + " output must be an object")
        if stage == "logic_proposal":
            ideas = output.get("ideas")
            if not isinstance(ideas, list) or not ideas:
                raise AgentIdeaError("logic_proposal must contain non-empty ideas")
            if self.task_packet is None:
                raise AgentIdeaError("logic_proposal requires a task packet")
            signatures = set()
            for idea in ideas:
                validate_agent_idea(idea, self.task_packet, existing_signatures=signatures)
                signatures.add(structure_signature(idea))
        elif stage == "factor_design":
            from campaigns.contracts import validate_document
            proposals = output.get("proposals")
            if not isinstance(proposals, list) or not proposals:
                raise AgentIdeaError("factor_design must contain non-empty proposals")
            signatures = set()
            for proposal in proposals:
                validate_document("candidate_proposal", proposal)
                actual = structure_signature(proposal)
                if proposal.get("structure_signature") != actual:
                    raise AgentIdeaError("candidate proposal structure signature mismatch")
                if actual in signatures:
                    raise AgentIdeaError("factor_design contains duplicate structures")
                signatures.add(actual)

    def _completed(self):
        completed = []
        for stage in AGENT_RESEARCH_STAGES:
            path = self._receipt(stage)
            if not path.is_file():
                continue
            value = json.loads(path.read_text(encoding="utf-8"))
            if value.get("stage") != stage or value.get("status") != "complete":
                raise AgentIdeaError("invalid stage receipt: " + stage)
            completed.append(stage)
        return completed

    def start(self, stop_after=None):
        if self.round.get("promotion_allowed") is not False:
            raise AgentIdeaError("automatic promotion is forbidden")
        if self.round.get("recursive"):
            raise AgentIdeaError("recursive research execution is forbidden")
        if stop_after is not None and stop_after not in AGENT_RESEARCH_STAGES:
            raise AgentIdeaError("unknown stop stage: " + str(stop_after))
        completed = set(self._completed())
        stop = AGENT_RESEARCH_STAGES.index(stop_after) if stop_after else len(AGENT_RESEARCH_STAGES) - 1
        final = None
        for index, stage in enumerate(AGENT_RESEARCH_STAGES):
            if index > stop:
                break
            if stage in completed:
                final = stage
                continue
            previous = AGENT_RESEARCH_STAGES[index - 1] if index else None
            if previous and previous not in completed:
                raise AgentIdeaError("{} requires {}".format(stage, previous))
            context = {
                "round_id": self.round.get("round_id"),
                "stage": stage,
                "promotion_allowed": False,
                "previous_receipt": None if previous is None else str(self._receipt(previous)),
                "previous_output": (
                    None if previous is None else
                    json.loads(self._artifact(previous).read_text(encoding="utf-8"))
                ),
            }
            if stage == "logic_proposal":
                context["task_packet"] = self.task_packet
            try:
                output = self.runner(stage, context)
            except Exception as error:
                raise AgentIdeaError("{} failed: {}".format(stage, error)) from error
            self._validate_output(stage, output)
            _atomic_json(self._artifact(stage), output)
            receipt = {
                "stage": stage,
                "status": "complete",
                "input_sha256": "sha256:" + hashlib.sha256(_canonical(context).encode()).hexdigest(),
                "output_sha256": "sha256:" + hashlib.sha256(_canonical(output).encode()).hexdigest(),
                "promotion_allowed": False,
                "recursive": False,
                "artifact_path": str(self._artifact(stage)),
            }
            _atomic_json(self._receipt(stage), receipt)
            completed.add(stage)
            final = stage
        return {
            "round_id": self.round.get("round_id"),
            "status": "complete" if final == AGENT_RESEARCH_STAGES[-1] else "paused",
            "stage": final,
            "promotion_allowed": False,
            "recursive": False,
        }


def _canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def structure_signature(proposal: Mapping[str, object]) -> str:
    """Return a stable signature for structural (not name) deduplication."""
    shape = {
        "inputs": sorted(str(value) for value in proposal.get("inputs", ())),
        "operators": sorted(str(value) for value in proposal.get("operators", ())),
        "formula": " ".join(str(proposal.get("formula", "")).lower().split()),
    }
    return "sha256:" + hashlib.sha256(_canonical(shape).encode("utf-8")).hexdigest()


def build_candidate_proposal(idea, *, proposal_id, formula, input_streams, operators,
                             readiness=None, falsification=None, novelty_claim=None,
                             supported_events=None):
    """Convert one reviewed Agent idea/design variant into a signed proposal."""
    proposal = {
        "schema_version": 1,
        "kind": "candidate_proposal",
        "proposal_id": str(proposal_id),
        "idea_id": str(idea["idea_id"]),
        "family_id": str(idea["family_id"]),
        "mechanism": str(idea["mechanism"]),
        "formula": str(formula),
        "inputs": [str(value) for value in input_streams],
        "operators": [str(value) for value in operators],
        "readiness": str(readiness or idea["readiness"]),
        "falsification": str(falsification or idea["falsification"]),
        "novelty_claim": str(novelty_claim or idea["novelty_claim"]),
        "supported_events": [int(value) for value in (supported_events or idea["supported_events"])],
    }
    proposal["structure_signature"] = structure_signature(proposal)
    from campaigns.contracts import validate_document
    validate_document("candidate_proposal", proposal)
    return proposal


def compile_design_blueprint(logic_artifact, blueprint):
    """Bind Agent-designed variants to reviewed ideas and sign each structure."""
    ideas = {str(value["idea_id"]): value for value in logic_artifact.get("ideas", ())}
    proposals = []
    for variant in blueprint.get("variants", ()):
        idea_id = str(variant.get("idea_id", ""))
        if idea_id not in ideas:
            raise AgentIdeaError("design references unknown reviewed idea: " + idea_id)
        optional = {name: variant[name] for name in (
            "readiness", "falsification", "novelty_claim", "supported_events"
        ) if name in variant}
        proposals.append(build_candidate_proposal(
            ideas[idea_id], proposal_id=variant["proposal_id"],
            formula=variant["formula"], input_streams=variant["input_streams"],
            operators=variant["operators"], **optional
        ))
    if not proposals:
        raise AgentIdeaError("design blueprint has no variants")
    signatures = [value["structure_signature"] for value in proposals]
    if len(signatures) != len(set(signatures)):
        raise AgentIdeaError("design blueprint contains duplicate structures")
    return {
        "schema_version": 1,
        "kind": "factor_design_artifact",
        "round_id": logic_artifact.get("round_id"),
        "proposals": proposals,
    }


def build_task_packet(round_data: Mapping[str, object], family_id: str,
                      operator_catalog: Mapping[str, object],
                      coverage_cells: Iterable[Mapping[str, object]]) -> dict:
    """Publish a bounded research task without prescribing a factor formula."""
    events = []
    for value in round_data.get("events", ()):
        event = int(value)
        # Runtime contracts use HHMM*100000 timestamps; Agent IdeaSpecs use
        # compact HHMM minute codes to stay readable and portable.
        if event == 92700000:
            # The engine's 09:27 source checkpoint is the compact research
            # event 09:26 (auction snapshot), retained as 926 in contracts.
            event = 926
        elif event >= 100000:
            event //= 100000
        events.append(event)
    gaps = [dict(cell) for cell in coverage_cells
            if str(cell.get("status", "")) in {"unexplored", "missing", "blocked"}]
    return {
        "schema_version": 1,
        "kind": "agent_task_packet",
        "round_id": str(round_data.get("round_id", "")),
        "campaign_id": str(round_data.get("campaign_id", "")),
        "family_id": str(family_id),
        "events": events,
        "operator_catalog": dict(operator_catalog),
        "coverage_gaps": gaps,
        "constraints": [
            "must_be_falsifiable",
            "must_use_declared_inputs",
            "must_have_structural_novelty",
            "must_declare_readiness",
            "must_not_use_future_data",
            "one_primary_mechanism_change",
        ],
        "deliverable": [
            "mechanism",
            "formula",
            "readiness",
            "falsification",
            "novelty_claim",
        ],
    }


def build_coverage_matrix(family_id, mechanism_axes, structure_axes, data_axes):
    """Create the explicit space the research Agents are expected to explore."""
    cells = []
    for mechanism, structure, data in itertools.product(
            mechanism_axes, structure_axes, data_axes):
        cells.append({
            "schema_version": 1,
            "kind": "coverage_cell",
            "cell_id": "{}.{}.{}.{}".format(family_id, mechanism, structure, data),
            "family_id": str(family_id),
            "mechanism_axis": str(mechanism),
            "structure_axis": str(structure),
            "data_axis": str(data),
            "status": "unexplored",
            "artifact_ids": [],
        })
    return cells


def load_task_packet(round_path, repo_root):
    """Load a frozen Agent round and expand its declared coverage axes."""
    from campaigns.contracts import validate_document

    root = Path(repo_root).resolve()
    round_data = json.loads(Path(round_path).read_text(encoding="utf-8"))
    validate_document("agent_research_round", round_data)
    catalog_path = root / round_data["operator_catalog_path"]
    coverage_path = root / round_data["coverage_matrix_path"]
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    coverage = json.loads(coverage_path.read_text(encoding="utf-8"))
    if catalog.get("kind") != "operator_catalog" or coverage.get("kind") != "coverage_space":
        raise AgentIdeaError("invalid operator catalog or coverage space")
    for operator in catalog.get("operators", ()):
        validate_document("operator_spec", {
            "schema_version": 1, "kind": "operator_spec", **operator,
        })
    try:
        axes = coverage["axes"]
        cells = build_coverage_matrix(
            round_data["research_domains"][0], axes["hypothesis_origin"],
            axes["representation"], axes["data_scope"],
        )
    except (KeyError, IndexError, TypeError) as error:
        raise AgentIdeaError("invalid coverage axes") from error
    packet = build_task_packet(
        round_data, round_data["research_domains"][0], catalog, cells
    )
    packet["reuse_policy"] = dict(round_data["reuse_policy"])
    packet["coverage_allocation"] = dict(coverage.get("allocation", {}))
    return packet


def validate_agent_idea(proposal: Mapping[str, object], task: Mapping[str, object],
                       existing_signatures: Optional[Iterable[str]] = None) -> None:
    """Validate an Agent proposal before it can become a Candidate."""
    required = ("idea_id", "family_id", "research_question", "mechanism", "inputs",
                "operators", "formula", "readiness", "falsification", "novelty_claim",
                "supported_events")
    missing = [field for field in required if not proposal.get(field)]
    if missing:
        raise AgentIdeaError("missing agent idea fields: " + ", ".join(missing))
    if str(proposal["family_id"]) != str(task.get("family_id")):
        raise AgentIdeaError("idea family does not match task family")
    allowed_events = set(int(value) for value in task.get("events", ()))
    try:
        events = [int(value) for value in proposal["supported_events"]]
    except (TypeError, ValueError) as error:
        raise AgentIdeaError("supported_events must be integer minute codes") from error
    if not events or len(events) != len(set(events)) or not set(events).issubset(allowed_events):
        raise AgentIdeaError("supported_events must be a unique subset of task events")
    if not isinstance(proposal["inputs"], (list, tuple)) or not proposal["inputs"]:
        raise AgentIdeaError("idea must declare at least one input")
    if not isinstance(proposal["operators"], (list, tuple)) or not proposal["operators"]:
        raise AgentIdeaError("idea must declare at least one operator")
    catalog = task.get("operator_catalog", {})
    catalog_ids = set(str(item.get("operator_id")) for item in catalog.get("operators", ())
                      if isinstance(item, Mapping))
    proposed_specs = proposal.get("new_operator_specs", ())
    if proposed_specs is None or not isinstance(proposed_specs, (list, tuple)):
        raise AgentIdeaError("new_operator_specs must be a list")
    proposed_ids = set()
    for spec in proposed_specs:
        if not isinstance(spec, Mapping):
            raise AgentIdeaError("new operator definition must be an object")
        required_spec = ("operator_id", "category", "description", "input_streams", "causal",
                         "readiness_requirement")
        if any(not spec.get(field) for field in required_spec) or spec.get("causal") is not True:
            raise AgentIdeaError("new operator must be causal and declare readiness")
        proposed_ids.add(str(spec["operator_id"]))
    unknown = [str(value) for value in proposal["operators"]
               if catalog_ids and str(value) not in catalog_ids and str(value) not in proposed_ids]
    if unknown:
        raise AgentIdeaError("unknown operators: " + ", ".join(unknown))
    signature = structure_signature(proposal)
    if signature in set(existing_signatures or ()):
        raise AgentIdeaError("proposal duplicates an existing formula structure")


__all__ = [
    "AGENT_RESEARCH_STAGES", "AgentIdeaError", "AgentResearchHarness",
    "build_candidate_proposal", "build_coverage_matrix", "build_task_packet",
    "compile_design_blueprint", "load_task_packet",
    "structure_signature", "validate_agent_idea",
]
