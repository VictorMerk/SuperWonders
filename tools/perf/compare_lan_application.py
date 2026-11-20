"""Compare bounded input-application evidence, without certifying game state."""
import argparse
import csv
import json
from pathlib import Path

PAD_FIELDS = ("buttons", "lx", "ly", "rx", "ry")
COMPARE_FIELDS = ("event", "players") + tuple(
    f"p{p}_{field}" for p in range(4)
    for field in ("loading_event",) + PAD_FIELDS)
APPLICATIONS = {"apply", "loading_neutral_apply"}


def read(path, *, include_fates=False):
    """Stream CSV; keep compact applied ticks and HID sample identities only."""
    metadata = {}
    observations = {}  # invocation -> retained enter/leave; local only
    files = []
    applied = {}
    fates = {}  # tick -> event -> (frame, sequence); only explicit retained evidence
    duplicates = []
    hid = {}  # (player, sampling number) -> (pad, tick, frame, sequence, changed)
    stats = dict(retained_events=0, retained_framework_calls=0,
                 retained_framework_calls_without_present=0,
                 first_retained_framework_call_without_present=None,
                 retained_loading_drain_discards=0, duplicate_applied_count=0,
                 retained_hid_reads=0, repeated_identical_hid_sample_reads=0,
                 hid_sample_payload_change_observations=0,
                 hid_samples_with_changed_payload=0,
                 first_hid_sample_payload_change_observation=None,
                 hid_commit_identity_mismatches=0, first_hid_commit_identity_mismatch=None)

    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        def data_lines():
            for line in stream:
                if line.startswith("#"):
                    if line.startswith("# schema="):
                        metadata.update(part.split("=", 1) for part in line[2:].strip().split(","))
                else:
                    yield line

        for row in csv.DictReader(data_lines()):
            stats["retained_events"] += 1
            event = row["event"]
            if include_fates and event in APPLICATIONS | {"loading_drain_discard", "loading_exchange"}:
                tick = int(row["tick"])
                if not 0 <= tick < 1 << 64:
                    raise ValueError("Invalid committed tick in publication evidence")
                fates.setdefault(tick, {}).setdefault(event, (
                    int(row["frame"]), int(row["sequence"]) if row.get("sequence") else None))
            if event in {"framework_enter", "framework_exit", "framework_throw", "loading_file"} and metadata.get("observation_identity") == "local_invocation_v1":
                def number(field, maximum=(1 << 64) - 1):
                    if row.get(field) in (None, ""):
                        raise ValueError("Missing observation field: " + field)
                    value = int(row[field])
                    if not 0 <= value <= maximum:
                        raise ValueError("Invalid observation field: " + field)
                    return value
                invocation = number("invocation")
                parent = number("parent_invocation")
                thread = number("thread_ordinal", 64)
                caller_module = number("caller_module", 2)
                offset = number("caller_offset")
                resource = number("resource_hash")
                sequence = number("sequence")
                if not caller_module and offset:
                    raise ValueError("Unknown caller must not carry an address/offset")
                if event == "loading_file":
                    files.append((invocation, thread, sequence))
                else:
                    if not invocation:
                        raise ValueError("Framework observation requires an invocation")
                    part = "enter" if event == "framework_enter" else "leave"
                    recorded = observations.setdefault(invocation, {})
                    if part in recorded:
                        raise ValueError("Duplicate framework observation identity")
                    recorded[part] = (thread, sequence, parent, event)
            if event in APPLICATIONS:
                tick = int(row["tick"])
                if tick in applied:
                    stats["duplicate_applied_count"] += 1
                    if len(duplicates) < 20:
                        duplicates.append(tick)
                # Avoid retaining the CSV dictionary and unused per-event strings.
                applied[tick] = (int(row["frame"]), tuple(
                    row[field] if field == "event" else int(row[field])
                    for field in COMPARE_FIELDS))
            elif event == "framework_frame":
                stats["retained_framework_calls"] += 1
                if int(row["frame"]) == int(row["frame_before"]):
                    stats["retained_framework_calls_without_present"] += 1
                    if stats["first_retained_framework_call_without_present"] is None:
                        stats["first_retained_framework_call_without_present"] = int(row["framework_sequence"])
            elif event == "loading_drain_discard":
                stats["retained_loading_drain_discards"] += 1
            elif event == "hid_delivery":
                player = int(row["player"])
                if not 0 <= player < 4:
                    raise ValueError("HID delivery player must be 0..3")
                sample = int(row["sampling_number"])
                pad = tuple(int(row[f"p{player}_{field}"]) for field in PAD_FIELDS)
                stats["retained_hid_reads"] += 1
                previous = hid.get((player, sample))
                sequence = int(row["sequence"]) if row.get("sequence") else None
                tick, frame = int(row["tick"]), int(row["frame"])
                if metadata.get("hid_identity") == "exposed_commit_plus_one" and (
                        not 0 <= tick < (1 << 63) - 1 or sample != tick + 1):
                    stats["hid_commit_identity_mismatches"] += 1
                    if stats["first_hid_commit_identity_mismatch"] is None:
                        stats["first_hid_commit_identity_mismatch"] = dict(
                            sequence=sequence, player=player, sampling_number=sample, committed_tick=tick)
                changed = previous[4] if previous else False
                if previous:
                    if previous[0] == pad:
                        stats["repeated_identical_hid_sample_reads"] += 1
                    else:
                        stats["hid_sample_payload_change_observations"] += 1
                        if not changed:
                            stats["hid_samples_with_changed_payload"] += 1
                        changed = True
                        if stats["first_hid_sample_payload_change_observation"] is None:
                            stats["first_hid_sample_payload_change_observation"] = {
                                "player": player, "sampling_number": sample,
                                "previous_pad": dict(zip(PAD_FIELDS, previous[0])),
                                "current_pad": dict(zip(PAD_FIELDS, pad)),
                                "previous_observed_tick": previous[1], "observed_tick": tick,
                                "previous_frame": previous[2], "frame": frame,
                                "previous_sequence": previous[3], "sequence": sequence}
                hid[player, sample] = (pad, tick, frame, sequence, changed)

    if metadata.get("schema") != "1":
        raise ValueError("Expected application trace schema 1")
    if not {"seed", "role", "omitted"} <= metadata.keys():
        raise ValueError("Application trace metadata must include seed, role and omitted")
    phase = [tick for tick, (_, values) in applied.items()
             if values[0] == "apply" and any(values[2 + p * 6] for p in range(4))]
    observation_contract = metadata.get("observation_identity") == "local_invocation_v1"
    matched = sum("enter" in pair and "leave" in pair and
                  pair["enter"][0] == pair["leave"][0] and
                  pair["enter"][1] < pair["leave"][1] and
                  pair["enter"][2] == pair["leave"][2]
                  for pair in observations.values())
    contained_files = 0
    for invocation, thread, sequence in files:
        pair = observations.get(invocation, {})
        if (thread and "enter" in pair and "leave" in pair and
                pair["enter"][0] == thread == pair["leave"][0] and
                pair["enter"][2] == pair["leave"][2] and
                pair["enter"][1] < sequence < pair["leave"][1]):
            contained_files += 1
    stats.update(
        framework_observation_contract="local_invocation_v1" if observation_contract else "legacy_or_unknown",
        retained_framework_entries=sum("enter" in pair for pair in observations.values()) if observation_contract else None,
        retained_framework_leaves=sum("leave" in pair for pair in observations.values()) if observation_contract else None,
        retained_framework_exceptional_leaves=sum(pair.get("leave", (0, 0, 0, ""))[3] == "framework_throw" for pair in observations.values()) if observation_contract else None,
        retained_matched_framework_invocations=matched if observation_contract else None,
        retained_unpaired_framework_invocations=len(observations) - matched if observation_contract else None,
        retained_loading_file_observations=len(files) if observation_contract else None,
        retained_loading_files_in_framework_pair=contained_files if observation_contract else None,
        retained_loading_files_without_framework_pair=len(files) - contained_files if observation_contract else None,
        framework_observation_scope="Local invocation/thread ordinals are not cross-PC identities. File observations precede guest API return, not loader readiness. Missing pairs may reflect retention, incomplete export or an in-flight call; they do not prove a hang or simulation advancement. Guest module-relative caller offsets are not host callers; resource hashes are noncryptographic.")
    atomic_hid = metadata.get("hid_identity") == "exposed_commit_plus_one"
    if not atomic_hid:
        stats["hid_commit_identity_mismatches"] = None
    stats.update(role=int(metadata["role"]), omitted_events=int(metadata["omitted"]),
                 applied_ticks=len(applied), duplicate_applied_ticks=duplicates,
                 gameplay_applies_with_peer_loading_marker=len(phase),
                 first_gameplay_apply_with_peer_loading_marker=min(phase, default=None),
                 retained_unique_hid_samples=len(hid),
                 hid_identity_contract="exposed_commit_plus_one" if atomic_hid else "legacy_or_unknown",
                 hid_observation_scope=(
                     "Per-peer retained observations only. Same player/sample with changed delivered pad is a candidate inconsistency, not proof of desync. Gaps/eviction can hide changes. " +
                     ("This trace declares pad, committed tick, sampling identity and control metadata captured together. Sampling identity is exposed commit tick + 1; transport-only loading pumps do not publish samples. Presentation frame and log time are observed later. " if atomic_hid else
                      "Callback tick is observed after pad acquisition, not atomically paired; legacy/unknown traces cannot establish the new input snapshot contract. ") +
                     "Sampling-number offsets across PCs are not compared."))
    if include_fates:
        return metadata, applied, stats, fates
    return metadata, applied, stats


def publication_fate(events):
    """LoadingExchange followed by final neutral publication is not a discard."""
    published = APPLICATIONS & events.keys()
    discarded = "loading_drain_discard" in events
    if len(published) > 1 or (published and discarded):
        return "ambiguous", None
    if published:
        event = "apply" if "apply" in published else "loading_neutral_apply"
        return "published", events[event]
    if discarded:
        return "discarded", events["loading_drain_discard"]
    return "loading_transport_only", events["loading_exchange"]


def compare(left, right):
    lm, li, ls, lf = read(left, include_fates=True)
    rm, ri, rs, rf = read(right, include_fates=True)
    if lm["seed"] != rm["seed"] or lm["role"] == rm["role"]:
        raise ValueError("Expected opposite roles from the same session seed")
    shared = li.keys() & ri.keys()
    mismatch_count, first = 0, None
    for tick in sorted(shared):
        a, b = li[tick], ri[tick]
        different = [field for field, av, bv in zip(COMPARE_FIELDS, a[1], b[1]) if av != bv]
        if different:
            mismatch_count += 1
            if first is None:
                first = {"tick": tick, "fields": different, "left_frame": a[0], "right_frame": b[0]}
    fate_count, fate_first, compared_fates, ambiguous = 0, None, 0, 0
    for tick in sorted(lf.keys() & rf.keys()):
        a, b = publication_fate(lf[tick]), publication_fate(rf[tick])
        if "ambiguous" in (a[0], b[0]):
            ambiguous += 1
            continue
        compared_fates += 1
        if a[0] != b[0]:
            fate_count += 1
            if fate_first is None:
                fate_first = dict(tick=tick,
                                  left=dict(fate=a[0], frame=a[1][0], sequence=a[1][1]),
                                  right=dict(fate=b[0], frame=b[1][0], sequence=b[1][1]))
    return {"schema": 1, "seed": lm["seed"], "matched_applied_ticks": len(shared),
            "application_mismatches": mismatch_count, "first_application_mismatch": first,
            "matched_commit_fate_ticks": compared_fates,
            "commit_fate_mismatches": fate_count, "first_commit_fate_mismatch": fate_first,
            "ambiguous_shared_fate_ticks": ambiguous,
            "left_only_retained_fate_ticks": len(lf.keys() - rf.keys()),
            "right_only_retained_fate_ticks": len(rf.keys() - lf.keys()),
            "commit_fate_scope": "Explicit retained HID-publication, loader-discard or loading-transport evidence only. Final loading-neutral publication supersedes its transport event. Ambiguous or missing evidence is not inferred; publication does not prove guest consumption, simulation advancement or world synchronization.",
            "left_only_retained_ticks": len(li.keys() - ri.keys()),
            "right_only_retained_ticks": len(ri.keys() - li.keys()),
            "hid_identity_contracts_match": (
                True if ls["hid_identity_contract"] == rs["hid_identity_contract"] == "exposed_commit_plus_one" else
                False if "exposed_commit_plus_one" in (ls["hid_identity_contract"], rs["hid_identity_contract"]) else None),
            "left": ls, "right": rs,
            "scope": "Retained input application evidence only. Missing ticks may be evicted. Each peer's HID identity contract is reported separately; legacy/unknown traces do not establish atomic snapshots. Equal input application does not certify simulation or actor synchronization."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left")
    parser.add_argument("right")
    args = parser.parse_args()
    print(json.dumps(compare(args.left, args.right), indent=2))


if __name__ == "__main__":
    main()
