# Twenty Wonder improvements

This follow-up improves the two-edition launcher and prepared Wonder runtime. It preserves separate saves, graphics settings and original-edition files. The supplied package remains the Product.120 executable examined in the edition notes; this work does not establish a native Bellabel payload.

## Launcher

1. **Preparation checks before Play.** Missing or empty game code, executable analyses, shader metadata and RomFS are reported before launching. Native settings remain accessible to repair incomplete preparation. Checks follow the adjacent game-library override used by developer builds.
2. **Overlapping-root protection.** Parent/child data roots are rejected as well as identical roots, preventing one edition's tree from containing the other's mutable data.
3. **Portable configuration paths.** Relative paths resolve against the configuration file, independent of the working directory. UTF-8 BOM files are accepted; malformed configuration shapes and NUL paths are rejected explicitly.
4. **Recoverable configuration writes.** A unique temporary file is flushed and replaced atomically; the previous readable configuration is retained as `library.json.bak`. Failed replacement preserves the current file and removes the temporary file.
5. **Window-free installation checks.** `python tools/wonder_library.py --check` prints JSON preparation checks and returns a nonzero exit status for missing preparation.
6. **Shareable preparation reports.** Export through the library or `--diagnostics report.json`. Reports contain fixed preparation labels, with no installation paths, usernames, save contents, environment values or room codes.
7. **Per-edition folder shortcuts.** Open each edition's mods or active profile's user-save container directly. Missing folders produce a readable error instead of opening an unrelated location.
8. **Visible process outcomes.** Normal closure and error exits remain visible. Windows exceptions include their hexadecimal code. Preparation is checked again after a native settings process closes.
9. **Consistent busy controls.** Launch/settings/installation-selection buttons are disabled across both entries while the library's game process runs. The launch guard remains in place as well.
10. **Reload without restarting the library.** Reread external configuration changes and refresh preparation status. Reload does not replace the active game's configuration. The library scrolls at smaller window heights so utility controls remain reachable.

## Mod loading and reports

11. **One indexed lookup for ordinary misses.** Normal roots share a file-name index instead of requiring one hash-table lookup per mod. Candidate order and removal fallback remain intact.
12. **Relative RomFS names only.** Overlay lookup rejects absolute paths, parent traversal, embedded NUL and drive/stream names, including filesystem fallback paths.
13. **Bounded indexing.** Deep trees switch to filesystem fallback without recursively indexing the entire tree. Excessive top-level mod inventories fail with an explicit limit instead of growing without bound.
14. **Platform-correct conflict names.** Windows reports ASCII case-insensitive conflicts; Linux preserves case-sensitive paths, matching runtime lookup.
15. **Actual load-order reporting.** Conflicts follow full folder names, including the leading disabled marker when an aspect profile activates that folder. Display names no longer accidentally reorder those mods.
16. **Effective aspect selection and incomplete inspection.** Reports distinguish the stored folder toggle from whether the selected aspect activates it. Empty RomFS and skipped nested links produce explanations; partial inspection is not presented as a complete conflict report.

## LAN preparation

17. **Generated game-code identity.** The fingerprint includes the selected generated game library, following explicit override, adjacent-library and data-root precedence. Windows DLL and Linux shared-library names are handled separately. Peers must share identical runtime/game-code binaries; rebuilds can require copying the same generated library to both PCs.
18. **Directory bounds as well as file bounds.** Directory-only and deeply nested mod trees cannot evade the preparation limit. Active mods retain the existing byte/file bounds, and individual compatibility files are bounded too.
19. **Cancellation while hashing.** The lobby cancellation request is checked between read blocks and mod-tree entries, rather than waiting for every compatibility file to finish hashing.
20. **Read-time mutation detection.** Size, modification time and read length are checked around each file hash. Growth and ordinary same-size modifications during the read fail preparation, allowing a retry after files stop changing. This is not a filesystem snapshot or adversarial tamper-proofing.

The fingerprint domain changed, so both peers need this update. Input matching still does not certify synchronized actor positions, RNG or complete world state. RomFS changes remain version-specific; arbitrary ExeFS patches are outside the mod manager's scope.

## Validation

All 61 non-GPU CTest targets passed, including expanded overlay/catalog/fingerprint tests. The library has 21 passing unit tests. Negative cases cover incomplete installs, overlapping roots, malformed configuration, failed atomic replacement, wrong generated game code, cancellation during reads, read-time mutation and directory-only deep mod trees.

Actual Tk callback tests covered both configuration entries, cross-edition busy controls, exception-code reporting, reload, both mod/save folder shortcuts and redacted report export. A visible 900-by-650 window capture was inspected, including the scrollable layout. Native configuration on the final runtime produced eleven captures; the mod page showed the active 22-file fixture. Early desktop-crop and PrintWindow attempts were unusable as UI images; only the subsequent foreground-verified capture is counted as visual evidence.

The standalone 7,200-frame gameplay regression produced twelve fresh nonflat captures, non-silent ten-second stereo PCM and intentional exit 0. The first course was visually inspected. This smoke preceded a final library-selection correction; the final runtime was then exercised through course gameplay in both LAN modes.

The final native LAN runs used independent movement/jump inputs and intentionally different disabled-mod inventories. Both peers reached the course at 60 Hz with no buffering and at 30 Hz with one buffered frame. Retained traces matched across 601 and 1,401 committed ticks respectively, with zero valid actor-position pairs. Both runs had no forced shutdown or fatal logs. Each ended through one peer's normal shutdown, followed by the remaining peer's disconnect exit 2. An earlier pre-correction 60 Hz course run also passed; its result is separate from the final-source run.

The asset-free 24-root / 4,000-miss CPU benchmark returned identical answers: reference filesystem probing 2,916,606 microseconds, indexed lookup 6,779 microseconds, index construction 3,656 microseconds. This compares filesystem probing with the current index, not the previous indexed release, and is not a new game-FPS or loading-time claim. See [sanitized results](data/wonder-twenty-improvements-20261119.json).

These checks do not certify full-game rendering, full actor/RNG/world synchronization or physical multi-PC LAN behavior. Linux naming/case branches have source and conditional test coverage but were not run on Linux in this Windows batch. The earlier first-clear/re-entry deswizzle fault remains unproven fixed.
