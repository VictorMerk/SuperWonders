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

    def application(self, name, role, button=0, omitted=0, seed=7):
        path = self.root / name
        row = dict(tick=1, frame=5, event="apply", players=2)
        row.update({f"p{p}_{field}": button if p == 0 and field == "buttons" else 0
                    for p in range(4) for field in ("loading_event", "buttons", "lx", "ly", "rx", "ry")})
        with path.open("w", newline="") as out:
            out.write(f"# schema=1,role={role},seed={seed},omitted={omitted}\n")
            writer = csv.DictWriter(out, fieldnames=row)
            writer.writeheader()
            writer.writerow(row)
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


if __name__ == "__main__":
    unittest.main()
