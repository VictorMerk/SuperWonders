# PC quality and multiplayer work

Initial investigation: 2026-11-16, upstream `c9fff40c9fc8aad348b5c001d6d1490bc2311e2e`.
Reference: [mkw-recomp](https://github.com/adriadam10/mkw-recomp/tree/dd9297545beb9d2c90170eea93a01030961bf7cf).

Latest follow-up: [one-folder setup and corrected Windows controller presets](PC-PREVIEW-2026-11-17-R8.md).
Source update: [official upstream merge, opt-in ultrawide and preserved fork behavior](UPSTREAM-SYNC-2026-11-17.md).
Graphics follow-up: [Vulkan synchronization fixes and the r7 LAN test preview](PC-PREVIEW-2026-11-17-R7.md).
Earlier follow-up: [pause-background fix, 2x–8x interpolation, controls, diagnostics and mod management](PC-PREVIEW-2026-11-17.md),
including native screenshots and explicit test/measurement limits. The earlier measurements below describe the
initial preview; they are not guarantees for the later build or other hardware.

## Current checkpoint (2026-11-20)

The sections below preserve earlier experiments and measurements. They do not
establish the status of the latest build. Two later physical-PC sessions exposed
substantial gameplay desync in the shared-input LAN mode, despite matching input
traces. That mode remains experimental and is unsuitable for claiming synchronized
course completion.

The current follow-up adds a separate, opt-in Windows Product.120 native UDP/NIFM
provider, guarded before guest imports are linked, plus isolated developer launchers
and a Steam AppID480 API-bootstrap prototype. The subsequent renderer and input
follow-up passed 118 CPU regressions, three GPU tests and 137 native probe cases.
Real native room discovery, two-PC gameplay, Steam Remote Play Together and FPS
improvements are not established. The next physical test is deferred until both
players' PCs are available; Steam testing also needs the official SDK runtime.

The README, [playing guide](PLAYING-WONDER.md), [multiplayer guide](MULTIPLAYER.md)
and [synchronization investigation](WONDER-SYNC-INVESTIGATION.md) describe current
behavior and remaining checks. The stage-map shadow artifact has now been
reproduced in a controlled native sequence. A default-off Deferred texture-binding
candidate improves which shadow handle replay reads, but both comparison clips
were bright: a visual cure and FPS gain remain unproved. See the
[graphics investigation](WONDER-GRAPHICS-INVESTIGATION.md). Repeated gear-menu
blur stayed intact in one non-frame-generated case; the frame-generated case
needs separate verification.

## Requested co-op interaction extension

Player-to-player collision and carrying are requested features, not completed
runtime options. Initial Product.120 analysis found collision-table and carry/throw
graph names, but those names alone do not establish an arbitrary-player contact or
attachment ABI. The implementation needs verified collision filtering, attach and
detach behavior, state/animation transitions, death/scene cleanup and consistency
across participating peers. Existing character-specific riding is a useful baseline
for a later controller test. No guessed player-position or velocity writes are
installed. The feature must be tested separately from repairing LAN synchronization.

Private two-controller contact tests confirmed independent Mario/Toad control.
Changing only one collision-table cell did not establish solid contact; a second
variant caused both actors to disappear followed by a death transition. Those
experimental packs are not installed or shipped. Yoshi riding is being checked
as an existing character-specific baseline; it does not implement arbitrary-player
carrying. Input/capture timing must overlap before judging a jump or attachment.

## Evidence from the reported sessions

The Windows logs identify an RX 570 using the AMD Vulkan 1.3 driver. They show:

- An access violation immediately after `friend:u cmd 0 not implemented (stub ok)`, with the guest return address
  in `nn::friends::GetPlayHistoryRegistrationKey`. This is evidence for a missing service interface, not a proven
  explanation for every crash.
- Repeated nonzero `draws skipped while compiling` counts. Missing geometry during compilation can explain some
  flashing; image aliasing, shader correctness and synchronization remain separate candidates.
- First-run shader preparation took 118.2 seconds for 12,557 programs. The next run loaded an approximately 88 MB
  driver cache and prewarmed 2,216 recorded pipelines in 1.39 seconds. Recompiling everything every launch is not
  supported by these logs. Do not delete working caches as a routine troubleshooting step.
- ASTC textures expanded to RGBA8 with the Low VRAM profile disabled. Low VRAM mode and 720p internal resolution
  are useful settings to measure on this machine; neither guarantees a specific frame rate.
- The installed settings already enabled FPS, 1% lows, game speed and the frametime graph.

## Initial implementation

- Return an actual child interface for friends-service creation. Unsupported operations return a failure instead
  of fake success with missing output. This does **not** enable online play.
- Preserve all graphics draws by default. F1 > Video > Skip draws while compiling explicitly opts into the existing
  asynchronous omission behavior. Keeping draws can cause first-use compilation pauses. Startup preparation and
  background GPL optimization remain available.
- Normalize ballots and indexed shuffles to logical 32-lane warps in prepared SPIR-V, including on the RX 570's
  64-lane subgroups. This adapts the principle from [the owner's mkw-recomp PR #458](https://github.com/adriadam10/mkw-recomp/pull/458)
  to this backend. It avoids regenerating the player's shaders. The preparation marker is versioned so the three
  changed compute programs are prepared again; later launches retain their normal cache behavior.
- Provide `tools/collect_diagnostics.ps1 -GameRoot <installation>` to preserve logs, settings, hardware information
  and cache metadata in a new local directory. It excludes game content and saves. Logs contain local paths.

## Implemented and checked locally (2026-11-17)

- The friends-menu path that previously crashed returns an offline error. Two in-game
  exercises reached commands 10700 and 10120 without the original access violation.
- RX 570 subgroup-64 GPU regression: old warp operations fail 64/128 invocations;
  patched operations fail 0/128. All three affected installed shaders validate.
- Menu, map and Scram, Skedaddlers! sessions report zero draws skipped while compiling.
  This addresses the identified omission and subgroup faults; further visual defects
  require their own reproductions.
- Four SDL player slots have independent input, histories, connection events and rumble.
  Keyboard + controller co-op was exercised with Mario and Blue Toad. Virtual-pad tests
  cover four pads, disconnect/replacement and keyboard reservation.
- In-game PlayStation and keyboard prompts were visually checked. Generated OFL button
  fonts also cover Xbox and Nintendo. Prompt/font binding changes require a restart
  because the game caches its font atlas.
- Launcher and F1 pages share settings. Performance profiles, section shortcuts, device
  rows, prompt selection and save backup/open-backup actions are implemented. Backups
  preserve previous snapshots and exclude game assets.
- Fast shader loading caches already-read translations in an append-only local journal.
  Later runs read that cache sequentially. Missing, stale or corrupt records fall back
  to original shaders. A full pack is an optional launcher action: the full shader set
  took approximately 909 seconds to prepare on the tested drive, so it never blocks
  normal first launch. Caches use additional disk and up to 512 MiB of payload memory.
- Optional x2 optical-flow frame generation runs on the RX 570. Synthetic GPU probes
  exercise actual interpolation and resizing; the 720p midpoint error was 0.404 versus
  approximately 42 for either original test picture. The in-game pacing corrections
  reached approximately 51-53 game FPS and 100 displayed FPS in a stationary Scram
  scene at 720p. This is one machine/scene measurement, not a general performance
  guarantee. Frame generation remains off by default and can produce artifacts.

## Earlier shared-input LAN experiments (2026-11-17)

The native launcher now provides a cancellable host/join lobby with private IPv4
addresses, pairing codes, 2-4 peer slots and authenticated host-save initialization.
Each PC runs a separate native Wonder instance and mounts its own save profile.
The runtime reuses the launcher's live connection instead of spawning a disconnected
process. Applying settings cannot move the primary local device into another slot.
See [the multiplayer guide](MULTIPLAYER.md) for operation and exact limits.

Two independent instances reached Scram with real co-op characters controlled from
each peer. A pre-fix trace found small position differences on 30/15,605 common
frames. The barrier now follows Wonder's complete framework frame. The guard uses
a stated 1/1024-world-unit tolerance instead of demanding bitwise float identity.
The later launcher-path run recorded over 34,000 matching course-position samples
at nine-digit precision, including movement, deaths/restarts and a client constrained
to two logical CPUs. Its normal save stayed separate from the host's session copy.

LAN remains experimental: the current guard samples one player, not every world
actor, RNG stream and physics object. Separate physical PCs, network conditions and
more courses still need verification. Nintendo online services remain offline.

The complete configured test suite passes 20/20, including native TCP 2/4-peer,
state-divergence, save-transfer/path-validation and lobby cancellation/retry tests,
plus GPU warp and ASTC tests. The frame-generation boot probe passes five real GPU
cases, including interpolation, resizing and byte-identical output for a still image.

The installed runtime passed its launcher/settings tour without changing the warm
recipe cache. Cache writes use separate staging directories and atomic replacement;
failed writes preserve the old cache. The recorded-pipeline shader reader now uses
the same pack/journal as normal shader loading. On the RX 570, the measured warmup
changed from 4,951 recipes in 22.754 seconds to 5,020 recipes in 2.815 seconds, with
zero skipped recipes. This measures that stage, not the entire boot/level load.
The installed executable also reached Scram and ran at approximately 60 FPS in the
stationary course check with frame generation off, at 720p/Low VRAM. The world map
was slower; a 60 FPS cap is a target, not a guarantee for every scene.

FSR 1 is available; temporal FSR/DLSS remain separate integrations requiring suitable
motion/depth inputs and supported hardware. No DLSS control is advertised for an
implementation that is not present.

## Applicability of the owner's Mario Kart World PRs

- [#458](https://github.com/adriadam10/mkw-recomp/pull/458): the cube-image flag correction is already present here:
  `nvn_image.cpp` enables cube compatibility only for NVN targets 8 and 9. The logical-warp bug does apply: a scan
  of the installed 12,751 prepared modules found subgroup ballots/shuffles in three compute shaders. Their ballot
  reads selected word zero, and indexed shuffles lacked the caller's logical-warp base. The new `patch_warp32`
  corrects those operations at module creation. Integer sampled-texture patching already exists here, with a
  different descriptor/translator design; the Mario Kart integer-image patch is not copied blindly.
- [#459](https://github.com/adriadam10/mkw-recomp/pull/459): sparse-tail/VMA ownership changes target a different
  allocator and sparse-image implementation. The principles are relevant to auditing this backend, but those
  exact changes do not apply to its separately allocated images.

The standalone native GPU regression uses original synthetic shader code, without game assets. On the RX 570 it
reports subgroup size 64: the old operations produce 64 incorrect invocations out of 128, and the patched module
produces zero. Khronos SPIRV-Tools validates the patched synthetic shader and all three affected installed modules.
This establishes the subgroup correction, not which particular visible Wonder effect each shader implements.
