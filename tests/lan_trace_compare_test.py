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

    def test_nonfinite_position_is_a_difference(self):
        result = trace.compare({7: row(state_valid="1", x="nan")}, {7: row(state_valid="1")})
        self.assertIn("x", result["first_difference"]["fields"])


if __name__ == "__main__":
    unittest.main()
