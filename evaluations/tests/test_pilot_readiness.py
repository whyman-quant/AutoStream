import json
from pathlib import Path
import tempfile
import unittest

from evaluations.pilot_readiness import inspect_dates, main


class PilotReadinessTests(unittest.TestCase):
    def test_classifies_missing_factor_and_label_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            factors = root / "factors"
            labels = root / "labels"
            factors.mkdir()
            labels.mkdir()
            (factors / "20251013.arrow").write_bytes(b"factor")
            (labels / "20251013.arrow").write_bytes(b"label")
            (labels / "20251014.arrow").write_bytes(b"label")
            report = inspect_dates(
                ["20251013", "20251014", "20251015"],
                factor_root=str(factors),
                label_root=str(labels),
            )
            self.assertEqual([item["status"] for item in report["dates"]], ["ready", "factor_missing", "factor_missing"])
            self.assertEqual(report["ready_count"], 1)

    def test_rejects_duplicate_dates(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            inspect_dates(["20251013", "20251013"], factor_root="/tmp", label_root="/tmp")

    def test_cli_writes_report_and_blocks_incomplete_pilot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            factors = root / "factors"
            labels = root / "labels"
            output = root / "readiness.json"
            factors.mkdir()
            labels.mkdir()
            (factors / "20251013.arrow").write_bytes(b"factor")
            (labels / "20251013.arrow").write_bytes(b"label")
            (labels / "20251014.arrow").write_bytes(b"label")

            exit_code = main([
                "--dates", "20251013,20251014",
                "--factor-root", str(factors),
                "--label-root", str(labels),
                "--output", str(output),
            ])

            self.assertEqual(exit_code, 2)
            report = json.loads(output.read_text())
            self.assertFalse(report["ready"])
            self.assertEqual(report["ready_count"], 1)


if __name__ == "__main__":
    unittest.main()
