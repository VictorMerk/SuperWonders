"""Compare bounded LAN input traces; matching traces do not prove world sync."""
import argparse
import csv
import json
import math
from pathlib import Path

PAD_FIELDS = [f"p{p}_{field}" for p in range(4)
              for field in ("buttons", "lx", "ly", "rx", "ry")]


def load(path):
    path = Path(path)
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError("Trace exceeds the 8 MiB diagnostic limit")
    with path.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))
    if len(rows) > 4096:
        raise ValueError("Trace exceeds the 4096-exchange ring")
    result = {}
    for row in rows:
        tick = int(row["tick"])
        if tick in result:
            raise ValueError(f"Duplicate tick {tick}")
        result[tick] = row
    return result



def phase(row):
    if row["committed"] != "1":
        return "failed"
    marker_fields = ["local_loading_event", *(f"p{p}_loading_event" for p in range(4))]
    if not all(row.get(key) not in (None, "") for key in marker_fields):
        return "unknown"
    return "loading" if any(int(row[key]) for key in marker_fields) else "steady"


def timing_summary(values):
    values = sorted(values)
    if not values:
        return {"samples": 0, "mean": None, "p95": None, "p99": None, "peak": None}
    return {"samples": len(values), "mean": sum(values) / len(values),
            "p95": values[min(len(values) - 1, int(len(values) * .95))],
            "p99": values[min(len(values) - 1, int(len(values) * .99))], "peak": values[-1]}


def phase_diagnostics(rows):
    groups = {name: [] for name in ("loading", "steady", "failed", "unknown")}
    start_age, finish_age = [], []
    longest = {name: 0 for name in groups}
    previous, previous_phase, streak = None, None, 0
    for tick in sorted(rows):
        row = rows[tick]
        name = phase(row)
        groups[name].append(int(row["wait_ns"]) / 1e6)
        frame = int(row["frame"]) if "frame" in row else None
        if frame is not None:
            streak = streak + 1 if frame == previous and name == previous_phase else 1
            longest[name] = max(longest[name], streak)
        else:
            streak = 0
        previous, previous_phase = frame, name
        # These timestamps are from the same PC's monotonic clock. Loading
        # exchanges carry neutral input; never count them as real pad input age.
        if name == "steady":
            sampled = int(row.get("sampled_ns", "0"))
            started = int(row.get("exchange_started_ns", "0"))
            finished = int(row.get("exchange_finished_ns", "0"))
            if sampled > 0 and started >= sampled and finished >= started:
                start_age.append((started - sampled) / 1e6)
                finish_age.append((finished - sampled) / 1e6)
    return {"exchange_phases": {name: {**timing_summary(values),
                    "longest_same_frame_exchanges": longest[name]} for name, values in groups.items()},
            "steady_input_sample_age_ms": {"to_exchange_start": timing_summary(start_age),
                "to_exchange_completion": timing_summary(finish_age),
                "scope": "Local monotonic pad sample to exchange timing only; excludes loading samples. Not network RTT, display latency or hardware button latency."}}


def compare(left, right):
    common = sorted(left.keys() & right.keys())
    if not common:
        raise ValueError("No overlapping ticks; reproduce and quit both clients close together")
    report = {"overlapping_ticks": len(common), "committed_pairs": 0,
              "valid_position_pairs": 0, "first_difference": None,
              "limitation": "Inputs and one detected position only; character IDs, RNG, actors and world state are not verified."}
    for tick in common:
        a, b = left[tick], right[tick]
        fields = ["seed"]
        if a["committed"] == b["committed"] == "1":
            report["committed_pairs"] += 1
            fields += ["players", *PAD_FIELDS]
            fields += [f"p{p}_loading_event" for p in range(4)
                       if f"p{p}_loading_event" in a and f"p{p}_loading_event" in b]
        differences = {key: [a[key], b[key]] for key in fields if a[key] != b[key]}
        if a.get("input_lead", "0") != b.get("input_lead", "0"):
            differences["input_lead"] = [a.get("input_lead", "0"), b.get("input_lead", "0")]
        if a.get("simulation_rate", "60") != b.get("simulation_rate", "60"):
            differences["simulation_rate"] = [a.get("simulation_rate", "60"), b.get("simulation_rate", "60")]
        if a["state_valid"] == b["state_valid"] == "1":
            report["valid_position_pairs"] += 1
            if a["scene"] != b["scene"]:
                differences["scene"] = [a["scene"], b["scene"]]
            for axis in ("x", "y", "z"):
                av, bv = float(a[axis]), float(b[axis])
                if not math.isfinite(av) or not math.isfinite(bv) or abs(av - bv) > 1 / 1024:
                    differences[axis] = [a[axis], b[axis]]
        if differences and report["first_difference"] is None:
            report["first_difference"] = {"tick": tick, "fields": differences}
    report["exchange_wait_ms"] = []
    report["local_frame_work_ms"] = []
    for rows in (left, right):
        waits = sorted(int(row["wait_ns"]) / 1e6 for row in rows.values())
        report["exchange_wait_ms"].append({"mean": sum(waits) / len(waits),
                                  "p95": waits[min(len(waits) - 1, int(len(waits) * .95))],
                                  "peak": waits[-1]})
        # Zero denotes an unknown first sample. This includes local pacing and
        # render/scheduling time, and excludes foreground LAN wait; it is not
        # CPU execution time or network RTT.
        work = sorted(int(row.get("local_work_ns", "0")) / 1e6 for row in rows.values()
                      if int(row.get("local_work_ns", "0")) > 0)
        report["local_frame_work_ms"].append({"samples": len(work),
            "mean": sum(work) / len(work) if work else None,
            "p95": work[min(len(work)-1, int(len(work)*.95))] if work else None,
            "peak": work[-1] if work else None})
    report["presentation_progress"] = []
    report["warnings"] = []
    for rows in (left, right):
        samples = [int(rows[tick]["frame"]) for tick in common
                   if "frame" in rows[tick] and rows[tick]["committed"] == "1"]
        longest = current = 0
        previous = None
        for frame in samples:
            current = current + 1 if frame == previous else 1
            longest = max(longest, current)
            previous = frame
        report["presentation_progress"].append({
            "samples": len(samples),
            "frames_advanced": samples[-1] - samples[0] if samples else None,
            "longest_same_frame_exchanges": longest,
        })
    a, b = report["presentation_progress"]
    if a["samples"] and b["samples"] and abs(a["frames_advanced"] - b["frames_advanced"]) >= 60:
        report["warnings"].append("Presented-frame progress differs by at least 60 frames across matching input ticks; inspect loading barriers or a stalled renderer. Matching inputs do not establish synchronized gameplay.")
    report["phase_diagnostics"] = [phase_diagnostics(left), phase_diagnostics(right)]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("client")
    parser.add_argument("--from-tick", type=int, default=0, help="Exclude loading/startup ticks when comparing steady gameplay")
    parser.add_argument("--to-tick", type=int, help="Last included simulation tick")
    args = parser.parse_args()
    try:
        def window(path):
            return {tick: row for tick, row in load(path).items()
                    if tick >= args.from_tick and (args.to_tick is None or tick <= args.to_tick)}
        report = compare(window(args.host), window(args.client))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f"Cannot compare LAN traces: {error}\n")
    print(json.dumps(report, indent=2))
    return 1 if report["first_difference"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
