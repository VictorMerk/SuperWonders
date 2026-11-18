# AMD renderer performance pass - 18 November 2026

This pass changes code at the same graphics quality. It builds on the previously merged synthesis cutoff and blend-state pipeline reuse. Testing used a Radeon RX 570 Series on Windows. The actual-game runs in this report use the matching r8 library ABI. Results below distinguish individual renderer operations from actual game cadence; they do not certify every course or NVIDIA hardware.

## Changes retained

- Motion search returns its existing zero-vector result when the entire new search region is constant and the start is zero. A positive distance penalty makes zero the unique winner, including the symmetric fractional fit. The non-fractional stages also stop at an exact zero cost; fractional fitting still evaluates its neighbouring costs.
- Luminance conversion and 4x4 reduction use fixed paths for the runtime's common inputs, with the generic path retained. The compiled modules match the tested overrides byte for byte.
- Compute write-back reads 32-byte blocks once before comparison, instead of repeatedly reading a possibly uncached mapping in small pieces. Only GPU-changed 32-bit words are copied into guest memory; the existing short-tail byte behaviour and whole-copy fallback remain intact. There is no new heap allocation per write-back.
- CPU-read image buffers prefer host-cached, coherent memory, falling back to the existing coherent selector when unavailable. This helps screenshot/debug/probe readbacks; the compute upload ring retains its existing allocation policy.
- The probe reuses its synchronous staging region after completed cases, and generates exact integer-shift fixtures without redundant interpolation. Earlier partial timing runs are excluded. Their completed correctness checks remain valid.

The earlier depth-padding fix is already merged separately; see [pipeline depth padding](PIPELINE-DEPTH-PADDING-2026-11-18.md).

## Pixel and API checks

The integrated main build passes all 51 CPU-only CTests; the r8 build passes its three targeted pipeline/write-back regressions. The write-back differential test covers 16,392 size/alignment cases, including guest changes after recording, GPU-changed words, partial tails and untouched surrounding bytes. A separate native benchmark uses GPU-filled mapped memory and checks unchanged, sparse-change and all-change patterns against their known expected output, with synchronization validation enabled.

The shader exploration matrix ran 160 validation-enabled processes over eight original picture patterns at 320x180 and 127x75. Its 1,760 output checks and 2,880 finite motion-field checks match each reference, with no Vulkan validation errors. Patterns include horizontal, diagonal and fractional movement, HUD, occlusion, flat old/new pictures and a synthetic palette change. Palette/occlusion readbacks are hash comparisons, not a claim that their analytic image is an interpolation-quality oracle.

The retained combination passed the full 87-image suite and all 144 motion-field checks, plus the pattern cases and pipelined/asynchronous submission checks. Both final main and r8-compatible builds passed with their embedded defaults: each completed 87 images/144 fields synchronously and 11 images/18 fields in each additional submission mode. Outputs match the tested combination, and Vulkan synchronization validation reports no errors. The full default fixture also matches the earlier unmodified fixture's image hashes.

Pipeline draw/readback testing retains live blend, mask and multiple-target distinctions while reusing reserved-byte-only variants. All ten identity checks and fourteen attachment readbacks pass in each submission mode, with baseline-identical pixels.

## GPU workload, not FPS

The final A/B uses one executable with module overrides, the same case order and allocation policy, validation disabled, no extra motion-field copies, and ABBAAB order (three samples per implementation). Each timestamp interval includes two synthetic captures, interpolation and output readback. Absolute values depend on the case schedule and allocation/history behaviour; comparisons between different schedules are not interchangeable.

| Multiplier | Scene | Baseline median ms | Retained combination ms |
|---|---|---:|---:|
| 2x | moving | 7.741 | 7.133 |
| 2x | still | 11.386 | 11.158 |
| 2x | cut | 15.483 | 12.612 |
| 4x | moving | 10.031 | 9.731 |
| 4x | still | 13.296 | 13.030 |
| 4x | cut | 18.750 | 16.174 |
| 8x | moving | 13.037 | 12.626 |
| 8x | still | 14.993 | 14.717 |
| 8x | cut | 25.286 | 22.300 |

For moving 2x, baseline samples were 7.720 / 7.751 / 7.741 ms and candidate samples were 7.142 / 7.133 / 7.104 ms. The median workload reduction is 7.9%. Some smaller differences, including still scenes, have overlapping sample ranges and should not be treated as guaranteed gains.

Partial-SAD rejection was pixel-correct but substantially slower on this AMD device and was rejected. The median-network rewrite, narrower inter-compute barriers, colour-only luminance path and duplicate-vector synthesis experiments were also excluded from production: their gains were too small or inconsistent across cases. The retained combination keeps the existing broad synchronization.

## CPU compute-result operation

The native CPU-operation benchmark reads actual GPU-filled coherent, uncached memory (type flags `HOST_VISIBLE | HOST_COHERENT`, matching the tested upload-ring choice). It alternates old/new routines in ABBAABBA order, four samples each, processing 8 MiB per sample. Vulkan filling and fence waits are outside the CPU timer; timing runs have validation disabled.

Across 256-byte, 4 KiB, 64 KiB and 1 MiB ranges, wider reads are about 2x faster for unchanged/sparsely changed results and 9.6-10x faster when all words change. Both implementations produce the same required guest bytes. This measures the write-back operation, not a whole frame or an FPS multiplier.

## Actual game scope and remaining hitch

Four runs used the same scripted course route, native 1080p, uncompressed textures and 2x frame generation, in ABBA order. The measured interval is course-relative frames 200-1100, with a valid player at the same stationary position; screenshots and movement are outside it. Original-picture host cadence is reconstructed from the trace, rather than counting generated presentations as game frames.

| Run | Build | Average original fps | 1% low fps | p99 ms | Frames over 50 ms |
|---|---|---:|---:|---:|---:|
| A1 | earlier r8 renderer | 58.25 | 51.82 | 18.50 | 0 |
| B1 | final renderer | 58.38 | 55.68 | 17.69 | 0 |
| B2 | final renderer | 58.17 | 41.56 | 18.22 | 1 |
| A2 | earlier r8 renderer | 58.06 | 46.86 | 20.47 | 0 |

These short samples do not establish an average-FPS or overall hitch-rate improvement. B2 still contains one 65.9 ms interval whose excess is unattributed: it has no pipeline creation, skipped draw, ASTC decode, file read, completed logging call or long input poll. It must not be presented as a solved shader-compilation hitch.

All four runs exited normally and skipped no draws in the measured window. Their twenty title/map/course captures were visually inspected without obvious black surfaces or missing scenery. Captures cannot rule out brief artifacts between frames. A further 20-minute course session with repeated pause/resume completed and exited normally. Its logs contain no device loss, GPU hang, VRAM allocation failure or pipeline failure. Sampled private memory was 5.041-5.054 GiB during the last ten minutes. Pause and resumed captures were inspected: the game menu applies its blue/green effect, and resumed gameplay returns to normal colours. A separate fifteen-minute actual-game run with the Khronos layer and synchronization validation enabled also completed and exited normally, with zero validation errors. The rebuilt latest main passes all 51 CPU-only tests and repeats the embedded-shader checks in all three submission modes.

## Cache replay

Both legacy fixtures replay with every retained recipe ready and zero skipped recipes or Vulkan validation errors. The 3,449-record fixture retains 381 identities after blend/padding normalization, and the 11,970-record snapshot retains 1,779. Relative to the blend-only baseline, the counts are 2,218 -> 381 and 6,066 -> 1,779.

Three restored-input, non-validation samples gave larger-snapshot prewarm medians of 2,137.9 ms before and 1,865.1 ms after the padding fix. The smaller fixture was effectively unchanged (1,150.3 vs 1,173.6 ms, overlapping ranges). There is no universal startup-time or gameplay-FPS claim from the recipe counts.

Aggregate samples are in [the measurement data](data/amd-renderer-performance-20261118.json). Game files, private traces, saves and machine-local paths are excluded from this repository. See [GPU probe instructions](RENDERER-GPU-PROBES.md) and acquire the machine-wide reservation before native tests.
