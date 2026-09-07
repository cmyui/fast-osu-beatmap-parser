"""Record effective native build commands, updating mtime only on changes."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
for name in ('cxx', 'cc', 'compile', 'cflags', 'link', 'oneshot'):
    p.add_argument('--' + name, required=True)
a = p.parse_args()
config = {k: v for k, v in vars(a).items() if k != 'output'}
config['source_directory'] = str(Path.cwd())
config['build_directory'] = str(a.output.parent.resolve())
for name in ('cxx', 'cc'):
    result = subprocess.run(shlex.split(config[name]) + ['--version'],
                            check=True, text=True, capture_output=True)
    config[name + '_version'] = result.stdout
# The optional one-shot compiler need not be installed for hosted builds.
try:
    result = subprocess.run(shlex.split(config['oneshot'])[:1] + ['--version'],
                            check=True, text=True, capture_output=True)
    config['oneshot_version'] = result.stdout
except (OSError, subprocess.CalledProcessError):
    config['oneshot_version'] = None
text = json.dumps(config, indent=2) + '\n'
changed = not a.output.exists() or a.output.read_text() != text
if changed:
    temp = a.output.with_suffix('.tmp')
    temp.write_text(text)
    temp.replace(a.output)
