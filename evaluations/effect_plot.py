"""Deterministic 6×3 factor effect chart renderer."""
from __future__ import annotations

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import cm
import pandas as pd

ROW_ORDER = (("000906", "raw926"), ("000906", "ease926"),
             ("003800", "raw926"), ("003800", "ease926"),
             ("000985", "raw926"), ("000985", "ease926"))


def _as_frame(data):
    if isinstance(data, dict) and "rows" in data:
        data = data["rows"]
    return data if isinstance(data, pd.DataFrame) else pd.DataFrame(data)


def plot_effect_grid(data, factor="factor", best_events=None, output_path=None,
                     figsize=(18, 22), dpi=150):
    frame = _as_frame(data)
    if frame.empty:
        raise ValueError("effect timeseries is empty")
    best_events = best_events or {}
    fig, axes = plt.subplots(6, 3, figsize=figsize, dpi=dpi, squeeze=False)
    event_values = sorted(frame["event"].astype(str).unique())
    event_colors = cm.coolwarm([i / max(1, len(event_values) - 1) for i in range(len(event_values))])
    q_colors = cm.coolwarm([i / 9.0 for i in range(10)])
    for row, (universe, label) in enumerate(ROW_ORDER):
        block = frame[(frame["universe"].astype(str).str.zfill(6) == universe) & (frame["label"] == label)]
        key = universe + "|" + label
        best = best_events.get(key)
        if best is None and "best_event" in block.columns and len(block):
            vals = block["best_event"].dropna()
            best = str(vals.iloc[0]) if len(vals) else None
        ax_ic, ax_long, ax_q = axes[row]
        ax_ic.set_ylabel("{} · {}".format(universe, label))
        for i, event in enumerate(event_values):
            part = block[block["event"].astype(str) == event].sort_values("date")
            if not part.empty and "ic_cumulative" in part:
                ax_ic.plot(part["date"].astype(str), part["ic_cumulative"], color=event_colors[i], label=event)
            if not part.empty and "long_cumulative" in part:
                ax_long.plot(part["date"].astype(str), part["long_cumulative"], color=event_colors[i], label=event)
        if row == 0:
            ax_ic.set_title("IC cumulative (8 events)")
            ax_long.set_title("Long cumulative (8 events)")
        if best is not None:
            chosen = block[block["event"].astype(str) == str(best)].sort_values("date")
            for i in range(1, 11):
                name = "q%d_cumulative" % i
                if name in chosen:
                    ax_q.plot(chosen["date"].astype(str), chosen[name], color=q_colors[i - 1], label="Q%d" % i)
        ax_q.set_title("Q1–Q10 · best event {}".format(best if best is not None else "(frozen)"))
        for ax in (ax_ic, ax_long, ax_q):
            # Date strings keep natural ordering while allowing fixed audit marks.
            ax.axvline("20241231", color="black", linewidth=.7, linestyle="--")
            ax.axvline("20250101", color="black", linewidth=.9, linestyle=":")
            ax.text(.99, .03, "2025 holdout", transform=ax.transAxes, ha="right", va="bottom", fontsize=7)
            ax.tick_params(axis="x", labelrotation=45)
        if row == 0:
            ax_ic.legend(title="event", fontsize=7)
            ax_long.legend(title="event", fontsize=7)
            ax_q.legend(title="decile", fontsize=7, ncol=2)
    fig.suptitle("{} — frozen effect display (2023–2024 selection; 2025 holdout)".format(factor), y=.995)
    fig.tight_layout(rect=(0, 0, 1, .985))
    if output_path:
        fig.savefig(output_path, dpi=dpi)
    return fig


__all__ = ["plot_effect_grid", "ROW_ORDER"]
