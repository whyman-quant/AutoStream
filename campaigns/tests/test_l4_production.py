import datetime as dt
import fcntl
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from campaigns import l4_production


PERSISTENT_TEST_TEMP_BASE = Path("/var/tmp")
REAL_MNT_TEST_BASE = Path("/mnt/beegfs_ssd_raid91/10513_fangwei")
PERSISTENT_TEST_SUBMISSION_WORKDIR = (
    REAL_MNT_TEST_BASE / "l4-production-tests"
)
PERSISTENT_TEST_SUBMISSION_WORKDIR.mkdir(parents=True, exist_ok=True)
tempfile.tempdir = str(PERSISTENT_TEST_TEMP_BASE)


def _sha256(path):
    return "sha256:" + hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _dates(start, count):
    first = dt.datetime.strptime(start, "%Y%m%d").date()
    return [(first + dt.timedelta(days=index)).strftime("%Y%m%d") for index in range(count)]


def _write_campaign(root, production_dates=None, holdout_dates=None):
    production_dates = production_dates or _dates("20210101", 969)
    holdout_dates = holdout_dates or (_dates("20250102", 227) + ["20251210"])
    manifest_dir = root / "manifests"
    batch_dir = root / "batches"
    manifest_dir.mkdir(parents=True)
    batch_dir.mkdir(parents=True)
    production_path = manifest_dir / "production.txt"
    holdout_path = manifest_dir / "holdout.txt"
    parent_path = manifest_dir / "parent.txt"
    production_path.write_text("\n".join(production_dates) + "\n", encoding="utf-8")
    holdout_path.write_text("\n".join(holdout_dates) + "\n", encoding="utf-8")
    parent_path.write_text(
        "\n".join(production_dates + holdout_dates) + "\n", encoding="utf-8"
    )
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
        "splits": {"holdout": {
            "date_count": 228,
            "date_start": "20250102",
            "date_end": "20251210",
        }},
        "parent_date_list_path": "manifests/parent.txt",
        "parent_date_list_sha256": _sha256(parent_path),
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
    for relative in (
        "l4_runner_bootstrap.py",
        "campaigns/__init__.py",
        "campaigns/l4_production.py",
        "evaluations/__init__.py",
        "evaluations/l4_preflight.py",
        "evaluations/pilot_postprocess.py",
        "evaluations/convert_production_hdf5.py",
    ):
        runner_file = root / relative
        runner_file.parent.mkdir(parents=True, exist_ok=True)
        runner_file.write_text("# frozen {}\n".format(relative), encoding="utf-8")
    (root / "slurm").symlink_to(
        PERSISTENT_TEST_SUBMISSION_WORKDIR, target_is_directory=True
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


def _runner_args(root, campaign_root):
    runner_root = Path(root).resolve()
    return (
        runner_root,
        l4_production.collect_runner_provenance(runner_root, campaign_root),
    )


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

            plan = l4_production.build_plan(campaign_root, binary, config, output_root, root.resolve())

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
                l4_production.build_plan(campaign_root, Path("main"), config, output_root, root.resolve())
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, binary, Path("config.json"), output_root, root.resolve())
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, (root / "missing").resolve(), config, output_root, root.resolve())
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, binary, (root / "missing-config").resolve(), output_root, root.resolve())
            with self.assertRaises(ValueError):
                l4_production.build_plan(campaign_root, binary, config, (root / "other").resolve(), root.resolve())
            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root, binary, config, output_root, root.resolve(),
                    expected_binary_sha256="sha256:" + "0" * 64,
                )
            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root,
                    binary,
                    config,
                    output_root,
                    root.resolve(),
                    Path("slurm"),
                )
            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root,
                    binary,
                    config,
                    output_root,
                    root.resolve(),
                    (root / "missing-slurm").resolve(),
                )
            inside_runner = root / "inside-runner-submit"
            inside_runner.mkdir()
            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root,
                    binary,
                    config,
                    output_root,
                    root.resolve(),
                    inside_runner,
                )
            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root,
                    binary,
                    config,
                    output_root,
                    root.resolve(),
                    Path("/tmp"),
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
                campaign_root, binary, config, configured_output, root.resolve()
            )

            self.assertEqual(plan["output_root"], str(configured_output.resolve()))

    def test_submission_workdir_requires_resolved_path_beneath_real_mnt(self):
        runner_root = Path("/var/tmp/frozen-l4-runner")
        for fake_base in (Path("/tmp"), Path("/var/tmp")):
            with self.subTest(fake_base=fake_base), tempfile.TemporaryDirectory(
                dir=str(fake_base)
            ) as directory:
                fake_workdir = Path(directory) / "mnt" / "fake"
                fake_workdir.mkdir(parents=True)
                with self.assertRaisesRegex(ValueError, "beneath /mnt"):
                    l4_production._validate_submission_workdir(
                        fake_workdir, runner_root
                    )

        with tempfile.TemporaryDirectory(
            dir=str(REAL_MNT_TEST_BASE)
        ) as directory:
            real_workdir = Path(directory).resolve()
            self.assertEqual(
                l4_production._validate_submission_workdir(
                    real_workdir, runner_root
                ),
                real_workdir,
            )

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
                    "--output-root", str(output_root), "--runner-root", str(root.resolve()),
                    "--submission-workdir", str((root / "slurm").resolve()),
                    "--plan-output", str(plan_output),
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
                    "--output-root", str(output_root), "--runner-root", str(root.resolve()),
                    "--submission-workdir", str((root / "slurm").resolve()),
                    "--plan-output", str(plan_output), "--submit",
                ])
            self.assertEqual(load_contract.call_count, 1)
            receipt_path = l4_production.submission_receipt_path(plan_output)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            self.assertEqual(receipt["plan_sha256"], _sha256(plan_output))
            self.assertEqual(len(receipt["jobs"]), 194)
            self.assertEqual(receipt["plan_provenance"]["submission_workdir"], str((root / "slurm").resolve()))
            root_jobs = [job for job in receipt["jobs"] if job["depends_on_job_id"] is None]
            self.assertEqual(len(root_jobs), 4)
            for index, call in enumerate(run.call_args_list):
                command = call.args[0]
                self.assertEqual(call.kwargs["cwd"], str((root / "slurm").resolve()))
                self.assertIn("-c12", command)
                self.assertIn("-m256G", command)
                self.assertIn("cpu_wgh", command)
                self.assertIn("-t2:00:00", command)
                if index < 4:
                    self.assertNotIn("-d", command)
                else:
                    self.assertIn("afterok:{}".format(7000 + index - 4), command)
            self.assertEqual(receipt["jobs"][0]["resources"]["mybatch_input_memory_gb"], 256)
            self.assertEqual(receipt["jobs"][0]["resources"]["effective_slurm_memory_gb"], 243)

    def test_submit_bad_output_or_midstream_failure_persists_failed_receipt(self):
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
                            "--output-root", str(output_root), "--runner-root", str(root.resolve()),
                            "--submission-workdir", str((root / "slurm").resolve()),
                            "--plan-output", str(plan_output), "--submit",
                        ])
                receipt_path = l4_production.submission_receipt_path(plan_output)
                self.assertTrue(receipt_path.exists())
                receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
                self.assertEqual(receipt["status"], "failed")
                self.assertIn("error", receipt)

    def test_submit_rejects_tampered_plan_file_and_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign_root = root / "campaign"
            output_root = (root / "outputs").resolve()
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(campaign_root, binary, config, output_root, root.resolve())
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
                runner_root=_runner_args(root, campaign_root)[0],
                runner_provenance=_runner_args(root, campaign_root)[1],
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
                *_runner_args(root, campaign_root),
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
                    *_runner_args(root, campaign_root),
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
                        *_runner_args(root, campaign_root),
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
                    *_runner_args(root, campaign_root),
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
                                *_runner_args(root, campaign_root),
                            )
                    else:
                        result = l4_production.run_chunk(
                            [production[0]], campaign_root, binary, config, output_root,
                            _sha256(binary), _sha256(config), manifest["production_date_list_sha256"],
                            *_runner_args(root, campaign_root),
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
                    *_runner_args(root, campaign_root),
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
                        *_runner_args(root, campaign_root),
                    )

    def test_build_plan_freezes_runner_and_batch_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)

            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )

            self.assertEqual(plan["runner_root"], str(root))
            self.assertEqual(len(plan["runner_provenance"]), 11)
            self.assertEqual(plan["submission_resources"]["mybatch_input_memory_gb"], 256)
            self.assertEqual(plan["submission_resources"]["effective_slurm_memory_gb"], 243)
            self.assertEqual(plan["submission_workdir"], str((root / "slurm").resolve()))
            command = l4_production._chunk_run_command(plan, plan["chunks"][0])
            self.assertNotIn("PYTHONPATH=", command)
            self.assertIn(" -I ", command)
            self.assertIn(str(root / "l4_runner_bootstrap.py"), command)
            self.assertIn("/usr/local/python3.8.10/bin/python3", command)

    def test_build_plan_rejects_feature_worktree_runner_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)

            with self.assertRaises(ValueError):
                l4_production.build_plan(
                    campaign_root,
                    binary,
                    config,
                    output_root,
                    l4_production.REPOSITORY_ROOT,
                    root / "slurm",
                )

    def test_plan_output_is_create_once_and_bound_to_single_read_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "plan.json"
            first = {"value": 1}
            second = {"value": 2}

            l4_production.write_plan_once(path, first)
            original = path.read_bytes()
            l4_production.write_plan_once(path, first)
            self.assertEqual(path.read_bytes(), original)
            with self.assertRaises(ValueError):
                l4_production.write_plan_once(path, second)
            self.assertEqual(path.read_bytes(), original)

            loaded, raw, digest = l4_production.read_plan_once(path)
            self.assertEqual(loaded, first)
            self.assertEqual(raw, original)
            self.assertEqual(
                digest, "sha256:" + hashlib.sha256(original).hexdigest()
            )

    def test_plan_output_is_atomically_published_and_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "plan.json"
            with mock.patch(
                "campaigns.l4_production.os.replace",
                wraps=os.replace,
            ) as replace:
                l4_production.write_plan_once(path, {"value": 1})
            self.assertEqual(replace.call_count, 1)
            self.assertEqual(Path(replace.call_args.args[1]), path)

            path.unlink()
            path.symlink_to(root / "elsewhere.json")
            with self.assertRaisesRegex(ValueError, "symlink"):
                l4_production.write_plan_once(path, {"value": 1})

    def test_holdout_contract_rejects_2026_even_with_matching_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, _, holdout, _ = _write_campaign(root)
            holdout[-1] = "20260102"
            holdout_path = root / "manifests" / "holdout.txt"
            holdout_path.write_text("\n".join(holdout) + "\n", encoding="utf-8")
            manifest["holdout_date_list_sha256"] = _sha256(holdout_path)
            (root / "manifests/formal-history-dataset-v2.json").write_text(
                json.dumps(manifest), encoding="utf-8"
            )

            with self.assertRaises(ValueError):
                l4_production.load_active_v2_contract(root)

    def test_submit_failure_persists_and_resume_skips_recorded_jobs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )
            plan_path = root / "plan.json"
            l4_production.write_plan_once(plan_path, plan)
            first = [
                mock.Mock(returncode=0, stdout="Jobid:7000\n", stderr=""),
                mock.Mock(returncode=0, stdout="Jobid:7001\n", stderr=""),
                mock.Mock(returncode=1, stdout="", stderr="failure"),
            ]
            with mock.patch(
                "campaigns.l4_production.subprocess.run", side_effect=first
            ):
                with self.assertRaises(RuntimeError):
                    l4_production.submit_plan(plan, plan_path)
            receipt_path = l4_production.submission_receipt_path(plan_path)
            failed = json.loads(receipt_path.read_text(encoding="utf-8"))
            self.assertEqual(failed["status"], "failed")
            self.assertEqual(
                [job.get("job_id") for job in failed["jobs"][:2]],
                ["7000", "7001"],
            )

            remaining = [
                mock.Mock(returncode=0, stdout="Jobid:{}\n".format(8000 + index), stderr="")
                for index in range(192)
            ]
            with mock.patch(
                "campaigns.l4_production.subprocess.run", side_effect=remaining
            ) as run:
                receipt = l4_production.submit_plan(
                    plan, plan_path, recover_job_id=lambda name, submitted_at: None
                )
            self.assertEqual(receipt["status"], "submitted")
            self.assertEqual(run.call_count, 192)
            self.assertEqual(receipt["jobs"][0]["job_id"], "7000")
            self.assertEqual(receipt["jobs"][1]["job_id"], "7001")
            self.assertIn("afterok:7000", run.call_args_list[2].args[0])

    def test_submit_recovers_submitting_job_and_concurrent_lock_rejects(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )
            plan_path = root / "plan.json"
            l4_production.write_plan_once(plan_path, plan)
            receipt_path = l4_production.submission_receipt_path(plan_path)
            submission_id = "abc123"
            chunk = plan["chunks"][0]
            job_name = submission_id + "-" + chunk["chunk_id"]
            run_command, command = l4_production._submission_command(
                plan, chunk, job_name, None
            )
            l4_production._write_json_atomic(receipt_path, {
                "schema_version": 2,
                "status": "submitting",
                "submission_id": submission_id,
                "submitted_at": "2026-08-31T12:00:00+00:00",
                "plan_path": str(plan_path),
                "plan_sha256": _sha256(plan_path),
                "plan_provenance": l4_production.plan_provenance(plan),
                "next_chunk_id": plan["chunks"][0]["chunk_id"],
                "jobs": [{
                    "chunk_id": chunk["chunk_id"],
                    "lane": 0,
                    "dates": list(chunk["dates"]),
                    "job_name": job_name,
                    "status": "submitting",
                    "depends_on_chunk_id": None,
                    "depends_on_job_id": None,
                    "command": command,
                    "run_command": run_command,
                    "resources": l4_production._job_resources(),
                }],
            })
            responses = [
                mock.Mock(returncode=0, stdout="Jobid:{}\n".format(9000 + index), stderr="")
                for index in range(193)
            ]
            with mock.patch(
                "campaigns.l4_production.subprocess.run", side_effect=responses
            ) as run:
                receipt = l4_production.submit_plan(
                    plan,
                    plan_path,
                    recover_job_id=lambda name, submitted_at: "7555" if name == job_name else None,
                )
            self.assertEqual(run.call_count, 193)
            self.assertEqual(receipt["jobs"][0]["job_id"], "7555")

            lock_path = l4_production.submission_lock_path(receipt_path)
            lock_path.touch()
            with lock_path.open("a+") as locked:
                fcntl.flock(locked.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                with self.assertRaises(RuntimeError):
                    l4_production.submit_plan(plan, plan_path)

    def test_completed_receipt_rejects_resubmission(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )
            plan_path = root / "plan.json"
            l4_production.write_plan_once(plan_path, plan)
            responses = [
                mock.Mock(returncode=0, stdout="Jobid:{}\n".format(10000 + index), stderr="")
                for index in range(194)
            ]
            with mock.patch(
                "campaigns.l4_production.subprocess.run", side_effect=responses
            ):
                l4_production.submit_plan(plan, plan_path)
            with mock.patch("campaigns.l4_production.subprocess.run") as run:
                with self.assertRaises(ValueError):
                    l4_production.submit_plan(plan, plan_path)
            run.assert_not_called()

    def test_run_chunk_rejects_runner_or_batch_hash_tampering(self):
        for relative in (
            "campaigns/l4_production.py",
            "campaign/batches/book_imbalance_seed_v1.json",
        ):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                campaign_root = root / "campaign"
                output_root = root / "outputs"
                manifest, production, _, names = _write_campaign(campaign_root)
                binary, config = _write_inputs(root, output_root)
                runner_root, provenance = _runner_args(root, campaign_root)
                target = output_root / production[0] / "all_families" / "factors.h5"
                _write_placeholder(target)
                (root / relative).write_text("tampered\n", encoding="utf-8")
                with mock.patch(
                    "campaigns.l4_production.subprocess.run"
                ) as run, mock.patch(
                    "evaluations.l4_preflight.validate_hdf5_only",
                    return_value=_inspection(names),
                ):
                    with self.assertRaises(ValueError):
                        l4_production.run_chunk(
                            [production[0]], campaign_root, binary, config, output_root,
                            _sha256(binary), _sha256(config),
                            manifest["production_date_list_sha256"],
                            runner_root, provenance,
                        )
                run.assert_not_called()

    def test_run_chunk_rejects_binary_or_config_changed_during_execution(self):
        for changed in ("binary", "config"):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                campaign_root = root / "campaign"
                output_root = root / "outputs"
                manifest, production, _, names = _write_campaign(campaign_root)
                binary, config = _write_inputs(root, output_root)
                binary_sha = _sha256(binary)
                config_sha = _sha256(config)
                target = output_root / production[0] / "all_families" / "factors.h5"

                def produce(command, **kwargs):
                    _write_placeholder(target)
                    path = binary if changed == "binary" else config
                    path.write_bytes(b"changed-during-execution")
                    return mock.Mock(returncode=0, stdout="", stderr="")

                with mock.patch(
                    "campaigns.l4_production.subprocess.run", side_effect=produce
                ), mock.patch(
                    "evaluations.l4_preflight.validate_hdf5_only",
                    return_value=_inspection(names),
                ):
                    with self.assertRaises(ValueError):
                        l4_production.run_chunk(
                            [production[0]], campaign_root, binary, config, output_root,
                            binary_sha, config_sha,
                            manifest["production_date_list_sha256"],
                            *_runner_args(root, campaign_root),
                        )

    def test_run_chunk_rechecks_hash_after_existing_hdf5_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            manifest, production, _, names = _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            binary_sha = _sha256(binary)
            target = output_root / production[0] / "all_families" / "factors.h5"
            _write_placeholder(target)

            def validate(path, active_campaign_root):
                binary.write_bytes(b"changed-during-validation")
                return _inspection(names)

            with mock.patch(
                "evaluations.l4_preflight.validate_hdf5_only",
                side_effect=validate,
            ):
                with self.assertRaises(ValueError):
                    l4_production.run_chunk(
                        [production[0]], campaign_root, binary, config, output_root,
                        binary_sha, _sha256(config),
                        manifest["production_date_list_sha256"],
                        *_runner_args(root, campaign_root),
                    )

    def test_cli_refuses_to_overwrite_plan_when_provenance_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan_path = root / "plan.json"
            argv = [
                "plan", "--campaign-root", str(campaign_root),
                "--binary", str(binary), "--config", str(config),
                "--output-root", str(output_root), "--runner-root", str(root),
                "--submission-workdir", str(root / "slurm"),
                "--plan-output", str(plan_path),
            ]
            with mock.patch("sys.stdout"):
                l4_production.main(argv)
            original = plan_path.read_bytes()
            (root / "evaluations/l4_preflight.py").write_text(
                "# changed provenance\n", encoding="utf-8"
            )
            with mock.patch("sys.stdout"):
                with self.assertRaises(ValueError):
                    l4_production.main(argv)
            self.assertEqual(plan_path.read_bytes(), original)

    def test_submit_binds_json_and_sha_to_one_plan_bytes_read(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )
            plan_path = root / "plan.json"
            l4_production.write_plan_once(plan_path, plan)
            real_read_bytes = Path.read_bytes
            plan_reads = []

            def tracked_read_bytes(path):
                if path == plan_path:
                    plan_reads.append(path)
                return real_read_bytes(path)

            responses = [
                mock.Mock(returncode=0, stdout="Jobid:{}\n".format(11000 + index), stderr="")
                for index in range(194)
            ]
            with mock.patch.object(
                Path, "read_bytes", autospec=True, side_effect=tracked_read_bytes
            ), mock.patch(
                "campaigns.l4_production.subprocess.run", side_effect=responses
            ):
                receipt = l4_production.submit_plan(plan, plan_path)

            self.assertEqual(len(plan_reads), 1)
            self.assertEqual(receipt["plan_sha256"], _sha256(plan_path))

    def test_submit_persists_initial_and_pre_mybatch_states(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )
            plan_path = root / "plan.json"
            l4_production.write_plan_once(plan_path, plan)
            real_write = l4_production._write_json_atomic
            snapshots = []

            def capture(path, payload):
                if Path(path) == l4_production.submission_receipt_path(plan_path):
                    snapshots.append(json.loads(json.dumps(payload)))
                return real_write(path, payload)

            with mock.patch(
                "campaigns.l4_production._write_json_atomic", side_effect=capture
            ), mock.patch(
                "campaigns.l4_production.subprocess.run",
                return_value=mock.Mock(returncode=1, stdout="", stderr="fail"),
            ):
                with self.assertRaises(RuntimeError):
                    l4_production.submit_plan(plan, plan_path)

            self.assertEqual(snapshots[0]["status"], "submitting")
            self.assertEqual(snapshots[0]["jobs"], [])
            pre_submit = next(snapshot for snapshot in snapshots if snapshot["jobs"])
            self.assertEqual(pre_submit["jobs"][0]["status"], "submitting")
            self.assertNotIn("job_id", pre_submit["jobs"][0])
            self.assertIn("submission_id", pre_submit)

    def test_bootstrap_prevents_cwd_campaign_shadowing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            release = root / "release"
            malicious = root / "malicious"
            (release / "campaigns").mkdir(parents=True)
            (malicious / "campaigns").mkdir(parents=True)
            bootstrap_source = l4_production.REPOSITORY_ROOT / "l4_runner_bootstrap.py"
            self.assertTrue(bootstrap_source.is_file())
            (release / "l4_runner_bootstrap.py").write_bytes(
                bootstrap_source.read_bytes()
            )
            (release / "campaigns/__init__.py").write_text("", encoding="utf-8")
            (release / "campaigns/l4_production.py").write_text(
                "import os\nfrom pathlib import Path\n"
                "def main():\n"
                "    Path(os.environ['TRUSTED_MARKER']).write_text('trusted')\n"
                "    return 0\n",
                encoding="utf-8",
            )
            (malicious / "campaigns/__init__.py").write_text(
                "from pathlib import Path\n"
                "Path(__import__('os').environ['MALICIOUS_MARKER']).write_text('bad')\n",
                encoding="utf-8",
            )
            malicious_pythonpath = root / "pythonpath"
            malicious_pythonpath.mkdir()
            (malicious_pythonpath / "json.py").write_text(
                "from pathlib import Path\n"
                "Path(__import__('os').environ['PYTHONPATH_MARKER']).write_text('bad')\n",
                encoding="utf-8",
            )
            trusted_marker = root / "trusted"
            malicious_marker = root / "malicious-hit"
            pythonpath_marker = root / "pythonpath-hit"
            environment = dict(os.environ)
            environment.update({
                "TRUSTED_MARKER": str(trusted_marker),
                "MALICIOUS_MARKER": str(malicious_marker),
                "PYTHONPATH_MARKER": str(pythonpath_marker),
                "PYTHONPATH": str(malicious_pythonpath),
            })

            completed = subprocess.run(
                [sys.executable, str(release / "l4_runner_bootstrap.py")],
                cwd=str(malicious),
                env=environment,
                capture_output=True,
                text=True,
            )

            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertTrue(trusted_marker.is_file())
            self.assertFalse(malicious_marker.exists())
            self.assertFalse(pythonpath_marker.exists())

    def test_runner_rejects_git_ancestor_and_symlinked_provenance(self):
        with tempfile.TemporaryDirectory(
            dir=str(l4_production.REPOSITORY_ROOT)
        ) as directory:
            nested = Path(directory)
            with self.assertRaisesRegex(ValueError, "git checkout"):
                l4_production._validate_runner_root(nested, nested)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            target = root / "campaigns/l4_production.py"
            target.unlink()
            target.symlink_to(root / "evaluations/l4_preflight.py")

            with self.assertRaisesRegex(ValueError, "symlink"):
                l4_production.build_plan(
                    campaign_root, binary, config, output_root, root
                )

    def test_recover_job_id_fails_closed_on_scheduler_query_error(self):
        for accounting_stdout in ("", "7555|submission-chunk\n"):
            with self.subTest(accounting_stdout=accounting_stdout):
                responses = [
                    mock.Mock(returncode=1, stdout="", stderr="squeue down"),
                    mock.Mock(
                        returncode=0, stdout=accounting_stdout, stderr=""
                    ),
                ]
                with mock.patch(
                    "campaigns.l4_production.subprocess.run", side_effect=responses
                ):
                    with self.assertRaises(RuntimeError):
                        l4_production.recover_job_id(
                            "submission-chunk", "2026-08-31T12:00:00+00:00"
                        )

    def test_recover_job_id_rejects_conflicting_scheduler_matches(self):
        responses = [
            mock.Mock(
                returncode=0,
                stdout="7555|submission-chunk\n",
                stderr="",
            ),
            mock.Mock(
                returncode=0,
                stdout="7666|submission-chunk\n",
                stderr="",
            ),
        ]
        with mock.patch(
            "campaigns.l4_production.subprocess.run", side_effect=responses
        ):
            with self.assertRaises(RuntimeError):
                l4_production.recover_job_id(
                    "submission-chunk", "2026-08-31T12:00:00+00:00"
                )

    def test_recover_job_id_covers_long_queue_until_recovery_time(self):
        responses = [
            mock.Mock(returncode=0, stdout="", stderr=""),
            mock.Mock(
                returncode=0,
                stdout="7555|submission-chunk\n",
                stderr="",
            ),
        ]
        recovery_time = dt.datetime(2026, 8, 31, 12, 0, tzinfo=dt.timezone.utc)
        with mock.patch(
            "campaigns.l4_production.subprocess.run", side_effect=responses
        ) as run:
            recovered = l4_production.recover_job_id(
                "submission-chunk",
                "2020-01-02T12:00:00+00:00",
                now_fn=lambda: recovery_time,
            )

        self.assertEqual(recovered, "7555")
        accounting_command = run.call_args_list[1].args[0]
        self.assertEqual(
            accounting_command[accounting_command.index("-S") + 1],
            "2020-01-02T11:00:00",
        )
        self.assertEqual(
            accounting_command[accounting_command.index("-E") + 1],
            "2026-08-31T12:00:00",
        )

    def test_resume_rejects_tampered_submitted_job_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            campaign_root = root / "campaign"
            output_root = root / "outputs"
            _write_campaign(campaign_root)
            binary, config = _write_inputs(root, output_root)
            plan = l4_production.build_plan(
                campaign_root, binary, config, output_root, root
            )
            plan_path = root / "plan.json"
            l4_production.write_plan_once(plan_path, plan)
            with mock.patch(
                "campaigns.l4_production.subprocess.run",
                side_effect=[
                    mock.Mock(returncode=0, stdout="Jobid:7000\n", stderr=""),
                    mock.Mock(returncode=1, stdout="", stderr="fail"),
                ],
            ):
                with self.assertRaises(RuntimeError):
                    l4_production.submit_plan(plan, plan_path)
            receipt_path = l4_production.submission_receipt_path(plan_path)
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            receipt["jobs"][0]["lane"] = 99
            l4_production._write_json_atomic(receipt_path, receipt)

            with mock.patch("campaigns.l4_production.subprocess.run") as run:
                with self.assertRaises(ValueError):
                    l4_production.submit_plan(
                        plan, plan_path, recover_job_id=lambda name, submitted_at: None
                    )
            run.assert_not_called()

    def test_atomic_publication_fsyncs_parent_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch(
                "campaigns.l4_production._fsync_directory"
            ) as fsync_directory:
                l4_production.write_plan_once(root / "plan.json", {"a": 1})
                l4_production._write_json_atomic(root / "receipt.json", {"b": 2})
            self.assertEqual(fsync_directory.call_count, 2)


if __name__ == "__main__":
    unittest.main()
