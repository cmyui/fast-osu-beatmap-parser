"""Verify the Linux C ABI exports and optional private runtime boundary."""
import argparse
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('library')
p.add_argument('--bundled', action='store_true')
a = p.parse_args()
exports = subprocess.check_output(['nm', '-D', '--defined-only', a.library], text=True)
names = {line.split()[-1] for line in exports.splitlines()}
expected = {'fosu_backend_name', 'fosu_backend_available', 'fosu_abi_version', 'fosu_new', 'fosu_free', 'fosu_parse',
            'fosu_parse_file', 'fosu_get_view'}
assert names == expected, names ^ expected
if a.bundled:
    dynamic = subprocess.check_output(['readelf', '-d', a.library], text=True)
    assert 'libstdc++' not in dynamic and 'libgcc_s' not in dynamic
print('C ABI exports and runtime dependencies passed')
