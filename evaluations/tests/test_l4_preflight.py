import contextlib
import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import h5py
import numpy as np

from evaluations import l4_preflight
from evaluations.l4_preflight import (
    DEFAULT_SOURCE_EVENTS,
    FROZEN_PREFLIGHT_DATES,
    _write_json_atomic,
    compose_date_summary,
    load_expected_factor_names,
    main,
    preflight_dates,
    run_frozen_preflight,
    validate_requested_dates,
)


SOURCE_EVENTS = (92700000, 100000000, 103000000, 110000000)
EVALUATION_EVENTS = (92600000, 100000000, 103000000, 110000000)
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
        output.create_dataset("codelist", data=np.asarray([b"metadata"]))
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


def _sha256_bytes(value):
    return "sha256:" + hashlib.sha256(value).hexdigest()


def _write_frozen_fixture(root):
    campaign_root = root / "campaign"
    manifests = campaign_root / "manifests"
    manifests.mkdir(parents=True)
    factor_names = ["factor_{:02d}".format(index) for index in range(48)]
    _write_batches(campaign_root, factor_names)
    dates = list(FROZEN_PREFLIGHT_DATES)
    production_bytes = ("\n".join(dates) + "\n").encode("utf-8")
    production_path = manifests / "formal-history-production-dates-v2.txt"
    production_path.write_bytes(production_bytes)
    manifest_path = manifests / "formal-history-dataset-v2.json"
    manifest_path.write_text(
        json.dumps({
            "dataset_id": "sfm_stream_001_formal_history_v2",
            "production_date_list_path": (
                "manifests/formal-history-production-dates-v2.txt"
            ),
            "production_date_list_sha256": _sha256_bytes(production_bytes),
            "events": {"source_snapshots": list(DEFAULT_SOURCE_EVENTS)},
        }),
        encoding="utf-8",
    )
    (campaign_root / "campaign.json").write_text(
        json.dumps({
            "campaign_id": "sfm_stream_001",
            "formal_history_dataset_manifest": (
                "manifests/formal-history-dataset-v2.json"
            ),
            "formal_history_dataset_id": "sfm_stream_001_formal_history_v2",
        }),
        encoding="utf-8",
    )
    for date in dates:
        _write_hdf5(
            root / "hdf5" / date / "all_families" / "factors.h5",
            factor_names=factor_names,
            events=DEFAULT_SOURCE_EVENTS,
        )
    return {
        "campaign_root": campaign_root,
        "dates": dates,
        "factor_names": factor_names,
        "production_path": production_path,
        "manifest_path": manifest_path,
        "hdf5_root": root / "hdf5",
        "arrow_root": root / "arrow",
    }


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
            self.assertEqual(summary["rows"], 8)
            self.assertEqual(summary["event_count"], 4)
            self.assertEqual(summary["factor_names"], list(FACTOR_NAMES))
            self.assertTrue(summary["all_values_finite"])
            self.assertTrue(summary["unique_symbol_event"])
            self.assertRegex(summary["hdf5_sha256"], r"^sha256:[0-9a-f]{64}$")
            self.assertRegex(summary["arrow_sha256"], r"^sha256:[0-9a-f]{64}$")
            self.assertEqual(result["aggregate"], {
                "date_count": 1,
                "total_rows": 8,
                "factor_count": 4,
                "event_count": 4,
                "decision": "validated_only",
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

    def test_preflight_rejects_extra_event_dataset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            date = "20210104"
            _write_hdf5(
                root / "hdf5" / date / "all_families" / "factors.h5",
                events=SOURCE_EVENTS + (113000000,),
            )
            dates_path = root / "dates.txt"
            dates_path.write_text(date + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unexpected event datasets"):
                preflight_dates(
                    [date], root / "hdf5", root / "arrow", root / "campaign",
                    dates_path, expected_source_events=SOURCE_EVENTS,
                    expected_factor_names=FACTOR_NAMES,
                )

    def test_preflight_rejects_extra_codelist_dataset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            date = "20210104"
            hdf5_path = (
                root / "hdf5" / date / "all_families" / "factors.h5"
            )
            _write_hdf5(hdf5_path)
            with h5py.File(str(hdf5_path), "a") as output:
                output.create_dataset(
                    "codelist_113000000",
                    data=np.asarray([b"000001.SZ", b"600000.SH"]),
                )
            dates_path = root / "dates.txt"
            dates_path.write_text(date + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unexpected codelist datasets"):
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
                    event: (
                        ["000001.SZ"]
                        if event == SOURCE_EVENTS[1]
                        else ["000001.SZ", "600000.SH"]
                    )
                    for event in SOURCE_EVENTS
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

    def test_frozen_preflight_requires_exact_five_dates_and_releases(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _write_frozen_fixture(Path(directory))

            result = run_frozen_preflight(
                fixture["dates"],
                fixture["hdf5_root"],
                fixture["arrow_root"],
                fixture["campaign_root"],
                fixture["production_path"],
            )

            self.assertEqual(
                result["aggregate"]["decision"], "continue_to_bulk_l4"
            )
            self.assertEqual(result["aggregate"]["date_count"], 5)
            self.assertEqual(result["aggregate"]["factor_count"], 48)
            self.assertEqual(result["aggregate"]["event_count"], 8)

    def test_frozen_preflight_rejects_missing_extra_or_reordered_dates(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _write_frozen_fixture(Path(directory))
            invalid_requests = (
                fixture["dates"][:-1],
                fixture["dates"] + ["20210105"],
                list(reversed(fixture["dates"])),
            )
            for requested in invalid_requests:
                with self.subTest(requested=requested):
                    with self.assertRaisesRegex(ValueError, "exact frozen preflight"):
                        run_frozen_preflight(
                            requested,
                            fixture["hdf5_root"],
                            fixture["arrow_root"],
                            fixture["campaign_root"],
                            fixture["production_path"],
                        )

    def test_frozen_preflight_rejects_wrong_date_list_path_or_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _write_frozen_fixture(Path(directory))
            other_path = Path(directory) / "other-dates.txt"
            other_path.write_text(
                "\n".join(fixture["dates"]) + "\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "manifest production date list"):
                run_frozen_preflight(
                    fixture["dates"], fixture["hdf5_root"],
                    fixture["arrow_root"], fixture["campaign_root"], other_path,
                )

            fixture["production_path"].write_text("20210104\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "production date list hash"):
                run_frozen_preflight(
                    fixture["dates"], fixture["hdf5_root"],
                    fixture["arrow_root"], fixture["campaign_root"],
                    fixture["production_path"],
                )

    def test_staging_validation_failure_preserves_existing_final(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            date = "20210104"
            hdf5_path = root / "hdf5" / date / "all_families" / "factors.h5"
            _write_hdf5(hdf5_path)
            dates_path = root / "dates.txt"
            dates_path.write_text(date + "\n", encoding="utf-8")
            final_path = root / "arrow" / (date + ".arrow")
            final_path.parent.mkdir(parents=True)
            final_path.write_bytes(b"old-final")

            with mock.patch(
                "evaluations.l4_preflight.validate_arrow",
                side_effect=ValueError("staging validation failed"),
            ):
                with self.assertRaisesRegex(ValueError, "staging validation failed"):
                    preflight_dates(
                        [date], root / "hdf5", root / "arrow", root / "campaign",
                        dates_path, expected_source_events=SOURCE_EVENTS,
                        expected_factor_names=FACTOR_NAMES,
                    )

            self.assertEqual(final_path.read_bytes(), b"old-final")
            self.assertEqual(list(final_path.parent.glob(".*.staging")), [])

    def test_hdf5_hash_change_rejects_before_publish(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            date = "20210104"
            hdf5_path = root / "hdf5" / date / "all_families" / "factors.h5"
            _write_hdf5(hdf5_path)
            dates_path = root / "dates.txt"
            dates_path.write_text(date + "\n", encoding="utf-8")
            final_path = root / "arrow" / (date + ".arrow")
            final_path.parent.mkdir(parents=True)
            final_path.write_bytes(b"old-final")
            real_sha256 = l4_preflight._sha256
            hdf5_hash_calls = [0]

            def changing_sha256(path):
                if Path(path) == hdf5_path:
                    hdf5_hash_calls[0] += 1
                    if hdf5_hash_calls[0] == 2:
                        return "sha256:" + ("0" * 64)
                return real_sha256(path)

            with mock.patch(
                "evaluations.l4_preflight._sha256", side_effect=changing_sha256
            ):
                with self.assertRaisesRegex(ValueError, "HDF5 input changed"):
                    preflight_dates(
                        [date], root / "hdf5", root / "arrow", root / "campaign",
                        dates_path, expected_source_events=SOURCE_EVENTS,
                        expected_factor_names=FACTOR_NAMES,
                    )

            self.assertEqual(final_path.read_bytes(), b"old-final")
            self.assertEqual(list(final_path.parent.glob(".*.staging")), [])

    def test_receipt_write_failure_preserves_old_file_and_cleans_unique_temps(self):
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "receipt.json"
            output_path.write_text("old-receipt\n", encoding="utf-8")
            temporary_paths = []

            def failing_replace(source, destination):
                temporary_paths.append(Path(source))
                raise OSError("replace failed")

            with mock.patch(
                "evaluations.l4_preflight.os.replace",
                side_effect=failing_replace,
            ):
                for value in (1, 2):
                    with self.assertRaisesRegex(OSError, "replace failed"):
                        _write_json_atomic(output_path, {"value": value})

            self.assertEqual(output_path.read_text(encoding="utf-8"), "old-receipt\n")
            self.assertEqual(len(set(temporary_paths)), 2)
            self.assertEqual(list(output_path.parent.glob(".receipt.json.*.tmp")), [])

    def test_cli_prints_json_and_atomically_writes_matching_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixture = _write_frozen_fixture(root)
            output_path = root / "receipt.json"
            output_path.write_text("stale\n", encoding="utf-8")

            completed = subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "evaluations.l4_preflight",
                    "--dates",
                    ",".join(fixture["dates"]),
                    "--hdf5-root",
                    str(fixture["hdf5_root"]),
                    "--arrow-root",
                    str(fixture["arrow_root"]),
                    "--campaign-root",
                    str(fixture["campaign_root"]),
                    "--production-date-list",
                    str(fixture["production_path"]),
                    "--output",
                    str(output_path),
                ],
                check=True,
                capture_output=True,
                text=True,
            )

            stdout_payload = json.loads(completed.stdout)
            output_payload = json.loads(output_path.read_text(encoding="utf-8"))
            self.assertEqual(output_payload, stdout_payload)
            self.assertEqual(stdout_payload["aggregate"]["date_count"], 5)
            self.assertEqual(stdout_payload["aggregate"]["factor_count"], 48)
            self.assertEqual(stdout_payload["aggregate"]["event_count"], 8)
            self.assertEqual(
                stdout_payload["aggregate"]["decision"], "continue_to_bulk_l4"
            )
            self.assertFalse(
                output_path.with_name("." + output_path.name + ".tmp").exists()
            )

    def test_cli_parser_rejects_range_arguments(self):
        base_arguments = [
            "--dates", "20210104",
            "--hdf5-root", "/unused/hdf5",
            "--arrow-root", "/unused/arrow",
            "--campaign-root", "/unused/campaign",
            "--production-date-list", "/unused/dates.txt",
        ]
        for flag in ("--sdate", "--edate", "--range"):
            with self.subTest(flag=flag):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as raised:
                        main(base_arguments + [flag, "20210104"])
                self.assertNotEqual(raised.exception.code, 0)

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
                    {"rows": 7, "events": list(EVALUATION_EVENTS),
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
                    {"rows": 8, "events": list(EVALUATION_EVENTS),
                     "factor_count": 4, "factor_names": list(FACTOR_NAMES),
                     "all_values_finite": True,
                     "unique_symbol_event": False},
                    expected_events=EVALUATION_EVENTS,
                )


if __name__ == "__main__":
    unittest.main()
