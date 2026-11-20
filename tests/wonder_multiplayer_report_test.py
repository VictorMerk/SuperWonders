import csv
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("multiplayer_report", Path(__file__).parents[1] / "tools/perf/summarize_wonder_multiplayer.py")
reporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reporter)


class ReportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, value):
        path = self.root / name
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def session(self, peer="left", mode="native_lan", **extra):
        value = dict(schema=1, mode=mode, runtime_sha256="ab" * 32, session_id="same-session", peer_id=peer,
                     machine_id="machine-" + peer, started="2020-01-01T00:00:00+00:00")
        value.update(extra)
        return self.write(peer + "-session.json", value)

    def native(self, name="native.json", **extra):
        value = dict(schema=1, backend="Product120-private-udp-v1", operations={
            "send_to": dict(calls=2, failures=1, duration_ns=1000000)},
            nifm_last_cached_snapshot=dict(initialized=1, local_mode=1, requested=1, available=1, selection_status=0, candidates=1),
            available_true_calls=1, available_false_calls=0, local_true_calls=1, local_false_calls=0,
            send_return_bytes=40, receive_return_bytes_including_peeks=80)
        value.update(extra)
        return self.write(name, value)

    def steam(self, **extra):
        value = dict(schema=1, source="steam_dev_bootstrap", app_id=480, checkpoint="shutdown", status="busy",
                     init_attempted=True, initialized_ever=True, callback_calls=1, callback_completed=0,
                     callback_failures=0, shutdown_calls=0, shutdown_completed=0)
        value.update(extra)
        return self.write("steam.json", value)

    def codes(self, result):
        return {note["code"] for note in result["diagnostics"]}

    def application(self, name, role, button=0, omitted=0, seed=7, event="apply", complete_loading=False):
        path = self.root / name
        row = dict(tick=1, frame=5, event=event, players=2)
        row.update({f"p{p}_{field}": button if p == 0 and field == "buttons" else 0
                    for p in range(4) for field in ("loading_event", "buttons", "lx", "ly", "rx", "ry")})
        with path.open("w", newline="") as out:
            out.write(f"# schema=1,role={role},seed={seed},omitted={omitted}\n")
            writer = csv.DictWriter(out, fieldnames=row)
            writer.writeheader()
            writer.writerow(row)
            if complete_loading:
                writer.writerow(dict(row, tick=2, frame=6, event="loading_neutral_apply"))
        return path

    def trace(self, name):
        path = self.root / name
        row = dict(tick=1, frame=5, seed=7, committed=1, players=2, wait_ns=1234, state_valid=0, local_loading_event=0)
        row.update({f"p{p}_{field}": 0 for p in range(4) for field in ("loading_event", "buttons", "lx", "ly", "rx", "ry")})
        with path.open("w", newline="") as out:
            writer = csv.DictWriter(out, fieldnames=row)
            writer.writeheader()
            writer.writerow(row)
        return path

    def test_native_pair_stays_local_evidence_and_redacts_metadata(self):
        left = dict(session=self.session(local_address="do-not-copy-ip", pairing_key="do-not-copy-key"), native_stats=self.native())
        right = dict(session=self.session("right"), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertTrue(result["correlation"]["eligible"])
        self.assertEqual(result["correlation"]["machine_identity"], "distinct_declared_machines")
        self.assertTrue(result["comparisons"]["native"]["both_cached_available"])
        self.assertEqual(result["peers"]["left"]["native"]["operations"]["send_to"]["mean_operation_duration_ms"], .5)
        text = json.dumps(result)
        self.assertNotIn("do-not-copy", text)
        self.assertNotIn(str(self.root), text)
        self.assertNotIn('"rtt_ms"', text)
        self.assertNotIn('"game_pass"', text)
        self.assertIn("not independently authenticated", result["correlation"]["scope"])

    def test_pair_identity_hash_and_mode_are_required(self):
        left = dict(session=self.session(), native_stats=self.native())
        for fields, reason in ((dict(runtime_sha256="cd" * 32), "runtime_hash_missing_or_mismatched"),
                               (dict(session_id="another"), "session_identity_mismatch"),
                               (dict(peer_id="left"), "peer_identity_not_distinct"),
                               (dict(session_id=None), "session_id_missing_or_invalid"),
                               (dict(mode="legacy_lan"), "mode_missing_or_mismatched")):
            with self.subTest(fields=fields):
                result = reporter.summarize(left, dict(session=self.session("right", **fields)))
                self.assertFalse(result["correlation"]["eligible"])
                self.assertIn(reason, result["correlation"]["reasons"])
                self.assertFalse(result["comparisons"])

    def test_missing_schema_and_bad_counter_are_diagnostics(self):
        result = reporter.summarize(dict(session=self.session()))
        self.assertIn("evidence_missing", self.codes(result))
        self.assertIsNone(result["peers"]["left"]["native"])
        for extra in (dict(schema=2), dict(operations={"poll": dict(calls=True, failures=0, duration_ns=0)})):
            result = reporter.summarize(dict(session=self.session(), native_stats=self.native(**extra)))
            self.assertIn("invalid_evidence_schema_or_data", self.codes(result))
            self.assertIsNone(result["peers"]["left"]["native"])

    def test_nontransactional_counter_anomaly_is_not_a_desync_verdict(self):
        path = self.native(operations={"poll": dict(calls=1, failures=2, duration_ns=10)})
        result = reporter.summarize(dict(session=self.session(), native_stats=path))
        self.assertIn("counter_consistency_warning", self.codes(result))
        self.assertEqual(result["peers"]["left"]["native"]["counter_anomalies"], ["poll"])

    def test_stale_and_digest_mismatch_do_not_correlate(self):
        path = self.native()
        os.utime(path, (1000000000, 1000000000))
        result = reporter.summarize(dict(session=self.session(), native_stats=path), dict(session=self.session("right")))
        self.assertIn("evidence_predates_session", self.codes(result))
        self.assertFalse(result["correlation"]["eligible"])
        result = reporter.summarize(dict(session=self.session(artifacts={"native_stats": "f" * 64}), native_stats=self.native()))
        self.assertIn("artifact_digest_mismatch", self.codes(result))
        self.assertIsNone(result["peers"]["left"]["native"])

    def test_steam_shutdown_checkpoint_is_not_completed_teardown(self):
        inputs = dict(session=self.session(mode="steam_dev"), steam_stats=self.steam())
        result = reporter.summarize(inputs)
        self.assertFalse(result["peers"]["left"]["steam"]["api_shutdown_completed_observed"])
        self.assertIn("steam_shutdown_not_completed", self.codes(result))
        inputs["steam_stats"] = self.steam(status="stopped", callback_completed=1, shutdown_calls=1, shutdown_completed=1)
        self.assertTrue(reporter.summarize(inputs)["peers"]["left"]["steam"]["api_shutdown_completed_observed"])
        inputs["steam_stats"] = self.steam(status="init-failed", initialized_ever=False, callback_calls=0)
        self.assertIn("steam_bootstrap_not_initialized", self.codes(reporter.summarize(inputs)))

    def test_incompatible_artifacts_are_not_merged(self):
        result = reporter.summarize(dict(session=self.session(mode="steam_dev"), native_stats=self.native(), steam_stats=self.steam()))
        self.assertIn("artifact_mode_mismatch", self.codes(result))
        self.assertIsNone(result["peers"]["left"]["native"])

    def test_legacy_reuses_existing_comparers_with_retention_gaps(self):
        left = dict(session=self.session(mode="legacy_lan", seed=7, role=0),
                    application_trace=self.application("left.csv", 0, button=1, omitted=3), legacy_trace=self.trace("left-input.csv"))
        right = dict(session=self.session("right", mode="legacy_lan", seed=7, role=1),
                     application_trace=self.application("right.csv", 1, button=2), legacy_trace=self.trace("right-input.csv"))
        result = reporter.summarize(left, right)
        self.assertEqual(result["comparisons"]["legacy_application"]["application_mismatches"], 1)
        self.assertIn("application_retention_gaps", self.codes(result))
        self.assertEqual(result["comparisons"]["legacy_input"]["committed_pairs"], 1)
        self.assertEqual(result["comparisons"]["legacy_input"]["valid_position_pairs"], 0)
        self.assertIn("not verified", result["comparisons"]["legacy_input"]["limitation"])

    def test_commit_fate_mismatch_has_an_explicit_warning(self):
        for event in ("loading_drain_discard", "loading_exchange"):
            left = dict(session=self.session(mode="legacy_lan", seed=7, role=0),
                        application_trace=self.application("left.csv", 0, event=event, complete_loading=True))
            right = dict(session=self.session("right", mode="legacy_lan", seed=7, role=1),
                         application_trace=self.application("right.csv", 1))
            result = reporter.summarize(left, right)
            self.assertIn("legacy_commit_fate_mismatch", self.codes(result))
            self.assertEqual(result["comparisons"]["legacy_application"]["commit_fate_mismatches"], 1)

    def test_legacy_source_metadata_conflict_blocks_pairing(self):
        left = dict(session=self.session(mode="legacy_lan", seed=7, role=0), application_trace=self.application("left.csv", 1))
        result = reporter.summarize(left, dict(session=self.session("right", mode="legacy_lan")))
        self.assertIn("legacy_session_trace_identity_mismatch", self.codes(result))
        self.assertIn("legacy_evidence_identity_conflict", result["correlation"]["reasons"])

    def test_malformed_and_duplicate_json_are_not_echoed(self):
        path = self.root / "bad.json"
        path.write_text('{"schema":1,"schema":2,"secret":"never-echo"}')
        result = reporter.summarize(dict(session=self.session(), native_stats=path))
        self.assertIn("invalid_evidence_schema_or_data", self.codes(result))
        self.assertNotIn("never-echo", json.dumps(result))

    def test_legacy_input_and_application_seeds_must_agree_locally(self):
        left = dict(session=self.session(mode="legacy_lan", role=0),
                    legacy_trace=self.trace("input.csv"), application_trace=self.application("applied.csv", 0, seed=8))
        result = reporter.summarize(left, dict(session=self.session("right", mode="legacy_lan")))
        self.assertIn("legacy_evidence_identity_conflict", result["correlation"]["reasons"])
        self.assertFalse(result["comparisons"])

    def test_shared_runtime_cannot_hide_declared_source_edition_mismatch(self):
        original = "cd6e42aee7934f4d8393cd43893ae25edc83d338000000000000000000000000"
        updated = "ff773e90972d544eb79406eaa65396d53c43efb9000000000000000000000000"
        left = dict(session=self.session(mode="legacy_lan", source_build_id=original),
                    application_trace=self.application("left.csv", 0))
        right = dict(session=self.session("right", mode="legacy_lan", source_build_id=updated),
                     application_trace=self.application("right.csv", 1))
        result = reporter.summarize(left, right)
        self.assertFalse(result["correlation"]["eligible"])
        self.assertIn("source_build_id_mismatch", result["correlation"]["reasons"])
        identity = result["correlation"]["game_code_identity"]
        self.assertEqual(identity["fields"]["source_build_id"], "declared_mismatch")
        self.assertEqual(identity["fields"]["game_library_sha256"], "unknown")
        self.assertFalse(result["comparisons"])
        right["session"] = self.session("right", mode="legacy_lan", source_build_id=original)
        same_source = reporter.summarize(left, right)
        self.assertTrue(same_source["correlation"]["eligible"])
        self.assertEqual(same_source["comparisons"]["legacy_application"]["matched_applied_ticks"], 1)

    def test_same_source_with_different_generated_libraries_blocks_pairing(self):
        left = dict(session=self.session(source_build_id="12" * 32, game_library_sha256="34" * 32), native_stats=self.native())
        right = dict(session=self.session("right", source_build_id="12" * 32, game_library_sha256="56" * 32), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertFalse(result["correlation"]["eligible"])
        self.assertIn("game_library_sha256_mismatch", result["correlation"]["reasons"])
        self.assertEqual(result["correlation"]["game_code_identity"]["fields"]["source_build_id"], "declared_match")
        self.assertFalse(result["comparisons"])

    def test_matching_case_insensitive_declarations_are_not_measured_proof(self):
        left = dict(session=self.session(source_build_id="ab" * 32, game_library_sha256="cd" * 32), native_stats=self.native())
        right = dict(session=self.session("right", source_build_id="AB" * 32, game_library_sha256="CD" * 32), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertTrue(result["correlation"]["eligible"])
        identity = result["correlation"]["game_code_identity"]
        self.assertEqual(identity["status"], "declared_match")
        self.assertIn("not measured loaded modules", identity["scope"])
        self.assertEqual(result["peers"]["right"]["metadata"]["game_code_declarations"]["source_build_id"]["value"], "ab" * 32)

    def test_missing_old_game_code_declarations_remain_unknown(self):
        left = dict(session=self.session(), native_stats=self.native())
        right = dict(session=self.session("right"), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertTrue(result["correlation"]["eligible"])
        self.assertEqual(result["correlation"]["game_code_identity"]["status"], "unknown")
        self.assertIn("game_code_identity_incomplete", self.codes(result))
        # A declaration on just one PC does not fill in the other PC's identity.
        right["session"] = self.session("right", source_build_id="ab" * 32, game_library_sha256="cd" * 32)
        result = reporter.summarize(left, right)
        self.assertEqual(result["correlation"]["game_code_identity"]["status"], "unknown")
        left["session"] = self.session(source_build_id="AB" * 32)
        result = reporter.summarize(left, right)
        self.assertEqual(result["correlation"]["game_code_identity"]["status"], "partial_declared_match")

    def test_invalid_game_code_declarations_fail_closed_without_echoing(self):
        for field in reporter.GAME_CODE_FIELDS:
            for value in (True, ["private-secret"], "path/private-secret", "a" * 63):
                with self.subTest(field=field, value=value):
                    left = dict(session=self.session(**{field: value}), native_stats=self.native())
                    right = dict(session=self.session("right"), native_stats=self.native("right-native.json"))
                    result = reporter.summarize(left, right)
                    self.assertFalse(result["correlation"]["eligible"])
                    self.assertIn(field + "_invalid", result["correlation"]["reasons"])
                    self.assertTrue(any(note["code"] == field + "_invalid" and note["level"] == "error" for note in result["diagnostics"]))
                    self.assertNotIn("private-secret", json.dumps(result))
                    self.assertFalse(result["comparisons"])

    def test_planned_library_match_does_not_fill_loaded_or_source_identity(self):
        left = dict(session=self.session(planned_game_library_sha256="ab" * 32), native_stats=self.native())
        right = dict(session=self.session("right", planned_game_library_sha256="AB" * 32), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertTrue(result["correlation"]["eligible"])
        self.assertEqual(result["correlation"]["planned_game_library_identity"]["status"], "planned_match")
        self.assertEqual(result["correlation"]["game_code_identity"]["status"], "unknown")
        self.assertIn("not proof of the loaded library", result["correlation"]["planned_game_library_identity"]["scope"])
        right["session"] = self.session("right", game_library_sha256="ab" * 32)
        result = reporter.summarize(left, right)
        self.assertEqual(result["correlation"]["planned_game_library_identity"]["status"], "unknown")
        self.assertEqual(result["correlation"]["game_code_identity"]["status"], "unknown")

    def test_planned_library_conflict_blocks_pair_even_with_matching_declarations(self):
        common = dict(source_build_id="12" * 32, game_library_sha256="34" * 32)
        left = dict(session=self.session(planned_game_library_sha256="ab" * 32, **common), native_stats=self.native())
        right = dict(session=self.session("right", planned_game_library_sha256="cd" * 32, **common), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertFalse(result["correlation"]["eligible"])
        self.assertEqual(result["correlation"]["game_code_identity"]["status"], "declared_match")
        self.assertEqual(result["correlation"]["planned_game_library_identity"]["status"], "planned_mismatch")
        self.assertIn("planned_game_library_sha256_mismatch", result["correlation"]["reasons"])
        self.assertFalse(result["comparisons"])

    def test_invalid_planned_library_digest_is_not_echoed_or_accepted(self):
        left = dict(session=self.session(planned_game_library_sha256=["private-library-path"]), native_stats=self.native())
        right = dict(session=self.session("right"), native_stats=self.native("right-native.json"))
        result = reporter.summarize(left, right)
        self.assertFalse(result["correlation"]["eligible"])
        self.assertIn("planned_game_library_sha256_invalid", self.codes(result))
        self.assertNotIn("private-library-path", json.dumps(result))


if __name__ == "__main__":
    unittest.main()
