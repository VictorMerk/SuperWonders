# Native Vulkan synchronization fixes

The Windows RX 570 game route exposed two actual synchronization hazards with Khronos Validation Layers 1.4.357
and synchronization validation enabled. These are independent of the earlier shader-warp and skipped-draw fixes.

1. `vkCmdCopyBufferToImage` wrote a region already written by an earlier copy without a dependency. In particular,
   multiple queued `WriteTexels` updates can overlap inside one batch. Keeping `TRANSFER_DST_OPTIMAL` does not order them.
2. The full-target clear fallback ran `vkCmdClearColorImage` in `GENERAL`, then an attachment load read it without
   a transfer-to-attachment dependency. Keeping `GENERAL` does not make that write visible either.

`transition` now accepts an explicit synchronization request even when its layout is unchanged. Texture uploads
and clears request that dependency before writes; each queued texel update does so separately. The out-of-pass
clear fallback also publishes its write before a later render pass can load the same `GENERAL` image. Ordinary
unchanged-layout calls retain their previous fast path. This is targeted resource synchronization, not a global
GPU-idle wait or a new frame queue.

The pre-fix native run reported ten write-after-write errors and ten read-after-write errors before each message
reached the validation layer's duplicate limit. It was stopped by the old QA time limit before completing the
route. The post-fix run completed the title, world map, **Scram, Skedaddlers!**, three pause/continue cycles and
movement/jumping with 4x frame generation: **zero validation errors**. Loader diagnostics confirm that the layer
was loaded. Validation overhead makes this run unsuitable as a performance comparison.

Unused fragment-output warnings remain: some guest shaders export a second output when only one attachment is
bound. They are recorded separately and are not silently filtered or counted as synchronization successes.
Zero errors on this route does not establish correctness in every course, driver or GPU mode.

## Regressions

`NX_COUNTER_CLEAR_PROBE=1` now has nine cases. The two additions exercise overlapping texel updates (the later
red pixels must win) and a 16x16 transfer clear around a shared 8x8 render area, followed by attachment reload.
Readback checks every RGBA pixel. All nine cases pass in synchronous, pipelined and asynchronous modes, with
zero core/synchronization validation errors: **27 cases**. These probe results do not promote the experimental
asynchronous modes to stable game defaults.

The frame-generation probe adds seven hard-cut variants at 2x through 8x. A red-to-blue discontinuity must choose
the nearer source exactly, including the midpoint rule, rather than blend unrelated pictures. It also verifies
opaque alpha for every output. Moving, stationary, resized and hard-cut checks now cover **87 output pictures**,
all passing with zero core/synchronization validation errors. The cut tests cover this deterministic discontinuity;
they do not prove artifact-free interpolation for all gameplay or scene changes.

## Repeating the synthetic probes on Windows

Use a prepared QA data directory and close games first. The tool refuses to overlap Wonder or Mario Kart.
It creates a new evidence directory and never removes prior reports.

```powershell
tools/perf/run_native_probes.ps1 -Runtime path/to/wonder.exe -DataRoot path/to/qa-data `
    -Output path/to/new-report -ValidationLayer path/to/portable-validation-layer
```

The optional layer directory must contain the official `VkLayer_khronos_validation.json` and its DLL. It is not
bundled with the preview. Environment settings apply only to the child process; registry/driver settings remain
untouched. Without `-ValidationLayer`, picture/input checks still run, but no validation coverage is claimed.
The tool drains both output pipes, checks exit codes and expected PASS counts, rejects validation errors, confirms
layer loading for GPU probes, and preserves per-case logs and `results.json`. A timeout stops only the probe it
started. Use `-Probe Counter`, `Framegen`, `Overlay` or `Texture` to select a subset.

References: [Khronos/LunarG synchronization validation](https://vulkan.lunarg.com/doc/view/latest/windows/synchronization_usage.html),
[Vulkan synchronization chapter](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).
