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
        self.assertEqual(classify_validity_cell(values, {"IC": values}, readiness=np.ones(6, dtype=bool))["status"], "pass")
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
            self.assertEqual({c["status"] for c in matrix}, {"review"})
            summary = summarize_validity_matrix(matrix)
            self.assertEqual(summary["cell_count"], len(matrix))
            self.assertEqual(summary["pass_count"], 0)

    def test_validity_cell_rejects_invalid_readiness_and_tracks_cross_section(self):
        values = np.array([0., 1., 2., 3., 4., 5.])
        cell = classify_validity_cell(values, {"IC": values}, readiness=np.ones(6, dtype=bool))
        self.assertIn("factor_std", cell)
        self.assertIn("factor_unique_count", cell)
        self.assertIn("factor_nonzero_ratio", cell)
        self.assertIn("effective_rank_count", cell)
        with self.assertRaisesRegex(ValueError, "readiness"):
            classify_validity_cell(values, {"IC": values}, readiness=np.array([0, 1, 2, 1, 0, 1]))

    def test_validity_cell_reports_coverage_data_error_review_and_partial_metrics(self):
        values = np.arange(6, dtype=float)
        partial = classify_validity_cell(values, {"IC": values, "RankIC": np.array([1., np.nan, np.nan, np.nan, np.nan, np.nan])}, readiness=np.ones(6, dtype=bool), coverage_threshold=.95)
        self.assertEqual(partial["status"], "coverage_fail")
        bad = classify_validity_cell(values, {"IC": np.array([1., 2., np.inf, 4., 5., 6.])}, readiness=np.ones(6, dtype=bool))
        self.assertEqual(bad["status"], "data_error")
        review = classify_validity_cell(values, {"IC": values})
        self.assertEqual(review["status"], "review")

    def test_build_matrix_does_not_fabricate_missing_factor_values(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            frames, _ = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, 0.0)
            matrix = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes)
            self.assertTrue(all(c["status"] == "review" for c in matrix))
            self.assertTrue(all(c["reason"] == "factor_values_not_provided" for c in matrix))

    def test_relaxed_frames_allow_partial_metrics_only_for_matrix_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp, missing_fraction=.06)
            mod = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])
            with self.assertRaisesRegex(ValueError, "coverage"):
                mod._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, .95)
            frames, _ = mod._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, .95, allow_partial_metrics=True)
            matrix = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes)
            self.assertTrue(any(c["status"] in ("coverage_fail", "review") for c in matrix))

    def test_matrix_missing_metric_column_is_data_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            frames, _ = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, 0.0)
            frame = frames[("training", self.labels[0], self.universes[0])].drop(columns=[self.factors[0] + "|IC"])
            frames[("training", self.labels[0], self.universes[0])] = frame
            cells = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes)
            bad = [c for c in cells if c["split"] == "training" and c["label"] == self.labels[0] and c["universe"] == self.universes[0] and c["factor"] == self.factors[0]]
            self.assertTrue(all(c["status"] == "data_error" and c["reason"] == "metric_column_missing" for c in bad))

    def test_invalid_input_and_readiness_do_not_hide_metric_error(self):
        bad_type = classify_validity_cell(["x"], {"IC": [1.]})
        self.assertEqual(bad_type["status"], "data_error")
        bad_metric = classify_validity_cell(np.ones(3), {"IC": [1., np.inf, 2.]}, readiness=[False, False, False])
        self.assertEqual(bad_metric["status"], "data_error")

    def test_cell_and_summary_expose_pass_only_metric_stats(self):
        values = np.arange(1., 7.)
        cell = classify_validity_cell(values, {"IC": values}, readiness=np.ones(6, dtype=bool))
        self.assertEqual(cell["metric_stats"]["IC"]["count"], 6)
        self.assertEqual(cell["metric_stats"]["IC"]["std_ddof"], 1)
        self.assertIn("ir", cell["metric_stats"]["IC"])
        summary = summarize_validity_matrix([dict(cell, split="training", factor="f", event=1, universe="u", label="l")])
        self.assertEqual(summary["summaries"]["event"]["1"]["metric_stats"]["IC"]["count"], 1)
        self.assertIn("mean", summary["summaries"]["event"]["1"]["metric_stats"]["IC"])

    def test_ready_true_requires_finite_factor_values(self):
        cell = classify_validity_cell(np.array([1., np.nan, 2.]), {"IC": [1., 2., 3.]}, readiness=[1, 1, 1])
        self.assertEqual(cell["status"], "data_error")

    def test_arrow_cross_section_stats_are_attached_per_event_without_using_parquet_as_cross_section(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            arrow_path = Path(tmp) / "arrow" / (dates["training"][0] + ".arrow")
            frame = pd.read_feather(arrow_path)
            frame["ready_" + self.factors[0]] = True
            frame.loc[(frame["event"] == self.events[0]) & (frame["symbol"] == "0"), "ready_" + self.factors[0]] = False
            frame.to_feather(arrow_path)
            mod = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])
            frames, _ = mod._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, 0.0)
            cells = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes,
                                          arrow_root=Path(tmp) / "arrow", dates=dates["training"])
            cell = next(c for c in cells if c["split"] == "training" and c["factor"] == self.factors[0] and c["event"] == self.events[0])
            self.assertEqual(cell["factor_value_source"], "arrow_cross_section")
            self.assertEqual(cell["cross_sectional"]["source"], "arrow")
            self.assertEqual(cell["cross_sectional"]["date_count"], len(dates["training"]))
            self.assertEqual(cell["cross_sectional"]["symbol_count"], 6)
            self.assertIn("ready_count", cell["cross_sectional"])
            self.assertEqual(cell["cross_sectional"]["ready_false_count"], 1)
            # Universe/label panels have no constituent mapping in Arrow, so the
            # same all-symbols summary is explicitly marked instead of inventing
            # a Parquet-derived cross section.
            self.assertEqual(cell["cross_sectional"]["universe_scope"], "all_symbols")

    def test_arrow_loader_rejects_holdout_dates_and_missing_factor_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            mod = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])
            frames, _ = mod._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, 0.0)
            with self.assertRaisesRegex(ValueError, "holdout"):
                build_validity_matrix(frames, {"training": ["20250102"], "observation": []}, self.factors, self.events,
                                       self.labels, self.universes, arrow_root=Path(tmp) / "arrow", dates=["20250102"])
            missing = Path(tmp) / "arrow" / (dates["training"][0] + ".arrow")
            frame = pd.read_feather(missing).drop(columns=[self.factors[0]])
            frame.to_feather(missing)
            cells = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes,
                                          arrow_root=Path(tmp) / "arrow", dates=dates["training"])
            cell = next(c for c in cells if c["split"] == "training" and c["factor"] == self.factors[0])
            self.assertEqual(cell["factor_value_source"], "not_provided")
            self.assertEqual(cell["cross_sectional"]["status"], "review")

    def test_arrow_ready_true_nan_is_review_but_ready_false_nan_is_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            path = Path(tmp) / "arrow" / (dates["training"][0] + ".arrow")
            frame = pd.read_feather(path)
            ready_name = "ready_" + self.factors[0]
            frame[ready_name] = True
            frame.loc[(frame["event"] == self.events[0]) & (frame["symbol"] == "0"), self.factors[0]] = np.nan
            frame.loc[(frame["event"] == self.events[0]) & (frame["symbol"] == "0"), ready_name] = False
            frame.loc[(frame["event"] == self.events[0]) & (frame["symbol"] == "1"), self.factors[0]] = np.nan
            frame.to_feather(path)
            mod = __import__("evaluations.l4_portrait", fromlist=["_load_arrow_cross_section"])
            summary = mod._load_arrow_cross_section(Path(tmp) / "arrow", dates["training"], self.factors, self.events)
            # A NaN under ready=false is allowed, while ready=true NaN is a review.
            self.assertEqual(summary[(self.factors[0], self.events[0])]["status"], "review")
            self.assertEqual(summary[(self.factors[0], self.events[0])]["reason"], "ready_factor_value_not_finite")

    def test_arrow_ready_false_nan_is_allowed_when_no_ready_true_nan(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            path = Path(tmp) / "arrow" / (dates["training"][0] + ".arrow")
            frame = pd.read_feather(path)
            ready_name = "ready_" + self.factors[0]
            frame[ready_name] = True
            mask = (frame["event"] == self.events[0]) & (frame["symbol"] == "0")
            frame.loc[mask, self.factors[0]] = np.nan
            frame.loc[mask, ready_name] = False
            frame.to_feather(path)
            mod = __import__("evaluations.l4_portrait", fromlist=["_load_arrow_cross_section"])
            summary = mod._load_arrow_cross_section(Path(tmp) / "arrow", dates["training"], self.factors, self.events)
            self.assertEqual(summary[(self.factors[0], self.events[0])]["status"], "provided")

    def test_arrow_loader_reads_only_required_columns(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            path = Path(tmp) / "arrow" / (dates["training"][0] + ".arrow")
            frame = pd.read_feather(path)
            frame["unrelated_payload"] = 1
            frame.to_feather(path)
            mod = __import__("evaluations.l4_portrait", fromlist=["_load_arrow_cross_section"])
            original = mod.pd.read_feather
            calls = []
            def wrapped(*args, **kwargs):
                calls.append(kwargs.get("columns"))
                return original(*args, **kwargs)
            mod.pd.read_feather = wrapped
            try:
                mod._load_arrow_cross_section(Path(tmp) / "arrow", dates["training"], self.factors, self.events)
            finally:
                mod.pd.read_feather = original
            self.assertTrue(calls)
            self.assertTrue(all("unrelated_payload" not in (columns or []) for columns in calls))

    def test_arrow_values_do_not_mask_metric_coverage_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp, missing_fraction=.06)
            mod = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])
            frames, _ = mod._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, .95, allow_partial_metrics=True)
            cells = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes,
                                          arrow_root=Path(tmp) / "arrow", dates=dates)
            target = next(c for c in cells if c["split"] == "training" and c["factor"] == self.factors[0] and c["event"] == self.events[0])
            self.assertEqual(target["factor_value_source"], "arrow_cross_section")
            self.assertEqual(target["status"], "coverage_fail")

    def test_arrow_values_do_not_mask_metric_undefined_or_data_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            dates = self._fixture(tmp)
            mod = __import__("evaluations.l4_portrait", fromlist=["_strict_frames"])
            frames, _ = mod._strict_frames(Path(tmp) / "results", dates, self.labels, self.universes, self.factors, self.events, 0.0)
            key = ("training", self.labels[0], self.universes[0])
            event_mask = frames[key].index.get_level_values("event") == self.events[0]
            frames[key].loc[event_mask, self.factors[0] + "|IC"] = 0.0
            frames[key].loc[event_mask, self.factors[1] + "|IC"] = np.inf
            cells = build_validity_matrix(frames, dates, self.factors, self.events, self.labels, self.universes,
                                          arrow_root=Path(tmp) / "arrow", dates=dates)
            undefined = next(c for c in cells if c["split"] == "training" and c["label"] == self.labels[0]
                             and c["universe"] == self.universes[0] and c["factor"] == self.factors[0]
                             and c["event"] == self.events[0])
            bad = next(c for c in cells if c["split"] == "training" and c["label"] == self.labels[0]
                       and c["universe"] == self.universes[0] and c["factor"] == self.factors[1]
                       and c["event"] == self.events[0])
            self.assertEqual(undefined["status"], "metric_undefined")
            self.assertEqual(bad["status"], "data_error")


if __name__ == "__main__": unittest.main()
