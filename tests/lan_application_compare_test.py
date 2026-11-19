import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("application_compare", Path(__file__).parents[1] / "tools/perf/compare_lan_application.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(tick, event="apply", **changes):
    result = dict(tick=str(tick), frame=str(tick + 100), event=event, players="2")
    result.update({f"p{p}_{field}": "0" for p in range(4)
                   for field in ("loading_event", "buttons", "lx", "ly", "rx", "ry")})
    result.update({key: str(value) for key, value in changes.items()})
    return result


class ComparisonTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def write(self, name, role, rows, seed=123, omitted=0):
        path = Path(self.temp.name) / name
        with path.open("w", newline="", encoding="utf-8") as out:
            out.write(f"# schema=1,role={role},seed={seed},capacity=8,total={len(rows)+omitted},retained={len(rows)},omitted={omitted}\n")
            writer = csv.DictWriter(out, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        return path

    def test_different_application_is_not_hidden_by_matching_packets(self):
        left = self.write("left", 0, [row(1), row(2, p0_buttons=8), row(3, "loading_neutral_apply", p0_loading_event=42)])
        right = self.write("right", 1, [row(1, frame=999), row(2, p0_buttons=16), row(3, p0_loading_event=42)])
        result = module.compare(left, right)
        self.assertEqual(result["matched_applied_ticks"], 3)
        self.assertEqual(result["application_mismatches"], 2)
        self.assertEqual(result["first_application_mismatch"]["tick"], 2)
        self.assertIn("p0_buttons", result["first_application_mismatch"]["fields"])
        self.assertEqual(result["right"]["gameplay_applies_with_peer_loading_marker"], 1)

    def test_retention_gaps_and_discard_counts_remain_explicit(self):
        left = self.write("left", 0, [row(1), row(2, "loading_drain_discard"), row(9), row(9)], omitted=20)
        right = self.write("right", 1, [row(1), row(8), row(9)])
        result = module.compare(left, right)
        self.assertEqual(result["application_mismatches"], 0)
        self.assertEqual(result["left"]["duplicate_applied_ticks"], [9])
        self.assertEqual(result["left"]["retained_loading_drain_discards"], 1)
        self.assertEqual(result["left"]["omitted_events"], 20)
        self.assertEqual(result["right_only_retained_ticks"], 1)

    def test_different_session_and_same_role_rejected(self):
        left = self.write("left", 0, [row(1)])
        for role, seed in [(0, 123), (1, 456)]:
            right = self.write("right", role, [row(1)], seed=seed)
            with self.assertRaises(ValueError):
                module.compare(left, right)


if __name__ == "__main__":
    unittest.main()
