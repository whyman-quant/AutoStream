import json
import tempfile
import unittest
from pathlib import Path

from evaluations.round_report import build_portrait_effect_report, write_effect_plots


class RoundReportTests(unittest.TestCase):
    def _portraits(self, root):
        root = Path(root)
        for family, values in {
            "book_imbalance": [0.01, -0.02],
            "flow_pressure": [0.03, 0.01],
        }.items():
            for i, value in enumerate(values):
                (root / ("{}__{}.json".format(family, i))).write_text(json.dumps({
                    "candidate_id": "{}_{}".format(family, i),
                    "family_id": family,
                    "portrait_id": "{}_{}__formal_history_v3".format(family, i),
                    "metrics": {"rank_ic": {"mean": value, "std": 0.1, "ir": value / 0.1, "positive_fraction": 0.55}},
                    "validity": {"status_counts": {"pass": 90, "not_ready": 6}},
                    "splits": {"training": {"event_slices": {}}, "observation": {"event_slices": {}}},
                    "decision": {"promotion_allowed": False},
                }))

    @staticmethod
    def _selection_receipt():
        return {
            "selected_for_holdout_display": ["book_imbalance_0"],
            "cells": [
                {"factor": "book_imbalance_0", "status": "supported", "event": 100000000,
                 "universe": "000906", "label": "raw926", "parent_delta": 0.012},
                {"factor": "book_imbalance_0", "status": "promising", "event": 103000000,
                 "universe": "000906", "label": "ease926", "parent_delta": -0.003},
                {"factor": "book_imbalance_1", "status": "unsupported", "event": 100000000,
                 "universe": "003800", "label": "raw926"},
                {"factor": "flow_pressure_0", "status": "not_evaluable", "event": 92600000,
                 "universe": "000985", "label": "raw926"},
                {"factor": "flow_pressure_1", "status": "error", "event": 92600000,
                 "universe": "000985", "label": "ease926"},
            ],
        }

    def test_report_uses_selection_receipt_unit_evidence_not_pass_cells(self):
        with tempfile.TemporaryDirectory() as tmp:
            portrait_root = Path(tmp) / "portraits"
            portrait_root.mkdir()
            self._portraits(portrait_root)
            report = build_portrait_effect_report(portrait_root, "round-1", self._selection_receipt())
            self.assertEqual(report["portrait_count"], 4)
            self.assertEqual(report["selected_for_holdout_display"], ["book_imbalance_0"])
            self.assertEqual(report["status_counts"], {
                "supported": 1, "promising": 1, "unsupported": 1,
                "not_evaluable": 1, "error": 1,
            })
            self.assertEqual(report["supported_cells"], [{
                "factor": "book_imbalance_0", "event": 100000000,
                "universe": "000906", "label": "raw926", "parent_delta": 0.012,
            }])
            self.assertNotIn("effective_factor_count", report)
            self.assertEqual(report["promotion_allowed"], False)
            self.assertIn("book_imbalance", report["families"])

    def test_write_effect_plots_returns_existing_pngs(self):
        with tempfile.TemporaryDirectory() as tmp:
            portrait_root = Path(tmp) / "portraits"
            portrait_root.mkdir()
            self._portraits(portrait_root)
            paths = write_effect_plots(portrait_root, Path(tmp) / "plots", "round-1", self._selection_receipt())
            self.assertGreaterEqual(len(paths), 1)
            self.assertTrue(all(Path(path).is_file() for path in paths))
            self.assertTrue(all(str(path).endswith(".png") for path in paths))


if __name__ == "__main__":
    unittest.main()
