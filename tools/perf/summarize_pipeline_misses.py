"""Summarize an explicit bounded pipeline-miss CSV without game-performance claims.

Usage: python tools/perf/summarize_pipeline_misses.py TRACE.csv [--output REPORT.json]
Durations overlap and nest. Worker compilation is observed later; its duration
must never be added to wait/probe durations or interpreted as a frame stall.
"""
import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import re
import sys

MAX_BYTES = 8 * 1024 * 1024
MAX_ROWS = 4096
MAX_LINE_BYTES = 4096
MAX_TOP = 20
U64 = (1 << 64) - 1
COMPILE_REQUIRED = 1000297000
OPERATIONS = (
    "lookup_miss", "cache_probe", "async_queued", "async_wait",
    "async_completion_observed", "sync_compile", "gpl_libraries_and_link",
    "draw_skipped_pipeline_pending",
)
WORKER = "async_completion_observed"
TIMED_CALLER = ("cache_probe", "async_wait", "sync_compile", "gpl_libraries_and_link")
FIELDS = (
    "sequence", "observed_ns", "frame", "key_fingerprint", "vs_hash", "fs_hash",
    "caller_may_skip", "effective_may_skip", "gpl", "operation", "duration_ns",
    "result_known", "result",
)
SCOPE = (
    "# scope=host_pipeline_operations;observed_ns=steady_clock;frame=observation_only;"
    "key_fingerprint=noncryptographic_fnv1a64;durations_overlap;"
    "async_completion_observed_duration=worker_compile_not_wait;"
    "cache_probe_result_1000297000=compile_required;"
    "missing_optional_probes=unknown_not_hit;no_shader_bytes"
)
META_KEYS = ("schema", "total_events", "retained_events", "evicted_events", "capacity")


class TraceError(ValueError):
    """Fixed public error messages; input values and paths are never included."""


def integer(value, field, minimum=0, maximum=U64):
    # Writer uses canonical decimal integers, never whitespace, signs on unsigned
    # fields, floating point, thousands separators, or exponent notation.
    pattern = r"0|[1-9][0-9]*" if minimum >= 0 else r"0|-?[1-9][0-9]*"
    if not isinstance(value, str) or not re.fullmatch(pattern, value) or len(value) > 20:
        raise TraceError("Invalid integer field: " + field)
    result = int(value)
    if not minimum <= result <= maximum:
        raise TraceError("Out-of-range integer field: " + field)
    return result


def load(path):
    """Read once, with bounded allocation; hash exactly the bytes parsed."""
    try:
        with Path(path).open("rb") as source:
            raw = source.read(MAX_BYTES + 1)
    except (OSError, ValueError):
        raise TraceError("Trace is missing or unreadable") from None
    if len(raw) > MAX_BYTES:
        raise TraceError("Trace exceeds the 8 MiB limit")
    if any(len(line) > MAX_LINE_BYTES for line in raw.split(b"\n")):
        raise TraceError("Trace line exceeds the 4096-byte limit")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeError:
        raise TraceError("Trace is not valid UTF-8") from None
    if "\x00" in text:
        raise TraceError("Trace contains a forbidden NUL byte")
    if not text.endswith("\n"):
        raise TraceError("Trace export has no final line terminator")
    stream = io.StringIO(text, newline="")
    metadata_line = stream.readline().rstrip("\r\n")
    if not metadata_line.startswith("# "):
        raise TraceError("Missing schema metadata")
    metadata = {}
    for item in metadata_line[2:].split(","):
        key, separator, value = item.partition("=")
        if not separator or key not in META_KEYS or key in metadata:
            raise TraceError("Invalid schema metadata fields")
        metadata[key] = integer(value, key)
    if set(metadata) != set(META_KEYS) or metadata["schema"] != 1:
        raise TraceError("Unsupported or incomplete schema metadata")
    if metadata["capacity"] > MAX_ROWS or metadata["retained_events"] > MAX_ROWS:
        raise TraceError("Trace exceeds the 4096-event retention limit")
    if stream.readline().rstrip("\r\n") != SCOPE:
        raise TraceError("Missing or unsupported scope metadata")
    events = []
    try:
        reader = csv.reader(stream, strict=True)
        if next(reader, None) != list(FIELDS):
            raise TraceError("Missing or unsupported CSV fields")
        for values in reader:
            if len(events) >= MAX_ROWS:
                raise TraceError("Trace exceeds the 4096-row limit")
            if len(values) != len(FIELDS):
                raise TraceError("CSV row has an invalid field count")
            event = dict(zip(FIELDS, values))
            for name in ("sequence", "observed_ns", "frame", "key_fingerprint", "duration_ns"):
                event[name] = integer(event[name], name, 1 if name == "sequence" else 0)
            for name in ("caller_may_skip", "effective_may_skip", "gpl", "result_known"):
                event[name] = integer(event[name], name, maximum=1)
            for name in ("vs_hash", "fs_hash"):
                if event[name] and not re.fullmatch(r"[0-9a-fA-F]{16}", event[name]):
                    raise TraceError("Invalid hexadecimal field: " + name)
                event[name] = event[name].lower()
            if event["operation"] not in OPERATIONS:
                raise TraceError("Unsupported pipeline operation")
            if event["result_known"]:
                event["result"] = integer(event["result"], "result", -(1 << 31), (1 << 31) - 1)
            elif event["result"]:
                raise TraceError("Unknown result must have an empty value")
            else:
                event["result"] = None
            if events and event["sequence"] <= events[-1]["sequence"]:
                raise TraceError("Event sequences must be unique and increasing")
            # observed_ns is sampled before the recorder's lock, so overlapping
            # callers can legitimately appear out of timestamp order.
            events.append(event)
    except csv.Error:
        raise TraceError("Malformed CSV data") from None
    return metadata, events, hashlib.sha256(raw).hexdigest()


def distribution(values):
    values = sorted(values)
    if not values:
        return dict(samples=0, min_ns=None, mean_ns=None, p50_ns=None,
                    p95_ns=None, p99_ns=None, max_ns=None)
    def percentile(fraction):
        return values[max(0, math.ceil(len(values) * fraction) - 1)]
    return dict(samples=len(values), min_ns=values[0], mean_ns=sum(values) / len(values),
                p50_ns=percentile(.5), p95_ns=percentile(.95),
                p99_ns=percentile(.99), max_ns=values[-1])


def outcome(event):
    value = event["result"]
    if value is None:
        return "unknown"
    if value == 0:
        return "success"
    if value == COMPILE_REQUIRED:
        return "compile_required"
    return "failure" if value < 0 else "other_positive_status"


def summarize(path):
    metadata, events, digest = load(path)
    diagnostics = []
    def note(code, severity="warning"):
        diagnostics.append(dict(code=code, severity=severity))
    total, retained, evicted, capacity = (metadata[name] for name in META_KEYS[1:])
    consistent = True
    if retained != len(events) or retained > capacity or total != retained + evicted:
        note("retention_metadata_mismatch", "error")
        consistent = False
    gaps = []
    if events:
        previous = evicted
        for event in events:
            sequence = event["sequence"]
            if sequence > previous + 1:
                gaps.append(dict(first=previous + 1, last=sequence - 1))
            if sequence <= evicted or sequence > total:
                consistent = False
            previous = sequence
        if events[-1]["sequence"] < total:
            gaps.append(dict(first=events[-1]["sequence"] + 1, last=total))
        if gaps or events[0]["sequence"] != evicted + 1 or events[-1]["sequence"] != total:
            consistent = False
    elif total > evicted:
        gaps.append(dict(first=evicted + 1, last=total))
        consistent = False
    if not consistent:
        note("sequence_or_retention_incomplete", "error")
    if evicted:
        note("earlier_events_evicted")
    if not events:
        note("no_retained_observations")
    if any(event["operation"] != "cache_probe" and event["result"] == COMPILE_REQUIRED for event in events):
        note("compile_required_outside_cache_probe")
    if any(not event["vs_hash"] or not event["fs_hash"] for event in events):
        note("shader_hash_metadata_missing_or_not_applicable")
    operation_stats = {}
    for operation in OPERATIONS:
        selected = [event for event in events if event["operation"] == operation]
        operation_stats[operation] = dict(
            count=len(selected), duration_ns=distribution([event["duration_ns"] for event in selected]),
            outcomes=dict(Counter(outcome(event) for event in selected)),
            duration_semantics="worker_compile_observed_later" if operation == WORKER else
                "caller_operation_may_overlap_or_nest" if operation in TIMED_CALLER else "event_marker",
        )
    by_key, by_frame = defaultdict(list), defaultdict(list)
    for event in events:
        by_key[event["key_fingerprint"]].append(event)
        by_frame[event["frame"]].append(event)
    def group_summary(selected):
        caller = [event["duration_ns"] for event in selected if event["operation"] in TIMED_CALLER]
        worker = [event["duration_ns"] for event in selected if event["operation"] == WORKER]
        frames = [event["frame"] for event in selected]
        return dict(events=len(selected), operations=dict(Counter(event["operation"] for event in selected)),
                    first_sequence=selected[0]["sequence"], last_sequence=selected[-1]["sequence"],
                    associated_frame_min=min(frames), associated_frame_max=max(frames),
                    associated_frame_count=len(set(frames)),
                    caller_operation_peak_ns=max(caller, default=None),
                    observed_worker_peak_ns=max(worker, default=None))
    # Rank counts deterministically; expose individual peaks, never summed costs.
    key_order = sorted(by_key, key=lambda key: (-len(by_key[key]), key))
    keys = []
    for key in key_order[:MAX_TOP]:
        selected = by_key[key]
        item = group_summary(selected)
        item["fingerprint_fnv1a64"] = f"{key:016x}"
        item["shader_hash_pairs"] = [dict(vs_hash=vs or None, fs_hash=fs or None)
                                     for vs, fs in sorted({(e["vs_hash"], e["fs_hash"]) for e in selected})[:MAX_TOP]]
        item["distinct_shader_hash_pairs"] = len({(e["vs_hash"], e["fs_hash"]) for e in selected})
        keys.append(item)
    peak_candidates = [key for key, selected in by_key.items()
                       if any(event["operation"] in TIMED_CALLER for event in selected)]
    peak_order = sorted(peak_candidates, key=lambda key: (
        -max((event["duration_ns"] for event in by_key[key] if event["operation"] in TIMED_CALLER), default=0), key))
    caller_peak_keys = []
    for key in peak_order[:MAX_TOP]:
        selected = by_key[key]
        item = group_summary(selected)
        item["fingerprint_fnv1a64"] = f"{key:016x}"
        caller_peak_keys.append(item)
    frame_order = sorted(by_frame, key=lambda frame: (-len(by_frame[frame]), frame))
    frames = []
    for frame in frame_order[:MAX_TOP]:
        item = group_summary(by_frame[frame])
        item["observation_frame"] = frame
        item["distinct_key_fingerprints"] = len({event["key_fingerprint"] for event in by_frame[frame]})
        frames.append(item)
    policy = Counter((event["caller_may_skip"], event["effective_may_skip"], event["gpl"]) for event in events)
    return dict(
        schema=1, source="pipeline_miss_trace_schema_1", evidence_sha256=digest,
        scope="Retained host pipeline observations only; no frame-cost, FPS, gameplay or synchronization certification",
        evidence_status="incomplete" if not consistent else "retained_observations" if events else "no_observations",
        retention=dict(**{key: metadata[key] for key in META_KEYS[1:]}, parsed_events=len(events),
                       consistent=consistent, complete_recorded_event_history=consistent and evicted == 0,
                       missing_sequence_ranges=gaps),
        limitations=[
            "Operation durations overlap or nest; never add them as exclusive frame cost.",
            "Async completion duration is worker compile time observed later, not time waiting or time spent in its associated frame.",
            "Frames are observation associations, not verified start/finish boundaries; timestamps can be recorded out of order.",
            "Only misses are traced. Missing cache probes are unknown, never evidence of a cache hit.",
            "draw_skipped_pipeline_pending counts recorded omitted draws while a pipeline is pending, not missing frames or a skip rate across all draws.",
            "Key fingerprints are noncryptographic identities; collisions are possible. Shader hash strings are opaque identities.",
            "No shutdown checkpoint or session identity exists in schema 1; a complete file does not certify a complete game session.",
        ],
        operations=operation_stats,
        caller_operations_duration_ns=distribution([event["duration_ns"] for event in events if event["operation"] in TIMED_CALLER]),
        worker_compile_observed_duration_ns=distribution([event["duration_ns"] for event in events if event["operation"] == WORKER]),
        policy_observations=[dict(caller_may_skip=bool(caller), effective_may_skip=bool(effective),
                                  gpl=bool(gpl), events=count)
                             for (caller, effective, gpl), count in sorted(policy.items())],
        unique_key_fingerprints=len(by_key), top_keys_by_event_count=keys,
        top_keys_by_caller_operation_peak=caller_peak_keys,
        associated_frame_count=len(by_frame), top_frames_by_event_count=frames,
        top_limit=MAX_TOP, diagnostics=diagnostics,
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="Explicit pipeline miss CSV file")
    parser.add_argument("--output", type=Path, help="Write JSON to this file instead of stdout")
    args = parser.parse_args(argv)
    def input_alias():
        return args.output and (args.output.resolve() == args.trace.resolve() or
                                (args.output.exists() and args.trace.exists() and args.output.samefile(args.trace)))
    try:
        if input_alias():
            raise TraceError("Report output must not overwrite the input trace")
        result = summarize(args.trace)
        code = 2 if result["evidence_status"] == "incomplete" else 0
    except TraceError as error:
        result = dict(schema=1, evidence_status="unusable", error=str(error),
                      scope="No performance or gameplay certification")
        code = 2
    except (OSError, ValueError, RuntimeError):
        result = dict(schema=1, evidence_status="unusable", error="Input or output path is unusable",
                      scope="No performance or gameplay certification")
        code = 2
    encoded = json.dumps(result, indent=2, ensure_ascii=True) + "\n"
    if args.output:
        # Never echo OS exceptions: they contain local paths.
        try:
            if input_alias():
                print(encoded, end="")
                return code
            args.output.write_text(encoded, encoding="utf-8")
        except (OSError, ValueError, RuntimeError):
            print("Cannot write pipeline report", file=sys.stderr)
            return 2
    else:
        print(encoded, end="")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
