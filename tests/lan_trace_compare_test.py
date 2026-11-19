import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("compare_lan_traces", Path(__file__).resolve().parents[1] / "tools/perf/compare_lan_traces.py")
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def row(**changes):
    result = dict.fromkeys(trace.PAD_FIELDS, "0")
    result.update(seed="42", committed="1", players="2", state_valid="0", scene="1", x="0", y="0", z="0", wait_ns="38000000")
    result.update(changes)
    return result


class Comparison(unittest.TestCase):
    def test_peer_work_retains_unknowns_and_separates_waits(self):
        result = trace.compare({7: row(local_work_ns="0"), 8: row(local_work_ns="20000000")},
                               {7: row(local_work_ns="30000000"), 8: row(local_work_ns="40000000")})
        self.assertEqual(result["local_frame_work_ms"][0], {"samples": 1, "mean": 20, "p95": 20, "peak": 20})
        self.assertEqual(result["local_frame_work_ms"][1]["mean"], 35)
        self.assertEqual(result["exchange_wait_ms"][0]["mean"], 38)
        legacy = trace.compare({7: row()}, {7: row()})
        self.assertIsNone(legacy["local_frame_work_ms"][0]["mean"])

    def test_missing_probe_never_claims_position_coverage(self):
        result = trace.compare({7: row()}, {7: row()})
        self.assertEqual(result["valid_position_pairs"], 0)
        self.assertEqual(result["committed_pairs"], 1)

    def test_first_different_remote_input(self):
        result = trace.compare({7: row(), 8: row()}, {7: row(p1_buttons="8"), 8: row(p1_buttons="16")})
        self.assertEqual(result["first_difference"]["tick"], 7)
        self.assertEqual(result["first_difference"]["fields"]["p1_buttons"], ["0", "8"])

    def test_float_tolerance_and_alignment(self):
        result = trace.compare({1: row(), 7: row(state_valid="1")}, {7: row(state_valid="1", x="0.0001"), 9: row()})
        self.assertIsNone(result["first_difference"])
        self.assertEqual(result["valid_position_pairs"], 1)
        result = trace.compare({7: row(state_valid="1")}, {7: row(state_valid="1", x="0.01")})
        self.assertIn("x", result["first_difference"]["fields"])

    def test_no_shared_ticks(self):
        with self.assertRaises(ValueError):
            trace.compare({1: row()}, {2: row()})

    def test_matching_inputs_warn_when_one_pc_stops_presenting(self):
        host = {i: row(frame="100") for i in range(120)}
        client = {i: row(frame=str(101 + i)) for i in range(120)}
        result = trace.compare(host, client)
        self.assertIsNone(result["first_difference"])
        self.assertEqual(result["presentation_progress"][0]["longest_same_frame_exchanges"], 120)
        self.assertEqual(result["presentation_progress"][1]["frames_advanced"], 119)
        self.assertTrue(result["warnings"])

    def test_async_one_frame_offset_is_not_a_progress_warning(self):
        host = {i: row(frame=str(100 + i)) for i in range(120)}
        client = {i: row(frame=str(101 + i)) for i in range(120)}
        result = trace.compare(host, client)
        self.assertEqual(result["warnings"], [])

    def test_nonfinite_position_is_a_difference(self):
        result = trace.compare({7: row(state_valid="1", x="nan")}, {7: row(state_valid="1")})
        self.assertIn("x", result["first_difference"]["fields"])

    def test_different_input_lead_is_not_silently_aligned(self):
        result = trace.compare({7: row(input_lead="0")}, {7: row(input_lead="1")})
        self.assertIn("input_lead", result["first_difference"]["fields"])

    def test_different_simulation_rate_is_a_difference(self):
        result = trace.compare({7: row(simulation_rate="30")}, {7: row(simulation_rate="60")})
        self.assertIn("simulation_rate", result["first_difference"]["fields"])


if __name__ == "__main__":
    unittest.main()
