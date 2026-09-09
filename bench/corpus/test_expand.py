"""The manifest classifies stored file contents, not database metadata."""
import unittest

from expand import mode_of


class ModeTests(unittest.TestCase):
    def test_native_modes(self):
        for mode in range(4):
            data = f"osu file format v14\r\n[General]\r\nMode: {mode}\r\n".encode()
            self.assertEqual(mode_of(data), mode)

    def test_default_and_unrelated_sections(self):
        self.assertEqual(mode_of(b"\xef\xbb\xbfosu file format v14\n[Metadata]\nMode: 3\n"), 0)

    def test_rejects_non_maps_and_invalid_mode(self):
        for data in (b"<html>Not found</html>", b"osu file format v14\n[General]\nMode: 9"):
            with self.assertRaises(ValueError):
                mode_of(data)


if __name__ == "__main__":
    unittest.main()
