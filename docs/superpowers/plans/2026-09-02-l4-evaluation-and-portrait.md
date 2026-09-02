# L4 Evaluation and Portrait Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Strictly certify the 2021–2024 evaluation outputs, generate 48 reproducible L4 portraits and evidence-backed Experience Records, and close L4 without reading the 2025 holdout.

**Architecture:** Twelve external evaluation jobs produce separate training/observation results for two labels and three universes. A repository-owned validator rejects incomplete date/event/factor/metric grids, then a versioned methodology drives deterministic portrait and experience generation. Existing L3 v1 artifacts remain valid; L4 uses new versioned contracts and remains `formal_observation` with promotion forbidden.

**Tech Stack:** Python 3.8, pandas/pyarrow, JSON Schema draft-07, Slurm `mybatch`, Parquet, unittest, SHA-256 provenance.

---

### Task 1: Freeze the L4 portrait methodology and v2 contracts

**Files:**
- Create: `campaigns/sfm_stream_001/manifests/formal-history-v2-portrait-methodology.json`
- Create: `campaigns/contracts/factor_portrait_v2.schema.json`
- Create: `campaigns/contracts/experience_record_v2.schema.json`
- Modify: `campaigns/contracts/__init__.py`
- Modify: `campaigns/tests/test_contracts.py`

- [ ] Write failing tests that require schema-version dispatch and reject L4 promotion or holdout slices.
- [ ] Run `python3 -m unittest campaigns.tests.test_contracts -v` and confirm the new tests fail because v2 schemas are absent.
- [ ] Freeze sample standard deviation (`ddof=1`), `IR=mean/std`, 60-observation rolling windows with 30 minimum observations, 0.90 peer-correlation review threshold, 95% metric-coverage threshold, split-sign disagreement review, and top-five absolute standardized RankIC anomaly contributions.
- [ ] Add v2 schemas without changing v1 behavior and rerun the focused tests to green.

### Task 2: Strictly validate the twelve evaluation outputs

**Files:**
- Create: `evaluations/l4_portrait.py`
- Create: `evaluations/tests/test_l4_portrait.py`

- [ ] Write failing tests for exact split dates, eight events, unique `(date,event)`, 48 factor names, 14 metrics, expected shapes, and rejection of every 2025 date.
- [ ] Run `/usr/local/python3.8.10/bin/python3 -m unittest evaluations.tests.test_l4_portrait -v` and confirm RED.
- [ ] Implement validation against the frozen v2 production list and four Batch candidate lists; calculate every Parquet SHA-256 and coverage ratio.
- [ ] Require training shape `3880 × 672`, observation shape `3872 × 672`, and all twelve combinations before issuing the evaluation receipt.
- [ ] Rerun the focused tests to green.

### Task 3: Generate 48 formal portraits

**Files:**
- Modify: `evaluations/l4_portrait.py`
- Create: `campaigns/sfm_stream_001/portraits/formal_history/<family>/<candidate>.json` (48 files)

- [ ] Add failing fixture tests for aggregate, event, year, quarter, rolling, consistency, redundancy, and anomaly sections.
- [ ] Implement deterministic metrics from the validated Parquet frames while retaining `raw_signed` direction.
- [ ] Compute peer IC-series correlation from aligned training RankIC series and factor-value correlation from deterministic per-date/event cross-sectional Spearman summaries.
- [ ] Validate all 48 documents against Factor Portrait v2 and keep `promotion_allowed=false`.

### Task 4: Generate Experience Records and close the control plane

**Files:**
- Create: `campaigns/sfm_stream_001/experience/formal_history/*.json`
- Create: `campaigns/sfm_stream_001/manifests/formal-history-v2-production-completion.json`
- Create: `campaigns/sfm_stream_001/manifests/formal-history-v2-evaluation-receipt.json`
- Create: `campaigns/sfm_stream_001/manifests/formal-history-v2-portrait-review.json`
- Modify: 48 Candidate JSON files
- Modify: 4 Batch JSON files
- Modify: `campaigns/sfm_stream_001/campaign.json`
- Modify: `campaigns/sfm_stream_001/events.jsonl`
- Modify: `docs/因子挖掘框架.md`

- [ ] Generate at least one evidence-backed record per family; use `retain_control`, `generate_variants`, or `close_branch` based on frozen thresholds, with explicit confidence and limitations.
- [ ] Require every evidence metric path to resolve inside a source portrait.
- [ ] Update Candidates to L4 and Batches to `formal_complete` only after 48/48 portraits validate.
- [ ] Record the sealed holdout proof and correct the framework completion rule to two evaluated splits plus one sealed holdout.

### Task 5: Verify, review, commit, and push

- [ ] Run `python3 -m unittest discover -q campaigns/tests`.
- [ ] Run `/usr/local/python3.8.10/bin/python3 -m unittest discover -q evaluations/tests`.
- [ ] Run `ctest --test-dir base/hf-open5m-factor-demo/build-flow-pressure --output-on-failure`.
- [ ] Run `git diff --check`.
- [ ] Independently review contract compliance and code quality, fix all findings, then commit and push `feat/l4-recent-history`.

