"""Freeze an immutable L4 runner release and describe the five-date preflight.

This module deliberately does not call ``mybatch``.  It creates the release
and a scheduler-neutral preflight plan; an operator may submit the recorded
commands only after reviewing the hashes and paths.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from campaigns.l4_production import FAMILIES, collect_runner_provenance


PREFLIGHT_DATES = ("20210104", "20220301", "20221230", "20230103", "20241231")
RELEASE_BASE = Path("/mnt/beegfs_ssd_raid91/10513_fangwei/AutoStream-l4-releases")
SUBMISSION_BASE = Path("/mnt/beegfs_ssd_raid91/10513_fangwei/AutoStream-l4-submissions")
RUNNER_FILES = (
    "l4_runner_bootstrap.py",
    "campaigns/__init__.py",
    "campaigns/l4_production.py",
    "campaigns/l4_release.py",
    "evaluations/__init__.py",
    "evaluations/l4_preflight.py",
    "evaluations/pilot_postprocess.py",
    "evaluations/convert_production_hdf5.py",
)


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def _atomic_json(path, payload):
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix="." + target.name + ".", dir=str(target.parent))
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(payload, output, ensure_ascii=False, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.replace(str(temporary), str(target))
    finally:
        if temporary.exists():
            temporary.unlink()


def _copy_readonly(source, target, executable=False):
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(str(source), str(target))
    target.chmod(0o555 if executable else 0o444)
    return target


def freeze_release(repo_root, *, commit, binary, config, release_base=RELEASE_BASE):
    """Copy the audited runner and contracts to an external immutable release."""
    repo = Path(repo_root).resolve()
    actual_commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
    if str(commit) != actual_commit:
        raise ValueError("release commit must equal repository HEAD")
    status = subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain"], text=True)
    if status.strip():
        raise ValueError("repository must be clean before release freeze")
    target = Path(release_base).resolve() / ("formal-history-v2-" + str(commit))
    if target.exists():
        metadata = target / "release.json"
        if not metadata.exists():
            raise ValueError("release exists without release.json: {}".format(target))
        return json.loads(metadata.read_text(encoding="utf-8"))
    target.mkdir(parents=True, mode=0o755)
    binary_path = Path(binary).resolve()
    config_path = Path(config).resolve()
    release_binary = _copy_readonly(binary_path, target / "factor_main", executable=True)
    release_config = _copy_readonly(config_path, target / "config_factor.json")
    for relative in RUNNER_FILES:
        source = repo / relative
        if not source.is_file():
            raise ValueError("missing release file: {}".format(source))
        _copy_readonly(source, target / relative, executable=relative.endswith(".py") and relative == "l4_runner_bootstrap.py")
    campaign_source = repo / "campaigns" / "sfm_stream_001"
    for relative in campaign_source.rglob("*"):
        if relative.is_file():
            if relative.name == "formal-history-holdout-dates-v2.txt":
                continue
            _copy_readonly(relative, target / relative.relative_to(repo))
    # The production runner needs only the production/parent lists.  Remove
    # the sealed holdout list from the release and its readable path metadata.
    release_manifest = target / "campaigns/sfm_stream_001/manifests/formal-history-dataset-v2.json"
    manifest_payload = json.loads(release_manifest.read_text(encoding="utf-8"))
    manifest_payload.pop("holdout_date_list_path", None)
    manifest_payload.pop("holdout_date_list_sha256", None)
    release_manifest.chmod(0o644)
    release_manifest.write_text(json.dumps(manifest_payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    release_manifest.chmod(0o444)
    # The compiled binary/config and all provenance files must be regular files.
    files = {
        "binary": {"path": str(release_binary), "sha256": sha256(release_binary)},
        "config": {"path": str(release_config), "sha256": sha256(release_config)},
    }
    for relative in RUNNER_FILES:
        copied = target / relative
        files[relative] = {"path": str(copied), "sha256": sha256(copied)}
    for family in FAMILIES:
        relative = "campaigns/sfm_stream_001/batches/{}_seed_v1.json".format(family)
        copied = target / relative
        files[relative] = {"path": str(copied), "sha256": sha256(copied)}
    metadata = {
        "schema_version": 1,
        "release_id": "formal-history-v2-" + str(commit),
        "source_commit": str(commit),
        "release_root": str(target),
        "runner_root": str(target),
        "campaign_root": str(target / "campaigns/sfm_stream_001"),
        "binary": {"path": str(release_binary), "sha256": sha256(release_binary)},
        "config": {"path": str(release_config), "sha256": sha256(release_config)},
        "files": files,
        "immutable": True,
        "holdout_data_included": False,
        "holdout_date_list_included": False,
    }
    _atomic_json(target / "release.json", metadata)
    # Freeze the entire tree only after all copies and metadata are complete.
    for directory in sorted((path for path in target.rglob("*") if path.is_dir()), reverse=True):
        directory.chmod(0o555)
    (target / "release.json").chmod(0o444)
    target.chmod(0o555)
    return metadata


def build_preflight_plan(release, *, submission_base=SUBMISSION_BASE, output_root=None):
    """Create a five-date, one-lane, dependency-chain plan without Slurm calls."""
    release = Path(release).resolve()
    metadata = json.loads((release / "release.json").read_text(encoding="utf-8"))
    submission = Path(submission_base).resolve() / metadata["release_id"]
    submission.mkdir(parents=True, exist_ok=True)
    output = Path(output_root or (submission / "output")).resolve()
    campaign_root = release / "campaigns/sfm_stream_001"
    manifest = json.loads((campaign_root / "manifests/formal-history-dataset-v2.json").read_text(encoding="utf-8"))
    date_list = campaign_root / "manifests/formal-history-production-dates-v2.txt"
    date_list_sha256 = sha256(date_list)
    provenance = collect_runner_provenance(release, release / "campaigns/sfm_stream_001")
    jobs = []
    previous = None
    for index, date in enumerate(PREFLIGHT_DATES):
        job_name = "l4-preflight-{}-{:02d}".format(metadata["release_id"], index)
        command = [
            "/usr/local/python3.8.10/bin/python3", "-I",
            str(release / "l4_runner_bootstrap.py"), "run-chunk",
            "--dates", date,
            "--campaign-root", str(release / "campaigns/sfm_stream_001"),
            "--binary", metadata["binary"]["path"], "--config", metadata["config"]["path"],
            "--output-root", str(output),
            "--binary-sha256", metadata["binary"]["sha256"],
            "--config-sha256", metadata["config"]["sha256"],
            "--date-list-sha256", date_list_sha256,
            "--runner-root", str(release),
            "--runner-provenance-json", json.dumps(provenance, separators=(",", ":")),
        ]
        jobs.append({"job_id": None, "kind": "production", "job_name": job_name,
                     "date": date, "depends_on": previous, "command": command})
        previous = job_name
    jobs.append({
        "job_id": None, "kind": "conversion_validation",
        "job_name": "l4-preflight-convert-" + metadata["release_id"],
        "date": None, "depends_on": previous,
        "command": ["/usr/bin/env", "PYTHONPATH=" + str(release),
                     "/usr/local/python3.8.10/bin/python3", "-m", "evaluations.l4_preflight",
                     "--dates", ",".join(PREFLIGHT_DATES),
                     "--hdf5-root", str(output), "--arrow-root", str(output / "arrow"),
                     "--campaign-root", str(release / "campaigns/sfm_stream_001"),
                     "--production-date-list", str(date_list),
                     "--output", str(submission / "preflight-validation.json")],
    })
    return {
        "schema_version": 1, "status": "planned", "decision": "pending",
        "promotion_allowed": False, "dates": list(PREFLIGHT_DATES),
        "release_id": metadata["release_id"], "release_root": str(release),
        "submission_root": str(submission), "output_root": str(output),
        "runner_provenance": provenance, "date_list_sha256": date_list_sha256,
        "manifest_date_list_sha256": manifest["production_date_list_sha256"], "jobs": jobs,
        "slurm_submission_performed": False,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    release = freeze_release(args.repo_root, commit=args.commit, binary=args.binary, config=args.config)
    config_payload = json.loads(Path(args.config).read_text(encoding="utf-8"))
    configured = config_payload["factors_config"]["save_info"]["dir"]
    output_root = Path(configured.replace("[DATE]", "__DATE__")).parent.parent
    plan = build_preflight_plan(release["release_root"], output_root=output_root)
    _atomic_json(args.output, plan)
    print(json.dumps({"release": release["release_root"], "plan": str(args.output)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
