import tempfile
import unittest
from pathlib import Path

from evaluations.reproducibility import compare_reproduction


class ReproducibilityTests(unittest.TestCase):
    def test_exact_match_passes(self):
        import h5py
        import numpy as np

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            current = root / "factors.h5"
            historical = root / "old"
            historical.mkdir()
            with h5py.File(str(current), "w") as target:
                target.create_dataset("factorlist", data=np.asarray([b"factor"]))
                target.create_dataset("92700000", data=np.asarray([[1.0]]))
                target.create_dataset("codelist_92700000", data=np.asarray([b"000001"]))
            with h5py.File(str(historical / "092700.h5"), "w") as target:
                target.create_dataset("factorlist", data=np.asarray([b"factor"]))
                target.create_dataset("factordata", data=np.asarray([[1.0]]))
                target.create_dataset("codelist", data=np.asarray([b"000001"]))
            report = compare_reproduction(current, historical, [(92700000, "092700")])
            self.assertTrue(report["passed"])
            self.assertEqual(report["events"][0]["max_abs_error"], 0.0)


if __name__ == "__main__":
    unittest.main()
