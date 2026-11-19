"""Compare bounded input-application evidence, without certifying game state."""
import argparse
import csv
import json
from pathlib import Path


def read(path):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        lines = list(stream)
    metadata = {}
    for line in lines:
        if line.startswith("# schema="):
            metadata = dict(part.split("=", 1) for part in line[2:].strip().split(","))
    rows = list(csv.DictReader(line for line in lines if not line.startswith("#")))
    if metadata.get("schema") != "1":
        raise ValueError("Expected application trace schema 1")
    return metadata, rows


def compare(left, right):
    lm, lr = read(left)
    rm, rr = read(right)
    if lm["seed"] != rm["seed"] or lm["role"] == rm["role"]:
        raise ValueError("Expected opposite roles from the same session seed")
    fields = [f"p{p}_{field}" for p in range(4)
              for field in ("loading_event", "buttons", "lx", "ly", "rx", "ry")]
    applications = {"apply", "loading_neutral_apply"}

    def index(rows):
        result = {}
        duplicates = []
        for row in rows:
            if row["event"] not in applications:
                continue
            tick = int(row["tick"])
            if tick in result:
                duplicates.append(tick)
            result[tick] = row
        return result, duplicates

    li, ld = index(lr)
    ri, rd = index(rr)
    shared = sorted(li.keys() & ri.keys())
    mismatch = []
    for tick in shared:
        a, b = li[tick], ri[tick]
        different = [field for field in ["event", "players"] + fields if a[field] != b[field]]
        if different:
            mismatch.append({"tick": tick, "fields": different,
                             "left_frame": int(a["frame"]), "right_frame": int(b["frame"])})

    def summary(meta, rows, indexed, duplicate):
        phase = [int(row["tick"]) for row in indexed.values() if row["event"] == "apply"
                 and any(int(row[f"p{p}_loading_event"]) for p in range(4))]
        return {"role": int(meta["role"]), "retained_events": len(rows),
                "omitted_events": int(meta["omitted"]), "applied_ticks": len(indexed),
                "duplicate_applied_ticks": duplicate[:20],
                "gameplay_applies_with_peer_loading_marker": len(phase),
                "first_gameplay_apply_with_peer_loading_marker": min(phase, default=None),
                "retained_loading_drain_discards": sum(r["event"] == "loading_drain_discard" for r in rows)}

    return {"schema": 1, "seed": lm["seed"], "matched_applied_ticks": len(shared),
            "application_mismatches": len(mismatch), "first_application_mismatch": next(iter(mismatch), None),
            "left_only_retained_ticks": len(li.keys() - ri.keys()),
            "right_only_retained_ticks": len(ri.keys() - li.keys()),
            "left": summary(lm, lr, li, ld), "right": summary(rm, rr, ri, rd),
            "scope": "Retained input application evidence only. Missing ticks may be evicted. HID delivery tick is observed separately from pad sampling. Equal input application does not certify simulation or actor synchronization."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left")
    parser.add_argument("right")
    args = parser.parse_args()
    print(json.dumps(compare(args.left, args.right), indent=2))


if __name__ == "__main__":
    main()
