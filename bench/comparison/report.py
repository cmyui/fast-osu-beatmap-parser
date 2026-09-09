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


def summarize(records, metadata, cohorts=None):
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
    modes = metadata.get("file_modes", {})
    if modes and (set(modes) != files or any(mode not in range(4) for mode in modes.values())):
        raise ValueError("Mode manifest does not cover the measured corpus")
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
        if modes:
            coverage[f"{name}/{kind}"]["by_mode"] = {
                str(mode): {
                    "files": sum(m == mode for m in modes.values()),
                    "matching_files": sum(modes[f] == mode for f in accepted),
                    "failed_files": sum(modes[f] == mode for f in errors),
                    "mismatching_files": sum(modes[f] == mode for f in mismatches),
                } for mode in sorted(set(modes.values()))
            }

    python = {"fosu-python-avx2", "fosu-python-scalar", "slider", "rosu-pp-py", "osupyparser", "pyttanko"}
    groups = {
        "python": sorted((n, k) for n, k in expected if n in python and k != "visit"),
        "traversal": sorted((n, k) for n, k in expected if k == "visit"),
        "other_languages": sorted((n, k) for n, k in expected
                                  if k == "bytes" and (n not in python or n == reference)),
    }
    scopes = {group: files for group in groups}
    if modes:
        for group, members in list(groups.items()):
            # Never let a standard-only decoder silently select the all-mode cohort.
            all_modes = f"{group}_all_modes"
            groups[all_modes] = [m for m in members if set(variants[m[0]].get("modes", range(4))) == set(range(4))]
            scopes[all_modes] = files
            for mode in sorted(set(modes.values())):
                name = f"{group}_mode_{mode}"
                groups[name] = [m for m in members if mode in variants[m[0]].get("modes", range(4))]
                scopes[name] = {f for f in files if modes[f] == mode}
    tables = {}
    for group, members in groups.items():
        if not members:
            continue
        scope = scopes[group]
        cohort = scope & set.intersection(*(matching[m] for m in members))
        if cohorts is not None:
            previous = cohorts[group]
            fixed = files - set(previous["excluded_files"])
            if (len(fixed) != previous["files"]
                    or sum(sizes[file] for file in fixed) != previous["bytes"]
                    or not fixed <= cohort):
                raise ValueError(f"Cannot reproduce the fixed {group} cohort")
            cohort = fixed
        byte_count = sum(sizes[file] for file in cohort)
        rows = {}
        for name, kind in members:
            if not cohort:
                continue
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
        if modes:
            tables[group]["modes"] = dict(sorted(Counter(str(modes[f]) for f in cohort).items()))
    return {"coverage": coverage, "tables": tables}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--cohorts", type=Path, help="Keep a prior report's exact table cohorts")
    args = parser.parse_args()
    metadata = json.loads(args.results.with_suffix(".meta.json").read_text())
    previous = json.loads(args.cohorts.read_text()) if args.cohorts else None
    if previous and previous["run"]["corpus_sha256"] != metadata["corpus_sha256"]:
        raise ValueError("Fixed cohorts require the same corpus contents")
    with args.results.open() as source:
        report = summarize((json.loads(line) for line in source), metadata,
                           previous["tables"] if previous else None)
    # Publish evidence without copying machine-specific commands or paths.
    report["run"] = {k: metadata[k] for k in (
        "started_utc", "finished_utc", "platform", "python", "affinity", "files", "bytes",
        "corpus_sha256", "corpus_manifest_sha256", "reps", "rounds", "warmup_maps", "request_timeout_seconds",
    )}
    report["run"]["versions"] = {v["name"]: v["version"] for v in metadata["config"]["variants"]}
    if args.cohorts:
        report["run"]["cohort_source_sha256"] = hashlib.sha256(args.cohorts.read_bytes()).hexdigest()
    report["run"]["raw_results_sha256"] = hashlib.sha256(args.results.read_bytes()).hexdigest()
    samples_path = args.output.with_suffix(".samples.csv.gz")
    fields = ("round", "file", "bytes", "mode", "variant", "workload", "count", "checksum", "error", "ns")
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
