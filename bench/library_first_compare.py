"""Rotate first-use executables within each file; time first parse after read.

The child reports parser time; process startup, file I/O, destruction and
serialization are excluded. c_api_first.c instead reports its whole first-library-use interval.
This is not the one-shot end-to-end benchmark.
"""

import argparse
import csv
import subprocess
import sys
from pathlib import Path

from common import select_files

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("corpus", type=Path)
parser.add_argument("binaries", nargs="+", type=Path)
parser.add_argument("--limit", type=int, default=1000)
parser.add_argument("--reps", type=int, default=5)
args = parser.parse_args()
files = select_files(parser, args.corpus, args.reps, args.limit)
binaries = [str(b.resolve()) for b in args.binaries]
writer = csv.writer(sys.stdout)
writer.writerow(["file", "bytes", "rep", "variant", "reuse", "wall_ns"])
for i, file in enumerate(files):
    file.read_bytes()  # resident input, outside every timed region
    for rep in range(args.reps):
        for j in range(len(binaries)):
            binary = binaries[(i + rep + j) % len(binaries)]
            columns = subprocess.check_output([binary, str(file)]).split()
            writer.writerow(
                [file.name, int(columns[0]), rep, binary, 0, int(columns[1])]
            )
