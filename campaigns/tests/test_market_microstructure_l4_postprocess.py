import tempfile
import unittest
from pathlib import Path


class MarketMicrostructureL4PostprocessTest(unittest.TestCase):
    def test_evaluator_view_keeps_readiness_and_drops_reason_columns(self):
        import pyarrow as pa
        import pyarrow.ipc as ipc
        from campaigns.sfm_stream_002.l4_postprocess import materialize_evaluator_views

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence"
            view = root / "view"
            evidence.mkdir()
            table = pa.table({
                "symbol": ["000001"], "date": ["20210104"],
                "event": [92600000], "alpha": [1.0],
                "ready_alpha": [True], "reason_alpha": pa.array([0], type=pa.uint8()),
            })
            with pa.OSFile(str(evidence / "20210104.arrow"), "wb") as sink:
                with ipc.RecordBatchFileWriter(sink, table.schema) as writer:
                    writer.write_table(table)

            rows = materialize_evaluator_views(evidence, view, ["20210104"])
            frame = pa.ipc.open_file(str(view / "20210104.arrow")).read_all()
            self.assertEqual(frame.column_names,
                             ["symbol", "date", "event", "alpha", "ready_alpha"])
            self.assertEqual(rows[0]["removed_reason_columns"], 1)


if __name__ == "__main__":
    unittest.main()
