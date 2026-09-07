"""Compare first Python use of packages extracted from wheels in separate dirs."""
import argparse
import json
import os
from pathlib import Path
from statistics import mean
import subprocess
import sys
from time import perf_counter_ns

CHILD = """
from time import perf_counter_ns
import sys
start = perf_counter_ns()
import fosu
loaded = perf_counter_ns()
bm = fosu.parse_file(sys.argv[1])
count = len(bm.hit_objects)
del bm
parsed = perf_counter_ns()
print(loaded - start, parsed - loaded)
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("packages", type=Path, nargs="+")
    parser.add_argument("--limit", type=int, default=100)
    parser.add_argument("--reps", type=int, default=3)
    args = parser.parse_args()
    files = sorted(args.corpus.glob("*.osu"))
    assert files and args.limit > 0 and args.reps > 0
    count = min(args.limit, len(files))
    files = [files[i * len(files) // count] for i in range(count)]
    packages = [path.resolve() for path in args.packages]
    assert len(set(packages)) == len(packages)
    for package in packages:
        assert (package / "fosu/__init__.py").is_file()
    runs, minima = ({path: [] for path in packages} for _ in range(2))
    for i, file in enumerate(files):
        file.read_bytes()  # resident file data; fresh interpreter each time
        per_file = {path: [] for path in packages}
        for rep in range(args.reps):
            for j in range(len(packages)):
                package = packages[(i + rep + j) % len(packages)]
                env = dict(os.environ, PYTHONPATH=str(package))
                start = perf_counter_ns()
                output = subprocess.check_output(
                    [sys.executable, "-c", CHILD, str(file.resolve())],
                    cwd=package, env=env, text=True,
                )
                elapsed = perf_counter_ns() - start
                load, parse = map(int, output.split())
                per_file[package].append((load, parse, elapsed))
        for package, values in per_file.items():
            runs[package].extend(values)
            minima[package].append(tuple(min(row[k] for row in values) for k in range(3)))
    def summary(values):
        return dict(zip(("import_us", "first_parse_file_us", "process_us"),
                        (mean(row[k] for row in values) / 1000 for k in range(3))))
    print(json.dumps({"files": len(files), "reps": args.reps, "variants": [
        {"package": str(path), "all_mean": summary(runs[path]),
         "mean_file_min": summary(minima[path])} for path in packages
    ]}, indent=2))


if __name__ == "__main__":
    main()
