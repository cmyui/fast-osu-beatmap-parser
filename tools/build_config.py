"""Record effective native build commands, updating mtime only on changes."""

import argparse
import json
import shlex
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
for name in ("cxx", "cc", "compile", "cflags", "link", "oneshot"):
    parser.add_argument("--" + name, required=True)
args = parser.parse_args()
config = {k: v for k, v in vars(args).items() if k != "output"}
config["source_directory"] = str(Path.cwd())
config["build_directory"] = str(args.output.parent.resolve())
for name in ("cxx", "cc"):
    result = subprocess.run(
        shlex.split(config[name]) + ["--version"],
        check=True,
        text=True,
        capture_output=True,
    )
    config[name + "_version"] = result.stdout
# The optional one-shot compiler need not be installed for hosted builds.
try:
    result = subprocess.run(
        shlex.split(config["oneshot"])[:1] + ["--version"],
        check=True,
        text=True,
        capture_output=True,
    )
    config["oneshot_version"] = result.stdout
except (OSError, subprocess.CalledProcessError):
    config["oneshot_version"] = None
text = json.dumps(config, indent=2) + "\n"
changed = not args.output.exists() or args.output.read_text() != text
if changed:
    temp = args.output.with_suffix(".tmp")
    temp.write_text(text)
    temp.replace(args.output)
