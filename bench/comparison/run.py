"""Rotate persistent parser workers on one CPU; preserve every result as JSONL."""
import argparse
import csv
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path
import platform
import select
import subprocess
import sys
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("corpus", type=Path)
p.add_argument("config", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--reps", type=int, default=1)
p.add_argument("--rounds", type=int, default=2)
p.add_argument("--limit", type=int, default=0)
p.add_argument("--warmup", type=int, default=64)
p.add_argument("--timeout", type=float, default=10, help="Per-request seconds; timeouts are failures, not samples")
args = p.parse_args()
assert args.reps > 0 and args.rounds > 0 and args.warmup >= 0 and args.limit >= 0 and args.timeout > 0
files = sorted(args.corpus.resolve().glob("*.osu"))
assert files
if args.limit:
    limit = min(args.limit, len(files))
    files = [files[i * len(files) // limit] for i in range(limit)]
config = json.loads(args.config.read_text())
names = [v["name"] for v in config["variants"]]
assert len(names) == len(set(names)) and config["reference"] in names
for variant in config["variants"]:
    workloads = variant["workloads"]
    assert workloads and len(workloads) == len(set(workloads))
    assert set(workloads) <= {"bytes", "file", "visit"}
digest = hashlib.sha256()
manifest = []
for path in files:
    content = path.read_bytes()
    checksum = hashlib.sha256(content)
    digest.update(path.name.encode() + b"\0" + checksum.digest())
    manifest.append((path.name, len(content), checksum.hexdigest()))
args.output.parent.mkdir(parents=True, exist_ok=True)
manifest_path = args.output.with_suffix(".corpus.csv.gz")
metadata = {
    "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "platform": platform.platform(), "python": platform.python_version(),
    "affinity": sorted(os.sched_getaffinity(0)),
    "files": len(files), "bytes": sum(f.stat().st_size for f in files),
    "corpus_sha256": digest.hexdigest(), "config": config,
    "reps": args.reps, "rounds": args.rounds, "warmup_maps": args.warmup,
    "request_timeout_seconds": args.timeout,
    "complete": False,
}
meta_path = args.output.with_suffix(".meta.json")

class Worker:
    def __init__(self, entry):
        self.entry = entry
        self.start()

    def start(self):
        entry = self.entry
        self.process = subprocess.Popen(
            entry["command"], env={**os.environ, **entry.get("env", {})},
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1,
        )

    def request(self, path, workload, reps):
        process = self.process
        process.stdin.write(json.dumps({"path": str(path), "workload": workload, "reps": reps}) + "\n")
        process.stdin.flush()
        if not select.select([process.stdout], [], [], args.timeout)[0]:
            self.process.kill()
            self.process.wait()
            self.start()
            return {"error": "TimeoutError", "detail": f"{args.timeout}-second request budget exceeded; worker restarted"}
        line = process.stdout.readline()
        if not line:
            code = process.wait()
            self.start()
            return {"error": "WorkerExit", "detail": f"exit {code}; worker restarted"}
        response = json.loads(line)
        if "error" not in response:
            assert response["count"] >= 0
            assert len(response["ns"]) == reps
            assert all(math.isfinite(ns) and ns > 0 for ns in response["ns"])
            if workload == "visit":
                assert math.isfinite(response["checksum"])
        return response

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=5)

workers = []
with args.output.open("x") as out:
    try:
        with gzip.GzipFile(filename=str(manifest_path), mode="wb", mtime=0) as compressed:
            with io.TextIOWrapper(compressed, newline="") as destination:
                writer = csv.writer(destination)
                writer.writerow(("file", "bytes", "sha256"))
                writer.writerows(manifest)
        metadata["corpus_manifest_sha256"] = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        for entry in config["variants"]:
            workers.append(Worker(entry))
        # Warm every decoder before recording, including JIT compilation.
        for worker in workers:
            for i in range(args.warmup):
                path = files[i * len(files) // args.warmup]
                worker.request(path, worker.entry["workloads"][0], 3)
        jobs = [(w, kind) for w in workers for kind in w.entry["workloads"]]
        for round_id in range(args.rounds):
            order = jobs if round_id % 2 == 0 else list(reversed(jobs))
            for i, path in enumerate(files):
                shift = i % len(order)
                for worker, kind in order[shift:] + order[:shift]:
                    result = worker.request(path, kind, args.reps)
                    record = {
                        "round": round_id, "file": path.name, "bytes": path.stat().st_size,
                        "variant": worker.entry["name"], "workload": kind, **result,
                    }
                    out.write(json.dumps(record, allow_nan=False) + "\n")
                if (i + 1) % 100 == 0:
                    out.flush()
                    print(f"round {round_id + 1}: {i + 1}/{len(files)}", file=sys.stderr, flush=True)
        metadata["complete"] = True
    finally:
        metadata["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        for worker in workers:
            worker.close()
        meta_path.write_text(json.dumps(metadata, indent=2) + "\n")
