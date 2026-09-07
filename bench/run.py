"""Capture a benchmark's raw stdout plus reproducibility metadata in a sidecar.

Run under taskset/flock externally when needed. Corpus hashing happens before
measurement. Sidecars include local paths/commands; review before publishing.
"""

import argparse
import hashlib
import json
import os
import platform
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--corpus", required=True, type=Path)
parser.add_argument(
    "--config",
    action="append",
    type=Path,
    default=[],
    help="build config.json, repeat for variants",
)
parser.add_argument("command", nargs=argparse.REMAINDER)
args = parser.parse_args()
command = args.command[1:] if args.command[:1] == ["--"] else args.command
if not command:
    parser.error("a benchmark command is required after --")
files = sorted(args.corpus.glob("*.osu"))
if not files:
    parser.error("empty corpus")
digest = hashlib.sha256()
size = 0
for file in files:
    data = file.read_bytes()
    digest.update(file.name.encode() + b"\0" + hashlib.sha256(data).digest())
    size += len(data)


def git(*args: str) -> bytes:
    return subprocess.check_output(["git", *args])


metadata = {
    "command": command,
    "platform": platform.platform(),
    "machine": platform.machine(),
    "python": platform.python_version(),
    "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "affinity": sorted(os.sched_getaffinity(0))
    if hasattr(os, "sched_getaffinity")
    else None,
    "corpus": {
        "files": len(files),
        "bytes": size,
        "sha256": digest.hexdigest(),
    },
    "configs": {str(path): json.loads(path.read_text()) for path in args.config},
    "environment": {k: v for k, v in os.environ.items() if k.startswith("FOSU_")},
}
try:
    metadata["revision"] = git("rev-parse", "HEAD").decode().strip()
    metadata["tracked_diff_sha256"] = hashlib.sha256(
        git("diff", "HEAD", "--")
    ).hexdigest()
except (OSError, subprocess.CalledProcessError):
    metadata["revision"] = None
# Never silently overwrite previous evidence.
args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.open("x") as out:
    result = subprocess.run(command, stdout=out)
metadata["exit_status"] = result.returncode
args.output.with_suffix(args.output.suffix + ".json").write_text(
    json.dumps(metadata, indent=2) + "\n"
)
raise SystemExit(result.returncode)
