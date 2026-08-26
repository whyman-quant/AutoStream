import tempfile
import unittest
from pathlib import Path

from evaluations.pilot_postprocess import inspect_hdf5, postprocess_pilot, validate_arrow


SOURCE_EVENTS = [92700000, 100000000]
EVALUATION_EVENTS = [92600000, 100000000]


def _write_hdf5(path, symbols, factor_names=("factor_a", "factor_b")):
    import h5py
    import numpy as np

    path.parent.mkdir(parents=True, exist_ok=True)
    with h5py.File(str(path), "w") as target:
        target.create_dataset(
            "factorlist",
            data=np.asarray([name.encode() for name in factor_names]),
        )
        for event_index, event in enumerate(SOURCE_EVENTS):
            values = np.arange(len(symbols) * len(factor_names), dtype=float).reshape(
                len(symbols), len(factor_names)
            )
            target.create_dataset(str(event), data=values + event_index)
            target.create_dataset(
                "codelist_{}".format(event),
                data=np.asarray([symbol.encode() for symbol in symbols]),
            )


def _write_arrow(path, symbols, events, values):
    import pyarrow as pa
    import pyarrow.ipc as ipc

    table = pa.table(
        {
            "symbol": pa.array(symbols, type=pa.string()),
            "date": pa.array(["20251014"] * len(symbols), type=pa.string()),
            "event": pa.array(events, type=pa.int64()),
            "factor": pa.array(values, type=pa.float64()),
        }
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    with pa.OSFile(str(path), "wb") as sink:
        with ipc.RecordBatchFileWriter(sink, table.schema) as writer:
            writer.write_table(table)


class PilotPostprocessTests(unittest.TestCase):
    def test_inspects_each_date_and_converts_with_inferred_dimensions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            hdf5_root = root / "hdf5"
            arrow_root = root / "arrow"
            family = "flow_pressure"
            _write_hdf5(
                hdf5_root / "20251013" / family / "factors.h5",
                ["000001", "000002"],
            )
            _write_hdf5(
                hdf5_root / "20251014" / family / "factors.h5",
                ["000001", "000002", "000003"],
            )

            results = postprocess_pilot(
                family,
                ["20251013", "20251014"],
                hdf5_root,
                arrow_root,
                expected_events=SOURCE_EVENTS,
            )

            self.assertEqual([item["stock_count"] for item in results], [2, 3])
            self.assertEqual([item["factor_count"] for item in results], [2, 2])
            self.assertEqual([item["rows"] for item in results], [4, 6])
            self.assertEqual(
                [Path(item["path"]).name for item in results],
                ["20251013.arrow", "20251014.arrow"],
            )
            self.assertEqual(results[0]["events"], EVALUATION_EVENTS)
            self.assertTrue((arrow_root / "20251013.arrow").is_file())
            self.assertTrue((arrow_root / "20251014.arrow").is_file())

    def test_inspection_rejects_inconsistent_codelist_counts(self):
        import h5py
        import numpy as np

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "factors.h5"
            with h5py.File(str(path), "w") as target:
                target.create_dataset("factorlist", data=np.asarray([b"factor"]))
                target.create_dataset("92700000", data=np.ones((2, 1)))
                target.create_dataset("codelist_92700000", data=np.asarray([b"000001", b"000002"]))
                target.create_dataset("100000000", data=np.ones((1, 1)))
                target.create_dataset("codelist_100000000", data=np.asarray([b"000001"]))

            with self.assertRaisesRegex(ValueError, "inconsistent codelist stock counts"):
                inspect_hdf5(path, expected_events=SOURCE_EVENTS)

    def test_validates_arrow_rows_events_finite_values_and_unique_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            valid = root / "valid.arrow"
            _write_arrow(
                valid,
                ["000001", "000002", "000001", "000002"],
                [92600000, 92600000, 100000000, 100000000],
                [1.0, 2.0, 3.0, 4.0],
            )
            result = validate_arrow(
                valid,
                expected_stock_count=2,
                expected_events=EVALUATION_EVENTS,
                expected_factor_names=["factor"],
            )
            self.assertEqual(result["rows"], 4)
            self.assertEqual(result["events"], EVALUATION_EVENTS)
            self.assertEqual(result["factor_count"], 1)

            invalid_cases = [
                (
                    "wrong_rows",
                    ["000001"],
                    [92600000],
                    [1.0],
                    "row count",
                ),
                (
                    "wrong_events",
                    ["000001", "000002", "000001", "000002"],
                    [92600000, 92600000, 103000000, 103000000],
                    [1.0, 2.0, 3.0, 4.0],
                    "event set",
                ),
                (
                    "non_finite",
                    ["000001", "000002", "000001", "000002"],
                    [92600000, 92600000, 100000000, 100000000],
                    [1.0, float("nan"), 3.0, 4.0],
                    "non-finite",
                ),
                (
                    "duplicate_key",
                    ["000001", "000001", "000001", "000002"],
                    [92600000, 92600000, 100000000, 100000000],
                    [1.0, 2.0, 3.0, 4.0],
                    "duplicate \(symbol,event\)",
                ),
            ]
            for name, symbols, events, values, message in invalid_cases:
                with self.subTest(name=name):
                    path = root / (name + ".arrow")
                    _write_arrow(path, symbols, events, values)
                    with self.assertRaisesRegex(ValueError, message):
                        validate_arrow(
                            path,
                            expected_stock_count=2,
                            expected_events=EVALUATION_EVENTS,
                            expected_factor_names=["factor"],
                        )


if __name__ == "__main__":
    unittest.main()
