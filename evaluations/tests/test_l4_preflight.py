import json
import tempfile
import unittest
from pathlib import Path

import h5py
import numpy as np

from evaluations.l4_preflight import (
    compose_date_summary,
    load_expected_factor_names,
    preflight_dates,
    validate_requested_dates,
)


SOURCE_EVENTS = (92700000, 100000000)
EVALUATION_EVENTS = (92600000, 100000000)
FACTOR_NAMES = ("factor_a", "factor_b", "factor_c", "factor_d")


def _write_hdf5(
    path,
    factor_names=FACTOR_NAMES,
    events=SOURCE_EVENTS,
    symbol_rows=None,
    non_finite=False,
):
    path.parent.mkdir(parents=True, exist_ok=True)
    if symbol_rows is None:
        symbol_rows = {
            event: ["000001.SZ", "600000.SH"] for event in events
        }
    with h5py.File(str(path), "w") as output:
        output.create_dataset(
            "factorlist", data=np.asarray([name.encode() for name in factor_names])
        )
        for event in events:
            symbols = symbol_rows[event]
            values = np.arange(
                len(symbols) * len(factor_names), dtype=float
            ).reshape(len(symbols), len(factor_names))
            if non_finite and event == events[0]:
                values[0, 0] = np.nan
            output.create_dataset(str(event), data=values)
            output.create_dataset(
                "codelist_" + str(event),
                data=np.asarray([symbol.encode() for symbol in symbols]),
            )


def _write_batches(campaign_root, names):
    batches = campaign_root / "batches"
    batches.mkdir(parents=True)
    families = (
        "book_imbalance",
        "flow_pressure",
        "liquidity_resilience",
        "impact_efficiency",
    )
    for index, family in enumerate(families):
        family_names = names[index * 12 : (index + 1) * 12]
        (batches / (family + "_seed_v1.json")).write_text(
            json.dumps({"candidate_ids": family_names}), encoding="utf-8"
        )


class L4PreflightTests(unittest.TestCase):
    def test_load_expected_factor_names_uses_frozen_family_order(self):
        with tempfile.TemporaryDirectory() as directory:
            campaign_root = Path(directory)
            names = ["factor_{:02d}".format(index) for index in range(48)]
            _write_batches(campaign_root, names)

            self.assertEqual(
                load_expected_factor_names(campaign_root=campaign_root), names
            )
            batch_paths = [
                campaign_root / "batches" / (family + "_seed_v1.json")
                for family in (
                    "book_imbalance",
                    "flow_pressure",
                    "liquidity_resilience",
                    "impact_efficiency",
                )
            ]
            self.assertEqual(
                load_expected_factor_names(batch_paths=reversed(batch_paths)), names
            )

    def test_load_expected_factor_names_rejects_non_unique_or_non_48(self):
        with tempfile.TemporaryDirectory() as directory:
            campaign_root = Path(directory)
            _write_batches(campaign_root, ["factor"] * 48)
            with self.assertRaisesRegex(ValueError, "48 unique"):
                load_expected_factor_names(campaign_root=campaign_root)

    def test_validate_requested_dates_accepts_only_unique_production_dates(self):
        with tempfile.TemporaryDirectory() as directory:
            dates_path = Path(directory) / "dates.txt"
            dates_path.write_text("20210104\n20241231\n", encoding="utf-8")
            self.assertEqual(
                validate_requested_dates(["20210104", "20241231"], dates_path),
                ["20210104", "20241231"],
            )

    def test_validate_requested_dates_rejects_empty_duplicate_outside_and_2025(self):
        with tempfile.TemporaryDirectory() as directory:
            dates_path = Path(directory) / "dates.txt"
            dates_path.write_text("20210104\n20241231\n", encoding="utf-8")
            for requested in (
                [],
                ["20210104", "20210104"],
                ["20200102"],
                ["20250102"],
            ):
                with self.subTest(requested=requested):
                    with self.assertRaises(ValueError):
                        validate_requested_dates(requested, dates_path)

    def test_preflight_converts_and_summarizes_valid_date(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            date = "20210104"
            hdf5_path = root / "hdf5" / date / "all_families" / "factors.h5"
            _write_hdf5(hdf5_path)
            production_dates = root / "dates.txt"
            production_dates.write_text(date + "\n", encoding="utf-8")

            result = preflight_dates(
                [date],
                root / "hdf5",
                root / "arrow",
                root / "campaign",
                production_dates,
                expected_source_events=SOURCE_EVENTS,
                expected_factor_names=FACTOR_NAMES,
            )

            summary = result["dates"][0]
            self.assertEqual(summary["date"], date)
            self.assertEqual(summary["stock_count"], 2)
            self.assertEqual(summary["rows"], 4)
            self.assertEqual(summary["event_count"], 2)
            self.assertEqual(summary["factor_names"], list(FACTOR_NAMES))
            self.assertTrue(summary["all_values_finite"])
            self.assertTrue(summary["unique_symbol_event"])
            self.assertRegex(summary["hdf5_sha256"], r"^sha256:[0-9a-f]{64}$")
            self.assertRegex(summary["arrow_sha256"], r"^sha256:[0-9a-f]{64}$")
            self.assertEqual(result["aggregate"], {
                "date_count": 1,
                "total_rows": 4,
                "factor_count": 4,
                "event_count": 2,
                "decision": "continue_to_bulk_l4",
            })
            self.assertTrue((root / "arrow" / (date + ".arrow")).is_file())

    def test_preflight_rejects_wrong_factor_order(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            date = "20210104"
            _write_hdf5(
                root / "hdf5" / date / "all_families" / "factors.h5",
                factor_names=tuple(reversed(FACTOR_NAMES)),
            )
            dates_path = root / "dates.txt"
            dates_path.write_text(date + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "factor names mismatch"):
                preflight_dates(
                    [date], root / "hdf5", root / "arrow", root / "campaign",
                    dates_path, expected_source_events=SOURCE_EVENTS,
                    expected_factor_names=FACTOR_NAMES,
                )

    def test_preflight_rejects_invalid_hdf5_contracts(self):
        cases = {
            "missing event": {
                "events": SOURCE_EVENTS[:1],
            },
            "inconsistent codelist": {
                "symbol_rows": {
                    SOURCE_EVENTS[0]: ["000001.SZ", "600000.SH"],
                    SOURCE_EVENTS[1]: ["000001.SZ"],
                },
            },
            "non-finite": {"non_finite": True},
            "duplicate symbols": {
                "symbol_rows": {
                    event: ["000001.SZ", "000001.SZ"] for event in SOURCE_EVENTS
                },
            },
        }
        for name, options in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                date = "20210104"
                _write_hdf5(
                    root / "hdf5" / date / "all_families" / "factors.h5",
                    **options
                )
                dates_path = root / "dates.txt"
                dates_path.write_text(date + "\n", encoding="utf-8")
                with self.assertRaises(ValueError):
                    preflight_dates(
                        [date], root / "hdf5", root / "arrow", root / "campaign",
                        dates_path, expected_source_events=SOURCE_EVENTS,
                        expected_factor_names=FACTOR_NAMES,
                    )

    def test_compose_date_summary_rejects_wrong_arrow_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            hdf5_path = root / "factors.h5"
            arrow_path = root / "factors.arrow"
            hdf5_path.write_bytes(b"hdf5")
            arrow_path.write_bytes(b"arrow")
            with self.assertRaisesRegex(ValueError, "row count mismatch"):
                compose_date_summary(
                    "20210104", hdf5_path, arrow_path,
                    {"stock_count": 2, "factor_count": 4,
                     "factor_names": list(FACTOR_NAMES)},
                    {"rows": 3, "events": list(EVALUATION_EVENTS),
                     "factor_count": 4, "factor_names": list(FACTOR_NAMES),
                     "all_values_finite": True,
                     "unique_symbol_event": True},
                    expected_events=EVALUATION_EVENTS,
                )

    def test_compose_date_summary_rejects_duplicate_symbol_event(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            hdf5_path = root / "factors.h5"
            arrow_path = root / "factors.arrow"
            hdf5_path.write_bytes(b"hdf5")
            arrow_path.write_bytes(b"arrow")
            with self.assertRaisesRegex(ValueError, "duplicate"):
                compose_date_summary(
                    "20210104", hdf5_path, arrow_path,
                    {"stock_count": 2, "factor_count": 4,
                     "factor_names": list(FACTOR_NAMES)},
                    {"rows": 4, "events": list(EVALUATION_EVENTS),
                     "factor_count": 4, "factor_names": list(FACTOR_NAMES),
                     "all_values_finite": True,
                     "unique_symbol_event": False},
                    expected_events=EVALUATION_EVENTS,
                )


if __name__ == "__main__":
    unittest.main()
