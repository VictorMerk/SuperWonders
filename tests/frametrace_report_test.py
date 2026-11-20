"""Timing-domain regressions. Run with Python's standard library only."""
import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("report", Path(__file__).resolve().parents[1] / "tools/perf/frametrace_report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class PictureCadence(unittest.TestCase):
    def test_presentation_sleep_survives_original_picture_merge(self):
        rows = self.rows()
        rows[5].update(present_pace_requested_ms=2.,present_pace_actual_ms=3.)
        rows[6].update(present_pace_requested_ms=4.,present_pace_actual_ms=40.)
        merged = report.picture_rows(rows,'original')[1]
        self.assertEqual(merged['present_pace_requested_ms'],6.)
        self.assertEqual(merged['present_pace_actual_ms'],43.)

    def test_only_excess_presentation_sleep_is_a_stall(self):
        fields = ('frame_ms','acquire_ms','queue_present_ms','replay_ms','fence_ms','present_cpu_ms',
                  'guest_cpu_ms','guest_replay_wait_ms','guest_gap_ms','minflt','pipeline_ms','astc_ms',
                  'file_ms','guest_interval','pipelines','skipped','majflt','upload_mb')
        normal = dict.fromkeys(fields,0.)
        normal.update(frame_ms=16.,guest_gap_ms=16.,guest_interval=1)
        hitch = dict(normal,frame_ms=80.,present_pace_requested_ms=5.,present_pace_actual_ms=65.)
        found,_ = report.classify([dict(normal) for _ in range(20)]+[hitch])
        self.assertEqual(found[0]['cause'],'present_pace_oversleep')
        hitch.update(present_pace_requested_ms=65.)
        found,_ = report.classify([dict(normal) for _ in range(20)]+[hitch])
        self.assertEqual(found[0]['cause'],'unknown')

    def test_call_totals_sum_but_call_maxima_do_not(self):
        rows = self.rows()
        for index,elapsed in ((5,2.),(6,3.)):
            rows[index].update(log_ms=elapsed,log_max_ms=elapsed,input_poll_ms=elapsed,input_poll_max_ms=elapsed)
        merged=report.picture_rows(rows,'original')
        self.assertEqual(merged[1]['log_ms'],5.)
        self.assertEqual(merged[1]['log_max_ms'],3.)
        self.assertEqual(merged[1]['input_poll_ms'],5.)
        self.assertEqual(merged[1]['input_poll_max_ms'],3.)
        self.assertNotIn('log_ms',report.picture_rows(self.rows(),'original')[0])

    def test_logging_phases_preserve_sum_peak_and_unknown_legacy(self):
        rows = self.rows()
        for index, elapsed in ((5, 2.), (6, 3.)):
            rows[index].update(log_ms=elapsed * 4, log_max_ms=elapsed * 4)
            for prefix in report.LOG_PHASES.values():
                rows[index][prefix + '_ms'] = elapsed
                rows[index][prefix + '_max_ms'] = elapsed
        merged = report.picture_rows(rows, 'original')[1]
        for prefix in report.LOG_PHASES.values():
            self.assertEqual(merged[prefix + '_ms'], 5.)
            self.assertEqual(merged[prefix + '_max_ms'], 3.)
        phase = report.logging_phase_summary([merged])
        self.assertEqual(phase['phases']['mutex_wait']['total_ms'], 5.)
        self.assertEqual(phase['phases']['platform_sink']['max_ms'], 3.)
        self.assertIn('different presentation intervals', phase['scope'])
        self.assertIn('exclusive frame cost', phase['scope'])
        self.assertIsNone(report.logging_phase_summary(self.rows()))
        partial = report.logging_phase_summary([dict(log_mutex_ms=4.)])
        self.assertIsNone(partial['phases']['platform_sink']['total_ms'])
        self.assertIsNone(partial['phases']['mutex_wait']['max_ms'])

    def test_upload_volume_does_not_invent_a_measured_stall(self):
        fields = ('frame_ms','acquire_ms','queue_present_ms','replay_ms','fence_ms','present_cpu_ms',
                  'guest_cpu_ms','guest_replay_wait_ms','guest_gap_ms','minflt','pipeline_ms','astc_ms',
                  'file_ms','guest_interval','pipelines','skipped','majflt','upload_mb')
        normal = dict.fromkeys(fields, 0.)
        normal.update(frame_ms=16., guest_gap_ms=16., guest_interval=1, replay_ms=5., fence_ms=8., upload_mb=11.)
        hitch = dict(normal, frame_ms=900.)
        found, _ = report.classify([dict(normal) for _ in range(20)] + [hitch])
        self.assertEqual(found[0]['cause'], 'unknown')
        hitch['replay_ms'] = 800.
        found, _ = report.classify([dict(normal) for _ in range(20)] + [hitch])
        self.assertEqual(found[0]['cause'], 'replay_cpu')

    def rows(self):
        return [dict(frame=i, t_ms=i * 5., frame_ms=5., original_picture=int(i % 4 == 0),
                     pipelines=int(i == 6), pipeline_ms=3. if i == 6 else 0., file_ms=1.,
                     log_line=i * 2, sample_to_present_ms=10. if i % 4 == 0 else 999.) for i in range(81)]

    def test_original_cadence_retains_generated_work(self):
        rows = report.picture_rows(self.rows(), "original")
        self.assertEqual(len(rows), 20)
        self.assertTrue(all(r["frame_ms"] == 20. for r in rows))
        self.assertEqual(report.stats(rows, 20)["avg_fps"], 50.)
        self.assertEqual(sum(r["pipelines"] for r in rows), 1)
        self.assertEqual(rows[1]["pipeline_ms"], 3.)
        self.assertEqual(rows[1]["file_ms"], 4.)
        self.assertEqual(rows[1]["previous_log_line"], 8)

    def test_presented_cadence_remains_separate(self):
        rows = report.picture_rows(self.rows(), "presented")
        self.assertEqual(len(rows), 80)
        self.assertEqual(report.stats(rows, 5)["avg_fps"], 200.)

    def test_legacy_traces_keep_interpretation(self):
        rows = self.rows()
        for row in rows: row.pop("original_picture")
        self.assertEqual(report.picture_rows(rows, "original"), rows[1:])

    def test_no_interpolation_preserves_original_rows(self):
        rows = self.rows()
        for row in rows: row["original_picture"] = 1
        self.assertEqual(report.picture_rows(rows, "original"), rows[1:])

    def test_partial_start_and_tail_are_not_complete_intervals(self):
        rows = report.picture_rows(self.rows()[2:-2], "original")
        self.assertEqual(rows[0]["frame"], 8)
        self.assertEqual(rows[-1]["frame"], 76)
        self.assertTrue(all(r["frame_ms"] == 20. for r in rows))

    def test_latency_excludes_generated_and_unavailable_samples(self):
        rows = self.rows()
        rows[0]["sample_to_present_ms"] = -1
        latency = report.latency_stats(rows)
        self.assertEqual(latency["samples"], 20)
        self.assertEqual(latency["p99_ms"], 10.)
        self.assertIsNone(report.latency_stats([{}]))

    def test_load_reconstructs_before_cropping_and_ignores_nonfinite(self):
        rows = self.rows()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "trace.csv"
            with path.open("w", newline="", encoding="utf-8") as out:
                writer = csv.DictWriter(out, fieldnames=rows[0])
                writer.writeheader()
                writer.writerows(rows)
                writer.writerow(dict(rows[-1], t_ms=float("nan")))
                out.write("cut,short,row\n")
            cropped = report.load(path, .025, .060)
            self.assertEqual([r["t_ms"] for r in cropped], [40., 60.])
            self.assertEqual(cropped[0]["file_ms"], 4.)


if __name__ == "__main__":
    unittest.main()
