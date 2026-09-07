"""Exercise process I/O, padding, streamed records and complete value preservation."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "oneshot"))
from decode import decode

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
    "512,192,2000,2,14,B|-129088:1726|123:456,2,240,2|0,0:0|0:0,0:0:0:0:\r\n"
    "256,192,2147483647,12,0,2147483647,0:0:0:0:\r\n"
    "320,192,5000,132,0,6000:0:0:0:0:"
).encode()

with tempfile.TemporaryDirectory(prefix="fosu-oneshot-") as temp:
    root = Path(temp)
    cases = [b"", header + objects]
    cases.extend(p.read_bytes() for p in (Path(__file__).parent / "fuzz-seeds").glob("*.osu"))
    cases.append(b"[HitObjects]\n\r\r// comment\n1,2,3,1,0\x00junk\n")
    # Padding ends on, immediately before, and immediately after pages.
    for size in (3967, 3968, 3969, 4095, 4096, 4097, 8064, 8191, 8192):
        padding = size - len(header) - len(objects) - 3
        cases.append(header + b"//" + b"x" * padding + b"\n" + objects)
    last_slider = b"[HitObjects]\n1,2,3,2,0,B|-129088:1726|123:456,2,240,2|0,0:0|0:0"
    for size in (3967, 3968, 3969):
        cases.append(b"//" + b"x" * (size - len(last_slider) - 3) + b"\n" + last_slider)
    # A failed slider retains points only after its point loop succeeds.
    orphan_case = b"[HitObjects]\n1,2,3,2,0,B|10:20|30:40,X,1\n1,2,4,2,0,L|50:60,1,10\n"
    cases.append(orphan_case)
    # hitSound has exactly the bytes TRLR: delimiter searching cannot frame records.
    cases.append(b"[Metadata]\nTitle:TRLR\n[HitObjects]\n1,2,3,1,1380733524,0:0:0:0:TRLR\n")
    cases.append(b"[Events]\n" + b"2,100,200\n" * 20 +
                 b"[Colours]\n" + b"Combo1 : 1,2,3\n" * 12)
    cases.append(b"[TimingPoints]\n0,500\n[HitObjects]\n1,2,3,1,0\n" * 3)
    cases.append(b"[TimingPoints]\n" + b"0,500\n" * 400 + b"[HitObjects]\n1,2,3,1,0\n")
    # Force output flushes; a larger trailer must keep its footer length valid.
    cases.append(b"[HitObjects]\n" + b"1,2,3,1,0\n" * 140000 +
                 b"[Metadata]\nTitle:" + b"t" * 200000 + b"\n")
    for index, data in enumerate(cases):
        path = root / f"{index}.osu"
        path.write_bytes(data)
        expected = subprocess.check_output([reference, str(path)])
        assert expected, "reference did not serialize a result"
        parsed = decode(expected)
        if data == orphan_case:
            assert parsed['slider_points'] == [(10, 20), (30, 40), (50, 60)]
            assert parsed['sliders'][0]['point_begin'] == 2
        if b"1380733524" in data:
            assert parsed['hit_objects'][0]['hitsound'] == int.from_bytes(b'TRLR', 'little')
        for candidate in candidates:
            actual = subprocess.check_output([candidate, str(path)])
            assert actual == expected, (candidate, len(data))
    for candidate in candidates:
        result = subprocess.run([candidate, str(root / "missing.osu")])
        assert result.returncode != 0, "missing file was accepted"

print(f"Passed {len(cases)} boundary/value cases and missing-file checks")
