"""Summarize complete runs without rewarding failures or removing slow samples."""
import argparse
from collections import Counter, defaultdict
import csv
import gzip
import hashlib
import io
import json
import math
from pathlib import Path
import shutil
import statistics


def summarize(records, metadata):
    if not metadata["complete"]:
        raise ValueError("Benchmark did not finish")
    rounds, reps = metadata["rounds"], metadata["reps"]
    variants = {v["name"]: v for v in metadata["config"]["variants"]}
    expected = {(name, kind) for name, v in variants.items() for kind in v["workloads"]}
    results = defaultdict(dict)
    sizes = {}
    for record in records:
        name, kind, file, round_id = (record[k] for k in ("variant", "workload", "file", "round"))
        if (name, kind) not in expected or not 0 <= round_id < rounds:
            raise ValueError("Unexpected worker, workload, or round")
        key = (name, kind, file)
        if round_id in results[key]:
            raise ValueError("Duplicate result")
        if "error" not in record:
            if len(record["ns"]) != reps or any(not math.isfinite(t) or t <= 0 for t in record["ns"]):
                raise ValueError("Invalid timing samples")
        results[key][round_id] = record
        if file in sizes and sizes[file] != record["bytes"]:
            raise ValueError("Input size changed")
        sizes[file] = record["bytes"]
    files = set(sizes)
    if len(files) != metadata["files"] or len(results) != len(files) * len(expected):
        raise ValueError("Missing benchmark results")
    if any(len(records) != rounds for records in results.values()):
        raise ValueError("Missing benchmark rounds")

    reference = metadata["config"]["reference"]
    matching = {}
    coverage = {}
    for name, kind in sorted(expected):
        accepted, mismatches, errors = set(), set(), {}
        for file in sorted(files):
            samples = results[name, kind, file]
            failures = {r["error"] for r in samples.values() if "error" in r}
            if failures:
                errors[file] = sorted(failures)
                continue
            refs = results[reference, kind, file]
            if any("error" in r for r in refs.values()):
                raise ValueError("Reference failed; cannot establish a comparison cohort")
            agrees = all(samples[i]["count"] == refs[i]["count"] for i in range(rounds))
            if kind == "visit":
                agrees &= all(math.isclose(samples[i]["checksum"], refs[i]["checksum"],
                                           rel_tol=0, abs_tol=0.001) for i in range(rounds))
            (accepted if agrees else mismatches).add(file)
        matching[name, kind] = accepted
        coverage[f"{name}/{kind}"] = {
            "decoded_files": len(files) - len(errors),
            "matching_files": len(accepted),
            "count_or_checksum_mismatch_files": sorted(mismatches),
            "failed_files": errors,
            "errors_by_type": dict(Counter(error for kinds in errors.values() for error in kinds)),
        }

    python = {"fosu-python-avx2", "fosu-python-scalar", "slider", "rosu-pp-py", "osupyparser", "pyttanko"}
    groups = {
        "python": sorted((n, k) for n, k in expected if n in python and k != "visit"),
        "traversal": sorted((n, k) for n, k in expected if k == "visit"),
        "other_languages": sorted((n, k) for n, k in expected
                                  if k == "bytes" and (n not in python or n == reference)),
    }
    tables = {}
    for group, members in groups.items():
        if not members:
            continue
        cohort = set.intersection(*(matching[m] for m in members))
        if not cohort:
            raise ValueError(f"No common successful maps in {group}")
        byte_count = sum(sizes[file] for file in cohort)
        rows = {}
        for name, kind in members:
            passes = [[t for file in sorted(cohort) for t in results[name, kind, file][i]["ns"]]
                      for i in range(rounds)]
            samples = [t for times in passes for t in times]
            mean = statistics.mean(samples)
            rows[f"{name}/{kind}"] = {
                "mean_us": mean / 1000,
                "pass_mean_us": [statistics.mean(times) / 1000 for times in passes],
                "median_sample_us": statistics.median(samples) / 1000,
                "maps_per_second": 1e9 / mean,
                "MB_per_second": byte_count / len(cohort) / mean * 1000,
                "samples": len(samples),
            }
        tables[group] = {
            "files": len(cohort), "bytes": byte_count,
            "excluded_files": sorted(files - cohort), "rows": rows,
        }
    return {"coverage": coverage, "tables": tables}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    metadata = json.loads(args.results.with_suffix(".meta.json").read_text())
    with args.results.open() as source:
        report = summarize((json.loads(line) for line in source), metadata)
    # Publish evidence without copying machine-specific commands or paths.
    report["run"] = {k: metadata[k] for k in (
        "started_utc", "finished_utc", "platform", "python", "affinity", "files", "bytes",
        "corpus_sha256", "corpus_manifest_sha256", "reps", "rounds", "warmup_maps", "request_timeout_seconds",
    )}
    report["run"]["versions"] = {v["name"]: v["version"] for v in metadata["config"]["variants"]}
    report["run"]["raw_results_sha256"] = hashlib.sha256(args.results.read_bytes()).hexdigest()
    samples_path = args.output.with_suffix(".samples.csv.gz")
    fields = ("round", "file", "bytes", "variant", "workload", "count", "checksum", "error", "ns")
    with gzip.GzipFile(filename=str(samples_path), mode="wb", mtime=0) as compressed:
        with io.TextIOWrapper(compressed, newline="") as destination, args.results.open() as source:
            writer = csv.DictWriter(destination, fieldnames=fields, extrasaction="ignore")
            writer.writeheader()
            for line in source:
                record = json.loads(line)
                if "ns" in record:
                    record["ns"] = json.dumps(record["ns"], separators=(",", ":"))
                writer.writerow(record)
    report["run"]["samples_sha256"] = hashlib.sha256(samples_path.read_bytes()).hexdigest()
    shutil.copyfile(args.results.with_suffix(".corpus.csv.gz"), args.output.with_suffix(".corpus.csv.gz"))
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
