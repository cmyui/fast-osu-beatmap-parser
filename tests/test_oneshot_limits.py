"""Standalone shares Parser's limits, without separate fixed-size arrays."""
from pathlib import Path
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
cases = [
    (b'[TimingPoints]\n0,500\n' * 9, 0),
    (b'[Events]\n' + b'2,1,2\n' * (16 + 32768 + 1), 0),
    (b'[Colours]\n' + b'Combo1 : 1,2,3\n' * (8 + 4096 + 1), 0),
]
with tempfile.TemporaryDirectory(prefix='fosu-limits-') as temp:
    path = Path(temp) / 'map.osu'
    for data, code in cases:
        path.write_bytes(data)
        result = subprocess.run([binary, str(path)], stdout=subprocess.DEVNULL)
        assert result.returncode == code, (len(data), code, result.returncode)
    with path.open('wb') as f:
        f.truncate(64 * 1024 * 1024 + 1)
    assert subprocess.run([binary, str(path)], stdout=subprocess.DEVNULL).returncode == 6
print('Standalone shared-parser limits passed')
