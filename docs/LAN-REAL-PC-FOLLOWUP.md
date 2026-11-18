# Real-PC LAN follow-up, 2026-11-18

## Observed failure and current scope

The first reported two-PC r8 test connected two independent native Wonder instances. The players reported a Random selection producing Mario on one PC and Toad on the other. Treat that as an unresolved simulation discrepancy. Transport connection and the earlier manually selected Mario/Blue Toad route do not establish whole-game synchronization.

The local log showed roughly 18–22 base FPS across several intervals. LAN barrier waits were about 36–39 ms in earlier intervals and about 18–27 ms later. These include the other machine's simulation/rendering and synchronization; they are not network RTT. The later snapshot had 4x optical-flow frame generation enabled. Other steady intervals compiled no pipelines, so shader compilation alone does not explain the sustained low base FPS. Render, fence and presentation time also remain material. No measured performance gain is claimed by the diagnostic changes below.

The player's saved settings had the low-VRAM profile off and pipeline draw skipping on. The existing launcher Performance preset selects 720p, compressed textures, a 60 FPS cap, and disables frame generation and draw skipping. Compare that preset against the current settings after restarting; then compare solo and LAN using identical settings and the same scene. A cap is not a guaranteed frame rate. Do not overwrite settings, replace executables, or run competing GPU tests during the user's session.

The position probe covers one detected course player, not character IDs, other players, enemies, RNG, or world state. Later live logs repeatedly reported no usable player object and a zero partial hash. The next runtime explicitly logs unavailable position coverage instead of letting a synchronized-input message imply verified game state. Host-time tick reads remain a possible source of nondeterminism requiring investigation; replacing all timers with a frozen per-frame clock without testing could break guest polling loops.

## Source fixes and diagnostics

- Forward Windows key/character messages whenever the launcher or overlay wants keyboard input. Previously the F1 overlay-open flag excluded the launcher's LAN form.
- Populate the visible host-address field when using the existing startup hook.
- Log asynchronous lobby failures in addition to displaying them.
- Respect explicit controller icon preferences in launcher hints; automatic selection can find a physical controller assigned after the keyboard slot. Draw the four PlayStation face shapes directly, with matching layout measurements.
- Keep a bounded ring of 4096 LAN exchanges in memory: tick, guest frame, role, seed, submitted local pad, all committed pads, wait duration, partial hash, available position and scene. No file writes occur in the per-frame recorder. Failed exchanges are retained too.
- Write `data/lan-input-trace.csv` on orderly Windows/Linux exit or a detected session failure. Forced termination, a hard crash or power loss may leave no trace. A later session replaces the file. It includes inputs and session seed, but no pairing code, save contents, IP address or user name; review before sharing.

After reproducing in a build with this change, quit both clients close together and preserve each trace under a different name. Compare with:

```text
python tools/perf/compare_lan_traces.py host.csv client.csv
```

The analyzer aligns overlapping simulation ticks, identifies the first different committed input/seed or available position, reports position coverage, and summarizes barrier waits. Exit 0 means no difference in the **observed fields**, not verified character/world sync. Exit 1 means an observed difference; exit 2 means the traces cannot be compared. Position tolerance matches the transport guard (1/1024 world unit). Character IDs and canonical multi-actor state still need reverse engineering and coverage.

This approach was informed by [Co-op Anything's flight recorder](https://github.com/cranebeaker/coop-anything/tree/main/tools/flight). No remote scripts, MCP server, network relay or binaries were installed, and no implementation was copied from that project. Its world-state/first-divergence approach is useful; its generic tools cannot supply Wonder's missing character and actor hooks automatically.

## Validation and remaining work

The Windows source build and 33 non-GPU CTest targets passed. The subsequent PlayStation shape drawing change also built successfully; its native visual review remains pending. Ring-buffer tests cover eviction, chronological order, failed exchanges, full-width buttons, remote sticks and reset. Analyzer tests cover missing probe coverage, first remote input mismatch, float tolerance, nonfinite positions and nonoverlapping recordings.

Native UI review, paired trace capture, Random-selection reproduction and solo/LAN performance comparisons remain pending while the user's existing r8 game is running. These changes do not modify that installation or either PC's running session. They are not included in the previously delivered r8 ZIP. The Mario/Toad discrepancy, general shader flicker, slow LAN play, configurable profiles/nicknames and additional gameplay mods remain unfinished.
