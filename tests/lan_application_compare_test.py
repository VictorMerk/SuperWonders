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

    def write(self, name, role, rows, seed=123, omitted=0, identity=None, observations=False):
        path = Path(self.temp.name) / name
        with path.open("w", newline="", encoding="utf-8") as out:
            marker = '' if identity is None else ',hid_identity=' + identity
            if observations:
                marker += ',observation_identity=local_invocation_v1'
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

    def test_pipeline_discard_is_not_hidden_by_equal_final_application(self):
        left = self.write("left", 0, [row(1, "loading_drain_discard", sequence=4),
                                      row(3, "loading_neutral_apply")])
        right = self.write("right", 1, [row(1, sequence=9), row(3, "loading_neutral_apply")])
        result = module.compare(left, right)
        self.assertEqual(result["application_mismatches"], 0)
        self.assertEqual(result["matched_commit_fate_ticks"], 2)
        self.assertEqual(result["commit_fate_mismatches"], 1)
        first = result["first_commit_fate_mismatch"]
        self.assertEqual(first["tick"], 1)
        self.assertEqual(first["left"], dict(fate="discarded", frame=101, sequence=4))
        self.assertEqual(first["right"]["fate"], "published")

    def test_serial_loading_transport_and_reverse_roles(self):
        loading = self.write("loading", 0, [row(1, "loading_exchange"), row(2, "loading_neutral_apply")])
        playing = self.write("playing", 1, [row(1)])
        result = module.compare(playing, loading)
        self.assertEqual(result["commit_fate_mismatches"], 1)
        first = result["first_commit_fate_mismatch"]
        self.assertEqual(first["left"]["fate"], "published")
        self.assertEqual(first["right"]["fate"], "loading_transport_only")

    def test_final_loading_neutral_supersedes_transport(self):
        left = self.write("left", 0, [row(1, "loading_exchange"), row(1, "loading_neutral_apply")])
        right = self.write("right", 1, [row(1, "loading_neutral_apply")])
        result = module.compare(left, right)
        self.assertEqual(result["commit_fate_mismatches"], 0)
        self.assertEqual(result["matched_commit_fate_ticks"], 1)
        self.assertIsNone(result["first_commit_fate_mismatch"])

    def test_export_ending_before_possible_final_neutral_is_unknown(self):
        left = self.write("left", 0, [row(1, "loading_exchange")])
        right = self.write("right", 1, [row(1)])
        result = module.compare(left, right)
        self.assertEqual(result["commit_fate_mismatches"], 0)
        self.assertEqual(result["matched_commit_fate_ticks"], 0)
        self.assertEqual(result["incomplete_shared_fate_ticks"], 1)

    def test_missing_fate_is_not_assumed_discarded(self):
        left = self.write("left", 0, [row(1, "exchange_complete")])
        right = self.write("right", 1, [row(1)])
        result = module.compare(left, right)
        self.assertEqual(result["commit_fate_mismatches"], 0)
        self.assertEqual(result["matched_commit_fate_ticks"], 0)
        self.assertEqual(result["right_only_retained_fate_ticks"], 1)

    def test_conflicting_local_fates_are_ambiguous(self):
        for events in (("apply", "loading_drain_discard"), ("apply", "loading_neutral_apply")):
            left = self.write("left", 0, [row(1, event) for event in events])
            right = self.write("right", 1, [row(1)])
            result = module.compare(left, right)
            self.assertEqual(result["ambiguous_shared_fate_ticks"], 1)
            self.assertEqual(result["matched_commit_fate_ticks"], 0)
            self.assertEqual(result["commit_fate_mismatches"], 0)

    def test_invalid_fate_tick_rejected(self):
        for tick in (-1, 1 << 64):
            left = self.write("left", 0, [row(tick, "loading_exchange")])
            right = self.write("right", 1, [row(1)])
            with self.assertRaises(ValueError):
                module.compare(left, right)

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

    def test_framework_file_pairs_remain_local_observations(self):
        def observation(event, sequence, invocation=7, thread=1, **changes):
            fields = dict(sequence=sequence, invocation=invocation, parent_invocation=0,
                          thread_ordinal=thread, resource_hash=42 if event == "loading_file" else 0,
                          caller_module=1, caller_offset=320)
            fields.update(changes)
            return row(0, event, **fields)
        events = [observation("framework_enter", 1), observation("loading_file", 2),
                  observation("framework_exit", 3),
                  observation("loading_file", 4, invocation=0, thread=2),
                  observation("framework_enter", 5, invocation=8),
                  observation("framework_throw", 6, invocation=8)]
        path = self.write("observed", 0, events, observations=True)
        _, _, stats = module.read(path)
        self.assertEqual(stats["retained_matched_framework_invocations"], 2)
        self.assertEqual(stats["retained_framework_exceptional_leaves"], 1)
        self.assertEqual(stats["retained_loading_files_in_framework_pair"], 1)
        self.assertEqual(stats["retained_loading_files_without_framework_pair"], 1)
        # A mismatched parent cannot establish a valid containing invocation.
        inconsistent = [observation("framework_enter", 1, parent_invocation=3),
                        observation("loading_file", 2),
                        observation("framework_exit", 3, parent_invocation=4)]
        bad_path = self.write("bad-parent", 0, inconsistent, observations=True)
        _, _, bad_stats = module.read(bad_path)
        self.assertEqual(bad_stats["retained_matched_framework_invocations"], 0)
        self.assertEqual(bad_stats["retained_loading_files_in_framework_pair"], 0)
        self.assertEqual(bad_stats["retained_loading_files_without_framework_pair"], 1)
        # Prefix/tail eviction may remove one end; never infer a blocked frame.
        path = self.write("partial", 0, events[:2], omitted=10, observations=True)
        _, _, partial = module.read(path)
        self.assertEqual(partial["retained_unpaired_framework_invocations"], 1)
        self.assertEqual(partial["retained_loading_files_without_framework_pair"], 1)
        self.assertIn("do not prove a hang", partial["framework_observation_scope"])
        path = self.write("legacy", 0, [row(1)])
        _, _, old = module.read(path)
        self.assertIsNone(old["retained_framework_entries"])
        self.assertEqual(old["framework_observation_contract"], "legacy_or_unknown")

    def test_observation_fields_reject_raw_unknown_caller_and_invalid_identity(self):
        event = row(0, "framework_enter", sequence=1, invocation=1, parent_invocation=0,
                    thread_ordinal=1, resource_hash=0, caller_module=0, caller_offset=123)
        path = self.write("invalidcaller", 0, [event], observations=True)
        with self.assertRaises(ValueError):
            module.read(path)
        event["caller_offset"] = "0"
        event["thread_ordinal"] = "65"
        path = self.write("invalidthread", 0, [event], observations=True)
        with self.assertRaises(ValueError):
            module.read(path)

    def test_source_phase_nesting_correlates_file_without_inflating_framework(self):
        def event(name, sequence, invocation, parent=0):
            return row(0, name, sequence=sequence, invocation=invocation,
                       parent_invocation=parent, thread_ordinal=1,
                       resource_hash=0, caller_module=0, caller_offset=0)
        rows = [event("framework_enter", 1, 1), event("phase_pre_enter", 2, 2, 1),
                event("phase_core_enter", 3, 3, 2), event("loading_file", 4, 3),
                event("phase_core_throw", 5, 3, 2), event("phase_pre_exit", 6, 2, 1),
                event("framework_exit", 7, 1)]
        path = self.write("phases", 0, rows, observations=True)
        stats = module.read(path)[2]
        self.assertEqual(stats["retained_framework_entries"], 1)
        self.assertEqual(stats["retained_matched_framework_invocations"], 1)
        self.assertEqual(stats["retained_matched_source_phase_invocations"], 2)
        self.assertEqual(stats["retained_source_phase_exceptional_leaves"], 1)
        self.assertEqual(stats["retained_loading_files_in_source_phase_pair"], 1)
        self.assertEqual(stats["retained_loading_files_in_phase_with_framework_ancestor"], 1)
        self.assertEqual(stats["retained_loading_files_in_framework_pair"], 1)
        # Retained phase/file evidence survives eviction, but framework ancestry is unknown.
        path = self.write("phaseonly", 0, rows[1:-1], observations=True)
        stats = module.read(path)[2]
        self.assertEqual(stats["retained_loading_files_in_source_phase_pair"], 1)
        self.assertEqual(stats["retained_loading_files_in_framework_pair"], 0)
        self.assertEqual(stats["retained_framework_entries"], 0)
        # Incomplete exit does not certify containment.
        path = self.write("partialphase", 0, rows[1:4], observations=True)
        self.assertEqual(module.read(path)[2]["retained_loading_files_in_source_phase_pair"], 0)

    def test_source_phase_invalid_pair_or_cycle_rejected(self):
        def event(name, sequence, invocation, parent):
            return row(0, name, sequence=sequence, invocation=invocation,
                       parent_invocation=parent, thread_ordinal=1,
                       resource_hash=0, caller_module=0, caller_offset=0)
        for rows in ([event("phase_pre_enter", 1, 1, 0), event("phase_core_exit", 2, 1, 0)],
                     [event("phase_pre_enter", 1, 1, 2), event("phase_core_enter", 2, 2, 1)],
                     [event("phase_post_enter", 1, 1, 1)]):
            path = self.write("badphase", 0, rows, observations=True)
            with self.assertRaises(ValueError):
                module.read(path)

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
