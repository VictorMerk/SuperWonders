# Renderer GPU regression probes

Acquire the machine-wide game-test reservation from `AGENT-WORKFLOW.md` before every native probe. Use a separate data/cache fixture. These probes initialize the actual Vulkan renderer; a CPU-only CTest filter does not authorize launching them.

## Pipeline identities and colour output

`NX_PIPELINE_DRAW_PROBE=1` draws original synthetic shaders into small RGBA8 targets and reads back every pixel. It checks opaque, alpha and additive blending, colour-write masks and independent multiple-target blending. Disabled blend metadata, unbound attachment metadata and the legacy reserved depth byte must reuse their equivalent pipelines; live blend/mask changes must remain distinct. Check every identity and pixel result, and compare digests with a baseline. The probe currently requires independent blending, dynamic rendering and the monolithic pipeline path (`NX_GPL=0`). Run synchronous, pipelined and asynchronous modes with Vulkan synchronization validation.

The shaders live in `tests/shaders/pipeline_draw*`; regenerate their embedded header with `tools/gen_shaders.py --only pipeline_draw` after changes. They contain no game assets.

## Frame interpolation comparisons

`NX_FRAMEGEN_PROBE=1` retains its full default suite of 24 cases and 87 output images. Additional options support controlled shader experiments:

- `NX_FRAMEGEN_PROBE_VARIANTS`: comma-separated case indices, 0 through 23, with duplicates removed. Invalid lists fail explicitly.
- `NX_FRAMEGEN_PROBE_FLOW_SPV`, `SYNTH_SPV`, `LUMA_SPV`, `MEDIAN_SPV` (each with the `NX_FRAMEGEN_PROBE_` prefix): load a local compiled module for that stage in the probe only. Normal gameplay uses embedded modules. Overrides are bounded and receive basic file/header checks; validate experimental SPIR-V with `spirv-val` before use.
- `NX_FRAMEGEN_PROBE_FLOW_DIGEST=1`: read and hash all six RG16F motion fields and check that their half-float components are finite. Compare these hashes as well as output-image hashes. Disable field copies during timing runs.
- `NX_FRAMEGEN_PROBE_PATTERN`: `diagonal`, `fractional`, `hud`, `occlusion`, `flat-new`, `flat-old` or `palette`. These are original synthetic patterns, not captures of a game effect.
- `NX_FRAMEGEN_PROBE_COMPARE_ONLY=1`: require opaque readable output but report `READBACK`, without treating the synthetic analytic image as a quality oracle. Flat and palette patterns select this mode automatically. A readback is useful for baseline/candidate hash comparisons and does not establish interpolation quality.
- `NX_FRAMEGEN_PROBE_COMPUTE_BARRIERS=1`: probe-only experiment restricting inter-compute dependencies to compute shader reads/writes. Capture/transfer boundaries retain the normal broad barriers. This flag does not change normal gameplay synchronization.

Enable the Khronos validation layer and synchronization validation for correctness runs. `VK_LAYER_VALIDATE_SYNC=1` is the [dedicated Khronos setting](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/updating_from_VK_EXT_validation_features.md); confirm that the layer was loaded as well as checking its error output. Use identical executables with module overrides to avoid unrelated binary differences. Compare hashes on varied motion, occlusion, flat/cut, HUD and odd-size fixtures. For performance, disable validation and extra field readbacks, alternate baseline/candidate order, repeat samples and retain the exact case schedule. The synchronous runner reuses its staging-buffer region after each completed case, matching the normal submit path. Integer horizontal synthetic shifts bypass redundant host interpolation while preserving the same input bytes; compare reference hashes when changing the fixture generator. GPU timestamps include the probe's two captures, interpolation and output readback; they are not gameplay FPS.

Standalone GPU CTests are labelled `gpu`. `ctest --test-dir BUILD -LE gpu --output-on-failure` runs the CPU suite only; run labelled tests separately under an owned reservation.

## Standalone upload-memory comparison

`tools/perf/upload_memory_bench.cpp` needs no game files. It checks CPU snapshots against GPU-copied bytes on the first coherent memory type the upload ring would select, then compares the old/new snapshot and scratch-upload routines. It prints the enumerated devices, chosen device, memory flags, alternating samples and correctness results. It prefers the first discrete device; use `--gpu INDEX` to match the GPU used by the game. No enumerated device, transfer queue or coherent memory type returns 77; Vulkan API failures return 1.

Build it with Clang/GCC in C++20 mode, the runtime's optimization/architecture flags, `-I runtime/src/nvn`, Vulkan headers and the Vulkan loader library. On Windows with the Vulkan SDK, for example:

```powershell
clang++ -std=c++20 -O3 -mavx2 -I runtime/src/nvn -I "$env:VULKAN_SDK/Include" tools/perf/upload_memory_bench.cpp "$env:VULKAN_SDK/Lib/vulkan-1.lib" -o private/upload_memory_bench.exe
```

Acquire the machine-wide reservation in the same long-lived PowerShell process before running it, keep that lease through the complete run, and release only that lease in `finally`, as described in [the workflow](AGENT-WORKFLOW.md). The executable does not acquire a lease itself. Run separately with synchronization validation for correctness and with validation disabled for timing; never combine another game, a heavy build or another performance test with the timing run.

The snapshot timer includes reference allocation, CPU copying, mapped uploading and a 65,552-byte zero tail per range; each sample has 8 MiB of guest payload **plus those tails**. The scratch timer processes 64 MiB of payload without extra padding. GPU submission/copy/fence waits are outside both CPU timers. These measurements cannot be reported as gameplay FPS or as proof that intermittent rendering artifacts are resolved.
