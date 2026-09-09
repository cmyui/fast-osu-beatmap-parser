"""Run each Python API in a separate process, retaining both complete passes."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("config", type=Path)
    parser.add_argument("summary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--table",
        default="python",
        help="Common cohort table from the comparison report",
    )
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    variants = json.loads(args.config.read_text())["variants"]
    python_names = {
        "fosu-python-avx2",
        "fosu-python-scalar",
        "slider",
        "rosu-pp-py",
        "osupyparser",
        "pyttanko",
    }
    table = json.loads(args.summary.read_text())["tables"][args.table]
    jobs = [
        (v, kind)
        for v in variants
        if v["name"] in python_names
        for kind in v["workloads"]
        if f"{v['name']}/{kind}" in table["rows"]
    ]
    if not jobs or not table["files"]:
        raise ValueError("The chosen table has no common Python inputs")
    worker = Path(__file__).with_name("python_batch_worker.py")
    report = {
        "complete": False,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "affinity": sorted(os.sched_getaffinity(0)),
        "warmup_maps": 64,
        "table": args.table,
        "modes": table.get("modes", {}),
        "cohort_source_sha256": hashlib.sha256(args.summary.read_bytes()).hexdigest(),
        "versions": {v["name"]: v["version"] for v, _ in jobs},
        "samples": [],
    }
    reference = None
    for round_id in range(2):
        for variant, kind in jobs if round_id == 0 else reversed(jobs):
            print(f"Pass {round_id + 1}: {variant['name']}/{kind}", flush=True)
            result = subprocess.run(
                [
                    variant["command"][0],
                    str(worker),
                    variant["command"][-1],
                    kind,
                    str(args.corpus),
                    str(args.summary),
                    args.table,
                ],
                env=os.environ | variant.get("env", {}),
                check=True,
                capture_output=True,
                text=True,
                timeout=600,
            )
            sample = json.loads(result.stdout)
            identity = {
                key: sample[key]
                for key in ("files", "bytes", "corpus_sha256", "counts")
            }
            if reference is None:
                reference = identity
            assert identity == reference, "Batch inputs or per-map object counts differ"
            sample.pop("counts")
            sample.update(variant=variant["name"], workload=kind, round=round_id)
            report["samples"].append(sample)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
    report["rows"] = {}
    for variant, kind in jobs:
        times = [
            s["elapsed_ns"] / s["files"] / 1000
            for s in report["samples"]
            if s["variant"] == variant["name"] and s["workload"] == kind
        ]
        report["rows"][f"{variant['name']}/{kind}"] = {
            "mean_us": statistics.mean(times),
            "pass_mean_us": times,
        }
    report.update(
        complete=True,
        finished_utc=datetime.now(timezone.utc).isoformat(),
        objects=sum(reference["counts"]),
        files=reference["files"],
        bytes=reference["bytes"],
        corpus_sha256=reference["corpus_sha256"],
    )
    args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
