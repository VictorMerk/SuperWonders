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

Enable the Khronos validation layer and synchronization validation for correctness runs. Use identical executables with module overrides to avoid unrelated binary differences. Compare hashes on varied motion, occlusion, flat/cut, HUD and odd-size fixtures. For performance, disable validation and extra field readbacks, alternate baseline/candidate order, repeat samples and retain the exact case schedule. GPU timestamps include the probe's two captures, interpolation and output readback; they are not gameplay FPS.

Standalone GPU CTests are labelled `gpu`. `ctest --test-dir BUILD -LE gpu --output-on-failure` runs the CPU suite only; run labelled tests separately under an owned reservation.
