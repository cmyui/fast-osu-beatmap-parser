"""Changed flags must rebuild each output, even after a partial build."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

Path('build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='config-test-', dir='build') as temp:
    out = Path(temp)
    binary = out / 'library_first'
    config = out / 'config.json'

    def make(target, optimization):
        subprocess.run(['make', f'BUILD_DIR={out}', f'CXXFLAGS=-std=c++20 {optimization}',
                        'CFLAGS=-O2', str(out / target)], check=True)

    make('library_first', '-O1')
    before = binary.stat().st_mtime_ns
    make('library_first', '-O1')
    assert binary.stat().st_mtime_ns == before, 'identical build recompiled'
    # Change the shared config while building another output, then make its
    # timestamp indistinguishable from the old binary. The per-output command
    # record must still cause recompilation, without sleeps or clock assumptions.
    make('c_api_first', '-O2')
    os.utime(config, ns=(before, before))
    make('library_first', '-O2')
    assert binary.stat().st_mtime_ns != before, 'partial build hid a flag change'
    recorded = json.loads(binary.with_suffix('.build.json').read_text())
    assert '-O2' in recorded['compile']
    before = binary.stat().st_mtime_ns
    make('library_first', '-O1')
    assert binary.stat().st_mtime_ns != before, 'restoring flags reused a stale binary'
print('Build configuration: idle reuse, partial builds and changed flags passed')
