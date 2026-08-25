import json
from pathlib import Path
import tempfile
import unittest

from evaluations.run_real_label_scorecard import run_scorecard


class RunRealLabelScorecardTests(unittest.TestCase):
    def test_writes_json_for_complete_matrix(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label in ("raw926", "ease926"):
                for universe in ("000985", "003800", "000906"):
                    path = root / label / universe
                    path.mkdir(parents=True)
                    (path / "res_full.json").write_text(
                        json.dumps({"factor_a": {"RankIC": [0.1], "IC": [0.2], "LS": [0.01], "Monotonicity": [0.3]}}),
                        encoding="utf-8",
                    )
            report = run_scorecard(str(root), output=str(root / "scorecard.json"), input_format="json")
            self.assertEqual(report["combination_count"], 6)
            self.assertTrue((root / "scorecard.json").exists())

    def test_records_artifact_hashes_when_supplied(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label in ("raw926", "ease926"):
                for universe in ("000985", "003800", "000906"):
                    path = root / label / universe
                    path.mkdir(parents=True)
                    (path / "res_full.json").write_text(
                        json.dumps({"factor_a": {"RankIC": [0.1], "IC": [0.2], "LS": [0.01], "Monotonicity": [0.3]}}),
                        encoding="utf-8",
                    )
            factor = root / "factor.arrow"
            label = root / "label.arrow"
            contract = root / "contract.json"
            factor.write_bytes(b"factor")
            label.write_bytes(b"label")
            contract.write_bytes(b"contract")
            report = run_scorecard(str(root), input_format="json", factor_input=str(factor), label_input=str(label), label_contract=str(contract))
            self.assertEqual(set(report["artifacts"]), {"factor_input", "label_input", "label_contract"})

    def test_rejects_nonstandard_json_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "raw926" / "000985"
            path.mkdir(parents=True)
            (path / "res_full.json").write_text('{"factor_a":{"RankIC":[NaN]}}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "non-standard JSON"):
                run_scorecard(str(root), labels=("raw926",), universes=("000985",), input_format="json")


if __name__ == "__main__":
    unittest.main()
