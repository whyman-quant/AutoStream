import hashlib
import unittest

import pandas as pd

from evaluations.l4_universe_materialization import (
    EXPECTED_EVENTS,
    materialize_date,
)


class L4UniverseMaterializationTests(unittest.TestCase):
    def test_uses_the_evaluator_exact_label_column_for_each_label(self):
        universe = pd.DataFrame({"000985": [1, 1]}, index=["000001.SZ", "000002.SZ"])
        tradability = pd.DataFrame(
            {"isdt1": [0, 0], "iszt1": [0, 0]}, index=universe.index
        )
        shared = pd.DataFrame(
            [
                ("000001", 92600000, 1.0, None),
                ("000002", 92600000, None, 2.0),
            ],
            columns=["symbol", "event", "v_1D_v_demean", "v_1D_v_neuted"],
        )

        rows = materialize_date(
            "20210104",
            universe,
            tradability,
            {"raw926": shared, "ease926": shared},
            universes=("000985",),
        )

        raw = next(r for r in rows if r["label"] == "raw926" and r["event"] == 92600000)
        ease = next(r for r in rows if r["label"] == "ease926" and r["event"] == 92600000)
        self.assertEqual(raw["symbols"], ["000001"])
        self.assertEqual(ease["symbols"], ["000002"])

    def test_rejects_ambiguous_label_columns_when_exact_column_is_missing(self):
        universe = pd.DataFrame({"000985": [1]}, index=["000001.SZ"])
        tradability = pd.DataFrame(
            {"isdt1": [0], "iszt1": [0]}, index=universe.index
        )
        ambiguous = pd.DataFrame(
            [("000001", 92600000, 1.0, 2.0)],
            columns=["symbol", "event", "wrong_a", "wrong_b"],
        )
        with self.assertRaisesRegex(ValueError, "exact label column"):
            materialize_date(
                "20210104",
                universe,
                tradability,
                {"raw926": ambiguous, "ease926": ambiguous},
                universes=("000985",),
            )

    def test_materializes_tradable_and_labeled_symbols_per_cell(self):
        universe = pd.DataFrame(
            {"000985": [1, 1, 0], "003800": [1, 0, 1]},
            index=["000001.SZ", "000002.SH", "000003.SZ"],
        )
        tradability = pd.DataFrame(
            {"isdt1": [0, 1, 0], "iszt1": [0, 0, 0]},
            index=["000001.SZ", "000002.SH", "000003.SZ"],
        )
        labels = {
            "raw926": pd.DataFrame(
                [("000001", 92600000, 1.0), ("000003", 92600000, None)],
                columns=["symbol", "event", "v_1D_v_demean"],
            ),
            "ease926": pd.DataFrame(
                [("000001", 92600000, 2.0), ("000003", 92600000, 3.0)],
                columns=["symbol", "event", "v_1D_v_neuted"],
            ),
        }
        rows = materialize_date("20210104", universe, tradability, labels, universes=("000985", "003800"))
        cell = next(r for r in rows if r["universe"] == "000985" and r["label"] == "raw926" and r["event"] == 92600000)
        self.assertEqual(cell["symbol_count"], 1)
        self.assertEqual(cell["label_nonnull_count"], 1)
        self.assertEqual(cell["non_limit_exclusion_count"], 1)
        self.assertEqual(cell["symbols"], ["000001"])
        self.assertEqual(cell["symbols_sha256"], "sha256:" + hashlib.sha256(b"000001\n").hexdigest())

    def test_rejects_holdout_dates(self):
        empty = pd.DataFrame(columns=["symbol", "event", "label"])
        with self.assertRaisesRegex(ValueError, "holdout"):
            materialize_date("20250102", empty, empty, {"raw926": empty, "ease926": empty})

    def test_emits_all_eight_events(self):
        empty_raw = pd.DataFrame(columns=["symbol", "event", "v_1D_v_demean"])
        empty_ease = pd.DataFrame(columns=["symbol", "event", "v_1D_v_neuted"])
        tradability = pd.DataFrame(columns=["isdt1", "iszt1"])
        rows = materialize_date("20210104", pd.DataFrame({"000985": []}), tradability, {"raw926": empty_raw, "ease926": empty_ease}, universes=("000985",))
        self.assertEqual({row["event"] for row in rows}, set(EXPECTED_EVENTS))
        self.assertEqual(len(rows), 8 * 1 * 2)

    def test_rejects_duplicate_label_keys(self):
        universe = pd.DataFrame({"000985": [1]}, index=["000001.SZ"])
        tradability = pd.DataFrame({"isdt1": [0], "iszt1": [0]}, index=universe.index)
        duplicate_raw = pd.DataFrame(
            [("000001", 92600000, 1.0), ("000001", 92600000, 2.0)],
            columns=["symbol", "event", "v_1D_v_demean"],
        )
        duplicate_ease = duplicate_raw.rename(columns={"v_1D_v_demean": "v_1D_v_neuted"})
        with self.assertRaisesRegex(ValueError, "duplicate"):
            materialize_date("20210104", universe, tradability, {"raw926": duplicate_raw, "ease926": duplicate_ease}, universes=("000985",))


if __name__ == "__main__":
    unittest.main()
