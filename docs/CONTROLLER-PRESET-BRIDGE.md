# Windows Circle-confirm preset: test the applied mapping

The shared settings parser and menu accepted controller preset 2, **PlayStation: Circle confirms**, but the Windows
apply callback still clamped the value to 0–1. Applying settings silently converted it to the Nintendo preset.
The menu could continue displaying the requested value even though the input backend used a different mapping.
This bug is present in the privately delivered r7 ZIP; the default bottom-button preset is unaffected.

The Windows apply callback now accepts all three existing presets. The overlay's native input probe also checks
the actual four face-button mappings reported by the controller backend after each asynchronous platform apply.
Checking only confirm/back would miss this regression: Circle-confirm and Nintendo share those two positions,
but their Square/Triangle mappings differ. The probe clears custom overrides for each check, uses a one-second
deadline for the window-thread callback and restores the startup settings without saving temporary preferences.

Before the clamp correction, presets 0 and 1 passed and preset 2 failed. After correction, all three pass, along
with the existing Esc/F1/controller-chord/input-isolation test, under core/synchronization validation with zero
errors. `tools/perf/run_native_probes.ps1 -Probe Overlay` now expects all four PASS results.

This tests the real Windows settings-to-input bridge. It does not claim that every physical PlayStation model,
Bluetooth stack, glyph atlas or custom binding has been exercised. Restart after changing prompts/bindings when
the resource notice requests it. Both LAN PCs must use the same runtime build when upgrading from r7.
