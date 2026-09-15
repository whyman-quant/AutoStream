"""Autonomous, selection-gated 2025 display pipeline for round 001."""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
if str(REPOSITORY_ROOT) not in sys.path:
    sys.path.insert(0, str(REPOSITORY_ROOT))

from campaigns.research_round import atomic_write_json
from campaigns.sfm_stream_002.l4_postprocess import (
    EVAL_EVENTS, LABELS, SOURCE_EVENTS, UNIVERSES, evaluate)


def _sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return "sha256:" + digest.hexdigest()


def make_holdout_config(source, output, output_root):
    document = json.loads(Path(source).read_text(encoding="utf-8"))
    document["factors_config"]["save_info"]["dir"] = str(
        Path(output_root) / "[DATE]" / "all_families")
    atomic_write_json(output, document)
    return document


def _contract_dates(campaign_root):
    from campaigns.l4_production import load_active_v2_contract
    return load_active_v2_contract(campaign_root)["holdout_dates"]


def run_chunk(dates, campaign_root, binary, config, output_root, factor_manifest,
              binary_sha256, config_sha256):
    requested = [str(value) for value in dates]
    allowed = set(_contract_dates(campaign_root))
    if not requested or len(requested) > 5 or len(set(requested)) != len(requested):
        raise ValueError("holdout chunk must contain one to five unique dates")
    if not set(requested).issubset(allowed):
        raise ValueError("date is outside the sealed holdout list")
    if _sha(binary) != binary_sha256 or _sha(config) != config_sha256:
        raise ValueError("frozen holdout executable/config hash drift")
    from evaluations.l4_preflight import validate_hdf5_only
    rows = []
    for date in requested:
        target = Path(output_root) / date / "all_families" / "factors.h5"
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            completed = subprocess.run([
                str(binary), "date=" + date, "thread_num=8",
                "config_file=" + str(config), "stock=all",
            ], capture_output=True, text=True)
            if completed.returncode:
                raise RuntimeError("factor binary failed for {}: {}".format(
                    date, completed.stderr.strip()))
        inspection = validate_hdf5_only(
            target, campaign_root, factor_manifest_path=factor_manifest)
        rows.append({"date": date, "path": str(target),
                     "factor_count": inspection["factor_count"],
                     "event_count": inspection["event_count"]})
    return rows


def convert_selected(hdf5_root, arrow_root, dates, factor_manifest, selected, workers=8):
    from evaluations.convert_production_hdf5 import convert_hdf5
    import h5py
    manifest = json.loads(Path(factor_manifest).read_text(encoding="utf-8"))
    source_count = int(manifest["factor_count"])

    def one(date):
        source = Path(hdf5_root) / date / "all_families" / "factors.h5"
        target = Path(arrow_root) / (date + ".arrow")
        with h5py.File(str(source), "r") as handle:
            expected_rows = len(handle["codelist_92700000"])
        return convert_hdf5(
            source, target, expected_rows=expected_rows,
            expected_events=SOURCE_EVENTS, expected_factor_count=source_count,
            require_explicit_reason=True, factor_only=True,
            output_factors=selected)

    with ThreadPoolExecutor(max_workers=int(workers)) as pool:
        return list(pool.map(one, dates))


def _effect_frame(result_root, factor, splits=("observation", "holdout")):
    import pandas as pd
    blocks = []
    for split in splits:
        for label in LABELS:
            for universe in UNIVERSES:
                source = pd.read_parquet(
                    Path(result_root) / split / label / universe / "res_full.parquet")
                frame = source[[factor + "|RankIC"] + [
                    factor + "|D{}".format(i) for i in range(1, 11)]].reset_index()
                frame = frame.rename(columns={
                    factor + "|RankIC": "rank_ic",
                    **{factor + "|D{}".format(i): "q{}_return".format(i)
                       for i in range(1, 11)},
                })
                frame["universe"] = universe
                frame["label"] = label
                blocks.append(frame)
    return pd.concat(blocks, ignore_index=True)


def finish(args):
    from evaluations.effect_plot import plot_effect_grid
    from evaluations.effect_timeseries import build_effect_timeseries
    import matplotlib.pyplot as plt

    selection = json.loads(Path(args.selection).read_text(encoding="utf-8"))
    selected = list(selection["selected_for_holdout_display"])
    dates = _contract_dates(args.campaign_root)
    conversion = convert_selected(
        args.hdf5_root, args.arrow_root, dates, args.factor_manifest,
        selected, workers=args.workers)
    evaluation = evaluate(
        args.result_root, args.arrow_root, {"holdout": dates},
        "market_microstructure_round_001_holdout_display", args.toolkit,
        selected, workers=args.workers)
    plot_root = Path(args.plot_root)
    series_root = Path(args.series_root)
    plot_root.mkdir(parents=True, exist_ok=True)
    series_root.mkdir(parents=True, exist_ok=True)
    plots = []
    for factor in selected:
        series_path = series_root / (factor + ".json")
        series = build_effect_timeseries(
            _effect_frame(args.result_root, factor), factor,
            selection_receipt=selection, authorization={
                "selection_frozen": True, "best_event_frozen": True,
                "readable_split": "holdout", "allowed_use": "display_only",
                "feedback_allowed": False,
            }, output_path=series_path)
        best = {key.split("|", 1)[1]: value for key, value in
                selection["best_events"].items() if key.startswith(factor + "|")}
        path = plot_root / (factor + ".png")
        fig = plot_effect_grid(series, factor=factor, best_events=best,
                               output_path=path)
        plt.close(fig)
        plots.append(str(path))
    from evaluations.round_experience import generate as generate_experience
    experiences, next_logic = generate_experience(
        args.selection, args.logic, args.blueprint, args.experience_root,
        args.next_logic)
    receipt = {
        "schema_version": 1, "kind": "holdout_display_receipt",
        "round_id": "sfm_stream_002_round_001", "status": "complete",
        "selection_sha256": _sha(args.selection), "holdout_date_count": len(dates),
        "selected_factors": selected, "conversion_count": len(conversion),
        "status_columns_emitted": False, "evaluation": evaluation,
        "plots": plots, "holdout_read": True, "allowed_use": "display_only",
        "experience_count": len(experiences),
        "experience_root": str(Path(args.experience_root).resolve()),
        "next_logic_path": str(Path(args.next_logic).resolve()),
        "next_round_candidate_budget": next_logic["search_policy"]["target_candidate_count"],
        "feedback_allowed": False, "promotion_allowed": False,
        "elimination_allowed": False,
    }
    atomic_write_json(args.receipt, receipt)
    print(json.dumps({"status": "complete", "plots": len(plots),
                      "selected": len(selected)}, ensure_ascii=False))


def _sbatch(wrap, name, out, dependency=None, memory="128G", hours="02:00:00"):
    command = ["sbatch", "--parsable", "-c", "12", "--mem", memory,
               "-p", "cpu_wgh", "-t", hours, "-J", name,
               "-o", out + ".%j.out", "-e", out + ".%j.err"]
    if dependency:
        command.extend(["--dependency", "afterok:" + str(dependency)])
    command.extend(["--wrap", wrap])
    return subprocess.check_output(command, text=True).strip().split(";")[0]


def submit(args):
    selection = json.loads(Path(args.selection).read_text(encoding="utf-8"))
    if not selection.get("selected_for_holdout_display") or selection.get("holdout_read"):
        raise ValueError("valid pre-holdout selection is required")
    make_holdout_config(args.release_config, args.config, args.hdf5_root)
    binary_sha, config_sha = _sha(args.binary), _sha(args.config)
    dates = _contract_dates(args.campaign_root)
    chunks = [dates[index:index + 5] for index in range(0, len(dates), 5)]
    lanes = [None] * 16
    jobs = []
    repo = str(REPOSITORY_ROOT)
    log = str(Path(args.log_root) / "mm-holdout")
    for index, chunk in enumerate(chunks):
        lane = index % len(lanes)
        wrap = (
            "cd {repo} && /usr/local/python3.8.10/bin/python3 "
            "campaigns/sfm_stream_002/holdout.py run-chunk --dates {dates} "
            "--campaign-root {campaign} --binary {binary} --config {config} "
            "--output-root {output} --factor-manifest {manifest} "
            "--binary-sha256 {binary_sha} --config-sha256 {config_sha}"
        ).format(repo=repo, dates=",".join(chunk), campaign=args.campaign_root,
                 binary=args.binary, config=args.config, output=args.hdf5_root,
                 manifest=args.factor_manifest, binary_sha=binary_sha,
                 config_sha=config_sha)
        job = _sbatch(wrap, "mm-holdout-{:02d}".format(index), log,
                      dependency=lanes[lane])
        lanes[lane] = job
        jobs.append(job)
    dependency = ":".join(value for value in lanes if value)
    finish_wrap = (
        "cd {repo} && /usr/local/python3.8.10/bin/python3 "
        "campaigns/sfm_stream_002/holdout.py finish --campaign-root {campaign} "
        "--selection {selection} --hdf5-root {hdf5} --arrow-root {arrow} "
        "--result-root {result} --factor-manifest {manifest} --plot-root {plots} "
        "--series-root {series} --receipt {receipt} --logic {logic} "
        "--blueprint {blueprint} --experience-root {experience} "
        "--next-logic {next_logic} --workers 8"
    ).format(repo=repo, campaign=args.campaign_root, selection=args.selection,
             hdf5=args.hdf5_root, arrow=args.arrow_root, result=args.result_root,
             manifest=args.factor_manifest, plots=args.plot_root,
             series=args.series_root, receipt=args.receipt, logic=args.logic,
             blueprint=args.blueprint, experience=args.experience_root,
             next_logic=args.next_logic)
    final_job = _sbatch(finish_wrap, "mm-holdout-finalize", log,
                        dependency=dependency, memory="48G", hours="03:00:00")
    receipt = {
        "schema_version": 1, "kind": "holdout_display_submission",
        "round_id": "sfm_stream_002_round_001", "status": "submitted",
        "selection_sha256": _sha(args.selection), "date_count": len(dates),
        "chunk_count": len(chunks), "lane_count": 16, "job_ids": jobs,
        "terminal_job_ids": [value for value in lanes if value],
        "finalizer_job_id": final_job, "binary_sha256": binary_sha,
        "config_sha256": config_sha, "holdout_read": False,
        "promotion_allowed": False, "elimination_allowed": False,
    }
    atomic_write_json(args.submission_receipt, receipt)
    print(json.dumps(receipt, ensure_ascii=False))


def parser():
    root = argparse.ArgumentParser(description=__doc__)
    sub = root.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run-chunk")
    for name in ("campaign-root", "binary", "config", "output-root",
                 "factor-manifest", "binary-sha256", "config-sha256"):
        run.add_argument("--" + name, required=True)
    run.add_argument("--dates", required=True)
    submit_p = sub.add_parser("submit")
    for name in ("campaign-root", "binary", "release-config", "config",
                 "selection", "hdf5-root", "arrow-root", "result-root",
                 "factor-manifest", "plot-root", "series-root", "receipt",
                 "submission-receipt", "log-root", "logic", "blueprint",
                 "experience-root", "next-logic"):
        submit_p.add_argument("--" + name, required=True)
    finish_p = sub.add_parser("finish")
    for name in ("campaign-root", "selection", "hdf5-root", "arrow-root",
                 "result-root", "factor-manifest", "plot-root", "series-root",
                 "receipt", "logic", "blueprint", "experience-root",
                 "next-logic"):
        finish_p.add_argument("--" + name, required=True)
    finish_p.add_argument("--toolkit", default="/mnt/beegfs_ssd_raid91/10513_fangwei/factor_eval_toolkit/scripts/evaluate_factors.py")
    finish_p.add_argument("--workers", type=int, default=8)
    return root


def main(argv=None):
    args = parser().parse_args(argv)
    if args.command == "run-chunk":
        print(json.dumps(run_chunk(
            args.dates.split(","), args.campaign_root, args.binary, args.config,
            args.output_root, args.factor_manifest, args.binary_sha256,
            args.config_sha256), ensure_ascii=False))
    elif args.command == "submit":
        submit(args)
    else:
        finish(args)


if __name__ == "__main__":
    main()
