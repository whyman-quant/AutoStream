import unittest

from evaluations.research_selection import classify_cells, freeze_holdout_selection


class ResearchSelectionTests(unittest.TestCase):
    def _cells(self):
        base = {"factor": "f", "parent_factor": "p", "event": 100000000,
                "universe": "000906", "label": "raw926", "coverage": 1.0,
                "ready": True, "metric_defined": True, "data_error": False,
                "training_rank_ic": .02, "observation_rank_ic": .01,
                "training_ls": .01, "observation_ls": .02,
                "training_monotonicity": .8, "observation_monotonicity": .7,
                "parent_rank_ic": .005}
        return [base]

    def test_stable_signed_cell_is_supported_and_not_ready_is_not_failed(self):
        cells = self._cells() + [{**self._cells()[0], "event": 92600000, "ready": False}]
        classified = classify_cells(cells)
        self.assertEqual(classified[0]["status"], "supported")
        self.assertAlmostEqual(classified[0]["parent_delta_rank_ic"], .01)
        self.assertEqual(classified[1]["status"], "not_evaluable")

    def test_holdout_selection_is_frozen_before_2025_and_best_event_per_combo(self):
        cells = self._cells() + [{**self._cells()[0], "event": 103000000,
                                  "training_rank_ic": .04, "observation_rank_ic": .03}]
        selection = freeze_holdout_selection(classify_cells(cells), allowed_years=(2021, 2022, 2023, 2024))
        self.assertEqual(selection["selected_for_holdout_display"], ["f"])
        self.assertEqual(selection["directions"]["f"], "positive")
        self.assertEqual(selection["best_events"]["000906|raw926"], 103000000)
        self.assertEqual(selection["selection_years"], [2021, 2022, 2023, 2024])
        self.assertFalse(selection["holdout_read"])

    def test_data_error_stops_selection(self):
        cells = [{**self._cells()[0], "data_error": True}]
        with self.assertRaisesRegex(ValueError, "data_error"):
            freeze_holdout_selection(classify_cells(cells))


if __name__ == "__main__":
    unittest.main()
