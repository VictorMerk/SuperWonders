# Wonder LAN preparation and session-save follow-up

LAN preparation now checks for detectable changes across the complete content/save copy pass, and cancellation covers more of that work. Session-save restoration removes its own partial destination on cancellation or write failure. Existing user profiles are never chosen as restore destinations.

## Changes

- **Complete content manifest recheck.** Earlier core files are checked again after later files finish hashing. Active mod names, selected RomFS roots, file inventories, sizes and modification times are collected again before preparation succeeds. The selected game-library location is checked again too. Ordinary additions/removals and metadata changes during preparation produce a retryable error.
- **Cancellable mod discovery.** Cancellation reaches the top-level mod list and nested RomFS discovery, in addition to hashing and active-file enumeration. Normal gameplay's indexed lookups do not invoke these preparation callbacks.
- **Bounded save traversal.** Session saves retain the 64 MiB and 4,096-file archive limits, with an additional 16,384-entry / 64-level traversal bound. Directory-only trees cannot evade the file limit indefinitely.
- **Cancellable save reads and writes.** Copies run in 64 KiB blocks with cancellation checks. The host's copy stage has its own lobby status message.
- **Stable save inventory.** The complete save-file inventory and metadata are checked again after capture. Adding a file after enumeration or changing an earlier save while another file is being copied fails capture instead of accepting the earlier inventory.
- **Partial-copy rollback.** Once restoration creates a new destination, cancellation or an I/O error removes that partial destination. Pre-existing destinations are rejected before writing. Cleanup does not intentionally remove a replaced linked root; cleanup errors are reported.
- **Long Windows parent paths.** Extended absolute paths are applied to the save root and joined nested file paths. This supports long Unicode parent folders while retaining the archive's existing portable filename rules.

The stable-content fingerprint encoding remains domain 4, and the session-save archive remains `SWLSAVE1`. A known-vector regression checks the fingerprint encoding. These are metadata consistency checks, not an atomic filesystem snapshot: close other game instances and finish editing mods before opening a room. Restoring a session copy is separate from modifying the host's saved progress.

The combined build also includes main's protocol-6 changes from `406aadf`: buffered input sampling happens after the previous exchange completes, and a redundant per-tick acknowledgement was removed. Both peers need matching compatible runtime/game-code builds and protocol 6. See [the transport changes and their separate measurements](LAN-PERFORMANCE-2026-11-19.md); this follow-up does not introduce a new latency or FPS measurement.

## Validation scope

The combined build passed all 62 non-GPU CTest targets in 28.59 seconds. Regressions cover cancellation after partial writes, rollback preserving an unrelated save, multi-block byte-for-byte roundtrips, long Unicode parent paths, additions after enumeration, earlier-file mutation during later hashing, and a stable fingerprint reference vector. A further focused regression passed for a new adjacent game-library selection appearing during preparation.

Both native pairs reached the first course and exercised independent movement/jumps. The 60 Hz unbuffered run reached at least 3,060 ticks and matched 601 committed input pairs. The 30 Hz / one-buffered-frame run reached at least 3,057 ticks and matched 1,401 committed pairs. The latter deliberately held one client's course-music loader for 3,000 ms; native logs confirm the hold, protocol 6 and the adopted timing. Both characters rendered in the retained course captures, which were visually inspected. There were no forced shutdowns or fatal/crash logs. Each pair ended with a normal shutdown request on one side and disconnect exit 2 on the remaining peer. All four captured ten-second stereo PCM streams were non-silent; hardware output was muted.

The installed Switch 2 slot uses the exact native-tested runtime snapshot `6d26d9e`, with copy hashes recorded locally. Both library configurations passed preparation/launch-spec checks; manual save/settings hashes were unchanged and the prior runtime was retained. The original-edition installation was not replaced. See [sanitized results](data/wonder-lan-preparation-20261119.json).

Native tests use isolated copies of a prepared Product.120 save and same-PC loopback. They cannot establish physical multi-PC LAN synchronization, all actors/RNG/world state, audible device quality or full-game stability. No Product.120 actor-position layout or native Bellabel payload has been established. The older first-clear/re-entry deswizzle fault remains unproven fixed.
