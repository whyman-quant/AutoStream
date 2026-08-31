import datetime as dt
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from campaigns import l4_production


def _sha256(path):
    return "sha256:" + hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _dates(start, count):
    first = dt.datetime.strptime(start, "%Y%m%d").date()
    return [(first + dt.timedelta(days=index)).strftime("%Y%m%d") for index in range(count)]


def _write_campaign(root, production_dates=None, holdout_dates=None):
    production_dates = production_dates or _dates("20210101", 969)
    holdout_dates = holdout_dates or _dates("20250102", 228)
    manifest_dir = root / "manifests"
    batch_dir = root / "batches"
    manifest_dir.mkdir(parents=True)
    batch_dir.mkdir(parents=True)
    production_path = manifest_dir / "production.txt"
    holdout_path = manifest_dir / "holdout.txt"
    production_path.write_text("\n".join(production_dates) + "\n", encoding="utf-8")
    holdout_path.write_text("\n".join(holdout_dates) + "\n", encoding="utf-8")
    manifest = {
        "schema_version": 2,
        "dataset_id": "sfm_stream_001_formal_history_v2",
        "date_count": 969,
        "date_list_path": "manifests/production.txt",
        "date_list_sha256": _sha256(production_path),
        "production_date_list_path": "manifests/production.txt",
        "production_date_list_sha256": _sha256(production_path),
        "holdout_date_list_path": "manifests/holdout.txt",
        "holdout_date_list_sha256": _sha256(holdout_path),
        "splits": {"holdout": {"date_count": 228}},
        "leakage_policy": {"holdout_may_read": []},
    }
    (manifest_dir / "formal-history-dataset-v2.json").write_text(
        json.dumps(manifest), encoding="utf-8"
    )
    (root / "campaign.json").write_text(
        json.dumps({
            "formal_history_dataset_id": manifest["dataset_id"],
            "formal_history_dataset_manifest": "manifests/formal-history-dataset-v2.json",
        }),
        encoding="utf-8",
    )
    names = []
    for family in l4_production.FAMILIES:
        family_names = ["{}_{}".format(family, index) for index in range(12)]
        names.extend(family_names)
        (batch_dir / (family + "_seed_v1.json")).write_text(
            json.dumps({"candidate_ids": family_names}), encoding="utf-8"
        )
    return manifest, production_dates, holdout_dates, names


def _write_inputs(root, output_root):
    binary = root / "release" / "main"
    config = root / "release" / "config.json"
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"frozen-binary")
    binary.chmod(0o755)
    config.write_text(
        json.dumps({
            "factors_config": {
                "save_info": {
                    "dir": str(output_root / "[DATE]" / "all_families")
                }
            }
        }),
        encoding="utf-8",
    )
    return binary.resolve(), config.resolve()


def _write_placeholder(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"hdf5-placeholder")


def _inspection(names):
    return {
        "stock_count": 2,
        "event_count": 8,
        "factor_count": 48,
        "factor_names": names,
    }


class L4ProductionTests(unittest.TestCase):
    def test_chunk_dates_splits_969_dates_into_194_ordered_chunks(self):
        dates = ["{:08d}".format(index) for index in range(969)]

        chunks = l4_production.chunk_dates(dates)

        self.assertEqual(len(chunks), 194)
        self.assertTrue(all(len(chunk) == 5 for chunk in chunks[:-1]))
        self.assertEqual(len(chunks[-1]), 4)
        self.assertEqual([date for chunk in chunks for date in chunk], dates)

    def test_chunk_dates_rejects_empty_and_nonpositive_size(self):
        with self.assertRaises(ValueError):
            l4_production.chunk_dates([])
        with self.assertRaises(ValueError):
            l4_production.chunk_dates(["20210104"], 0)
        with self.assertRaises(ValueError):
            l4_production.chunk_dates(["20210104", "20210104"])

    def test_validate_dates_against_frozen_list_is_pure_and_holdout_safe(self):
        frozen = ["20210104", "20210105"]
        self.assertEqual(
            l4_production.validate_dates_against_frozen_list(
                ["20210105"], frozen
            ),
            ["20210105"],
        )
        for requested in (
            [],
            ["20210104", "20210104"],
            ["2021-01-04"],
            ["20250102"],
            ["20210106"],
        ):
            with self.subTest(requested=requested), self.assertRaises(ValueError):
                l4_production.validate_dates_against_frozen_list(
                    requested, frozen
                )

    def test_assign_lanes_is_stable_round_robin_with_each_chunk_once(self):
        chunks = [[str(index)] for index in range(10)]

        lanes = l4_production.assign_lanes(chunks, lane_count=4)

        self.assertEqual(lanes, [chunks[0::4], chunks[1::4], chunks[2::4], chunks[3::4]])
        self.assertEqual(sum(len(lane) for lane in lanes), len(chunks))
        with self.assertRaises(ValueError):
            l4_production.assign_lanes([], lane_count=4)
        with self.assertRaises(ValueError):
            l4_production.assign_lanes(chunks, lane_count=0)
        with self.assertRaises(ValueError):
            l4_production.assign_lanes(chunks, lane_count=5)

    def test_load_active_v2_contract_accepts_only_frozen_sealed_lists(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, production, holdout, _ = _write_campaign(root)

            contract = l4_production.load_active_v2_contract(root)

            self.assertEqual(contract["dataset_id"], manifest["dataset_id"])
            self.assertEqual(contract["production_dates"], production)
            self.assertEqual(contract["holdout_dates"], holdout)
            self.assertEqual(contract["production_date_list_sha256"], manifest["production_date_list_sha256"])

    def test_load_active_v2_contract_rejects_wrong_path_hash_holdout_and_2025(self):
        mutations = (
            lambda manifest, production, holdout: manifest.update(dataset_id="wrong"),
            lambda manifest, production, holdout: manifest.update(production_date_list_path="manifests/other.txt"),
            lambda manifest, production, holdout: manifest.update(production_date_list_sha256="sha256:" + "0" * 64),
            lambda manifest, production, holdout: manifest["leakage_policy"].update(holdout_may_read=["labels"]),
        )
        for mutate in mutations:
            with self.subTest(mutate=mutate), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                manifest, production, holdout, _ = _write_campaign(root)
                mutate(manifest, production, holdout)
                (root / "manifests/formal-history-dataset-v2.json").write_text(json.dumps(manifest), encoding="utf-8")
                with self.assertRaises(ValueError):
                    l4_production.load_active_v2_contract(root)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            production = _dates("20210101", 968) + ["20250101"]
            _write_campaign(root, production_dates=production)
            with self.assertRaises(ValueError):
                l4_production.load_active_v2_contract(root)

    def test_build_plan_freezes_paths_hashes_chunks_lanes_and_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, _ = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)

            plan = l4_production.build_plan(campaign_root, binary, config, output_root)

            self.assertEqual(plan["dataset_id"], manifest["dataset_id"])
            self.assertEqual(plan["date_count"], 969)
            self.assertEqual(plan["chunk_count"], 194)
            self.assertEqual(plan["chunk_size"], 5)
            self.assertEqual(plan["lane_count"], 4)
            self.assertEqual(plan["binary"], str(binary))
            self.assertEqual(plan["config"], str(config))
            self.assertEqual(plan["binary_sha256"], _sha256(binary))
            self.assertEqual(plan["config_sha256"], _sha256(config))
            self.assertEqual([date for chunk in plan["chunks"] for date in chunk["dates"]], production)
            previous = {}
            for chunk in plan["chunks"]:
                self.assertEqual(chunk["depends_on_chunk_id"], previous.get(chunk["lane"]))
                previous[chunk["lane"]] = chunk["chunk_id"]

    def test_build_plan_rejects_relative_missing_mismatched_output_and_hash_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, Path("main"), config, output_root)
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, binary, Path("config.json"), output_root)
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, (root / "missing").resolve(), config, output_root)
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, binary, (root / "missing-config").resolve(), output_root)
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, binary, config, (root / "other").resolve())
            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root, binary, config, output_root,
                    expected_binary_sha256="sha256:" + "0" * 64,
                )

    def test_build_plan_accepts_configured_output_through_equivalent_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            real_parent = root / "real"
            real_parent.mkdir()
            alias_parent = root / "alias"
            alias_parent.symlink_to(real_parent, target_is_directory=True)
            configured_output = alias_parent / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, configured_output)

            plan = l4_production.build_plan(
                campaign_root, binary, config, configured_output
            )

            self.assertEqual(plan["output_root"], str(configured_output.resolve()))

    def test_dry_run_cli_never_invokes_mybatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan_output = root / "plan.json"
            with mock.patch("campaigns.l4_production.subprocess.run") as run, mock.patch("sys.stdout"):
                returncode = l4_production.main([
                    "plan", "--campaign-root", str(campaign_root),
                    "--binary", str(binary), "--config", str(config),
                    "--output-root", str(output_root), "--plan-output", str(plan_output),
                ])
            self.assertEqual(returncode, 0)
            run.assert_not_called()
            self.assertEqual(json.loads(plan_output.read_text(encoding="utf-8"))["chunk_count"], 194)

    def test_submit_records_four_roots_dependencies_resources_and_plan_sha(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan_output = root / "plan.json"
            responses = [mock.Mock(returncode=0, stdout="Jobid:{}\n".format(7000 + index), stderr="") for index in range(194)]
            real_loader = l4_production.load_active_v2_contract
            with mock.patch(
                "campaigns.l4_production.load_active_v2_contract",
                wraps=real_loader,
            ) as load_contract, mock.patch(
                "campaigns.l4_production.subprocess.run", side_effect=responses
            ) as run, mock.patch("sys.stdout"):
                l4_production.main([
                    "plan", "--campaign-root", str(campaign_root),
                    "--binary", str(binary), "--config", str(config),
                    "--output-root", str(output_root), "--plan-output", str(plan_output), "--submit",
                ])
            self.assertEqual(load_contract.call_count, 1)
            receipt_path = l4_production.submission_receipt_path(plan_output)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            self.assertEqual(receipt["plan_sha256"], _sha256(plan_output))
            self.assertEqual(len(receipt["jobs"]), 194)
            root_jobs = [job for job in receipt["jobs"] if job["depends_on_job_id"] is None]
            self.assertEqual(len(root_jobs), 4)
            for index, call in enumerate(run.call_args_list):
                command = call.args[0]
                self.assertIn("-c12", command)
                self.assertIn("-m243G", command)
                self.assertIn("cpu_wgh", command)
                self.assertIn("-t2:00:00", command)
                if index < 4:
                    self.assertNotIn("-d", command)
                else:
                    self.assertIn("afterok:{}".format(7000 + index - 4), command)

    def test_submit_bad_output_or_midstream_failure_writes_no_receipt(self):
        for responses in (
            [mock.Mock(returncode=0, stdout="submitted\n", stderr="")],
            [mock.Mock(returncode=0, stdout="Jobid:7000\n", stderr=""), mock.Mock(returncode=1, stdout="", stderr="no")],
        ):
            with self.subTest(responses=responses), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                campaign_root = root / "campaign"
                output_root = (root / "outputs").resolve()
                _write_campaign(campaign_root)
                binary, config = _write_inputs(root, output_root)
                plan_output = root / "plan.json"
                with mock.patch("campaigns.l4_production.subprocess.run", side_effect=responses), mock.patch("sys.stdout"):
                    with self.assertRaises(RuntimeError):
                        l4_production.main([
                            "plan", "--campaign-root", str(campaign_root),
                            "--binary", str(binary), "--config", str(config),
                            "--output-root", str(output_root), "--plan-output", str(plan_output), "--submit",
                        ])
                self.assertFalse(l4_production.submission_receipt_path(plan_output).exists())

    def test_submit_rejects_tampered_plan_file_and_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(campaign_root, binary, config, output_root)
            plan_path = root / "plan.json"
            plan_path.write_text(json.dumps(plan) + "\n", encoding="utf-8")
            plan["chunks"][0]["dates"][0] = plan["chunks"][0]["dates"][1]

            with mock.patch("campaigns.l4_production.subprocess.run") as run:
                with self.assertRaises(ValueError):
                    l4_production.submit_plan(plan, plan_path)

            run.assert_not_called()

            plan_path.write_text(json.dumps(plan) + "\n", encoding="utf-8")
            with mock.patch("campaigns.l4_production.subprocess.run") as run:
                with self.assertRaises(ValueError):
                    l4_production.submit_plan(plan, plan_path)

            run.assert_not_called()

    def test_run_chunk_rejects_holdout_and_frozen_hash_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, holdout, _ = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            kwargs = dict(
                campaign_root=campaign_root, binary=binary, config=config,
                output_root=output_root, binary_sha256=_sha256(binary),
                config_sha256=_sha256(config), date_list_sha256=manifest["production_date_list_sha256"],
            )
            with mock.patch("campaigns.l4_production.subprocess.run") as run:
                with self.assertRaises(ValueError):
                    l4_production.run_chunk([holdout[0]], **kwargs)
                run.assert_not_called()
            binary.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                l4_production.run_chunk([production[0]], **kwargs)

    def test_run_chunk_rejects_duplicate_or_more_than_five_dates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, _ = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            frozen = (
                campaign_root, binary, config, output_root,
                _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
            )
            with self.assertRaises(ValueError):
                l4_production.run_chunk([production[0], production[0]], *frozen)
            with self.assertRaises(ValueError):
                l4_production.run_chunk(production[:6], *frozen)

    def test_run_chunk_does_not_open_holdout_date_list(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, names = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            target = output_root / production[0] / "all_families" / "factors.h5"
            _write_placeholder(target)
            real_reader = l4_production._read_frozen_lines

            def reject_holdout(path, expected_sha256, description):
                if description == "holdout date list":
                    raise AssertionError("run-chunk accessed holdout")
                return real_reader(path, expected_sha256, description)

            with mock.patch(
                "campaigns.l4_production._read_frozen_lines",
                side_effect=reject_holdout,
            ), mock.patch(
                "evaluations.l4_preflight.validate_hdf5_only",
                return_value=_inspection(names),
            ):
                result = l4_production.run_chunk(
                    [production[0]], campaign_root, binary, config, output_root,
                    _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
                )

            self.assertEqual(result[0]["status"], "existing_valid")

    def test_run_chunk_rejects_date_added_after_verified_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, names = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            production_path = campaign_root / "manifests" / "production.txt"
            real_loader = l4_production._load_active_v2_production_contract

            def load_then_tamper(active_campaign_root):
                contract = real_loader(active_campaign_root)
                production_path.write_text(
                    production_path.read_text(encoding="utf-8") + "20200101\n",
                    encoding="utf-8",
                )
                return contract

            with mock.patch(
                "campaigns.l4_production._load_active_v2_production_contract",
                side_effect=load_then_tamper,
            ), mock.patch(
                "campaigns.l4_production.subprocess.run"
            ) as run, mock.patch(
                "evaluations.l4_preflight.validate_hdf5_only",
                return_value=_inspection(names),
            ):
                with self.assertRaises(ValueError):
                    l4_production.run_chunk(
                        ["20200101"], campaign_root, binary, config, output_root,
                        _sha256(binary), _sha256(config),
                        manifest["production_date_list_sha256"],
                    )

            run.assert_not_called()

    def test_run_chunk_uses_legal_date_from_verified_snapshot_after_tamper(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, names = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            target = output_root / production[0] / "all_families" / "factors.h5"
            _write_placeholder(target)
            production_path = campaign_root / "manifests" / "production.txt"
            real_loader = l4_production._load_active_v2_production_contract

            def load_then_tamper(active_campaign_root):
                contract = real_loader(active_campaign_root)
                production_path.write_text(
                    production_path.read_text(encoding="utf-8") + "20200101\n",
                    encoding="utf-8",
                )
                return contract

            with mock.patch(
                "campaigns.l4_production._load_active_v2_production_contract",
                side_effect=load_then_tamper,
            ), mock.patch(
                "campaigns.l4_production.subprocess.run"
            ) as run, mock.patch(
                "evaluations.l4_preflight.validate_hdf5_only",
                return_value=_inspection(names),
            ):
                result = l4_production.run_chunk(
                    [production[0]], campaign_root, binary, config, output_root,
                    _sha256(binary), _sha256(config),
                    manifest["production_date_list_sha256"],
                )

            run.assert_not_called()
            self.assertEqual(result[0]["status"], "existing_valid")

    def test_run_chunk_existing_valid_skips_binary_and_existing_invalid_is_preserved(self):
        for invalid in (False, True):
            with self.subTest(invalid=invalid), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                campaign_root = root / "campaign"
                output_root = (root / "outputs").resolve()
                manifest, production, _, names = _write_campaign(campaign_root)
                binary, config = _write_inputs(root, output_root)
                target = output_root / production[0] / "all_families" / "factors.h5"
                _write_placeholder(target)
                before = target.read_bytes()
                validation = ValueError("invalid HDF5") if invalid else _inspection(names)
                with mock.patch("campaigns.l4_production.subprocess.run") as run, mock.patch(
                    "evaluations.l4_preflight.validate_hdf5_only",
                    side_effect=validation if invalid else None,
                    return_value=None if invalid else validation,
                ):
                    if invalid:
                        with self.assertRaises(ValueError):
                            l4_production.run_chunk(
                                [production[0]], campaign_root, binary, config, output_root,
                                _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
                            )
                    else:
                        result = l4_production.run_chunk(
                            [production[0]], campaign_root, binary, config, output_root,
                            _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
                        )
                        self.assertEqual(result[0]["status"], "existing_valid")
                run.assert_not_called()
                self.assertEqual(target.read_bytes(), before)

    def test_run_chunk_missing_calls_fixed_binary_then_validates(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, names = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            target = output_root / production[0] / "all_families" / "factors.h5"

            def produce(command, **kwargs):
                _write_placeholder(target)
                return mock.Mock(returncode=0, stdout="", stderr="")

            with mock.patch("campaigns.l4_production.subprocess.run", side_effect=produce) as run, mock.patch(
                "evaluations.l4_preflight.validate_hdf5_only",
                return_value=_inspection(names),
            ):
                result = l4_production.run_chunk(
                    [production[0]], campaign_root, binary, config, output_root,
                    _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
                )
            self.assertEqual(result[0]["status"], "produced_valid")
            self.assertEqual(run.call_args.args[0], [
                str(binary), "date=" + production[0], "thread_num=8",
                "config_file=" + str(config), "stock=all",
            ])

    def test_run_chunk_nonzero_fails_immediately(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            manifest, production, _, _ = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            with mock.patch(
                "campaigns.l4_production.subprocess.run",
                return_value=mock.Mock(returncode=9, stdout="bad", stderr="worse"),
            ):
                with self.assertRaises(RuntimeError):
                    l4_production.run_chunk(
                        [production[0], production[1]], campaign_root, binary, config, output_root,
                        _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
                    )


if __name__ == "__main__":
    unittest.main()
