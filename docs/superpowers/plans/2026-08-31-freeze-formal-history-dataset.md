# Freeze Formal History Dataset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze a reproducible L4 formal-history dataset with explicit training, observation, holdout, market-state, evaluator, and stopping rules.

**Architecture:** Build the date universe from the intersection of the existing quote/trade source and raw926/ease926 label calendars. Store the exact date list and hashes in a versioned manifest; keep holdout sealed by policy and validate the manifest with repository tests.

**Tech Stack:** JSON manifest, Python standard-library validation, existing factor_eval_toolkit evaluator, campaign documentation.

---

### Task 1: Freeze dataset manifest

**Files:**
- Create: `campaigns/sfm_stream_001/manifests/formal-history-dataset-v1.json`
- Test: `campaigns/tests/test_formal_history_dataset.py`

- [ ] Write a manifest containing the exact 2,171-date source/label intersection, split counts, source paths, eight events, two labels, three universes, evaluator and leakage policy.
- [ ] Add tests for contiguous non-overlapping splits, exact counts, date-list hash, holdout sealing, state coverage thresholds, and fixed evaluator configuration.

### Task 2: Document L4 rules

**Files:**
- Modify: `docs/因子挖掘框架.md`

- [ ] Document train/observation/holdout boundaries, market-state classifier and frozen thresholds, evaluation version, and stop conditions.
- [ ] Mark L4 as dataset-frozen but formal production not yet run.

### Task 3: Verify and publish

- [ ] Run campaign/evaluation tests, C++ tests, and `git diff --check`.
- [ ] Commit and push the manifest, tests, and documentation to `main`.
