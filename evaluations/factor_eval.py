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


def portfolio_metrics(returns: Sequence[float], *, weights: Optional[Sequence[Sequence[float]]] = None, periods_per_year: int = 252) -> Dict[str, float]:
    """Calculate deterministic post-cost-ready portfolio summary metrics.

    ``returns`` are decimal period returns.  Risk-free return is assumed to be
    zero; callers should subtract a benchmark or risk-free series before
    calling this helper when that is required by the experiment contract.
    ``weights`` may contain consecutive portfolio vectors; turnover is the
    average one-way turnover (half the L1 change) between vectors.
    """
    if periods_per_year <= 0:
        raise ValueError("periods_per_year must be positive")
    values = [float(value) for value in returns]
    if any(not math.isfinite(value) for value in values):
        raise ValueError("returns must be finite")
    if not values:
        return {
            "period_count": 0,
            "cumulative_return": math.nan,
            "annualized_return": math.nan,
            "volatility": math.nan,
            "sharpe": math.nan,
            "max_drawdown": math.nan,
            "calmar": math.nan,
            "average_turnover": math.nan,
        }
    growth = 1.0
    equity = 1.0
    peak = equity
    max_drawdown = 0.0
    for value in values:
        growth *= 1.0 + value
        equity *= 1.0 + value
        peak = max(peak, equity)
        max_drawdown = min(max_drawdown, equity / peak - 1.0)
    cumulative = growth - 1.0
    annualized = -1.0 if growth <= 0.0 else growth ** (periods_per_year / len(values)) - 1.0
    mean = sum(values) / len(values)
    volatility = math.sqrt(sum((value - mean) ** 2 for value in values) / len(values))
    annualized_volatility = volatility * math.sqrt(periods_per_year)
    sharpe = (mean / volatility) * math.sqrt(periods_per_year) if volatility > 0.0 else math.nan
    calmar = annualized / abs(max_drawdown) if max_drawdown < 0.0 else math.nan
    average_turnover = math.nan
    if weights is not None:
        vectors = [[float(value) for value in vector] for vector in weights]
        if len(vectors) < 2:
            raise ValueError("weights must contain at least two consecutive portfolios")
        width = len(vectors[0])
        if width == 0 or any(len(vector) != width for vector in vectors):
            raise ValueError("weights must be non-empty vectors of equal length")
        if any(not math.isfinite(value) for vector in vectors for value in vector):
            raise ValueError("weights must be finite")
        turnover = [
            0.5 * sum(abs(current - previous) for current, previous in zip(now, before))
            for before, now in zip(vectors, vectors[1:])
        ]
        average_turnover = sum(turnover) / len(turnover)
    return {
        "period_count": len(values),
        "cumulative_return": cumulative,
        "annualized_return": annualized,
        "volatility": annualized_volatility,
        "sharpe": sharpe,
        "max_drawdown": max_drawdown,
        "calmar": calmar,
        "average_turnover": average_turnover,
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
