"""Exercise fresh-process file mapping and compact result preservation."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("reference", type=Path)
parser.add_argument("candidates", nargs="+", type=Path)
args = parser.parse_args()
reference = str(args.reference.resolve())
candidates = [str(path.resolve()) for path in args.candidates]

header = (
    "\ufeffosu file format v14\r\n[Metadata]\r\nTitle:日本語\r\n"
    "[Difficulty]\r\nOverallDifficulty:9\r\n"
    "[TimingPoints]\r\n8074.13793103448,344.827586206897\r\n"
    "9000,342.857142857142857142857,4,2,1,60,1,0\r\n"
).encode()
objects = (
    "[HitObjects]\r\n"
    "-48,192,1000,1,0\r\n"
    "512,192,2000,2,14,B|-259088:1726|123:456,2,240,2|0,0:0|0:0,0:0:0:0:\r\n"
    "256,192,4294967290,12,0,4294967290,0:0:0:0:\r\n"
    "320,192,5000,132,0,6000:0:0:0:0:"
).encode()

with tempfile.TemporaryDirectory(prefix="fosu-oneshot-") as temp:
    root = Path(temp)
    cases = [b"", header + objects]
    # Padding ends on, immediately before, and immediately after pages.
    for size in (3967, 3968, 3969, 4095, 4096, 4097, 8064, 8191, 8192):
        padding = size - len(header) - len(objects) - 3
        cases.append(header + b"//" + b"x" * padding + b"\n" + objects)
    for index, data in enumerate(cases):
        path = root / f"{index}.osu"
        path.write_bytes(data)
        expected = subprocess.check_output([reference, str(path), "--dump"])
        assert expected, "reference did not serialize a result"
        for candidate in candidates:
            actual = subprocess.check_output([candidate, str(path), "--dump"])
            assert actual == expected, (candidate, len(data))
    for candidate in candidates:
        result = subprocess.run([candidate, str(root / "missing.osu")])
        assert result.returncode != 0, "missing file was accepted"

print(f"Passed {len(cases)} boundary/value cases and missing-file checks")
