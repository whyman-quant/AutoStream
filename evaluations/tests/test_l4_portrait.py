import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
import pandas as pd

from campaigns.contracts import validate_document
from evaluations.l4_portrait import METRICS, build_portraits, write_portraits, classify_validity_cell, build_validity_matrix, summarize_validity_matrix


class L4PortraitTests(unittest.TestCase):
    labels = ("raw926", "ease926")
    universes = ("000985", "003800", "000906")
    events = (1, 2)
    factors = ("family_op_w16", "family_op_w32")

    def _fixture(self, root, missing_fraction=0.0):
        root = Path(root)
        split_dates = {
            "training": ["202101{:02d}".format(i) for i in range(1, 32)] + ["202102{:02d}".format(i) for i in range(1, 30)],
            "observation": ["202301{:02d}".format(i) for i in range(1, 32)] + ["202302{:02d}".format(i) for i in range(1, 30)],
        }
        for fi, factor in enumerate(self.factors):
            cdir = root / "candidates" / "family"; cdir.mkdir(parents=True, exist_ok=True)
            (cdir / (factor + ".json")).write_text(json.dumps({"candidate_id": factor, "family_id": "family", "canonical_hash": "sha256:" + str(fi + 1) * 64, "lineage": {"source_commit": "abcdef1"}, "parameters": {"window_events": 16 + 16 * fi, "lag_events": 0}}))
        for split, dates in split_dates.items():
            index = pd.MultiIndex.from_product([dates, self.events], names=["date", "event"])
            for label in self.labels:
                for universe in self.universes:
                    frame = pd.DataFrame(index=index)
                    for fi, factor in enumerate(self.factors):
                        base = np.arange(len(index), dtype=float) + 1 + fi
                        if split == "observation" and fi == 0: base = -base
                        for metric in METRICS: frame[factor + "|" + metric] = base / 100.0
                    if missing_fraction:
                        n = int(len(frame) * missing_fraction); frame.iloc[:n, 0] = np.nan
                    path = root / "results" / split / label / universe; path.mkdir(parents=True, exist_ok=True)
                    frame.to_parquet(path / "res_full.parquet")
        arrow = root / "arrow"; arrow.mkdir()
        for dates in split_dates.values():
            for date in dates:
                rows = []
                for event in self.events:
                    for symbol in range(6): rows.append({"symbol": str(symbol), "date": date, "event": event, self.factors[0]: float(symbol), self.factors[1]: float(symbol) * 2})
                pd.DataFrame(rows).to_feather(arrow / (date + ".arrow"))
        return split_dates

    def test_strict_two_split_matrix_and_metrics(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            docs = build_portraits(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, Path(tmp) / "arrow", Path(tmp) / "candidates")
        self.assertEqual(len(docs), 2)
        p = docs[0]
        self.assertEqual(set(p["splits"]), {"training", "observation"})
        self.assertEqual(len(p["splits"]["training"]["combinations"]), 6)
        self.assertEqual(p["metrics"]["rank_ic"]["std_ddof"], 1)
        self.assertTrue(p["rolling_stability"]["split_mean_sign_disagreement"])
        self.assertAlmostEqual(p["correlations"]["factor_value"]["peers"][0]["spearman_mean"], 1.0)
        self.assertAlmostEqual(p["correlations"]["ic_series"]["peers"][0]["spearman"], 0.0)
        self.assertEqual(p["parameter_neighborhood"][0]["candidate_id"], self.factors[1])
        self.assertEqual(len(p["anomaly_days"]), 5)
        validate_document("factor_portrait", p)

    def test_rolling_is_per_event_and_combo(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            docs = build_portraits(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, Path(tmp) / "arrow", Path(tmp) / "candidates")
        groups = docs[0]["rolling_stability"]["groups"]
        self.assertEqual(len(groups), 2 * 2 * 3 * 2)
        self.assertTrue(all(g["window"] == 60 and g["min_periods"] == 30 for g in groups))

    def test_coverage_below_95_percent_hard_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp, missing_fraction=.06)
            with self.assertRaisesRegex(ValueError, "coverage"):
                build_portraits(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, Path(tmp) / "arrow", Path(tmp) / "candidates")

    def test_holdout_date_and_wrong_factor_order_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            bad = dict(dates); bad["observation"] = list(dates["observation"]) + ["20250102"]
            with self.assertRaisesRegex(ValueError, "holdout"):
                build_portraits(Path(tmp) / "results", bad, self.labels, self.universes, self.factors, self.events, Path(tmp) / "arrow", Path(tmp) / "candidates")
            with self.assertRaisesRegex(ValueError, "column order"):
                build_portraits(Path(tmp) / "results", dates, self.labels, self.universes, tuple(reversed(self.factors)), self.events, Path(tmp) / "arrow", Path(tmp) / "candidates")

    def test_writes_one_schema_document_per_factor(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            docs = build_portraits(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, Path(tmp) / "arrow", Path(tmp) / "candidates")
            paths = write_portraits(docs, Path(tmp) / "out")
            self.assertEqual(len(paths), 2)
            self.assertTrue(all(json.loads(p.read_text())["schema_version"] == 2 for p in paths))

    def test_validity_matrix_classifies_independent_cells(self):
        values = np.arange(1, 7, dtype=float)
        self.assertEqual(classify_validity_cell(values, {"IC": values})["status"], "pass")
        self.assertEqual(classify_validity_cell(values, {"IC": np.zeros(6)})["status"], "metric_undefined")
        self.assertEqual(classify_validity_cell(values, {"IC": np.full(6, np.nan)})["status"], "metric_undefined")
        self.assertEqual(classify_validity_cell(values, {"IC": values}, readiness=np.zeros(6, dtype=bool))["status"], "not_ready")

    def test_zero_values_are_not_inferred_as_not_ready(self):
        cell = classify_validity_cell(np.zeros(6), {"IC": np.arange(6, dtype=float)})
        self.assertNotEqual(cell["status"], "not_ready")
        cell = classify_validity_cell(np.zeros(6), {"IC": np.arange(6, dtype=float)}, readiness=np.array([False, True, True, True, True, True]))
        self.assertEqual(cell["status"], "not_ready")
        self.assertEqual(cell["not_ready_zero_count"], 1)

    def test_build_and_summarize_four_dimensional_matrix(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            frames, _ = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, 0.0)
            matrix = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes)
            self.assertEqual(len(matrix), 2 * 2 * 2 * 3 * 2)
            self.assertEqual({c["status"] for c in matrix}, {"pass"})
            summary = summarize_validity_matrix(matrix)
            self.assertEqual(summary["cell_count"], len(matrix))
            self.assertEqual(summary["pass_count"], len(matrix))


if __name__ == "__main__": unittest.main()
