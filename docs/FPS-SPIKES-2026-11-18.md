# FPS-spike follow-up — Windows settings migration

The real RX 570 session exposed a gap in the original graphics update: `platform_win.cpp` has its own settings reader and writer. The shared overlay settings parser was corrected, but the Windows path still loaded legacy `pipeline_skip_draws=1` and omitted the migration version when saving. Earlier QA fixtures already had draw skipping off, so their zero-skip results did not exercise this upgrade path.

Windows now reads/writes `pipeline_skip_draws_version=1` and disables the legacy value before applying renderer settings. Runtime logs report the effective draw-skipping state. The opt-in settings-migration regression uses the actual platform loader, renderer bridge and platform serializer, then reloads the saved result with the shared parser.

Native validation covers legacy enabled settings (expected off), an explicit versioned enabled choice (expected on), and an explicit versioned disabled choice (expected off). The integration build passed all three with exit 0 and zero Vulkan errors.

## What the session shows

- Startup prepared all 11,304 affected shader programs without failures, then warmed existing pipeline recipes. These startup costs must be distinguished from gameplay hitches.
- Compilation continued later: one 60-frame reporting window created 320 pipelines with 8,621.5 ms of aggregate compilation work. This is accumulated worker time, not an 8.6-second measured single-frame freeze.
- The log recorded 3,766 skipped draws. The loader omission explains why draw skipping remained active despite the original migration claim.
- Process memory peaked at 9.72 GB; the separately reported Vulkan image-allocation peak was 2.36 GB. Repeated cleanup requests crossed the configured soft process-memory threshold. This alone does not prove physical RAM or VRAM exhaustion.
- A slow reporting window logged 29.5 base FPS, zero pipelines compiled, replay 8.32 ms, fence wait 5.27 ms and presentation 18.76 ms. The diagnostic timing fields are not a complete non-overlapping wall-time breakdown. Compilation cannot explain every slow window.
- The session started with 4x optical-flow presentation and later logged 2x. Its saved final settings used native internal resolution, a 1920x1080 output, uncompressed textures and 2x presentation. These are workload observations, not a controlled comparison of settings.

Completing draws preserves geometry and can expose first-use pipeline stalls that draw skipping previously hid. The next performance checks should compare a repeated, warmed route at a lower internal resolution and with compressed textures, then measure frame generation separately at the same route/settings. Do not infer an FPS improvement from this settings-correctness repair alone.

Raw user logs and machine-specific paths remain private.
