# L4 End-to-End Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不读取 2025 留出集的前提下，验收 AutoStream 从 48 个冻结 Candidate 到 L4 Portrait、Experience Record 和下一轮 Candidate/Batch 的完整流程、工具调用、数据内容和防泄漏约束。

**Architecture:** 先用隔离 release 和 fixture 验证工具链，再用五日期 real-data preflight，最后才允许 969 个训练/观察日期正式生产。L4 只读取 training/observation，依次经过 HDF5、readiness-aware Arrow、标签评价、Portrait；holdout 由路径和权限检查封存，Portrait review 后才允许写 Experience 和下一 Batch。

**Tech Stack:** C++ factor binary, HDF5, Arrow/Feather, Python 3.8, Slurm/mybatch, JSON contracts, SHA256, unittest, CTest.

---

### Task 1: Freeze and audit the L4 input contract

**Files:**
- Read: `campaigns/sfm_stream_001/manifests/formal-history-dataset-v2.json`
- Read: `campaigns/sfm_stream_001/manifests/formal-history-production-dates-v2.txt`
- Read: `campaigns/sfm_stream_001/manifests/formal-history-holdout-dates-v2.txt`
- Read: the four `*_seed_v1.json` Batches
- Test: `campaigns/tests/test_l4_production.py`, `evaluations/tests/test_l4_preflight.py`

- [ ] **Step 1: Verify split counts, hashes, disjointness, and leakage policy.**

    Run `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_l4_preflight campaigns.tests.test_l4_production -q`, then assert dataset v2 has 969 production dates, 485 training dates, 484 observation dates, 228 holdout dates, matching date-list SHA256 values, disjoint production/holdout lists, and `holdout_may_read=[]`.

    Expected: all tests pass and no frozen manifest/date list changes.

- [ ] **Step 2: Verify the exact formal candidate set.**

    Load `book_imbalance_seed_v1`, `flow_pressure_seed_v1`, `liquidity_resilience_seed_v1`, and `impact_efficiency_seed_v1`; concatenate `candidate_ids`; assert exactly 48 unique IDs. Assert no `opening_v2` or `liquidity_resilience_v2` ID is mixed into this L4 set.

    Expected: `48 seed candidates ok`; v2 readiness Pilots remain separate evidence.

- [ ] **Step 3: Verify frozen provenance.**

    Recompute SHA256 for the binary, L4 config, runner modules, four Batches, label contract, methodology, and date lists. Store the values in the preflight and submission receipts before any Slurm submission.

### Task 2: Validate planner, release isolation, and dependency graph

**Files:**
- Read: `campaigns/l4_production.py`; modify only when a listed acceptance test exposes a defect
- Test: `campaigns/tests/test_l4_production.py`
- Runtime: `/mnt/beegfs_ssd_raid91/AutoStream-l4-releases/formal-history-v2-5d5fffe796615c85ce878e1eac2a09728ffbb0bc`
- Runtime: `/mnt/beegfs_ssd_raid91/AutoStream-l4-submissions/formal-history-v2-5d5fffe796615c85ce878e1eac2a09728ffbb0bc`

- [ ] **Step 1: Build a non-submitting plan from the frozen release.**

    Run `/usr/local/python3.8.10/bin/python3 -m campaigns.l4_production plan` with the release campaign root, frozen binary/config, output root, runner root, submission workdir, and plan output path; do not pass `--submit`.

    Expected: `chunk_size=5`, `lane_count=4`, `date_count=969`, `chunk_count=194`; all planned dates are before 20250101; runner root has no Git ancestor and no holdout path.

- [ ] **Step 2: Check dependencies and resources.**

    Parse `plan.json`; for each lane assert every chunk depends on the previous chunk in that lane. Assert each job requests 12 CPUs, 256G input memory on `cpu_wgh`, and carries binary/config/date-list/runner provenance.

- [ ] **Step 3: Run planner failure injection.**

    In temporary copies, mutate binary hash, config hash, date-list hash, duplicate dates, a holdout date, runner root, and dependency receipt. Run planner tests and `run_chunk`.

    Expected: each mutation stops with a specific validation error and leaves the frozen release unchanged.

### Task 3: Complete the five-date real-data preflight

**Files:**
- Read: `campaigns/sfm_stream_001/manifests/formal-history-v2-preflight.json`
- Run: `evaluations/l4_preflight.py`
- Run: `evaluations/pilot_postprocess.py`

- [ ] **Step 1: Produce exactly the frozen dates.**

    Use `20210104,20220301,20221230,20230103,20241231`, the frozen binary/config hashes, and the eight source events.

- [ ] **Step 2: Validate HDF5 and Arrow content.**

    For every date assert 48 factors, 8 events, matching codelists, no duplicate symbols, binary readiness matrices with matching shape, finite values for `ready=true`, and no Inf. Arrow must contain the 8 mapped evaluation events, 48 factors, matching `ready_<factor>` columns, `stock_count*8` rows, and unique `(symbol,event)`.

- [ ] **Step 3: Verify the preflight receipt.**

    Assert `holdout_read=false`, `date_count=5`, `factor_count=48`, `event_count=8`, all hashes match, and `decision=continue_to_bulk_l4`. Remove one event, duplicate a symbol, inject a non-finite ready value, and change one factor name in temporary copies; each must fail validation.

### Task 4: Produce and validate formal training/observation data

**Files:**
- Run: `campaigns/l4_production.py plan --submit`
- Run: `campaigns/l4_production.py run-chunk`
- Run: `evaluations/l4_preflight.py`
- Run: `evaluations/pilot_postprocess.py`

- [ ] **Step 1: Submit the frozen plan exactly once.**

    Record the 194 chunk Job IDs and four lane dependency chains in `formal-history-v2-bulk-submission.json`; do not replace the planner with hand-written Slurm commands.

- [ ] **Step 2: Validate every HDF5 before conversion.**

    For all 969 dates assert exact 48-factor order, exact 8 source events, event/codelist row alignment, no duplicate symbols, readiness shape/binary values, finite `ready=true` values, no Inf, positive stock counts, and zero unexplained job failures. Record per-date HDF5 SHA, stock count, event rows, readiness counts, Job ID and exit code.

- [ ] **Step 3: Convert and validate every Arrow file.**

    Require 969 Arrow files, 8 mapped evaluation events, 48 factors plus matching readiness columns, rows equal to `stock_count*8`, unique `(symbol,event)), and finite values wherever ready. Compare HDF5 and Arrow values for at least one date per year and every event; preserve the documented 09:27→09:26 mapping and never replace unavailable values with real zero.

### Task 5: Run leakage-safe labels and build the evaluation receipt

**Files:**
- Add: `evaluations/run_l4_evaluation.py` (explicit frozen-date-list runner)
- Read: `evaluations/label_contract.json`
- Run: readiness-aware `/home/fangwei/mnt-ssd/factor_eval_toolkit/scripts/evaluate_factors.py`
- Run: `evaluations/real_label_scorecard.py`

- [ ] **Step 1: Force evaluation to consume frozen date lists.**

    The runner accepts `--date-list`, rejects dates outside it, writes each of the six explicit panel paths (`training/raw926/000985`, `training/raw926/003800`, `training/raw926/000906`, `training/ease926/000985`, `training/ease926/003800`, `training/ease926/000906`) and the corresponding six observation paths, passes `ready_<factor>` masks, excludes unavailable values, preserves real zeros, and never opens holdout files.

- [ ] **Step 2: Evaluate both splits.**

    Run 12 panels per split: `raw926/ease926` × `000985/003800/000906`. Each result has exactly `split_date_count*8` rows, fixed `(date,event)` index, 48×14 columns (`D1-D10, LS, Monotonicity, IC, RankIC)), and identical factor/metric order.

- [ ] **Step 3: Test readiness and label failure semantics.**

    In a temporary Arrow file, give one `ready=false` value a finite compatibility number and one `ready=true` value NaN. Expected: the first is excluded from metric denominators; the second is rejected. Missing labels are dropped and counted, never imputed.

- [ ] **Step 4: Write the evaluation receipt.**

    Record evaluator/label/date-list/factor hashes, every panel path/SHA/row count, factor and event sets, coverage, tradability exclusions, and `holdout_read=false`.

### Task 6: Generate and review all 48 Factor Portraits

**Files:**
- Run: `evaluations/l4_portrait.py`
- Read: `campaigns/sfm_stream_001/manifests/formal-history-v2-portrait-methodology.json`
- Output: `campaigns/sfm_stream_001/portraits/formal_history_v2/`

- [ ] **Step 1: Build exactly 48 portraits.**

    Use the frozen dataset manifest, date list, Arrow root, candidate root and 48-factor list. The builder must reject holdout results, missing panels, factor order changes, coverage below 95%, non-finite metrics, and missing lineage.

- [ ] **Step 2: Validate portrait fields.**

    Every portrait must have `evidence_level=L4`, `scope=formal_history`, `direction=raw_signed`, `promotion_allowed=false`, training/observation statistics, year/quarter slices, rolling stability, parameter neighbors, peer/value correlations, anomaly days, and complete provenance hashes.

- [ ] **Step 3: Review without mutating the Batch.**

    Produce a review table with mean/std/IR, positive fraction, split sign agreement, event/state coverage, anomaly contribution and redundancy clusters. Weak signal or high correlation is evidence, not an in-run deletion rule.

### Task 7: Generate Experience Records and the next Batch only after review

**Files:**
- Add: `campaigns/sfm_stream_001/experience/l4-formal-history-review-v1.json`
- Add: `campaigns/sfm_stream_001/batches/l4-formal-history-next-v1.json`
- Add: generated candidate JSON files under `campaigns/sfm_stream_001/candidates/` using IDs emitted by the deterministic generator

- [ ] **Step 1: Write fact-based Experience Records.**

    Each record includes source portrait IDs/SHA, dataset/evaluator hashes, condition, observed fact, interpretation, uncertainty and bounded next hypothesis. It must not reduce evidence to success/failure or read holdout metrics.

- [ ] **Step 2: Validate experience lineage.**

    Require every source portrait to be L4 with `promotion_allowed=false`; require every referenced artifact and SHA to exist. Reject missing portraits, holdout sources and unsupported conclusions.

- [ ] **Step 3: Generate the next Candidate/Batch deterministically.**

    Require a signed portrait-review receipt before generation. Use only reviewed training/observation evidence and explicit mechanism/parameter changes; recompute canonical hashes; enforce unique IDs, parent portrait/experience IDs, Batch budget and C++ metadata consistency.

### Task 8: Final acceptance and stop conditions

**Files:**
- Test: `campaigns/tests/`, `evaluations/tests/`
- Read: `campaigns/sfm_stream_001/manifests/formal-history-dataset-v2.json`

- [ ] **Step 1: Run the complete suite.**

    Run `python3 -m unittest discover -q campaigns/tests`, `/usr/local/python3.8.10/bin/python3 -m unittest discover -q evaluations/tests`, `ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure`, and `git diff --check`.

    Expected: all pass and the worktree is clean.

- [ ] **Step 2: Verify hard stops and immutability.**

    Test changed input/evaluator/label hashes, missing event, duplicate key, non-finite ready value, timestamp lookahead, holdout access, missing Job, and missing evaluation panel. Each must stop the relevant stage and preserve prior artifacts.

- [ ] **Step 3: Apply pause conditions.**

    Pause without deleting factors when state coverage is below minimum, any metric coverage is below 95%, missing required rows exceed 1%, redundancy makes the search space excessive, or training/observation signs disagree. L4 completion requires 48 complete portraits, lineage and anomaly records; promotion remains forbidden.

- [ ] **Step 4: Commit the acceptance evidence.**

    Commit manifests, receipts, tests and reviewed tooling; push to `origin/feat/l4-recent-history`. Merge to `main` only after review. The final receipt must include all production/evaluation Job IDs, artifact hashes, `holdout_read=false`, portrait-review status, Experience IDs, next Batch ID if generated, and the reason for continuing or pausing L4.
