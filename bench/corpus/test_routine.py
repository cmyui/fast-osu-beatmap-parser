import collections
import hashlib
from pathlib import Path
import tempfile
import unittest

from routine import PERFORMANCE_SLOTS, materialize, performance_sample


class RoutineProfilesTest(unittest.TestCase):
    def test_popularity_weighting_and_determinism(self):
        maps = [dict(file=f"{mode * 100 + i}.osu", mode=mode, bytes=i + 1, timing_rows=i, complexity_band=i % 5,
                     playcount=91 if i == 0 else 1)
                for mode in range(4) for i in range(10)]
        sample = performance_sample(maps, "test")
        self.assertEqual(sample, performance_sample(list(reversed(maps)), "test"))
        counts = collections.Counter(sample)
        for mode in range(4):
            self.assertEqual(sum(counts[m["file"]] for m in maps if m["mode"] == mode), PERFORMANCE_SLOTS)
            self.assertLessEqual(abs(counts[f"{mode * 100}.osu"] - .91 * PERFORMANCE_SLOTS), 2)
        self.assertNotEqual(sample, performance_sample(maps, "confirmation"))

    def test_materialization_checks_content_and_preserves_repeated_plays(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = b"osu file format v14\n"
            (root / "1.osu").write_bytes(data)
            selection = dict(schema=1, maps={"1.osu": dict(mode=0, bytes=len(data),
                             sha256=hashlib.sha256(data).hexdigest())},
                             profiles={"performance": ["1.osu", "1.osu"]})
            materialize(selection, root, root / "valid")
            self.assertEqual(len(list((root / "valid/performance").glob("*.osu"))), 2)
            (root / "1.osu").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "Corpus content differs"):
                materialize(selection, root, root / "invalid")
            self.assertFalse((root / "invalid").exists())


if __name__ == "__main__":
    unittest.main()
