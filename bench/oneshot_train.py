"""Collect compiler profiles from fresh processes on a distributed sample."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("corpus", type=Path)
parser.add_argument("--stride", type=int, default=5)
args = parser.parse_args()
files = sorted(args.corpus.glob("*.osu"))
if not files or args.stride < 1:
    parser.error("provide a nonempty corpus and positive stride")
for path in files[::args.stride]:
    subprocess.run([str(args.binary.resolve()), str(path)], check=True)
print(f"Trained on {len(files[::args.stride])} fresh processes")
