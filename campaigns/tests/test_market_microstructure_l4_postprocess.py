import tempfile
import unittest
import importlib.util
from pathlib import Path


class MarketMicrostructureL4PostprocessTest(unittest.TestCase):
    def test_candidates_root_accepts_campaign_or_family_directory(self):
        from campaigns.sfm_stream_002.l4_postprocess import resolve_candidates_root

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "candidates"
            family = root / "family"
            family.mkdir(parents=True)
            (family / "candidate.json").write_text("{}", encoding="utf-8")
            self.assertEqual(resolve_candidates_root(root), root)
            self.assertEqual(resolve_candidates_root(family), root)

    @unittest.skipUnless(importlib.util.find_spec("pandas"), "pandas is optional in campaign-only environment")
    def test_completed_evaluation_is_reused_only_when_contract_matches(self):
        import pandas as pd
        from campaigns.sfm_stream_002.l4_postprocess import valid_evaluation_result

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "res_full.parquet"
            index = pd.MultiIndex.from_product(
                [["20210104"], [92600000, 100000000]], names=["date", "event"])
            metrics = ["D{}".format(i) for i in range(1, 11)] + ["LS", "Monotonicity", "IC", "RankIC"]
            frame = pd.DataFrame(0.0, index=index,
                                 columns=["alpha|" + metric for metric in metrics])
            frame.to_parquet(path)
            self.assertTrue(valid_evaluation_result(
                path, ["20210104"], ["alpha"], [92600000, 100000000]))
            self.assertFalse(valid_evaluation_result(
                path, ["20210104"], ["beta"], [92600000, 100000000]))

    @unittest.skipUnless(importlib.util.find_spec("pyarrow") and importlib.util.find_spec("numpy"),
                         "pyarrow/numpy are optional in campaign-only environment")
    def test_factor_view_contains_only_values_and_masks_unready_as_nan(self):
        import pyarrow as pa
        import pyarrow.ipc as ipc
        import numpy as np
        from campaigns.sfm_stream_002.l4_postprocess import materialize_factor_views

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence"
            view = root / "view"
            evidence.mkdir()
            table = pa.table({
                "symbol": ["000001", "000002"], "date": ["20210104", "20210104"],
                "event": [92600000, 92600000], "alpha": [1.0, 0.0],
                "ready_alpha": [True, False],
                "reason_alpha": pa.array([0, 3], type=pa.uint8()),
            })
            with pa.OSFile(str(evidence / "20210104.arrow"), "wb") as sink:
                with ipc.RecordBatchFileWriter(sink, table.schema) as writer:
                    writer.write_table(table)

            rows = materialize_factor_views(evidence, view, ["20210104"])
            frame = pa.ipc.open_file(str(view / "20210104.arrow")).read_all()
            self.assertEqual(frame.column_names,
                             ["symbol", "date", "event", "alpha"])
            values = np.asarray(frame["alpha"].to_numpy(zero_copy_only=False), dtype=float)
            self.assertEqual(values[0], 1.0)
            self.assertTrue(np.isnan(values[1]))
            self.assertEqual(rows[0]["removed_status_columns"], 2)


if __name__ == "__main__":
    unittest.main()
