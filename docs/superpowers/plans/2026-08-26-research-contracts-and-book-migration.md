# Research Contracts and Book Imbalance Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze four machine-verifiable research contracts, align the append-only ledger state machine, and migrate the twelve existing `book_imbalance` candidates plus their pilot evidence into those contracts.

**Architecture:** JSON Schema Draft 7 files define persisted artifacts; a small Python validation package loads schemas and performs cross-artifact checks that JSON Schema cannot express. Candidate files remain one-file-per-factor, one Batch groups the twelve seeds, and one pilot Portrait is generated per candidate from the immutable 17-day observation receipt.

**Tech Stack:** Python 3.10, `jsonschema` 3.2, `unittest`, JSON Schema Draft 7, existing C++ factor metadata and campaign JSON.

---

## File map

- `campaigns/contracts/*.schema.json`: authoritative persisted artifact contracts.
- `campaigns/contracts/__init__.py`: schema loading and single-document validation.
- `campaigns/contracts/consistency.py`: candidate/batch/family/code and portrait/receipt consistency checks.
- `campaigns/tests/test_contracts.py`: schema behavior tests.
- `campaigns/tests/test_book_imbalance_migration.py`: migration and code-lineage tests.
- `campaigns/ledger/schema.json` and `campaigns/ledger/__init__.py`: one aligned experiment state vocabulary.
- `campaigns/sfm_stream_001/batches/book_imbalance_seed_v1.json`: the first family batch.
- `campaigns/sfm_stream_001/candidates/book_imbalance/*.json`: twelve canonical candidates.
- `campaigns/sfm_stream_001/portraits/pilot/book_imbalance/*.json`: twelve L3 pilot portraits.
- `campaigns/sfm_stream_001/experience/`: intentionally empty until facts are synthesized from a frozen formal-history portrait; pilot does not create promotion experience.

### Task 1: Freeze JSON contracts

**Files:**
- Create: `campaigns/contracts/candidate.schema.json`
- Create: `campaigns/contracts/batch.schema.json`
- Create: `campaigns/contracts/factor_portrait.schema.json`
- Create: `campaigns/contracts/experience_record.schema.json`
- Create: `campaigns/contracts/__init__.py`
- Test: `campaigns/tests/test_contracts.py`

- [ ] **Step 1: Write failing schema tests**

Create valid minimal examples for all four kinds and assert that `validate_document(kind, document)` accepts them. Add one focused negative test per contract: candidate rejects an unfrozen direction, Batch rejects duplicate candidate IDs, Portrait rejects `promotion_allowed=true` at L3, and Experience rejects an action without a falsifiable test.

- [ ] **Step 2: Verify the tests fail for the missing package**

Run: `python3 -m unittest -v campaigns.tests.test_contracts`

Expected: import failure for `campaigns.contracts`.

- [ ] **Step 3: Implement the Draft 7 schemas and loader**

The public API is:

```python
def validate_document(kind: str, document: Mapping[str, object]) -> None:
    """Raise ValueError containing all schema violations, otherwise return None."""
```

Contract invariants:

- Candidate identity and lineage are immutable; research direction is always `raw_signed`; L6 may separately freeze a trading direction.
- Batch changes exactly one declared research dimension and lists unique candidate IDs.
- Portrait declares `scope` (`pilot` or `formal_history`), evidence level, full dataset dimensions, data quality, signed metrics, redundancy and decision. L3 forces `observation_only` and `promotion_allowed=false`.
- Experience separates `fact`, `interpretation` and `action`; the action must name a changed dimension and a falsifiable test.
- Every contract uses `additionalProperties=false` at the document boundary.

- [ ] **Step 4: Run contract tests green**

Run: `python3 -m unittest -v campaigns.tests.test_contracts`

Expected: all tests pass.

### Task 2: Align and validate the ledger state machine

**Files:**
- Modify: `campaigns/ledger/schema.json`
- Modify: `campaigns/ledger/__init__.py`
- Modify: `campaigns/ledger/README.md`
- Modify: `campaigns/tests/test_ledger.py`

- [ ] **Step 1: Add failing tests for the real state vocabulary**

Assert that `observation_only` is accepted, promotion states require `promotion_allowed=true`, and non-promotion evidence states reject `promotion_allowed=true`. Assert that every current line in `campaigns/sfm_stream_001/events.jsonl` validates.

- [ ] **Step 2: Verify the new tests fail**

Run: `python3 -m unittest -v campaigns.tests.test_ledger`

Expected: `observation_only` is unknown in the old implementation.

- [ ] **Step 3: Implement one state vocabulary**

Add `observation_only` and `formal_observation` while retaining historical terminal statuses. Define `PROMOTION_STATUSES = {"promoted"}` and default missing `promotion_allowed` to false for backward compatibility. Reject promotion flag/state contradictions before appending. Add `validate_event` and `validate_ledger` read-only APIs and update the schema/README to the same vocabulary.

- [ ] **Step 4: Run ledger tests green**

Run: `python3 -m unittest -v campaigns.tests.test_ledger`

Expected: all current ledger lines and new state tests pass.

### Task 3: Migrate the twelve candidates and their Batch

**Files:**
- Create: `campaigns/sfm_stream_001/batches/book_imbalance_seed_v1.json`
- Create: `campaigns/sfm_stream_001/candidates/book_imbalance/book_imbalance_weighted_depth_imbalance_w16_v0_lag0.json`
- Create eleven sibling candidate JSON files matching `meta_config.h` names
- Create: `campaigns/contracts/consistency.py`
- Test: `campaigns/tests/test_book_imbalance_migration.py`

- [ ] **Step 1: Write failing migration tests**

Tests must prove:

1. exactly twelve candidate files exist;
2. every file passes Candidate validation;
3. candidate IDs exactly match C++ `kFactorNames`;
4. `(operator_id, window_events, variant, lag_events)` exactly matches `ideas/book_imbalance.json`;
5. canonical hashes are unique and recomputable;
6. the Batch lists exactly those twelve IDs and passes Batch validation;
7. every candidate points to the Batch, family idea and source streams used by its implementation.

- [ ] **Step 2: Verify migration tests fail because files are absent**

Run: `python3 -m unittest -v campaigns.tests.test_book_imbalance_migration`

Expected: missing Batch/candidate artifact failure.

- [ ] **Step 3: Implement canonical hashing and cross-artifact checks**

Public APIs:

```python
def canonical_candidate_payload(document: Mapping[str, object]) -> bytes: ...
def candidate_hash(document: Mapping[str, object]) -> str: ...
def validate_candidate_batch(candidate_paths: Sequence[Path], batch_path: Path, idea_path: Path, metadata_path: Path) -> None: ...
```

The canonical payload includes family, operator, parameters, state, availability and output semantics, but excludes commit and artifact paths. Hash format is `sha256:<hex>`.

- [ ] **Step 4: Write the Batch and twelve candidate artifacts**

Use batch ID `book_imbalance_seed_v1`, generation `0`, evidence level `L3`, parent list `[]`, source commit `b455935`, and direction policy `raw_signed`. Preserve the currently implemented operator/window/variant/lag grid exactly; record `operation` from `factor_entry.cpp` as a parameter so different formulas cannot collide under one canonical hash.

- [ ] **Step 5: Run migration tests green**

Run: `python3 -m unittest -v campaigns.tests.test_book_imbalance_migration`

Expected: twelve candidates and one Batch validate and match C++ metadata.

### Task 4: Migrate the 17-day pilot observations into Portraits

**Files:**
- Create: `campaigns/sfm_stream_001/portraits/pilot/book_imbalance/*.json`
- Modify: `campaigns/contracts/consistency.py`
- Modify: `campaigns/tests/test_book_imbalance_migration.py`

- [ ] **Step 1: Write failing portrait migration tests**

Assert one Portrait per Candidate, `scope=pilot`, `evidence_level=L3`, 17 dates, eight events, two labels, three universes, six signed metric slices, source receipt path/hash, `decision=observation_only`, and `promotion_allowed=false`. Compare all stored slice values with `multi-day-observation-20251009-20251031.json`.

- [ ] **Step 2: Verify the tests fail because Portraits are absent**

Run: `python3 -m unittest -v campaigns.tests.test_book_imbalance_migration`

Expected: portrait count mismatch.

- [ ] **Step 3: Write twelve Portrait artifacts from the immutable receipt**

Copy signed values without flipping or ranking. Store null where a metric is unavailable. Mark redundancy as `not_measured_in_receipt`, because the current observation receipt has no pairwise-correlation matrix. Do not create Experience records from this pilot.

- [ ] **Step 4: Run migration and complete campaign tests**

Run: `python3 -m unittest discover -v campaigns/tests`

Expected: all campaign contract, ledger and migration tests pass.

### Task 5: Document and verify the frozen boundary

**Files:**
- Create: `campaigns/contracts/README.md`
- Modify: `docs/因子挖掘框架.md`

- [ ] **Step 1: Document artifact ownership and allowed evidence transitions**

Explain that schema version 1 is frozen, compatibility changes require a new version, JSON Schema checks document shape, consistency checks prove cross-file lineage, and pilot Portraits cannot create promotion decisions. Link the concrete `book_imbalance` Batch and candidate directory from the framework.

- [ ] **Step 2: Run all Python tests and document checks**

Run:

```bash
python3 -m unittest discover -v campaigns/tests
python3 -m unittest discover -v evaluations/tests
git diff --check
rg -n "TODO|TBD" campaigns/contracts campaigns/sfm_stream_001 docs/因子挖掘框架.md
```

Expected: tests pass, diff check is clean, and the placeholder scan returns no matches.

- [ ] **Step 3: Commit and push the independently usable first stage**

```bash
git add campaigns docs/因子挖掘框架.md docs/superpowers/plans/2026-08-26-research-contracts-and-book-migration.md
git commit -m "feat: freeze research contracts and migrate book factors"
git push origin main
```

Expected: local and remote `main` point at the same commit.

## Follow-on implementation boundaries

This plan intentionally ends at a working, testable control-plane boundary. The remaining approved roadmap is implemented in three subsequent plans after this one passes:

1. three new mechanism families and thirty-six C++ seeds;
2. unified L2/L3 production and evaluation of all forty-eight seeds;
3. frozen formal-history dataset, L4 Portrait generation, Experience synthesis and next-Batch generation.

No L4 job is submitted and no factor is eliminated until the first two follow-on plans have passed their own acceptance checks.
