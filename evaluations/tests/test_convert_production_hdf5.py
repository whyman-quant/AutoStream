import tempfile
import unittest
from pathlib import Path

from evaluations.convert_production_hdf5 import convert_hdf5


class ProductionHdf5ConversionTests(unittest.TestCase):
    def test_rejects_wrong_shape_before_writing(self):
        import h5py
        import numpy as np

        with tempfile.TemporaryDirectory() as directory:
            input_path = Path(directory) / "input.h5"
            with h5py.File(str(input_path), "w") as target:
                target.create_dataset("factorlist", data=np.asarray(["f{}".format(i).encode() for i in range(12)]))
                target.create_dataset("92700000", data=np.zeros((1, 12)))
                target.create_dataset("codelist_92700000", data=np.asarray([b"000001"]))
            with self.assertRaisesRegex(ValueError, "4968"):
                convert_hdf5(
                    input_path,
                    Path(directory) / "output.arrow",
                    expected_rows=4968,
                    expected_events=[92700000],
                    expected_factor_count=12,
                )

    def test_maps_927_source_checkpoint_to_926_evaluation_event(self):
        import h5py
        import numpy as np
        import pyarrow as pa
        import pyarrow.ipc as ipc

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "20251014" / "book_imbalance"
            root.mkdir(parents=True)
            input_path = root / "factors.h5"
            output_path = root / "20251014.arrow"
            with h5py.File(str(input_path), "w") as target:
                target.create_dataset("factorlist", data=np.asarray([b"factor"]))
                target.create_dataset("92700000", data=np.ones((1, 1)))
                target.create_dataset("codelist_92700000", data=np.asarray([b"000001"]))
            convert_hdf5(input_path, output_path, expected_rows=1, expected_events=[92700000], expected_factor_count=1)
            table = ipc.open_file(pa.memory_map(str(output_path), "r")).read_all()
            self.assertEqual(table.column("event").to_pylist(), [92600000])

    def test_infers_date_for_any_factor_family_directory(self):
        import h5py
        import numpy as np
        import pyarrow as pa
        import pyarrow.ipc as ipc

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "20251014" / "flow_pressure"
            root.mkdir(parents=True)
            input_path = root / "factors.h5"
            output_path = root / "20251014.arrow"
            with h5py.File(str(input_path), "w") as target:
                target.create_dataset("factorlist", data=np.asarray([b"factor"]))
                target.create_dataset("92700000", data=np.ones((1, 1)))
                target.create_dataset("codelist_92700000", data=np.asarray([b"000001"]))
            convert_hdf5(input_path, output_path, expected_rows=1, expected_events=[92700000], expected_factor_count=1)
            table = ipc.open_file(pa.memory_map(str(output_path), "r")).read_all()
            self.assertEqual(table.column("date").to_pylist(), ["20251014"])


if __name__ == "__main__":
    unittest.main()
