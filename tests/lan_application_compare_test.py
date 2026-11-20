import csv
import importlib.util
from pathlib import Path
import tempfile
import tracemalloc
import unittest

spec = importlib.util.spec_from_file_location("application_compare", Path(__file__).parents[1] / "tools/perf/compare_lan_application.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(tick, event="apply", **changes):
    result = dict(tick=str(tick), frame=str(tick + 100), event=event, players="2", frame_before="0", framework_sequence="0", player="0", sampling_number="0", sequence="0")
    result.update({f"p{p}_{field}": "0" for p in range(4)
                   for field in ("loading_event", "buttons", "lx", "ly", "rx", "ry")})
    result.update({key: str(value) for key, value in changes.items()})
    return result


class ComparisonTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)

    def write(self, name, role, rows, seed=123, omitted=0, identity=None):
        path = Path(self.temp.name) / name
        with path.open("w", newline="", encoding="utf-8") as out:
            marker = '' if identity is None else ',hid_identity=' + identity
            out.write(f"# schema=1,role={role},seed={seed},capacity=8,total={len(rows)+omitted},retained={len(rows)},omitted={omitted}{marker}\n")
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

    def test_framework_calls_are_not_assumed_to_present_or_advance_simulation(self):
        left = self.write("left", 0, [row(1), row(1, "framework_frame", frame=100, frame_before=100, framework_sequence=50)])
        right = self.write("right", 1, [row(1), row(1, "framework_frame", frame=100, frame_before=99, framework_sequence=40)])
        result = module.compare(left, right)
        self.assertEqual(result["matched_applied_ticks"], 1)
        self.assertEqual(result["application_mismatches"], 0)
        self.assertEqual(result["left"]["retained_framework_calls_without_present"], 1)
        self.assertEqual(result["left"]["first_retained_framework_call_without_present"], 50)
        self.assertEqual(result["right"]["retained_framework_calls_without_present"], 0)

    def test_repeated_identical_hid_reads_are_not_flagged(self):
        reads = [row(1, "hid_delivery", player=0, sampling_number=99, sequence=1, p0_buttons=8),
                 row(2, "hid_delivery", player=0, sampling_number=99, sequence=2, p0_buttons=8),
                 row(2, "hid_delivery", player=1, sampling_number=99, sequence=3, p1_buttons=16)]
        left = self.write("left", 0, reads)
        right = self.write("right", 1, [row(1)])
        result = module.compare(left, right)["left"]
        self.assertEqual(result["retained_unique_hid_samples"], 2)
        self.assertEqual(result["repeated_identical_hid_sample_reads"], 1)
        self.assertEqual(result["hid_sample_payload_change_observations"], 0)
        self.assertIsNone(result["first_hid_sample_payload_change_observation"])

    def test_same_hid_sample_changed_payload_is_an_observation_even_across_gaps(self):
        reads = [row(1, "hid_delivery", player=1, sampling_number=99, sequence=1, p1_buttons=8),
                 row(2, "hid_delivery", player=1, sampling_number=100, sequence=2),
                 row(9, "hid_delivery", player=1, sampling_number=99, sequence=500, p1_buttons=16, p1_lx=-32767),
                 row(10, "hid_delivery", player=1, sampling_number=99, sequence=501, p1_buttons=16, p1_lx=-32767)]
        left = self.write("left", 0, reads, omitted=497)
        right = self.write("right", 1, [row(1)])
        result = module.compare(left, right)
        summary = result["left"]
        self.assertEqual(summary["hid_sample_payload_change_observations"], 1)
        self.assertEqual(summary["hid_samples_with_changed_payload"], 1)
        self.assertEqual(summary["repeated_identical_hid_sample_reads"], 1)
        observed = summary["first_hid_sample_payload_change_observation"]
        self.assertEqual(observed["previous_sequence"], 1)
        self.assertEqual(observed["sequence"], 500)
        self.assertEqual(observed["previous_observed_tick"], 1)
        self.assertEqual(observed["observed_tick"], 9)
        self.assertEqual(observed["current_pad"]["lx"], -32767)
        self.assertEqual(result["application_mismatches"], 0)
        self.assertIn("not proof of desync", summary["hid_observation_scope"])
        self.assertIn("not atomically paired", summary["hid_observation_scope"])
        self.assertEqual(summary["omitted_events"], 497)

    def test_old_schema_one_without_framework_or_hid_columns(self):
        old = row(1)
        for key in ("player", "sampling_number", "sequence", "frame_before", "framework_sequence"):
            del old[key]
        left = self.write("left", 0, [old])
        right = self.write("right", 1, [old])
        result = module.compare(left, right)
        self.assertEqual(result["matched_applied_ticks"], 1)
        self.assertEqual(result["left"]["retained_hid_reads"], 0)

    def test_declared_atomic_commit_identity_ignores_presentation_offsets(self):
        reads = [row(0, "hid_delivery", sampling_number=1, sequence=1, frame=100),
                 row(0, "hid_delivery", sampling_number=1, sequence=2, frame=900),
                 row(3, "hid_delivery", sampling_number=4, sequence=3, frame=900, p0_buttons=8)]
        left = self.write("left", 0, reads, identity="exposed_commit_plus_one")
        right = self.write("right", 1, reads, identity="exposed_commit_plus_one")
        result = module.compare(left, right)
        self.assertTrue(result["hid_identity_contracts_match"])
        stats = result["left"]
        self.assertEqual(stats["hid_commit_identity_mismatches"], 0)
        self.assertEqual(stats["repeated_identical_hid_sample_reads"], 1)
        self.assertEqual(stats["hid_identity_contract"], "exposed_commit_plus_one")
        self.assertIn("captured together", stats["hid_observation_scope"])
        self.assertIn("do not publish samples", stats["hid_observation_scope"])
        self.assertNotIn("not atomically paired", stats["hid_observation_scope"])

    def test_declared_atomic_identity_violation_and_mixed_legacy_remain_explicit(self):
        left = self.write("left", 0, [row(0, "hid_delivery", sampling_number=0, sequence=1),
                                     row((1 << 63) - 1, "hid_delivery", sampling_number=1 << 63, sequence=2)],
                          identity="exposed_commit_plus_one")
        right = self.write("right", 1, [row(1)])
        result = module.compare(left, right)
        self.assertFalse(result["hid_identity_contracts_match"])
        self.assertEqual(result["left"]["hid_commit_identity_mismatches"], 2)
        self.assertEqual(result["left"]["first_hid_commit_identity_mismatch"]["sequence"], 1)
        self.assertIsNone(result["right"]["hid_commit_identity_mismatches"])
        self.assertEqual(result["right"]["hid_identity_contract"], "legacy_or_unknown")
        future = self.write("future", 0, [row(1)], identity="future-unrecognized")
        self.assertIsNone(module.compare(future, right)["hid_identity_contracts_match"])

    def test_nonindexed_events_are_streamed_without_retaining_rows(self):
        path = Path(self.temp.name) / "large"
        event = row(1, "exchange_complete")
        with path.open("w", newline="", encoding="utf-8") as out:
            out.write("# schema=1,role=0,seed=123,omitted=0\n")
            writer = csv.DictWriter(out, fieldnames=list(event))
            writer.writeheader()
            for _ in range(10000):
                writer.writerow(event)
        tracemalloc.start()
        try:
            _, indexed, stats = module.read(path)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertEqual(indexed, {})
        self.assertEqual(stats["retained_events"], 10000)
        self.assertLess(peak, 2_000_000, "nonindexed CSV rows must not accumulate in memory")

    def test_invalid_schema_and_hid_player_rejected(self):
        left = self.write("left", 0, [row(1)])
        right = self.write("right", 1, [row(1, "hid_delivery", player=4)])
        with self.assertRaises(ValueError):
            module.compare(left, right)
        left.write_text(left.read_text().replace("schema=1", "schema=2"))
        with self.assertRaises(ValueError):
            module.read(left)


if __name__ == "__main__":
    unittest.main()
