# L4 4D Validity Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Extend L4 evaluation to report independent `factor × event × universe × label × split` validity without reading holdout data or changing production outputs.

**Architecture:** Add a small, deterministic validity evaluator around the existing 12 Parquet result files and 969-day Arrow evidence. Each cell receives one explicit state and metrics are aggregated only from `pass` cells. Keep the existing strict data-contract checks and v1 behavior; write a versioned L4 readiness receipt and expose cell-level data to portrait generation without enabling promotion.

**Tech Stack:** Python 3.8, pandas, pyarrow, numpy, unittest, JSON manifests, SHA-256.

---

### Task 1: Add failing unit tests for cell-state classification

**Files:**
- Modify: `evaluations/tests/test_l4_portrait.py`
- Modify: `evaluations/l4_portrait.py`

- [ ] **Step 1: Add fixture tests for independent cell states.** Add tests that construct two events, two universes and two labels where one factor/event cell has no finite metric values, one has an explicit readiness mask marked false, one has zero variance, and the remaining cells pass. Assert the returned cell keys include `split`, `factor`, `event`, `universe`, and `label`, and statuses are respectively `metric_undefined`, `not_ready`, `metric_undefined`, and `pass`.

- [ ] **Step 2: Add a test proving zero is not not-ready.** Put a real numeric zero in a ready cell and assert the cell is not classified as `not_ready`; put a false readiness flag beside another zero and assert only that cell is `not_ready` with `not_ready_zero_count=1`.

- [ ] **Step 3: Run the focused tests and verify RED.** Run `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_l4_portrait -v`. Expected: the new tests fail because no cell-level validity API exists.

- [ ] **Step 4: Commit only the failing tests.**

```bash
git add evaluations/tests/test_l4_portrait.py
git commit -m "test: specify l4 four-dimensional validity cells"
```

### Task 2: Implement cell-level validity and aggregation

**Files:**
- Modify: `evaluations/l4_portrait.py`
- Modify: `evaluations/tests/test_l4_portrait.py`

- [ ] **Step 1: Implement `classify_validity_cell`.** Add a pure helper accepting factor values, metric series, optional readiness values, date count, and thresholds. It must return one of the six frozen states, preserve finite/zero/readiness counts, and never infer `not_ready` from a zero value.

- [ ] **Step 2: Implement `build_validity_matrix`.** Add a helper that iterates every split, factor, event, label and universe from validated frames, computes one cell per Cartesian product, and returns deterministic ordering. Missing readiness columns must produce `readiness_source="not_provided"` and may only yield `metric_undefined`/`review`, never `not_ready`.

- [ ] **Step 3: Implement pass-only summaries.** Add `summarize_validity_matrix` returning event, universe, label and split summaries. Exclude `not_ready` and `metric_undefined` from means, IR and portfolio metrics; retain counts and reasons. Mark a factor `insufficient_scope` when fewer than 6 events have broad pass coverage, and `observation_failure` when training passes but observation has no pass evidence.

- [ ] **Step 4: Run focused tests to GREEN.** Run `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_l4_portrait -v` and ensure all existing tests plus new cell-state tests pass.

- [ ] **Step 5: Commit implementation.**

```bash
git add evaluations/l4_portrait.py evaluations/tests/test_l4_portrait.py
git commit -m "feat: add l4 four-dimensional validity matrix"
```

### Task 3: Add holdout and contract regression coverage

**Files:**
- Modify: `evaluations/tests/test_l4_portrait.py`
- Modify: `evaluations/l4_portrait.py`
- Modify: `campaigns/sfm_stream_001/manifests/formal-history-v2-evaluation-receipt.json`

- [ ] **Step 1: Add holdout rejection tests.** Assert a 2025 date in any split raises a holdout error and that no path under `formal-history-holdout-dates-v2.txt` is accepted by the matrix builder.

- [ ] **Step 2: Add matrix completeness tests.** Assert every requested combination has exactly one cell, event presence is still required, duplicate date/event indexes fail, and `data_error` remains a hard stop.

- [ ] **Step 3: Add a versioned receipt field.** Extend the evaluation receipt with `validity_granularity="factor_event_universe_label_split"`, `readiness_policy="explicit_only"`, `promotion_allowed=false`, and a `validity_matrix_path` pointing to the generated report. Preserve the original Parquet paths and hashes.

- [ ] **Step 4: Run the focused tests and commit.**

```bash
/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_l4_portrait evaluations.tests.test_pilot_postprocess -q
git add evaluations/tests/test_l4_portrait.py evaluations/l4_portrait.py campaigns/sfm_stream_001/manifests/formal-history-v2-evaluation-receipt.json
git commit -m "test: enforce l4 validity matrix isolation"
```

### Task 4: Generate the current 4D report without portraits or promotion

**Files:**
- Create: `campaigns/sfm_stream_001/manifests/formal-history-v2-validity-matrix.json`
- Modify: `evaluations/l4_portrait.py` CLI if needed
- Create: `docs/superpowers/plans/` execution evidence only if required by the runner

- [ ] **Step 1: Run the matrix builder against the existing 12 Parquet files and 969 Arrow files.** Use only training dates 20210104–20221230 and observation dates 20230103–20241231; pass the production date-list SHA and the existing evaluator/candidate contracts.

- [ ] **Step 2: Verify report invariants.** Require 48 factors, 8 events, 3 universes, 2 labels, 2 splits, 48×8×3×2×2 = 4608 cells, no 2025 date, no duplicate cell key, and zero `data_error` cells. Record counts by status and affected early events.

- [ ] **Step 3: Keep the decision observation-only.** Write `decision="observation_only"`, `promotion_allowed=false`, and `portraits_allowed=false`; do not modify Candidate evidence levels, Batch statuses or Experience Records.

- [ ] **Step 4: Commit the report and receipt update.**

```bash
git add campaigns/sfm_stream_001/manifests/formal-history-v2-validity-matrix.json campaigns/sfm_stream_001/manifests/formal-history-v2-evaluation-receipt.json
git commit -m "test: record l4 four-dimensional validity report"
```

### Task 5: Full verification and handoff

**Files:**
- No additional source files unless a test exposes a regression.

- [ ] **Step 1: Run repository tests.**

```bash
python3 -m unittest discover -q campaigns/tests
/usr/local/python3.8.10/bin/python3 -m unittest discover -q evaluations/tests
ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure
git diff --check
```

- [ ] **Step 2: Review holdout isolation.** Confirm no command, report or receipt references a 2025 result path, and confirm `holdout_market_data_read`, `holdout_factor_data_read`, and `holdout_labels_read` remain false.

- [ ] **Step 3: Push the final branch.**

```bash
git push origin feat/l4-recent-history
```

- [ ] **Step 4: Report status.** State the cell-state counts, affected events/families, test results, and that no factor was promoted or eliminated.
