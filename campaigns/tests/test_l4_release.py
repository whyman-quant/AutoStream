import json
import tempfile
import unittest
from unittest import mock
from pathlib import Path

from campaigns.l4_release import PREFLIGHT_DATES, build_preflight_plan, freeze_release


class L4ReleaseTests(unittest.TestCase):
    def test_freeze_release_copies_readonly_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "campaigns/sfm_stream_001/batches").mkdir(parents=True)
            (root / "campaigns/sfm_stream_001/campaign.json").write_text("{}")
            for relative in (
                "l4_runner_bootstrap.py", "campaigns/__init__.py",
                "campaigns/l4_production.py", "campaigns/l4_release.py",
                "evaluations/__init__.py", "evaluations/l4_preflight.py",
                "evaluations/pilot_postprocess.py", "evaluations/convert_production_hdf5.py",
            ):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(relative)
            campaign = root / "campaigns/sfm_stream_001"
            (campaign / "campaign.json").write_text(json.dumps({
                "formal_history_dataset_id": "sfm_stream_001_formal_history_v2",
                "formal_history_dataset_manifest": "manifests/formal-history-dataset-v2.json",
            }))
            (campaign / "manifests").mkdir(parents=True, exist_ok=True)
            date_list = campaign / "manifests/formal-history-production-dates-v2.txt"
            date_list.write_text("20210104\n")
            import hashlib
            date_sha = "sha256:" + hashlib.sha256(date_list.read_bytes()).hexdigest()
            (campaign / "manifests/formal-history-dataset-v2.json").write_text(json.dumps({
                "dataset_id": "sfm_stream_001_formal_history_v2",
                "production_date_list_path": "manifests/formal-history-production-dates-v2.txt",
                "production_date_list_sha256": date_sha,
                "holdout_date_list_path": "manifests/formal-history-production-dates-v2.txt",
                "holdout_date_list_sha256": date_sha,
                "parent_date_list_path": "manifests/formal-history-production-dates-v2.txt",
                "parent_date_list_sha256": date_sha,
            }))
            for family in ("book_imbalance", "flow_pressure", "liquidity_resilience", "impact_efficiency"):
                path = root / "campaigns/sfm_stream_001/batches/{}_seed_v1.json".format(family)
                path.write_text("{}")
            binary = root / "main"
            binary.write_bytes(b"bin")
            config = root / "config.json"
            config.write_text("{}")
            release = freeze_release(root, commit="abc123", binary=binary, config=config, release_base=root / "releases")
            self.assertTrue(Path(release["release_root"]).is_dir())
            self.assertEqual((Path(release["binary"]["path"]).stat().st_mode & 0o222), 0)
            self.assertTrue((Path(release["release_root"]) / "release.json").is_file())

    def test_preflight_plan_is_exact_five_dates_and_never_submits(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release = root / "formal-history-v2-abc"
            release.mkdir()
            (release / "factor_main").write_bytes(b"x")
            (release / "config_factor.json").write_text("{}")
            (release / "release.json").write_text(json.dumps({
                "release_id": "formal-history-v2-abc",
                "binary": {"path": str(release / "factor_main"), "sha256": "sha256:" + "0" * 64},
                "config": {"path": str(release / "config_factor.json"), "sha256": "sha256:" + "0" * 64},
            }))
            for relative in (
                "l4_runner_bootstrap.py", "campaigns/__init__.py", "campaigns/l4_production.py",
                "campaigns/l4_release.py", "evaluations/__init__.py", "evaluations/l4_preflight.py",
                "evaluations/pilot_postprocess.py", "evaluations/convert_production_hdf5.py",
            ):
                path = release / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(relative)
            campaign = release / "campaigns/sfm_stream_001"
            (campaign / "manifests").mkdir(parents=True, exist_ok=True)
            (campaign / "batches").mkdir(parents=True, exist_ok=True)
            (campaign / "campaign.json").write_text(json.dumps({
                "formal_history_dataset_id": "sfm_stream_001_formal_history_v2",
                "formal_history_dataset_manifest": "manifests/formal-history-dataset-v2.json",
            }))
            date_list = campaign / "manifests/formal-history-production-dates-v2.txt"
            date_list.write_text("20210104\n")
            import hashlib
            date_sha = "sha256:" + hashlib.sha256(date_list.read_bytes()).hexdigest()
            (campaign / "manifests/formal-history-dataset-v2.json").write_text(json.dumps({
                "dataset_id": "sfm_stream_001_formal_history_v2",
                "production_date_list_path": "manifests/formal-history-production-dates-v2.txt",
                "production_date_list_sha256": date_sha,
                "holdout_date_list_path": "manifests/formal-history-production-dates-v2.txt",
                "holdout_date_list_sha256": date_sha,
                "parent_date_list_path": "manifests/formal-history-production-dates-v2.txt",
                "parent_date_list_sha256": date_sha,
            }))
            for family in ("book_imbalance", "flow_pressure", "liquidity_resilience", "impact_efficiency"):
                (campaign / "batches" / (family + "_seed_v1.json")).write_text(json.dumps({"candidate_ids": [family + "_%d" % i for i in range(12)]}))
            fake_provenance = [{"kind": "runner", "name": "x", "path": str(release / "x"), "sha256": "sha256:" + "0" * 64}]
            with mock.patch("campaigns.l4_release.collect_runner_provenance", return_value=fake_provenance):
                plan = build_preflight_plan(release, submission_base=root / "submissions")
            self.assertEqual(plan["dates"], list(PREFLIGHT_DATES))
            self.assertEqual(len(plan["jobs"]), 6)
            self.assertFalse(plan["slurm_submission_performed"])
            self.assertEqual(plan["jobs"][-1]["depends_on"], plan["jobs"][-2]["job_name"])
            command = plan["jobs"][0]["command"]
            for required in ("--binary-sha256", "--config-sha256", "--date-list-sha256",
                             "--runner-root", "--runner-provenance-json"):
                self.assertIn(required, command)
            self.assertNotIn("2025", json.dumps(plan))


if __name__ == "__main__":
    unittest.main()
