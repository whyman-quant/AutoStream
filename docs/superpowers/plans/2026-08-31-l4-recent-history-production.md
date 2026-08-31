# L4 Recent-History Production Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze `formal_history_v2`, prove one-pass 48-factor production on five representative dates, and submit the 2021–2024 L4 production set with holdout-safe bounded concurrency.

**Architecture:** v2 is an immutable subset of v1 with separate production and sealed-holdout date lists. One frozen executable replays each date once with four enabled factor families; strict post-processing validates the 48-column output. A deterministic production planner creates five-date chunks in four dependency lanes and refuses any date outside the v2 production list.

**Tech Stack:** Python 3.10 contract/planner tests, Python 3.8 h5py/pyarrow validation, C++ factor executable, JSON manifests/configuration, Slurm `mybatch`/`afterok`, Git SHA-256 provenance.

---

### Task 1: Freeze formal-history v2 and switch the campaign active dataset

**Files:**
- Create: `campaigns/sfm_stream_001/manifests/formal-history-dataset-v2.json`
- Create: `campaigns/sfm_stream_001/manifests/formal-history-production-dates-v2.txt`
- Create: `campaigns/sfm_stream_001/manifests/formal-history-holdout-dates-v2.txt`
- Modify: `campaigns/sfm_stream_001/campaign.json`
- Modify: `campaigns/tests/test_formal_history_dataset.py`

- [ ] **Step 1: Add failing v2 subset and sealing tests**

Add tests that load v1 and v2, assert production dates equal the v1 dates from `20210104` through `20241231`, assert counts `485 + 484 = 969`, assert the holdout list is exactly the 228 dates from `20250102` through `20251210`, and assert the two sets do not intersect. Assert `campaign.json` points to v2 while v1 remains present.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
python3 -m unittest campaigns.tests.test_formal_history_dataset -v
```

Expected: failure because v2 files and active campaign fields do not exist.

- [ ] **Step 3: Generate and freeze the two date lists**

Derive both lists only from `formal-history-dates-v1.txt`; write one `YYYYMMDD` per line with a trailing newline. Store SHA-256 values, boundaries, counts, split purposes, `formal_production_started=false`, `promotion_allowed=false`, and `holdout_access=sealed_until_l6` in v2.

- [ ] **Step 4: Switch only the active dataset pointer**

Set `formal_history_dataset_manifest` and `formal_history_dataset_id` in `campaign.json` to v2. Preserve v1 unchanged and record it in v2 as `parent_dataset_id`.

- [ ] **Step 5: Run the focused tests and verify GREEN**

Run the same unittest command and require all v1/v2 tests to pass.

- [ ] **Step 6: Commit the dataset freeze**

```bash
git add campaigns/sfm_stream_001/campaign.json campaigns/sfm_stream_001/manifests/formal-history-*v2* campaigns/tests/test_formal_history_dataset.py
git commit -m "chore: freeze recent-history dataset for l4"
git push origin main
```

### Task 2: Freeze the one-pass 48-factor configuration

**Files:**
- Create: `base/hf-open5m-factor-demo/config_factor_sfm_stream_001_formal_history_v2.json`
- Create: `campaigns/tests/test_l4_formal_history_config.py`

- [ ] **Step 1: Add a failing configuration contract test**

The test must concatenate `candidate_ids` from the four Batch files in this order:

```python
FAMILIES = [
    "book_imbalance",
    "flow_pressure",
    "liquidity_resilience",
    "impact_efficiency",
]
```

Assert 48 unique names. Assert the JSON enables those four factor sets in the same order, uses eight frozen save times, uses `thread_num=8`, and writes to `data/sfm_autoresearch_001/formal-history-v2/[DATE]/all_families`.

- [ ] **Step 2: Run the focused test and verify RED**

```bash
python3 -m unittest campaigns.tests.test_l4_formal_history_config -v
```

Expected: failure because the configuration file does not exist.

- [ ] **Step 3: Create the minimal unified configuration**

Use the existing market root and code-list template. Enable exactly the four approved families; do not enable `demofw00`. Use the source events represented by save times `92700,100000,103000,110000,113000,133000,140000,143000`.

- [ ] **Step 4: Run the focused test and verify GREEN**

Require the configuration contract test to pass.

- [ ] **Step 5: Build and verify the executable**

Run:

```bash
cmake --build base/hf-open5m-factor-demo/build-flow-pressure --target main -j2
ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure
base/hf-open5m-factor-demo/build-flow-pressure/app_factor/main --version
```

Require all four family tests to pass and all four family registrations to appear.

- [ ] **Step 6: Commit the configuration and test**

```bash
git add base/hf-open5m-factor-demo/config_factor_sfm_stream_001_formal_history_v2.json campaigns/tests/test_l4_formal_history_config.py
git commit -m "chore: freeze unified l4 factor config"
git push origin main
```

### Task 3: Add strict 48-factor preflight validation

**Files:**
- Create: `evaluations/l4_preflight.py`
- Create: `evaluations/tests/test_l4_preflight.py`

- [ ] **Step 1: Write failing validation tests**

Test a fixture with four events and four expected names, then reject wrong factor order, missing event, inconsistent stock count, non-finite value, duplicate symbol, wrong Arrow rows, and duplicate `(symbol,event)` keys. Test that requested dates must belong to the v2 production list.

- [ ] **Step 2: Run tests and verify RED**

```bash
/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_l4_preflight -v
```

Expected: import failure because `evaluations.l4_preflight` does not exist.

- [ ] **Step 3: Implement the validator by composing existing strict helpers**

Reuse `inspect_hdf5`, `convert_hdf5`, and `validate_arrow`. Add `expected_factor_names` comparison and emit a JSON summary containing date, stock count, rows, event count, factor count, names, finite/unique flags, and input/output SHA-256.

- [ ] **Step 4: Run tests and verify GREEN**

Require all new tests and existing `evaluations/tests` to pass.

- [ ] **Step 5: Commit the validator**

```bash
git add evaluations/l4_preflight.py evaluations/tests/test_l4_preflight.py
git commit -m "test: add strict l4 preflight validation"
git push origin main
```

### Task 4: Add a holdout-safe deterministic production planner

**Files:**
- Create: `campaigns/l4_production.py`
- Create: `campaigns/tests/test_l4_production.py`

- [ ] **Step 1: Write failing planner tests**

Test `chunk_dates(dates, size=5)`, `assign_lanes(chunks, lane_count=4)`, and command generation. Assert 969 production dates become 194 chunks, each date appears exactly once, no chunk contains a holdout date, lane count is at most four, and every non-first chunk in a lane depends on the previous job with `afterok`.

- [ ] **Step 2: Run tests and verify RED**

```bash
python3 -m unittest campaigns.tests.test_l4_production -v
```

Expected: import failure because the planner does not exist.

- [ ] **Step 3: Implement pure planning functions and a dry-run CLI**

The CLI reads v2 production dates and writes a JSON plan. It must not accept arbitrary date ranges. Each job command runs a fixed release binary for the chunk dates sequentially and invokes strict HDF5 validation after each date. Existing valid output is skipped; existing invalid output exits nonzero without deleting it.

- [ ] **Step 4: Add explicit submission mode**

Only `--submit` may call `mybatch`. Use 12 CPU, 243G, `cpu_wgh`, two-hour time limit, four dependency lanes, and write returned job IDs into a submission receipt. Dry-run remains the default.

- [ ] **Step 5: Run tests and verify GREEN**

Require planner tests and all campaign tests to pass.

- [ ] **Step 6: Commit the planner**

```bash
git add campaigns/l4_production.py campaigns/tests/test_l4_production.py
git commit -m "feat: add bounded l4 production planner"
git push origin main
```

### Task 5: Freeze a clean release and run the five-date preflight

**Files:**
- Create after completion: `campaigns/sfm_stream_001/manifests/formal-history-v2-preflight.json`
- Local ignored release: `base/hf-open5m-factor-demo/release/formal-history-v2-<commit>/`

- [ ] **Step 1: Run full pre-release verification**

```bash
python3 -m unittest discover -q campaigns/tests
/usr/local/python3.8.10/bin/python3 -m unittest discover -q evaluations/tests
ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure
git diff --check
```

- [ ] **Step 2: Freeze release artifacts**

Copy the verified executable and unified config into a commit-named ignored release directory. Mark the executable read/execute-only and config read-only. Record binary, config, converter, evaluator, dataset manifest, and date-list SHA-256.

- [ ] **Step 3: Submit only the five approved preflight dates**

Submit `20210104,20220301,20221230,20230103,20241231` as a success-dependent chain. Do not submit any other production date. Attach one strict conversion job after the fifth date.

- [ ] **Step 4: Wait for completion and validate results**

Require every Slurm job to be `COMPLETED 0:0`. Run `evaluations.l4_preflight` across all five dates and require 48 factors, eight events, finite values, unique keys, exact names/order, and valid Arrow rows.

- [ ] **Step 5: Write and commit the preflight receipt**

The receipt records dates, jobs, per-date stock/row counts, runtime, SHA values, strict checks, `decision=continue_to_bulk_l4` only when all checks pass, and `promotion_allowed=false`.

### Task 6: Submit the 969-day production only after preflight acceptance

**Files:**
- Create: `campaigns/sfm_stream_001/manifests/formal-history-v2-production-plan.json`
- Create after submission: `campaigns/sfm_stream_001/manifests/formal-history-v2-production-submission.json`

- [ ] **Step 1: Generate and audit the dry-run plan**

Require 969 dates, 194 chunks, four lanes, first date `20210104`, last date `20241231`, and zero 2025 dates. Store release SHA values and expected output paths.

- [ ] **Step 2: Submit the audited plan**

Run the planner with `--submit` only after the preflight receipt says `continue_to_bulk_l4`. Record every returned job ID and dependency edge.

- [ ] **Step 3: Verify scheduler state**

Confirm at most four jobs are runnable/running from this production, all other jobs are pending on valid dependencies, and no holdout job exists.

- [ ] **Step 4: Commit and push the production receipts**

Commit the plan and submission receipt without committing HDF5/Arrow/log artifacts.

### Task 7: Long-history post-processing and L4 portrait handoff

**Files:**
- Create after production: `campaigns/sfm_stream_001/manifests/formal-history-v2-production-completion.json`
- Create after evaluation: `campaigns/sfm_stream_001/manifests/formal-history-v2-evaluation-receipt.json`

- [ ] **Step 1: Strictly validate all 969 HDF5 files and convert Arrow**

Fail if any date, event, factor name/order, finite-value check, or unique key fails. Record total rows and per-date hashes.

- [ ] **Step 2: Run six evaluations without holdout**

Evaluate the unified 48-factor Arrow root for `raw926/ease926 × 000985/003800/000906`, separately summarize training and observation, and verify each result contains the exact expected date/event index.

- [ ] **Step 3: Hand off to portrait generation**

Mark the L4 data/evaluation layer `formal_observation`, `promotion_allowed=false`; do not update candidates beyond L4 until formal portraits and Experience Records are generated and reviewed.

---

## Global completion checks

Before any completion claim or bulk submission, run:

```bash
python3 -m unittest discover -q campaigns/tests
/usr/local/python3.8.10/bin/python3 -m unittest discover -q evaluations/tests
ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure
git diff --check
git status --short
```

No task may access, produce, convert, evaluate, or summarize a 2025 holdout date.
