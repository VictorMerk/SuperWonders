import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "pipeline_miss_report", Path(__file__).parents[1] / "tools/perf/summarize_pipeline_misses.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class PipelineReportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def event(self, sequence=1, operation="lookup_miss", **extra):
        row = dict(sequence=sequence, observed_ns=1000, frame=3,
                   key_fingerprint=123, vs_hash="1234567890abcdef", fs_hash="",
                   caller_may_skip=1, effective_may_skip=0, gpl=0,
                   operation=operation, duration_ns=0, result_known=0, result="")
        row.update(extra)
        return row

    def trace(self, rows, **metadata):
        meta = dict(schema=1, total_events=len(rows), retained_events=len(rows),
                    evicted_events=0, capacity=4096)
        meta.update(metadata)
        path = self.root / "private-path-never-echoed.csv"
        with path.open("w", encoding="utf-8", newline="") as out:
            out.write("# " + ",".join(f"{key}={value}" for key, value in meta.items()) + "\n")
            out.write(report.SCOPE + "\n")
            writer = csv.DictWriter(out, fieldnames=report.FIELDS)
            writer.writeheader()
            writer.writerows(rows)
        return path

    def test_nested_and_async_durations_are_separate_not_summed(self):
        path = self.trace([
            self.event(1),
            self.event(2, "gpl_libraries_and_link", duration_ns=100),
            self.event(3, "cache_probe", duration_ns=80, result_known=1, result=report.COMPILE_REQUIRED),
            self.event(4, "async_queued"),
            self.event(5, "async_wait", duration_ns=30, frame=9),
            self.event(6, report.WORKER, duration_ns=500, result_known=1, result=0, frame=9),
        ])
        result = report.summarize(path)
        self.assertEqual(result["caller_operations_duration_ns"]["samples"], 3)
        self.assertEqual(result["caller_operations_duration_ns"]["max_ns"], 100)
        self.assertEqual(result["worker_compile_observed_duration_ns"]["max_ns"], 500)
        self.assertEqual(result["operations"][report.WORKER]["duration_semantics"], "worker_compile_observed_later")
        self.assertEqual(result["associated_frame_count"], 2)
        key = result["top_keys_by_event_count"][0]
        self.assertEqual(key["caller_operation_peak_ns"], 100)
        self.assertEqual(key["observed_worker_peak_ns"], 500)
        self.assertEqual(key["associated_frame_count"], 2)
        self.assertNotIn("total_duration_ns", json.dumps(result))
        self.assertEqual(result["top_keys_by_caller_operation_peak"][0]["caller_operation_peak_ns"], 100)
        self.assertIn("durations overlap", " ".join(result["limitations"]).lower())

    def test_vulkan_compile_required_is_not_a_failure(self):
        rows = [
            self.event(1, "cache_probe", result_known=1, result=report.COMPILE_REQUIRED),
            self.event(2, "cache_probe", result_known=1, result=-1),
            self.event(3, "cache_probe", result_known=1, result=0),
            self.event(4, "cache_probe", result_known=1, result=5),
            self.event(5, "cache_probe"),
        ]
        stats = report.summarize(self.trace(rows))["operations"]["cache_probe"]
        self.assertEqual(stats["outcomes"], dict(compile_required=1, failure=1, success=1,
                                                other_positive_status=1, unknown=1))

    def test_pending_pipeline_draw_omission_is_a_marker_not_compile_or_frame_time(self):
        result = report.summarize(self.trace([self.event(1, "draw_skipped_pipeline_pending")]))
        skipped = result["operations"]["draw_skipped_pipeline_pending"]
        self.assertEqual(skipped["count"], 1)
        self.assertEqual(skipped["outcomes"], {"unknown": 1})
        self.assertEqual(skipped["duration_semantics"], "event_marker")
        self.assertEqual(result["caller_operations_duration_ns"]["samples"], 0)
        self.assertEqual(result["worker_compile_observed_duration_ns"]["samples"], 0)

    def test_eviction_and_internal_gap_report_incomplete(self):
        path = self.trace([self.event(7), self.event(9)], total_events=9, evicted_events=7)
        result = report.summarize(path)
        self.assertEqual(result["evidence_status"], "incomplete")
        self.assertFalse(result["retention"]["consistent"])
        self.assertIn(dict(first=8, last=8), result["retention"]["missing_sequence_ranges"])
        self.assertIn("earlier_events_evicted", {note["code"] for note in result["diagnostics"]})
        result = report.summarize(self.trace([self.event(8), self.event(9)], total_events=9, evicted_events=7))
        self.assertTrue(result["retention"]["consistent"])
        self.assertFalse(result["retention"]["complete_recorded_event_history"])

    def test_metadata_count_truncation_and_zero_capacity(self):
        result = report.summarize(self.trace([self.event(1)], total_events=2, retained_events=2))
        self.assertEqual(result["evidence_status"], "incomplete")
        self.assertEqual(result["retention"]["missing_sequence_ranges"], [dict(first=2, last=2)])
        result = report.summarize(self.trace([], total_events=50, evicted_events=50, capacity=0))
        self.assertEqual(result["evidence_status"], "no_observations")
        self.assertEqual(result["operations"]["cache_probe"]["duration_ns"]["samples"], 0)
        self.assertEqual(result["caller_operations_duration_ns"]["p95_ns"], None)

    def test_out_of_order_timestamps_are_not_rejected_or_used_as_stalls(self):
        result = report.summarize(self.trace([
            self.event(1, observed_ns=100), self.event(2, observed_ns=20, frame=1),
        ]))
        self.assertTrue(result["retention"]["consistent"])
        self.assertEqual(result["top_keys_by_event_count"][0]["associated_frame_min"], 1)

    def test_policy_and_hashes_are_bounded_opaque_data(self):
        rows = [self.event(n, key_fingerprint=n, frame=n, vs_hash="ABCDEF1234567890",
                           caller_may_skip=n % 2, effective_may_skip=(n + 1) % 2, gpl=n % 2)
                for n in range(1, 26)]
        path = self.trace(rows)
        result = report.summarize(path)
        self.assertEqual(result["unique_key_fingerprints"], 25)
        self.assertEqual(len(result["top_keys_by_event_count"]), 20)
        self.assertEqual(len(result["top_frames_by_event_count"]), 20)
        self.assertEqual(sum(item["events"] for item in result["policy_observations"]), 25)
        self.assertEqual(result["top_keys_by_event_count"][0]["shader_hash_pairs"][0]["vs_hash"], "abcdef1234567890")
        self.assertNotIn(str(path), json.dumps(result))
        self.assertEqual(len(result["evidence_sha256"]), 64)

    def test_strict_row_types_do_not_echo_private_values(self):
        invalid = [dict(sequence="0"), dict(frame="-1"), dict(observed_ns="1e9"),
                   dict(key_fingerprint=str(1 << 64)), dict(caller_may_skip="true"),
                   dict(gpl=2), dict(duration_ns=" 1"), dict(result_known=0, result="1"),
                   dict(result_known=1, result=""), dict(result_known=1, result=str(1 << 31)),
                   dict(vs_hash="private-account-name"), dict(operation="secret-path")]
        for extra in invalid:
            with self.subTest(extra=extra):
                with self.assertRaises(report.TraceError) as caught:
                    report.summarize(self.trace([self.event(**extra)]))
                self.assertNotIn("private-account-name", str(caught.exception))
                self.assertNotIn("secret-path", str(caught.exception))

    def test_header_schema_missing_duplicate_and_malformed_csv(self):
        valid = self.trace([self.event()]).read_text()
        mutations = ["", valid.replace("schema=1", "schema=2", 1),
                     valid.replace("schema=1", "schema=1,schema=1", 1),
                     valid.replace(report.SCOPE, "# scope=private-value", 1),
                     valid.replace("sequence,observed_ns", "sequence,other", 1),
                     valid + '"unterminated\n', valid + "\n", valid.rstrip("\n"),
                     valid.replace("capacity=4096", "capacity=4097")]
        for content in mutations:
            with self.subTest(content=content[:30]):
                path = self.root / "bad.csv"
                path.write_text(content, encoding="utf-8")
                with self.assertRaises(report.TraceError):
                    report.summarize(path)

    def test_duplicate_or_backward_sequence_rejected(self):
        for second in (1, 2):
            with self.assertRaises(report.TraceError):
                report.summarize(self.trace([self.event(2), self.event(second)]))

    def test_file_line_row_and_encoding_limits(self):
        path = self.root / "bad.csv"
        path.write_bytes(b"a" * (report.MAX_LINE_BYTES + 1))
        with self.assertRaisesRegex(report.TraceError, "line exceeds"):
            report.load(path)
        path.write_bytes(b"\xff")
        with self.assertRaisesRegex(report.TraceError, "UTF-8"):
            report.load(path)
        path.write_bytes(b"\0")
        with self.assertRaisesRegex(report.TraceError, "NUL"):
            report.load(path)
        valid = self.trace([self.event(1), self.event(2)])
        with mock.patch.object(report, "MAX_BYTES", 10):
            with self.assertRaisesRegex(report.TraceError, "MiB"):
                report.load(valid)
        with mock.patch.object(report, "MAX_ROWS", 1):
            with self.assertRaisesRegex(report.TraceError, "retention limit"):
                report.load(valid)
        self.trace([self.event(1), self.event(2)], capacity=1, retained_events=1)
        with mock.patch.object(report, "MAX_ROWS", 1):
            with self.assertRaisesRegex(report.TraceError, "row limit"):
                report.load(valid)

    def test_cli_missing_input_and_output_alias_do_not_leak_or_overwrite(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = report.main([str(self.root / "private-missing.csv")])
        self.assertEqual(code, 2)
        self.assertNotIn("private-missing", output.getvalue())
        self.assertEqual(json.loads(output.getvalue())["evidence_status"], "unusable")
        path = self.trace([self.event()])
        before = path.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(report.main([str(path), "--output", str(path)]), 2)
        self.assertEqual(path.read_bytes(), before)
        hardlink = self.root / "alias.csv"
        try:
            hardlink.hardlink_to(path)
        except OSError:
            pass
        else:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(report.main([str(path), "--output", str(hardlink)]), 2)
            self.assertEqual(path.read_bytes(), before)
        destination = self.root / "report.json"
        self.assertEqual(report.main([str(path), "--output", str(destination)]), 0)
        self.assertEqual(json.loads(destination.read_text())["evidence_status"], "retained_observations")


if __name__ == "__main__":
    unittest.main()
