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
