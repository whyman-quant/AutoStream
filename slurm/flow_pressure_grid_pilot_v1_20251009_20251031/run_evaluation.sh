#!/usr/bin/env bash
set -euo pipefail
FACTOR_PATH=/home/fangwei/mnt-ssd/AutoStream/data/sfm_autoresearch_001/flow-pressure-grid-pilot-v1-arrow
ROOT=/home/fangwei/mnt-ssd/AutoStream/data/sfm_autoresearch_001/evaluation/flow_pressure_grid_pilot_v1_20251009_20251031
EVAL=/home/fangwei/mnt-ssd/factor_eval_toolkit/scripts/evaluate_factors.py
for label in raw926 ease926; do
  for universe in 000985 003800 000906; do
    /usr/local/python3.8.10/bin/python3 "$EVAL" \
      --sdate 20251009 --edate 20251101 --label "$label" --universe "$universe" \
      --factor_group flow_pressure_grid_pilot_v1 \
      --factor_path "$FACTOR_PATH" --workers 8 \
      --output_dir "$ROOT/$label/$universe"
  done
done
