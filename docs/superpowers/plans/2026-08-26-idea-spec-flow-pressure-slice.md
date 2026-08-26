# Idea Spec and Flow Pressure Vertical Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a machine-verifiable Idea Spec contract and one `flow_pressure` representative candidate that proves Trade input, signed-flow semantics, causality, and Candidate lineage before expanding the family.

**Architecture:** Idea Specs are immutable L0/L1 research inputs under a campaign's `ideas/` directory. A representative Candidate references the Idea Spec and uses the existing FactorEntry interface. The first implementation stores signed trade pressure in a bounded event window and emits a finite raw-signed value at scheduled snapshots.

**Tech Stack:** JSON Schema Draft 7, Python 3.10/3.8, `jsonschema`, C++17, existing `FactorEntryBase`, `unittest`, CTest.

---

### Task 1: Freeze the Idea Spec contract

**Files:**
- Create: `campaigns/contracts/idea_spec.schema.json`
- Create: `campaigns/sfm_stream_001/ideas/flow_pressure.json`
- Modify: `campaigns/contracts/__init__.py`
- Test: `campaigns/tests/test_contracts.py`

- [ ] **Step 1: Write failing tests**

Add a valid `flow_pressure` Idea Spec fixture and assert it validates. Add negative tests for an empty hypothesis, an unsupported input field, and a missing falsifiable condition.

- [ ] **Step 2: Verify red**

Run: `python3 -m unittest -v campaigns.tests.test_contracts`

Expected: `idea_spec` is an unknown contract kind.

- [ ] **Step 3: Implement the schema and fixture**

The contract must require: mechanism, falsifiable condition, source streams/fields, operator definitions, parameter space, state/warmup, availability, invalid-value policy, expected sign as a hypothesis only, and implementation constraints. The fixture must define three operators and four windows but mark only one candidate as `representative_candidate_id`.

- [ ] **Step 4: Run green**

Run: `python3 -m unittest -v campaigns.tests.test_contracts`

Expected: all contract tests pass.

### Task 2: Add the flow-pressure representative Candidate

**Files:**
- Create: `campaigns/sfm_stream_001/batches/flow_pressure_seed_v1.json`
- Create: `campaigns/sfm_stream_001/candidates/flow_pressure/flow_pressure_signed_trade_flow_w16.json`
- Create: `campaigns/sfm_stream_001/ideas/flow_pressure.json`
- Modify: `campaigns/contracts/consistency.py`
- Test: `campaigns/tests/test_flow_pressure_migration.py`

- [ ] **Step 1: Write failing migration tests**

Assert the Idea Spec, Batch, and Candidate validate; the candidate references the Idea Spec; its operator is `signed_trade_flow`; its stream is `trade`; its parameters are `window_events=16`, `normalization=signed_total_volume`, `include_cancels=false`; its canonical hash recomputes; and the Batch lists exactly one candidate with budget one.

- [ ] **Step 2: Verify red**

Run: `python3 -m unittest -v campaigns.tests.test_flow_pressure_migration`

Expected: missing contract/artifact failure.

- [ ] **Step 3: Implement consistency checks and artifacts**

Extend canonical candidate hashing to include `idea_spec_id` through the existing `lineage.idea_path` and validate that `source_streams` and operator agree with the Idea Spec. Preserve `raw_signed`; no direction flip is allowed.

- [ ] **Step 4: Run green**

Run: `python3 -m unittest -v campaigns.tests.test_flow_pressure_migration`

Expected: all migration tests pass.

### Task 3: Implement the C++ representative factor with TDD

**Files:**
- Create: `base/hf-open5m-factor-demo/factors/flow_pressure/meta_config.h`
- Create: `base/hf-open5m-factor-demo/factors/flow_pressure/factor_entry.h`
- Create: `base/hf-open5m-factor-demo/factors/flow_pressure/factor_entry.cpp`
- Create: `base/hf-open5m-factor-demo/factors/flow_pressure/CMakeLists.txt`
- Create: `base/hf-open5m-factor-demo/tests/flow_pressure/factor_entry_test.cpp`

- [ ] **Step 1: Write failing C++ semantic tests**

Cover: buy volume produces positive output, sell volume produces negative output, equal buy/sell volume is near zero, cancel trades are ignored, invalid/zero volume is finite zero, and a new trading-day instance does not inherit prior state.

- [ ] **Step 2: Verify red**

Run the module test through the existing CMake test target. Expected: missing `factors_flow_pressure` target or missing header.

- [ ] **Step 3: Implement minimal event-window factor**

Maintain a deque of signed trade volumes and a running signed sum/absolute volume sum. On each valid trade, append `+volume` for `bsflag='B'`, `-volume` for `bsflag='S'`, ignore `trade_type='C'` and non-positive volume. Drop entries beyond 16 trade events. Emit `signed_sum / max(abs_sum, 1)` and finite-zero on no valid trades. Do not use future events or wall-clock labels.

- [ ] **Step 4: Run C++ tests green**

Run the focused CTest target and the existing book-imbalance test. Expected: both pass.

### Task 4: Add causality and determinism checks

**Files:**
- Modify: `base/hf-open5m-factor-demo/tests/flow_pressure/factor_entry_test.cpp`
- Modify: `campaigns/tests/test_flow_pressure_migration.py`

- [ ] **Step 1: Add prefix-invariance test**

Feed an identical prefix into two entries, then append divergent future trades to only one entry. Assert values from all snapshots in the prefix are identical.

- [ ] **Step 2: Add finite/deterministic migration assertions**

Run candidate hash twice and assert the same hash; assert the output name equals C++ metadata and the Candidate direction remains `raw_signed`.

- [ ] **Step 3: Run all focused tests**

Run:

```bash
python3 -m unittest -v campaigns.tests.test_contracts campaigns.tests.test_flow_pressure_migration

cmake -S base/hf-open5m-factor-demo -B base/hf-open5m-factor-demo/build-flow-pressure \
  -DBUILD_APP_LIVE=OFF -DBUILD_APP_FACTOR=ON -DBUILD_APP_MODEL=OFF -DBUILD_TESTING=ON \
  -DCMAKE_CXX_STANDARD=17
cmake --build base/hf-open5m-factor-demo/build-flow-pressure --target flow_pressure_factor_entry_test book_imbalance_factor_entry_test -j2
ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure -R '(flow_pressure_factor_entry|book_imbalance_factor_entry)' --output-on-failure
```

Expected: focused Python and C++ tests pass.

### Task 5: Record the vertical-slice boundary

**Files:**
- Create: `campaigns/sfm_stream_001/manifests/flow-pressure-vertical-slice.json`
- Modify: `docs/因子挖掘框架.md`

- [ ] **Step 1: Write the L2 receipt**

Record the source commit, Candidate hash, C++ target, test command, and `promotion_allowed=false`; explicitly state that no real-data L3 run has occurred yet.

- [ ] **Step 2: Update framework status**

Mark Idea Spec contract complete and `flow_pressure` representative as L2-in-progress; leave the full 48-candidate L2/L3 and L4 formal-history stages pending.

- [ ] **Step 3: Verify and commit**

Run all focused tests plus `git diff --check`, then commit and push:

```bash
git add campaigns base/hf-open5m-factor-demo/factors/flow_pressure base/hf-open5m-factor-demo/tests/flow_pressure docs/因子挖掘框架.md docs/superpowers/plans/2026-08-26-idea-spec-flow-pressure-slice.md
git commit -m "feat: add idea spec and flow pressure vertical slice"
git push origin main
```
