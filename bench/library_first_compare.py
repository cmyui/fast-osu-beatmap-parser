"""Rotate first-use executables within each file; time first parse after read.

The child reports parser time; process startup, file I/O, destruction and
serialization are excluded. c_api_first.c instead reports its whole first-library-use interval.
This is not the one-shot end-to-end benchmark.
"""
import argparse
import csv
from pathlib import Path
import subprocess
import sys
from common import select_files

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('corpus', type=Path)
p.add_argument('binaries', nargs='+', type=Path)
p.add_argument('--limit', type=int, default=1000)
p.add_argument('--reps', type=int, default=5)
a = p.parse_args()
files = select_files(p, a.corpus, a.reps, a.limit)
bins = [str(b.resolve()) for b in a.binaries]
out = csv.writer(sys.stdout)
out.writerow(['file', 'bytes', 'rep', 'variant', 'reuse', 'wall_ns'])
for i, f in enumerate(files):
    f.read_bytes()  # resident input, outside every timed region
    for rep in range(a.reps):
        for j in range(len(bins)):
            binary = bins[(i + rep + j) % len(bins)]
            cols = subprocess.check_output([binary, str(f)]).split()
            out.writerow([f.name, int(cols[0]), rep, binary, 0, int(cols[1])])
