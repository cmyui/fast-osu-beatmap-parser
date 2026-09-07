"""Compare first Python use of packages extracted from wheels in separate dirs."""

import argparse
import csv
import os
import subprocess
import sys
from pathlib import Path
from time import perf_counter_ns

from common import select_files

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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("packages", type=Path, nargs="+")
    parser.add_argument("--limit", type=int, default=100)
    parser.add_argument("--reps", type=int, default=3)
    args = parser.parse_args()
    files = select_files(parser, args.corpus, args.reps, args.limit)
    packages = [path.resolve() for path in args.packages]
    assert len(set(packages)) == len(packages)
    for package in packages:
        assert (package / "fosu/__init__.py").is_file()
    out = csv.writer(sys.stdout)
    out.writerow(["file", "bytes", "rep", "variant", "workload", "wall_ns"])
    for i, file in enumerate(files):
        file.read_bytes()  # resident file data; fresh interpreter each time
        for rep in range(args.reps):
            for j in range(len(packages)):
                package = packages[(i + rep + j) % len(packages)]
                env = {
                    **os.environ,
                    "PYTHONPATH": str(package),
                }
                start = perf_counter_ns()
                output = subprocess.check_output(
                    [sys.executable, "-c", CHILD, str(file.resolve())],
                    cwd=package,
                    env=env,
                    text=True,
                )
                elapsed = perf_counter_ns() - start
                load, parse = map(int, output.split())
                for kind, ns in zip(
                    ("import", "first-file", "process"), (load, parse, elapsed)
                ):
                    out.writerow(
                        [file.name, file.stat().st_size, rep, str(package), kind, ns]
                    )


if __name__ == "__main__":
    main()
