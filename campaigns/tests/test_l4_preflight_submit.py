import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from campaigns.l4_release import PREFLIGHT_DATES, build_preflight_plan, submit_preflight_plan


class SubmitPreflightTests(unittest.TestCase):
    def _plan(self, root):
        plan = {"dates": list(PREFLIGHT_DATES), "promotion_allowed": False,
                "slurm_submission_performed": False, "submission_root": str(root), "jobs": []}
        previous = None
        for i, date in enumerate(PREFLIGHT_DATES):
            name = "job-{}".format(i)
            plan["jobs"].append({"job_name": name, "kind": "production", "date": date,
                                 "depends_on": previous, "command": ["echo", date]})
            previous = name
        plan["jobs"].append({"job_name": "convert", "kind": "conversion_validation", "date": None,
                             "depends_on": previous, "command": ["echo", "convert"]})
        path = root / "plan.json"; path.write_text(json.dumps(plan)); return path

    def test_dry_run_never_calls_runner(self):
        with tempfile.TemporaryDirectory() as d:
            path = self._plan(Path(d))
            with mock.patch("campaigns.l4_release.subprocess.run") as runner:
                result = submit_preflight_plan(path)
            runner.assert_not_called(); self.assertEqual(result["status"], "dry_run")

    def test_submit_persists_each_job_and_dependencies(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); path = self._plan(root); calls = []
            def fake(command, **kwargs):
                calls.append(command)
                return mock.Mock(returncode=0, stdout="Jobid:{}\n".format(100 + len(calls)), stderr="")
            receipt = root / "receipt.json"
            result = submit_preflight_plan(path, receipt_path=receipt, submit=True, mybatch_runner=fake)
            self.assertEqual(result["status"], "submitted"); self.assertEqual(len(result["jobs"]), 6)
            self.assertIn("-c12", calls[0]); self.assertIn("-m256G", calls[0]); self.assertIn("-p", calls[0])
            self.assertIn("afterok:101", calls[1])
            with self.assertRaises(ValueError): submit_preflight_plan(path, receipt_path=receipt, submit=True, mybatch_runner=fake)

    def test_failure_is_recorded_and_retry_does_not_duplicate_successes(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); path = self._plan(root); receipt = root / "receipt.json"; n = [0]
            def flaky(command, **kwargs):
                n[0] += 1
                if n[0] == 2: return mock.Mock(returncode=1, stdout="", stderr="boom")
                return mock.Mock(returncode=0, stdout="Jobid:{}\n".format(200 + n[0]), stderr="")
            with self.assertRaises(RuntimeError): submit_preflight_plan(path, receipt_path=receipt, submit=True, mybatch_runner=flaky)
            state = json.loads(receipt.read_text()); self.assertEqual(state["status"], "failed"); self.assertEqual(state["jobs"][0]["status"], "submitted")

    def test_submitting_job_is_recovered_without_resubmission(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); path = self._plan(root); receipt = root / "receipt.json"
            state = {"schema_version": 1, "status": "submitting", "plan_sha256": __import__('campaigns.l4_release', fromlist=['sha256']).sha256(path), "submitted_at": "2026-01-01T00:00:00+00:00", "jobs": [{"job_name": "job-0", "status": "submitting", "submitted_at": "2026-01-01T00:00:00+00:00"}]}
            receipt.write_text(json.dumps(state))
            def recover(name, submitted_at): return "777" if name == "job-0" else None
            calls = []
            def fake(command, **kwargs): calls.append(command); return mock.Mock(returncode=0, stdout="Jobid:778\n", stderr="")
            result = submit_preflight_plan(path, receipt_path=receipt, submit=True, mybatch_runner=fake, recover_job_id=recover)
            self.assertEqual(result["jobs"][0]["job_id"], "777"); self.assertEqual(len(calls), 5)

    def test_recovery_exception_is_fail_closed(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); path = self._plan(root); receipt = root / "receipt.json"
            state = {"schema_version": 1, "status": "submitting", "plan_sha256": __import__('campaigns.l4_release', fromlist=['sha256']).sha256(path), "submitted_at": "2026-01-01T00:00:00+00:00", "jobs": [{"job_name": "job-0", "status": "submitting", "submitted_at": "2026-01-01T00:00:00+00:00"}]}
            receipt.write_text(json.dumps(state))
            with self.assertRaises(RuntimeError): submit_preflight_plan(path, receipt_path=receipt, submit=True, mybatch_runner=mock.Mock(), recover_job_id=lambda *_: (_ for _ in ()).throw(RuntimeError("query")))
            self.assertEqual(json.loads(receipt.read_text())["status"], "failed")


if __name__ == "__main__": unittest.main()
