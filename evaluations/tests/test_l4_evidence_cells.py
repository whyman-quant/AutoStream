import tempfile
import unittest
from pathlib import Path

import pandas as pd

from evaluations.l4_evidence_cells import build_evidence_cells


class L4EvidenceCellTests(unittest.TestCase):
    def test_builds_one_cell_without_status_columns(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            metrics = ["D{}".format(i) for i in range(1, 11)] + [
                "LS", "Monotonicity", "IC", "RankIC"]
            for split, dates in (("training", ["20210104", "20210105"]),
                                 ("observation", ["20230103", "20230104"])):
                path = root / split / "raw926" / "000906" / "res_full.parquet"
                path.parent.mkdir(parents=True)
                index = pd.MultiIndex.from_product(
                    [dates, [100000000]], names=["date", "event"])
                frame = pd.DataFrame(0.0, index=index,
                                     columns=["f|" + metric for metric in metrics])
                frame["f|RankIC"] = .02 if split == "training" else .01
                frame["f|IC"] = .02
                frame["f|LS"] = .001
                frame["f|Monotonicity"] = -.5
                frame.to_parquet(path)

            cells = build_evidence_cells(
                root,
                {"training": ["20210104", "20210105"],
                 "observation": ["20230103", "20230104"]},
                ["f"], [100000000], ["raw926"], ["000906"],
            )
            self.assertEqual(len(cells), 1)
            self.assertEqual(cells[0]["coverage"], 1.0)
            self.assertEqual(cells[0]["training_rank_ic"], .02)
            self.assertEqual(cells[0]["availability_source"], "finite_metrics")
            self.assertNotIn("readiness", cells[0])


if __name__ == "__main__":
    unittest.main()
