import json
import tempfile
import unittest
from pathlib import Path

from evaluations.l4g1_preflight import load_l4g1_factor_names


class L4G1PreflightTests(unittest.TestCase):
    def test_loads_four_next_batches_in_family_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); batches = root / "batches"; batches.mkdir()
            for family in ("book_imbalance", "flow_pressure", "liquidity_resilience", "impact_efficiency"):
                (batches / ("l4_formal_history_next_v1_" + family + ".json")).write_text(json.dumps({
                    "candidate_ids": [family + "_a", family + "_b", family + "_c", family + "_d"]
                }))
            names = load_l4g1_factor_names(root)
            self.assertEqual(len(names), 16)
            self.assertEqual(names[:4], ["book_imbalance_a", "book_imbalance_b", "book_imbalance_c", "book_imbalance_d"])

    def test_rejects_wrong_candidate_budget(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); batches = root / "batches"; batches.mkdir()
            for family in ("book_imbalance", "flow_pressure", "liquidity_resilience", "impact_efficiency"):
                (batches / ("l4_formal_history_next_v1_" + family + ".json")).write_text(json.dumps({"candidate_ids": [family]}))
            with self.assertRaisesRegex(ValueError, "16"):
                load_l4g1_factor_names(root)


if __name__ == "__main__":
    unittest.main()
