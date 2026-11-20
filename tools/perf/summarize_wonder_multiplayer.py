"""Combine explicit Wonder evidence paths; never certify gameplay or network RTT.

Example: python summarize_wonder_multiplayer.py --left-session host/session.json
 --left-native-stats host/native-lan-stats.json --right-session peer/session.json
 --right-native-stats peer/native-lan-stats.json --output combined.json

Session metadata: mode (native_lan/legacy_lan/steam_dev), runtime_sha256,
session_id (shared experiment identity), peer_id (distinct participant identity),
optional machine_id and started (timezone-qualified ISO timestamp). Older explicit
native_lan:true metadata is recognized, but missing identities block pairing.
Optional source_build_id (64 hex NSO build ID) and game_library_sha256 declare
the game code independently of the shared runtime executable. Declared conflicts
block pairing; missing old declarations remain explicitly unknown, not verified.
planned_game_library_sha256 is compared separately: it fingerprints a prelaunch
candidate, not the actual loaded module or the source build identity.
Optional artifacts maps native_stats/steam_stats/legacy_trace/application_trace
to SHA256 digests. Participant identifiers/paths/addresses/keys are not copied.
Legacy CSV parsing/comparison reuses the existing diagnostic tools. JSON input
is bounded to1MiB; CSV to256MiB (legacy packet ring retains its own8MiB limit).
Exit0 means a report without malformed-input errors, never a game/test pass.
Exit2 means malformed/unreadable evidence; warnings can still prevent pairing.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
import re
from datetime import datetime
from pathlib import Path

MODES = {"native": "native_lan", "native_lan": "native_lan",
         "legacy": "legacy_lan", "legacy_lan": "legacy_lan",
         "steam": "steam_dev", "steam_dev": "steam_dev"}
NATIVE_OPS = set("initialize initialize_config finalize is_initialized socket bind send_to recv_from poll close get_errno set_errno set_option get_option shutdown get_sock_name recv send read write fcntl unsupported_socket resolve_failure host_resolve_failure resolver_get_option resolver_set_option inet_pton inet_ntop nifm_initialize nifm_local_mode nifm_submit nifm_submit_wait nifm_cancel nifm_available nifm_on_hold nifm_request_error nifm_ip_config nifm_proxy_unavailable preserved".split())
STEAM_STATUS = set("disabled invalid-path unsupported-platform load-failed missing-exports already-active init-failed init-exception ready busy stopped deferred callback-failed shutdown-failed".split())
SCOPE = ("Local diagnostic evidence only. Matching declarations, inputs, counters or API lifecycle do not certify rooms, physical connectivity, world synchronization, RTT, display/input latency, FPS, Remote Play Together or P2P.")
_TOOLS = {}
GAME_CODE_FIELDS = ("source_build_id", "game_library_sha256")
PLANNED_LIBRARY_FIELD = "planned_game_library_sha256"


def _tool(name):
    if name not in _TOOLS:
        spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _TOOLS[name] = module
    return _TOOLS[name]


def _note(notes, code, peer=None, artifact=None, error=False):
    item = {"code": code, "level": "error" if error else "warning"}
    if peer:
        item["peer"] = peer
    if artifact:
        item["artifact"] = artifact
    if item not in notes:
        notes.append(item)


def _uint(value):
    return type(value) is int and 0 <= value <= (1 << 64) - 1


def _hash(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{64}", value) is not None


def _digest_declaration(value, field, label, notes):
    if value is None or value == "":
        return {"status": "missing", "value": None}
    if _hash(value):
        return {"status": "declared", "value": value.lower()}
    # Never echo arbitrary malformed metadata into a shareable report.
    _note(notes, field + "_invalid", label, error=True)
    return {"status": "invalid", "value": None}


def _game_code_declarations(session, label, notes):
    result = {field: _digest_declaration(session.get(field), field, label, notes)
              for field in GAME_CODE_FIELDS}
    if any(item["status"] == "missing" for item in result.values()):
        _note(notes, "game_code_identity_incomplete", label)
    return result


def _compare_game_code_declarations(left, right, reasons):
    fields = {}
    for field in GAME_CODE_FIELDS:
        a, b = left[field], right[field]
        if "invalid" in (a["status"], b["status"]):
            status = "invalid"
            reasons.append(field + "_invalid")
        elif a["status"] != "declared" or b["status"] != "declared":
            status = "unknown"
        elif a["value"] != b["value"]:
            status = "declared_mismatch"
            reasons.append(field + "_mismatch")
        else:
            status = "declared_match"
        fields[field] = status
    states = set(fields.values())
    status = ("invalid" if "invalid" in states else
              "declared_mismatch" if "declared_mismatch" in states else
              "declared_match" if states == {"declared_match"} else
              "partial_declared_match" if "declared_match" in states else "unknown")
    return {"status": status, "fields": fields,
            "scope": "Session declarations only, not measured loaded modules. Matching runtime executables do not establish matching source editions or generated game libraries. Missing declarations remain unknown; no gameplay or simulation synchronization is verified."}


def _compare_planned_libraries(left, right, reasons):
    if "invalid" in (left["status"], right["status"]):
        status = "invalid"
        reasons.append(PLANNED_LIBRARY_FIELD + "_invalid")
    elif left["status"] != "declared" or right["status"] != "declared":
        status = "unknown"
    elif left["value"] != right["value"]:
        status = "planned_mismatch"
        reasons.append(PLANNED_LIBRARY_FIELD + "_mismatch")
    else:
        status = "planned_match"
    return {"status": status,
            "scope": "Declared prelaunch file fingerprints only. A planned match is not proof of the loaded library, source edition, ABI or unchanged files at startup. Missing planned identities remain unknown and never fill loaded-module declarations."}


def _identity(value):
    return isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9_.:-]{1,128}", value) is not None


def _seed(value):
    if isinstance(value, str) and re.fullmatch(r"[0-9]{1,20}", value):
        value = int(value)
    return value if _uint(value) else None


def _pairs(values):
    result = {}
    for key, value in values:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _json(path, expected_hash):
    with Path(path).open("rb") as stream:
        content = stream.read(1024 * 1024 + 1)
    if len(content) > 1024 * 1024 or hashlib.sha256(content).hexdigest() != expected_hash:
        raise ValueError("JSON changed after fingerprint")
    return json.loads(content.decode("utf-8-sig"), object_pairs_hook=_pairs)


def _csv_unchanged(path, artifact):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    if digest.hexdigest() != artifact["sha256"]:
        raise ValueError("CSV changed after fingerprint")


def _artifact(path, kind, peer, notes, session=None):
    if not path:
        return {"provided": False}
    result = {"provided": True, "valid": False}
    try:
        path = Path(path)
        before = path.stat()
        maximum = 1024 * 1024 if kind in ("session", "native_stats", "steam_stats") else 256 * 1024 * 1024
        if before.st_size > maximum:
            _note(notes, "evidence_size_limit", peer, kind, True)
            return result
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        after = path.stat()
        if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
            _note(notes, "evidence_changed_during_read", peer, kind, True)
            return result
        result.update(bytes=before.st_size, sha256=digest.hexdigest(), valid=True)
        if session is not None:
            result["freshness"] = "unknown"
            started = session.get("started")
            if isinstance(started, str) and len(started) <= 64:
                try:
                    stamp = datetime.fromisoformat(started.replace("Z", "+00:00"))
                    if stamp.tzinfo is None:
                        raise ValueError("timezone missing")
                    stale = before.st_mtime + .001 < stamp.timestamp()
                    result["freshness"] = "older_than_session" if stale else "mtime_not_older_than_session"
                    if stale:
                        _note(notes, "evidence_predates_session", peer, kind)
                except ValueError:
                    _note(notes, "invalid_session_start_time", peer)
            expected = session.get("artifacts", {})
            if isinstance(expected, dict) and kind in expected:
                result["binding"] = "matched" if _hash(expected[kind]) and expected[kind].lower() == result["sha256"] else "mismatch"
                if result["binding"] == "mismatch":
                    result["valid"] = False
                    _note(notes, "artifact_digest_mismatch", peer, kind, True)
            else:
                result["binding"] = "not_recorded"
    except (OSError, ValueError, TypeError):
        _note(notes, "evidence_unreadable", peer, kind, True)
    return result


def _native(data):
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 1 or data.get("backend") != "Product120-private-udp-v1":
        raise ValueError("native schema")
    operations = data.get("operations")
    if not isinstance(operations, dict):
        raise ValueError("operations missing")
    result, anomalies = {}, []
    for name, values in operations.items():
        if name not in NATIVE_OPS:
            continue
        if not isinstance(values, dict) or not all(_uint(values.get(key)) for key in ("calls", "failures", "duration_ns")):
            raise ValueError("invalid operation counters")
        calls, failures, duration = (values[key] for key in ("calls", "failures", "duration_ns"))
        result[name] = {"calls": calls, "failures": failures, "total_operation_duration_ns": duration,
                        "mean_operation_duration_ms": duration / calls / 1e6 if calls else None}
        if failures > calls or (not calls and duration):
            anomalies.append(name)
    keys = ("available_true_calls", "available_false_calls", "local_true_calls", "local_false_calls",
            "send_return_bytes", "receive_return_bytes_including_peeks")
    if not all(_uint(data.get(key)) for key in keys):
        raise ValueError("invalid totals")
    snapshot = data.get("nifm_last_cached_snapshot")
    if not isinstance(snapshot, dict):
        raise ValueError("missing cached snapshot")
    cache = {}
    for key in ("initialized", "local_mode", "requested", "available"):
        value = snapshot.get(key)
        if type(value) is not bool and not (type(value) is int and value in (0, 1)):
            raise ValueError("invalid snapshot bool")
        cache[key] = bool(value)
    for key in ("selection_status", "candidates"):
        if not _uint(snapshot.get(key)):
            raise ValueError("invalid snapshot scalar")
        cache[key] = snapshot[key]
    if cache["available"] and (not all(cache[k] for k in ("initialized", "local_mode", "requested")) or cache["candidates"] != 1):
        anomalies.append("cached_availability")
    return {"operations": result, "totals": {key: data[key] for key in keys},
            "cached_nifm_snapshot": cache, "counter_anomalies": anomalies,
            "unknown_operation_count": len(set(operations) - NATIVE_OPS),
            "scope": "Non-transactional call counters; operation durations include waits, not RTT. Return bytes are not wire bytes; receives include peeks. Cached availability is not room/gameplay evidence."}


def _steam(data):
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data["schema"] != 1 or data.get("source") != "steam_dev_bootstrap" or type(data.get("app_id")) is not int or data["app_id"] != 480:
        raise ValueError("Steam schema")
    if data.get("status") not in STEAM_STATUS or data.get("checkpoint") not in ("initialization", "shutdown"):
        raise ValueError("Steam lifecycle status")
    flags = ("init_attempted", "initialized_ever")
    counters = ("callback_calls", "callback_completed", "callback_failures", "shutdown_calls", "shutdown_completed")
    if not all(type(data.get(key)) is bool for key in flags) or not all(_uint(data.get(key)) for key in counters):
        raise ValueError("Steam counters")
    anomalies = []
    if (data["callback_completed"] + data["callback_failures"] > data["callback_calls"] or
            data["shutdown_completed"] > data["shutdown_calls"] or data["shutdown_calls"] > 1 or
            (data["initialized_ever"] and not data["init_attempted"]) or
            (data["shutdown_calls"] and not data["initialized_ever"])):
        anomalies.append("lifecycle_counters")
    return {**{key: data[key] for key in flags + counters}, "status": data["status"], "checkpoint": data["checkpoint"],
            "api_shutdown_completed_observed": data["shutdown_completed"] > 0,
            "counter_anomalies": anomalies,
            "scope": "Local API lifecycle only. A shutdown checkpoint can be deferred/incomplete. Initialization/callback/shutdown counts do not establish Remote Play Together, P2P, remote players or gameplay."}


def _peer(label, paths, notes):
    paths = paths or {}
    source = paths.get("session")
    descriptor = _artifact(source, "session", label, notes)
    session = {}
    if descriptor.get("valid"):
        try:
            session = _json(source, descriptor["sha256"])
            if not isinstance(session, dict):
                raise ValueError("metadata object")
        except (OSError, ValueError, TypeError):
            _note(notes, "invalid_session_metadata", label, error=True)
            session = {}
    else:
        _note(notes, "session_metadata_missing", label)
    if "schema" not in session:
        _note(notes, "session_schema_missing", label)
    elif type(session["schema"]) is not int or session["schema"] != 1:
        _note(notes, "unsupported_session_schema", label, error=True)
        session = {}
    mode = MODES.get(session.get("mode")) if isinstance(session.get("mode"), str) else None
    if mode is None and session.get("native_lan") is True and "mode" not in session:
        mode = "native_lan" # An explicit older launcher flag, not a filename heuristic.
    if mode is None:
        _note(notes, "session_mode_missing_or_unknown", label)
    if session.get("native_lan") is True and mode != "native_lan":
        _note(notes, "conflicting_session_mode", label, error=True)
        mode = None
    runtime = session.get("runtime_sha256")
    if not _hash(runtime):
        _note(notes, "runtime_hash_missing_or_invalid", label)
        runtime = None
    else:
        runtime = runtime.lower()
    metadata = {"mode": mode, "runtime_sha256": runtime,
                "game_code_declarations": _game_code_declarations(session, label, notes),
                "planned_game_library_declaration": _digest_declaration(
                    session.get(PLANNED_LIBRARY_FIELD), PLANNED_LIBRARY_FIELD, label, notes),
                "session_identity_present": _identity(session.get("session_id")),
                "peer_identity_present": _identity(session.get("peer_id")),
                "machine_identity_present": _identity(session.get("machine_id"))}
    peer = {"metadata": metadata, "artifacts": {"session": descriptor}, "native": None, "steam": None, "legacy": {}}
    internals = {"session": session, "paths": paths, "mode": mode}
    expected_mode = {"native_stats": "native_lan", "steam_stats": "steam_dev", "legacy_trace": "legacy_lan", "application_trace": "legacy_lan"}
    for kind, expected in expected_mode.items():
        path = paths.get(kind)
        artifact = _artifact(path, kind, label, notes, session)
        peer["artifacts"][kind] = artifact
        if not path:
            if mode == expected and kind != "legacy_trace":
                _note(notes, "evidence_missing", label, kind)
            continue
        if mode != expected:
            _note(notes, "artifact_mode_mismatch", label, kind)
            continue
        if not artifact.get("valid"):
            continue
        try:
            if kind in ("native_stats", "steam_stats"):
                value = (_native if kind == "native_stats" else _steam)(_json(path, artifact["sha256"]))
                peer["native" if kind == "native_stats" else "steam"] = value
                if value["counter_anomalies"]:
                    _note(notes, "counter_consistency_warning", label, kind)
                if kind == "native_stats" and not any(op["calls"] for op in value["operations"].values()):
                    _note(notes, "native_no_calls_recorded", label, kind)
                if kind == "steam_stats" and not value["initialized_ever"]:
                    _note(notes, "steam_bootstrap_not_initialized", label, kind)
                if kind == "steam_stats" and value["checkpoint"] == "shutdown" and value["initialized_ever"] and not value["api_shutdown_completed_observed"]:
                    _note(notes, "steam_shutdown_not_completed", label, kind)
            elif kind == "application_trace":
                metadata, _, stats = _tool("compare_lan_application").read(path)
                _csv_unchanged(path, artifact)
                if not 0 <= stats["role"] < 4 or _seed(metadata.get("seed")) is None:
                    raise ValueError("invalid application identity")
                if (("role" in session and session["role"] != stats["role"]) or
                        ("seed" in session and _seed(session["seed"]) != _seed(metadata["seed"])) or
                        ("input_seed" in internals and internals["input_seed"] != _seed(metadata["seed"]))):
                    internals["legacy_identity_conflict"] = True
                    _note(notes, "legacy_session_trace_identity_mismatch", label, kind)
                    continue
                peer["legacy"]["application"] = stats
                internals["application_metadata"] = metadata
                if stats["omitted_events"]:
                    _note(notes, "application_retention_gaps", label, kind)
            else:
                tool = _tool("compare_lan_traces")
                rows = tool.load(path)
                seeds = {_seed(row["seed"]) for row in rows.values()}
                if len(seeds) != 1 or None in seeds:
                    raise ValueError("empty or mixed-seed trace")
                _csv_unchanged(path, artifact)
                if "seed" in session and _seed(session["seed"]) != next(iter(seeds)):
                    internals["legacy_identity_conflict"] = True
                    _note(notes, "legacy_session_trace_identity_mismatch", label, kind)
                    continue
                peer["legacy"]["input"] = {"retained_exchanges": len(rows), "phase_diagnostics": tool.phase_diagnostics(rows)}
                internals["input_rows"] = rows
                internals["input_seed"] = next(iter(seeds))
        except (OSError, ValueError, TypeError, KeyError, OverflowError, csv.Error):
            _note(notes, "invalid_evidence_schema_or_data", label, kind, True)
    return peer, internals


def summarize(left, right=None):
    """Paths dict keys: session/native_stats/steam_stats/legacy_trace/application_trace."""
    notes = []
    lp, li = _peer("left", left, notes)
    report = {"schema": 1, "scope": SCOPE, "peers": {"left": lp}, "diagnostics": notes,
              "correlation": {"eligible": False, "reasons": []}, "comparisons": {}}
    if right is None:
        report["correlation"]["reasons"] = ["single_peer_evidence"]
        return report
    rp, ri = _peer("right", right, notes)
    report["peers"]["right"] = rp
    reasons = report["correlation"]["reasons"]
    a, b = lp["metadata"], rp["metadata"]
    if not a["mode"] or a["mode"] != b["mode"]:
        reasons.append("mode_missing_or_mismatched")
    if not a["runtime_sha256"] or a["runtime_sha256"] != b["runtime_sha256"]:
        reasons.append("runtime_hash_missing_or_mismatched")
    report["correlation"]["game_code_identity"] = _compare_game_code_declarations(
        a["game_code_declarations"], b["game_code_declarations"], reasons)
    report["correlation"]["planned_game_library_identity"] = _compare_planned_libraries(
        a["planned_game_library_declaration"], b["planned_game_library_declaration"], reasons)
    for key in ("session_id", "peer_id"):
        av, bv = li["session"].get(key), ri["session"].get(key)
        if not _identity(av) or not _identity(bv):
            reasons.append(key + "_missing_or_invalid")
        elif (key == "session_id" and av != bv) or (key == "peer_id" and av == bv):
            reasons.append("session_identity_mismatch" if key == "session_id" else "peer_identity_not_distinct")
    for peer in (lp, rp):
        if any(v.get("freshness") == "older_than_session" or v.get("binding") == "mismatch" for v in peer["artifacts"].values()):
            reasons.append("stale_or_misbound_evidence")
            break
    if li.get("legacy_identity_conflict") or ri.get("legacy_identity_conflict"):
        reasons.append("legacy_evidence_identity_conflict")
    if a["mode"] == b["mode"] == "native_lan" and (lp["native"] is None or rp["native"] is None):
        reasons.append("native_stats_missing_or_invalid")
    report["correlation"]["eligible"] = not reasons
    machines = (li["session"].get("machine_id"), ri["session"].get("machine_id"))
    report["correlation"]["machine_identity"] = ("distinct_declared_machines" if machines[0] != machines[1] else "same_declared_machine") if all(_identity(v) for v in machines) else "not_provided"
    report["correlation"]["scope"] = "Matching declared identities/hash only, not independently authenticated physical PCs or a verified connection. File mtime alone does not prove session provenance. Optional artifact digests bind supplied files to the session declaration."
    if reasons:
        _note(notes, "pair_comparison_blocked")
        return report
    if a["mode"] == "native_lan":
        if lp["native"] is not None and rp["native"] is not None:
            report["comparisons"]["native"] = {"both_cached_available": all(p["native"]["cached_nifm_snapshot"]["available"] for p in (lp, rp)),
                "scope": "Two local provider snapshots only. Availability/call or return-byte counts cannot establish peer delivery, rooms, gameplay sync or RTT."}
    elif a["mode"] == "legacy_lan":
        app_pair = False
        if all(p["legacy"].get("application") is not None for p in (lp, rp)):
            try:
                compared = _tool("compare_lan_application").compare(li["paths"]["application_trace"], ri["paths"]["application_trace"])
                for p, internal in ((lp, li), (rp, ri)):
                    _csv_unchanged(internal["paths"]["application_trace"], p["artifacts"]["application_trace"])
                report["comparisons"]["legacy_application"] = compared
                app_pair = True
            except (OSError, ValueError, KeyError, TypeError, csv.Error):
                _note(notes, "legacy_application_pair_metadata_mismatch")
        if "input_rows" in li and "input_rows" in ri:
            roles = (li["session"].get("role"), ri["session"].get("role"))
            roles_ok = all(type(v) is int and 0 <= v < 4 for v in roles) and roles[0] != roles[1]
            if li["input_seed"] != ri["input_seed"] or not (roles_ok or app_pair):
                _note(notes, "legacy_input_seed_or_role_mismatch")
            else:
                try:
                    compared = _tool("compare_lan_traces").compare(li["input_rows"], ri["input_rows"])
                    json.dumps(compared, allow_nan=False) # Nonfinite position evidence must not break JSON output.
                    report["comparisons"]["legacy_input"] = compared
                except (OSError, ValueError, KeyError, TypeError):
                    _note(notes, "legacy_input_comparison_unavailable")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for side in ("left", "right"):
        for kind in ("session", "native_stats", "steam_stats", "legacy_trace", "application_trace"):
            parser.add_argument(f"--{side}-{kind.replace('_', '-')}", type=Path, required=side == "left" and kind == "session")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    inputs = [{kind: getattr(args, side + "_" + kind) for kind in ("session", "native_stats", "steam_stats", "legacy_trace", "application_trace")} for side in ("left", "right")]
    report = summarize(inputs[0], inputs[1] if any(inputs[1].values()) else None)
    text = json.dumps(report, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 2 if any(note["level"] == "error" for note in report["diagnostics"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
