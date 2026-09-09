"""Compare every parsed value against a reference executable, byte for byte."""
import argparse
import hashlib
import pathlib
import subprocess
import sys
import struct
import json
from support.decode import decode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("reference", type=pathlib.Path)
parser.add_argument("candidate", type=pathlib.Path)
parser.add_argument("corpus", type=pathlib.Path)
parser.add_argument("--expected-files", type=int)
parser.add_argument("--report", type=pathlib.Path,
                    help="collect all mismatches in a host-local report instead of stopping at the first")
args = parser.parse_args()
ref, candidate = str(args.reference.resolve()), str(args.candidate.resolve())
files = sorted(args.corpus.glob("*.osu"))
if not files or (args.expected_files is not None and len(files) != args.expected_files):
    parser.error(f"unexpected corpus size: {len(files)}")
digest = hashlib.sha256()
mismatches = []


def canonical(data):
    values = decode(data)
    def bits(value):
        if isinstance(value, float):
            return ('float64', struct.pack('<d', value))
        if isinstance(value, dict):
            return {k: bits(v) for k, v in value.items()}
        if isinstance(value, (list, tuple)):
            return tuple(bits(v) for v in value)
        return value
    return bits(values)

for i, path in enumerate(files, 1):
    expected = subprocess.check_output([ref, str(path)])
    if not expected:
        raise RuntimeError(f"Reference emitted no values for {path.name}")
    actual = subprocess.check_output([candidate, str(path)])
    equal = expected == actual
    if not equal:
        offset = next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), min(len(expected), len(actual)))
        print('MISMATCH', path.name, 'offset', offset, 'lengths', len(expected), len(actual), flush=True)
        if args.report is None:
            sys.exit(1)
        a, b = canonical(expected), canonical(actual)
        mismatches.append({'file': path.name,
                           'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                           'fields': [key for key in a if a[key] != b[key]]})
    digest.update(path.name.encode() + b'\0' + expected)
    if i % 2000 == 0:
        print('verified', i, flush=True)
if args.report:
    args.report.write_text(json.dumps({'files': len(files), 'mismatches': mismatches,
                                      'reference_sha256': digest.hexdigest()}, indent=2) + '\n')
if mismatches:
    print('Mismatches:', len(mismatches), 'of', len(files))
    sys.exit(1)
print('Exact byte equality', len(files),
      'files; string bytes, float bits, pool indices and stats; reference sha256', digest.hexdigest())
