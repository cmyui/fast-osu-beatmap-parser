"""Summarize benchmark CSV: mean per-map minima and all-run distribution.

Groups by variant and workload (or fresh/reused result). Minima estimate an
uncontended lower envelope; all-run statistics retain scheduler interference.
"""

import argparse
import collections
import csv
import json
from pathlib import Path
from statistics import mean, median

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("csv", type=Path)
args = parser.parse_args()
groups: collections.defaultdict[
    tuple[str, str], collections.defaultdict[str, list[dict[str, str]]]
] = collections.defaultdict(lambda: collections.defaultdict(list))
with args.csv.open(newline="") as stream:
    for row in csv.DictReader(stream):
        if int(row.get("exit_status", 0)):
            parser.error("a measured process failed")
        kind = row.get(
            "workload", "reuse=" + row["reuse"] if "reuse" in row else "process"
        )
        groups[(row["variant"], kind)][row["file"]].append(row)
if not groups:
    parser.error("no measurements")
summaries = []
for (variant, kind), files in groups.items():
    best = [min(rows, key=lambda r: int(r["wall_ns"])) for rows in files.values()]
    values = sorted(int(r["wall_ns"]) / 1000 for r in best)
    all_values = [int(r["wall_ns"]) / 1000 for rows in files.values() for r in rows]
    result = {
        "variant": variant,
        "workload": kind,
        "files": len(files),
        "runs": len(all_values),
        "reps_min": min(map(len, files.values())),
        "reps_max": max(map(len, files.values())),
        "mean_file_min_us": mean(values),
        "p50_file_min_us": median(values),
        "p90_file_min_us": values[int(0.9 * len(values))],
        "all_mean_us": mean(all_values),
        "all_p50_us": median(all_values),
    }
    if "bytes" in best[0]:
        result["MB_per_second"] = sum(int(r["bytes"]) for r in best) / sum(values)
    if "minor_faults" in best[0]:
        result["mean_faults_at_file_min"] = mean(int(r["minor_faults"]) for r in best)
    summaries.append(result)
print(json.dumps(summaries, indent=2))
