# Playing Wonder on PC

[Project overview](../README.md) · [Multiplayer](MULTIPLAYER.md) · [Game editions](WONDER-EDITIONS.md)

SuperWonders needs a prepared runtime, a compatible generated game library and your own game assets. The repository alone is not a playable installation.

## Prepare an installation

The separate [upstream Windows GUI Builder](https://github.com/vfjoo888/SuperWonders/releases) bundles portable build tools. Its published packages may differ from this fork's current source.

1. Extract the builder into a writable folder and run `Start WonderBuilder.cmd`.
2. Select your own extracted ExeFS folder containing `main` and `sdk`, and the matching RomFS folder.
3. Choose an output folder and select **Build game**.
4. When preparation completes, run `wonder.exe` in the output folder.

No game files or firmware are supplied. Keep generated game code, shaders, libraries and assets private; do not upload a prepared installation to an issue or share it as a release.

The primary upstream version is **1.0.0**. The inspected **Product.120** update has separate source-identity checks and runtime support. Other updates and complete Switch 2 Edition content are not automatically compatible. See [edition evidence](WONDER-EDITIONS.md) before mixing executables or assets.

## Choose an edition

`Start Wonder Library.cmd` opens the shared edition library on Windows with Python 3 and Tk. Alternatively, run:

```text
python tools/wonder_library.py
```

Configure a prepared runtime executable and a **separate data folder for each edition**. The library selects an existing installation; it does not turn an arbitrary NUP into a playable build. It does not migrate saves between editions. Closing the library leaves a launched game running.

## First launch and controls

Let shader preparation finish. The runtime saves its Vulkan pipeline cache for later launches, but previously unseen shader variants may still cause first-use stutter.

See [shader preparation and stutter](SHADER-PREPARATION.md) for what the caches
prepare, why new variants can appear and which evidence helps investigate a hitch.

The PC settings menu opens with **Esc**, **F1**, or the controller's **Minus+Plus** chord. It contains Video, Audio, Controls, Overlay and System settings. The game's own pause menu and gear icon are separate from this PC menu.

Connect controllers before selecting the game's player count. In **F1 → Controls**, select a button-layout preset and check each player row. **Keyboard as player 1** reserves the keyboard for the first player and places gamepads into the remaining slots. Up to four local controller slots are available; physical controller combinations should be checked on your hardware.

The launcher now has a dedicated **Controls** page. In the in-game menu, choose
**Controls**, then **Keyboard remapping**, **Controller remapping**, or
**Players & devices**. Keyboard bindings appear first; secondary choices are under
**Keyboard options** below the binding list. Esc cancels an active remapping
capture. Release the controller button before navigating after a new binding.
Local co-op shortcuts open **Players & devices** directly; the general bindings
shortcut opens **Keyboard remapping**. Changing keyboard mode replaces its key
bindings with that mode's defaults. Comfort shortcuts live in launcher **Settings**,
while **Mods** contains installed game mods. **Enable sound & full rumble** unmutes
sound and sets vibration to 100%; it does not restore an earlier custom strength.
The quick **Show/Hide performance HUD** action toggles the whole panel, preserving
its individual metric selections.

See the [interface refresh](UI-REFRESH-2026-11-20.md) for the changes and test scope.

## Graphics and smoothness

Start with ordinary rendering and a modest internal resolution. Change one setting at a time if you are investigating stutter or glitches.

- Use the FPS/frametime overlay to distinguish sustained rendering cost from occasional loading or shader stalls.
- FSR 1 scales a lower internal image; it does not remove CPU bottlenecks.
- Optional frame generation can make motion look smoother while adding artifacts or latency. Record whether it is enabled when reporting performance.
- The Low VRAM profile can reduce decoded texture storage with a possible image-quality tradeoff. Its cleanup threshold is not a hard limit on live GPU memory.

A controlled Product.120 test preserved the in-game pause/gear background blur across ten close/reopen cycles with frame generation and pipeline draw skipping disabled. This does not cover every scene or settings combination. Dark stage-map artifacts during movement remain under [active investigation](WONDER-GRAPHICS-INVESTIGATION.md); no general graphics fix is certified.

## Mods

The launcher's **Mods** page inspects RomFS replacement folders, enabled state and file conflicts. The first enabled mod wins a conflicting path; changes take effect after restarting. This is not an arbitrary executable-patch installer or a built-in online mod marketplace.

Use mods that match your game version. Establish a clean baseline before diagnosing a crash or visual issue, and preserve your saves before changing a mod set. For multiplayer investigations, every participating installation must use the same content and enabled mods.

## Updating an existing installation

Keep your output/data folder, generated game library, saves and useful caches. When using the upstream builder, retain the builder folder and update its files from the newer package, then build to the same output location so unchanged work can be reused.

A runtime-only update can reuse an already prepared library **when its ABI and game version remain compatible**. A changed library ABI or game executable can require rebuilding; not every update avoids compilation. Keep a backup before replacing a working runtime. Where supplied, update `wonder-features.json` together with its matching executable: experimental launchers verify its executable hash.

## Playing together and reporting problems

Start with [the multiplayer guide](MULTIPLAYER.md). One-host local co-op, shared-input LAN, the new native networking experiment and Steam development are distinct paths with different verification limits.

For a report, note the runtime revision, edition, GPU/driver, settings, active mods and exact sequence that failed. A short screenshot or clip helps for visual issues. Share sanitized diagnostics, never game files, saves, account data or private connection codes. See [contributing](../CONTRIBUTING.md).
