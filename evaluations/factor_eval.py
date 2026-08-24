"""Small, dependency-free factor quality and cross-sectional evaluation helpers."""

from __future__ import annotations

import math
import csv
import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Optional, Sequence


def _finite_pairs(xs: Sequence[float], ys: Sequence[float]):
    if len(xs) != len(ys):
        raise ValueError("factor and label lengths must match")
    return [(float(x), float(y)) for x, y in zip(xs, ys) if math.isfinite(float(x)) and math.isfinite(float(y))]


def _rank(values: Sequence[float]) -> List[float]:
    order = sorted(range(len(values)), key=lambda i: values[i])
    ranks = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i + 1
        while j < len(order) and values[order[j]] == values[order[i]]:
            j += 1
        rank = (i + j - 1) / 2.0 + 1.0
        for k in order[i:j]:
            ranks[k] = rank
        i = j
    return ranks


def _pearson(xs: Sequence[float], ys: Sequence[float]) -> float:
    if len(xs) < 2:
        return math.nan
    mx = sum(xs) / len(xs)
    my = sum(ys) / len(ys)
    numerator = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
    dx = math.sqrt(sum((x - mx) ** 2 for x in xs))
    dy = math.sqrt(sum((y - my) ** 2 for y in ys))
    return numerator / (dx * dy) if dx and dy else math.nan


def rank_ic(factor: Sequence[float], labels: Sequence[float]) -> float:
    pairs = _finite_pairs(factor, labels)
    if len(pairs) < 2:
        return math.nan
    fx, ly = zip(*pairs)
    return _pearson(_rank(fx), _rank(ly))


def quantile_group_returns(factor: Sequence[float], labels: Sequence[float], groups: int = 10):
    if groups < 2:
        raise ValueError("groups must be at least 2")
    pairs = sorted(_finite_pairs(factor, labels), key=lambda pair: pair[0])
    if not pairs:
        return {"group_count": 0, "group_returns": [], "long_short": math.nan, "row_count": 0}
    actual_groups = min(groups, len(pairs))
    outputs = []
    for group_index in range(actual_groups):
        start = (group_index * len(pairs)) // actual_groups
        end = ((group_index + 1) * len(pairs)) // actual_groups
        chunk = pairs[start:end]
        outputs.append(sum(label for _, label in chunk) / len(chunk))
    return {
        "group_count": actual_groups,
        "group_returns": outputs,
        "long_short": outputs[-1] - outputs[0],
        "row_count": len(pairs),
    }


def evaluate_columns(columns: Mapping[str, Sequence[float]], labels: Optional[Sequence[float]] = None, *, warmup: int = 0, min_post_warmup_coverage: float = 0.8, groups: int = 10) -> Dict[str, dict]:
    if warmup < 0 or not 0.0 <= min_post_warmup_coverage <= 1.0:
        raise ValueError("invalid warmup or coverage threshold")
    results = {}
    for name, values in columns.items():
        numeric = [float(value) for value in values]
        finite = [value for value in numeric if math.isfinite(value)]
        post = numeric[warmup:]
        post_finite = [value for value in post if math.isfinite(value)]
        rejects = []
        coverage = len(post_finite) / len(post) if post else 0.0
        if coverage < min_post_warmup_coverage:
            rejects.append("post_warmup_coverage_below_threshold")
        if len(finite) >= 2 and len(set(finite)) == 1:
            rejects.append("constant_column")
        result = {
            "status": "technical_reject" if rejects else "technical_pass",
            "evaluation_status": "data_missing" if labels is None else "pending",
            "row_count": len(numeric),
            "finite_count": len(finite),
            "nan_count": sum(math.isnan(value) for value in numeric),
            "inf_count": sum(math.isinf(value) for value in numeric),
            "zero_count": sum(value == 0.0 for value in numeric if math.isfinite(value)),
            "post_warmup_coverage": coverage,
            "reject_reasons": rejects,
        }
        if labels is not None:
            result["rank_ic"] = rank_ic(numeric, labels)
            result["groups"] = quantile_group_returns(numeric, labels, groups=groups)
            result["evaluation_status"] = "evaluated"
        results[name] = result
    return results


def _run_h5dump(dataset: str, path: str) -> str:
    if shutil.which("h5dump") is None:
        raise RuntimeError("h5dump is required to read HDF5 files")
    completed = subprocess.run(["h5dump", "-y", "-w", "0", "-d", dataset, path], check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "h5dump failed")
    return completed.stdout


def read_hdf5_factor_file(path: str, dataset: str = "/factordata") -> Dict[str, List[float]]:
    """Read a factor HDF5 written by FactorCalculationEngine using h5dump."""
    text = _run_h5dump(dataset, path)
    shape_match = re.search(r"DATASPACE\s+SIMPLE\s+\{\s*\(\s*(\d+)\s*,\s*(\d+)\s*\)", text)
    if not shape_match:
        raise ValueError("factordata must be a 2-D HDF5 dataset")
    rows, columns = int(shape_match.group(1)), int(shape_match.group(2))
    data_start = text.find("DATA {")
    data_end = text.rfind("\n   }", data_start)
    if data_start < 0 or data_end < 0:
        raise ValueError("factordata DATA block not found")
    raw = text[data_start + len("DATA {"):data_end]
    values = []
    for token in raw.replace("\n", " ").split(","):
        token = token.strip()
        if token:
            values.append(float(token))
    if len(values) != rows * columns:
        raise ValueError(f"factordata value count {len(values)} != {rows}*{columns}")
    names = read_hdf5_string_dataset(path, "/factorlist")
    if len(names) != columns:
        raise ValueError("factorlist length does not match factordata columns")
    return {name: [values[row * columns + column] for row in range(rows)] for column, name in enumerate(names)}


def read_hdf5_string_dataset(path: str, dataset: str) -> List[str]:
    text = _run_h5dump(dataset, path)
    data_start = text.find("DATA {")
    data_end = text.rfind("\n   }", data_start)
    if data_start < 0 or data_end < 0:
        raise ValueError(f"string DATA block not found in {dataset}")
    values = []
    raw = text[data_start:data_end]
    for match in re.finditer(r"\"((?:[^\"\\]|\\.)*)\"", raw):
        value = match.group(1).replace("\\000", "").replace("\\\"", "\"")
        values.append(value.rstrip("\x00"))
    if not values:
        raise ValueError(f"no string values found in {dataset}")
    return values


def read_label_csv(path: str, label_column: str = "label") -> List[float]:
    with open(path, newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows or label_column not in rows[0]:
        raise ValueError(f"label CSV must contain {label_column!r}")
    return [float(row[label_column]) for row in rows]
