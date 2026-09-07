"""Compare every parsed value against a reference executable, byte for byte."""
import argparse
import hashlib
import pathlib
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("reference", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
parser.add_argument("corpus", type=pathlib.Path)
parser.add_argument("--expected-files", type=int)
args = parser.parse_args()
ref, candidate = str(args.reference.resolve()), str(args.candidate.resolve())
files = sorted(args.corpus.glob("*.osu"))
if not files or (args.expected_files is not None and len(files) != args.expected_files):
    parser.error(f"unexpected corpus size: {len(files)}")
digest = hashlib.sha256()
for i, path in enumerate(files, 1):
    expected = subprocess.check_output([ref, str(path), '--dump'])
    if not expected:
        raise RuntimeError(f"Reference emitted no values for {path.name}")
    actual = subprocess.check_output([candidate, str(path), '--dump'])
    if expected != actual:
        offset = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), min(len(expected), len(actual)))
        print('MISMATCH', path.name, 'offset', offset, 'lengths', len(expected), len(actual), flush=True)
        sys.exit(1)
    digest.update(path.name.encode() + b'\0' + expected)
    if i % 2000 == 0:
        print('verified', i, flush=True)
print('Exact equality:', len(files), 'files; all fields, string bytes, float bits, pool indices and stats; sha256', digest.hexdigest())
