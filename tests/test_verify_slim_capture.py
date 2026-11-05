import copy
import json
from pathlib import Path
import tempfile
import unittest

from verify_slim_capture import verify


class CaptureAcceptance(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.full_path = Path(self.temp.name) / "full.jsonl"
        self.slim_path = Path(self.temp.name) / "slim.jsonl"
        self.full = [{"header": {
            "event_id": 1, "event_opcode": 0, "event_version": 1,
            "process_id": 42, "thread_id": 7, "timestamp": "2026-11-05 12:00:00Z",
            "trace_name": "fixture", "provider_name": "fixture", "task_name": "task",
            "activity_id": "{00000000-0000-0000-0000-000000000000}",
            "event_name": "", "event_flags": 576},
            "properties": {"Sequence": i, "Broken": "ABCD"},
            "property_types": {"Sequence": "INT32", "Broken": "OTHER"}}
            for i in range(3)]
        self.slim = copy.deepcopy(self.full)
        for row in self.slim:
            row.pop("property_types")
            for key in ("activity_id", "event_name", "event_flags"):
                row["header"].pop(key)
            row["header"]["timestamp"] = "2026-11-05 12:00:00.123Z"
            row["parse_errors"] = ["Broken"]

    def check(self, **kwargs):
        for path, rows in ((self.full_path, self.full), (self.slim_path, self.slim)):
            path.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
        return verify(self.full_path, self.slim_path, **kwargs)

    def test_accepts_matching_capture_and_channel(self):
        result = self.check(same_records=True)
        self.assertEqual(result["records"], 3)
        self.assertGreater(result["saved_percent"], 0)
        self.check(same_records=True, channel_path=self.slim_path)

    def test_rejects_dropped_record(self):
        self.slim.pop()
        with self.assertRaisesRegex(ValueError, "counts differ"):
            self.check()

    def test_rejects_missing_failure_diagnostic(self):
        self.slim[0].pop("parse_errors")
        with self.assertRaisesRegex(ValueError, "parse_errors"):
            self.check()

    def test_rejects_reordered_replay(self):
        self.slim.reverse()
        with self.assertRaisesRegex(ValueError, "order changed"):
            self.check(same_records=True)

    def test_rejects_changed_value(self):
        self.slim[0]["properties"]["Broken"] = "wrong"
        with self.assertRaisesRegex(ValueError, "values or order"):
            self.check(same_records=True)

    def test_rejects_channel_pid_mismatch(self):
        channel = Path(self.temp.name) / "channel.jsonl"
        rows = copy.deepcopy(self.slim)
        rows[0]["header"]["process_id"] = 999
        channel.write_text("\n".join(map(json.dumps, rows)), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "channel/file"):
            self.check(channel_path=channel)


if __name__ == "__main__":
    unittest.main()
