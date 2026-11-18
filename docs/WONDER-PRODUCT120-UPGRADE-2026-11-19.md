# Wonder package performance, mods and LAN follow-up

The shared Wonder library now opens each edition's native LAN, mod and graphics settings directly. It keeps separate data roots and removes inherited test/input/session overrides before starting a game. The original installation and saves are preserved.

## Runtime changes

- RomFS mod lookup indexes file names at boot instead of probing every enabled mod for every missing file. Precedence and aspect-selected profiles remain consistent with runtime loading. Unicode names, nested links and oversized trees retain filesystem fallback. Removing an indexed file falls through to later mods; adding new names or changing selection requires a restart.
- Runtime loading, the mod catalog and LAN compatibility checks share RomFS discovery, including Wonder-specific Atmosphere bundles. LAN fingerprints include enabled mod content, order, selected aspect, executable identity and runtime. Disabled mods and notes outside RomFS no longer prevent matching. Both peers need the updated runtime and matching effective content; this does not enable cross-edition multiplayer.
- Player-position probing selects only a verified executable layout. The supplied Product.120 build no longer scans original-version actor addresses. Input synchronization remains available, while position verification correctly stays pending. This is not a port of the Product.120 actor layout.
- The smoke runner can borrow its owner's existing reservation without releasing it, enabling uninterrupted A/B batches. Borrowing checks lease ID, owner PID and process start time.

Mods here mean RomFS replacements. ExeFS patches and original-version mods are not automatically compatible with the updated executable. Existing native mod controls handle selection and diagnostics; no new mod download service is included.

## Validation

All 61 non-GPU CTest tests passed, including new overlay, content-fingerprint, source-layout and launcher checks. Native configuration opened the mod page and recognized the enabled 22-file fixture. Library callback checks covered both edition roots.

Four native A/B/B/A runs compared the previously selected runtime with this update, including AMD changes integrated from main through `0bc82cf`. Each ran 7,200 guest frames, reached the same first-course scene, produced nonflat captures and non-silent stereo PCM, and shut down normally without fatal logs. Physical controllers were excluded. This does not certify audio-device quality.

On the RX 570, at 720p internal resolution, frame generation 2x and the same 60 Hz cap/settings, the original-picture rate over seconds 60–110 averaged 52.35 in A and 54.49 in B, approximately 4.1% higher. These are hidden-presented host timestamps from one idle course scene, not physical scanout or generated-frame FPS. Tail timings varied; this is not a whole-game performance or stutter guarantee. Pipeline compilation and draw skipping were zero in the measurement windows. The comparison includes previously merged AMD work, so it does not isolate the mod index's effect.

An asset-free CPU benchmark with 24 roots and 4,000 missing-file queries returned identical answers: reference lookup 2,730,521 microseconds, indexed lookup 5,479 microseconds, index construction 3,494 microseconds. This synthetic filesystem result is not a gameplay FPS multiplier.

Two native 60 Hz two-process loopback runs reached the first course. The latest used independent movement/jump inputs, the default source-aware probe guard and different disabled-mod inventories. Its retained traces matched across 701 committed ticks, with no valid position pairs. Both characters were visible and moved. Normal shutdown can cause the remaining peer to report disconnect (exit 2); no forced shutdown or fatal log occurred. An earlier attempt ended because the private test runner read an empty telemetry line; it is retained as a failed harness attempt, not counted as gameplay validation.

## Remaining limits

These are same-PC loopback checks, not a physical multi-PC LAN test. Matching input traces do not establish matching RNG, actors, world state or positions. Scheduling spikes remain. The 30 Hz buffered check is documented separately when complete; its initial connected run did not reach the course and is not a gameplay pass.

The supplied package's inspected executable is Product.120 using NVN/Maxwell. Native Switch 2/Bellabel payload support remains unestablished. The older first-clear/re-entry deswizzle fault has not been proven fixed; successful one-seed first clears and saved-course reloads do not close that issue. Full-game perfection is not claimed.
