"""Capture a benchmark's raw stdout plus reproducibility metadata in a sidecar.

Run under taskset/flock externally when needed. Corpus hashing happens before
measurement. Sidecars include local paths/commands; review before publishing.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--corpus', required=True, type=Path)
p.add_argument('--config', action='append', type=Path, default=[], help='build config.json, repeat for variants')
p.add_argument('command', nargs=argparse.REMAINDER)
a = p.parse_args()
command = a.command[1:] if a.command[:1] == ['--'] else a.command
if not command:
    p.error('a benchmark command is required after --')
files = sorted(a.corpus.glob('*.osu'))
if not files:
    p.error('empty corpus')
digest = hashlib.sha256()
size = 0
for file in files:
    data = file.read_bytes()
    digest.update(file.name.encode() + b'\0' + hashlib.sha256(data).digest())
    size += len(data)

def git(*args):
    return subprocess.check_output(['git', *args])

metadata = dict(command=command, platform=platform.platform(), machine=platform.machine(),
                python=platform.python_version(), started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
                corpus=dict(files=len(files), bytes=size, sha256=digest.hexdigest()),
                configs={str(path): json.loads(path.read_text()) for path in a.config},
                environment={k: v for k, v in os.environ.items() if k.startswith('FOSU_')})
try:
    metadata['revision'] = git('rev-parse', 'HEAD').decode().strip()
    metadata['tracked_diff_sha256'] = hashlib.sha256(git('diff', 'HEAD', '--')).hexdigest()
except (OSError, subprocess.CalledProcessError):
    metadata['revision'] = None
# Never silently overwrite previous evidence.
a.output.parent.mkdir(parents=True, exist_ok=True)
with a.output.open('x') as out:
    result = subprocess.run(command, stdout=out)
metadata['exit_status'] = result.returncode
a.output.with_suffix(a.output.suffix + '.json').write_text(json.dumps(metadata, indent=2) + '\n')
raise SystemExit(result.returncode)
