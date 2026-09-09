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

    def test_fixed_cohort_does_not_expand_when_a_variant_is_removed(self):
        tables = summarize(self.records, self.metadata)["tables"]
        tables["python"].update(files=2, bytes=200, excluded_files=["c.osu"])
        refreshed = summarize(self.records, self.metadata, tables)["tables"]["python"]
        self.assertEqual(refreshed["files"], 2)
        self.assertEqual(refreshed["excluded_files"], ["c.osu"])
        self.assertTrue(all(row["samples"] == 4 for row in refreshed["rows"].values()))

    def test_fixed_cohort_cannot_silently_drop_a_new_failure(self):
        tables = summarize(self.records, self.metadata)["tables"]
        self.records[-1].update(error="DecodeError")
        with self.assertRaises(ValueError):
            summarize(self.records, self.metadata, tables)

    def test_modes_do_not_disappear_behind_standard_only_parser(self):
        self.metadata["file_modes"] = {"a.osu": 0, "b.osu": 1, "c.osu": 3}
        self.metadata["config"]["variants"][1]["modes"] = [0]
        for record in self.records:
            if record["variant"] == "slider" and record["file"] != "a.osu":
                record["error"] = "NotImplementedError"
        report = summarize(self.records, self.metadata)
        self.assertEqual(report["tables"]["python"]["files"], 1)
        self.assertEqual(report["tables"]["python_all_modes"]["files"], 3)
        self.assertEqual(report["tables"]["python_mode_3"]["modes"], {"3": 1})
        self.assertEqual(report["coverage"]["slider/bytes"]["by_mode"]["3"]["failed_files"], 1)

    def test_empty_mode_cohort_has_no_misleading_timings(self):
        self.metadata["file_modes"] = {"a.osu": 0, "b.osu": 0, "c.osu": 3}
        for record in self.records:
            if record["variant"] == "slider" and record["file"] == "c.osu":
                record["count"] = 0
        table = summarize(self.records, self.metadata)["tables"]["python_mode_3"]
        self.assertEqual(table["files"], 0)
        self.assertEqual(table["rows"], {})


if __name__ == "__main__":
    unittest.main()
