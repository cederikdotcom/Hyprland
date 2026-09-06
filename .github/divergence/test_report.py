"""Regression checks for the copied Omarchy accounting engine."""
import copy
import tempfile
from pathlib import Path
import unittest
import report

class AccountingTests(unittest.TestCase):
    def registry(self, duplicate=False):
        groups = [{"id": "one", "title": "One", "status": "porting",
                   "handling": "Test", "rationale": "Test", "pathspecs": ["src/"]}]
        if duplicate:
            other = copy.deepcopy(groups[0])
            other["id"] = "two"
            groups.append(other)
        import json
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "registry.json"
            path.write_text(json.dumps({"groups": groups}))
            return report.load_registry(path)

    def test_single_owner(self):
        entries = report.parse_numstat("3\t2\tsrc/test.cpp\n")
        buckets, _ = report.classify(entries, self.registry())
        measured = report.weigh(buckets, self.registry(), entries)
        self.assertEqual(measured["total_files"], 1)
        self.assertEqual(measured["total_lines"], 5)

    def test_unknown_path_fails(self):
        with self.assertRaises(report.AccountingError):
            report.classify(report.parse_numstat("1\t0\tunknown.cpp\n"), self.registry())

    def test_duplicate_owner_fails(self):
        with self.assertRaises(report.AccountingError):
            report.classify(report.parse_numstat("1\t0\tsrc/test.cpp\n"), self.registry(True))

    def test_preserve_human_issue_text(self):
        once = report.splice("Human notes", "Old", "one")
        twice = report.splice(once, "New", "one")
        self.assertIn("Human notes", twice)
        self.assertNotIn("Old", twice)
        self.assertEqual(twice.count("New"), 1)

    def test_binary_has_no_line_weight(self):
        entries = report.parse_numstat("-\t-\tsrc/blob\n")
        buckets, _ = report.classify(entries, self.registry())
        measured = report.weigh(buckets, self.registry(), entries)
        self.assertEqual(measured["total_binary_files"], 1)
        self.assertEqual(measured["total_lines"], 0)

if __name__ == "__main__":
    unittest.main()
