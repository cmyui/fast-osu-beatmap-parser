"""Checks for the comparison's inclusion and aggregation rules."""
import copy
import unittest

from report import summarize


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.metadata = {"complete": True, "rounds": 2, "reps": 1, "files": 3, "config": {
            "reference": "fosu-python-avx2", "variants": [
                {"name": name, "workloads": ["bytes"]}
                for name in ("fosu-python-avx2", "slider")
            ],
        }}
        self.records = [
            {"variant": variant, "workload": "bytes", "file": file, "round": round_id,
             "bytes": 100, "count": 2, "ns": [1000]}
            for variant in ("fosu-python-avx2", "slider")
            for file in ("a.osu", "b.osu", "c.osu") for round_id in range(2)
        ]

    def test_failure_and_count_mismatch_exclude_same_files_from_both_rows(self):
        for record in self.records:
            if record["variant"] == "slider":
                if record["file"] == "b.osu" and record["round"] == 1:
                    record.update(error="DecodeError")
                if record["file"] == "c.osu":
                    record["count"] = 1
        table = summarize(self.records, self.metadata)["tables"]["python"]
        self.assertEqual(table["files"], 1)
        self.assertEqual(table["excluded_files"], ["b.osu", "c.osu"])
        self.assertTrue(all(row["samples"] == 2 for row in table["rows"].values()))

    def test_slow_samples_are_not_filtered(self):
        self.records[0]["ns"] = [61000]
        row = summarize(self.records, self.metadata)["tables"]["python"]["rows"]["fosu-python-avx2/bytes"]
        self.assertEqual(row["mean_us"], 11)
        self.assertEqual(row["pass_mean_us"], [21, 1])

    def test_matching_counts_do_not_hide_traversal_disagreement(self):
        for variant in self.metadata["config"]["variants"]:
            variant["workloads"] = ["visit"]
        for record in self.records:
            record.update(workload="visit", checksum=1000.0)
            if record["variant"] == "slider" and record["file"] == "a.osu":
                record["checksum"] = 1001.0
        table = summarize(self.records, self.metadata)["tables"]["traversal"]
        self.assertEqual(table["files"], 2)
        self.assertEqual(table["excluded_files"], ["a.osu"])

    def test_incomplete_and_duplicate_runs_are_rejected(self):
        with self.assertRaises(ValueError):
            summarize(self.records[:-1], self.metadata)
        with self.assertRaises(ValueError):
            summarize(self.records + [self.records[0]], self.metadata)
        incomplete = copy.deepcopy(self.metadata)
        incomplete["complete"] = False
        with self.assertRaises(ValueError):
            summarize(self.records, incomplete)


if __name__ == "__main__":
    unittest.main()
