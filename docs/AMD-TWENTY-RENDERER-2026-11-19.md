# Twenty renderer and shader changes

This pass addresses malformed or aliased texture/sampler state and repeated CPU/GPU work without lowering graphics settings. It preserves the existing texture-cache names and format. Tests on an AMD Radeon RX 570 support the changes; they do not establish that every course, driver or transient rendering problem is fixed.

## Retained changes

1. Texture dirty hashing includes trailing bytes after the last 64-bit word.
2. Sampler LOD/bias/anisotropy normalization handles non-finite values and actual enabled device limits.
3. Mip layout rejects chains beyond the 32-bit dimension shift range.
4. Texture layout sizing/alignment uses checked arithmetic before allocations and deswizzle walks.
5. Texture-cache requests compute both existing source fingerprints in one traversal.
6. Request metadata and output-level vectors reserve their known capacity once.
7. Mirror-clamp sampler modes require their enabled Vulkan feature. Unsupported devices use an approximate clamp-to-edge fallback and log it; the supported AMD path retains mirror-clamp.
8. Sampler keys describe effective Vulkan state, excluding source padding and ignored fields.
9. A bounded raw sampler-state front cache skips repeated normalization and map lookup on hits.
10. Re-registering an unchanged sampler-pool slot skips redundant history copies and writes.
11. Proven corrupt cache records are negatively cached; missing entries leave the index. Valid source collisions and transient I/O failures remain retryable, and a stale rejection cannot invalidate a newer committed repair.
12. Complete successful quota scans reconcile the cache index with actual surviving files. Failed/incomplete scans preserve unknown entries.
13. An exact-zero fractional motion-search winner evaluates only the missing neighbours needed by its axis fit.
14. A fixed nineteen-comparison vector median network replaces dynamic component selection loops.
15. Buffer-to-image copy span validation checks offset underflow and arithmetic overflow, including padded compressed slices.
16. ASTC decoder source bounds check required/stored layer bytes and remaining span without unsigned wraparound.
17. Sampled image views reject empty metadata and incomplete cube ranges before creation; failed views use the existing null/dummy path.
18. Sampled-view cache keys retain full-width normalized subresource ranges, preventing packed 16-bit aliases and reusing equivalent clamped requests.
19. Swizzle decoding bounds each byte before its lookup, falling back to the original channel for invalid values.
20. Texture content hashes have an explicit initialized flag, so a legitimate zero-valued hash still detects subsequent changes.

## Evidence and limits

The asset-free CPU checks cover every byte mutation in short/tail/unaligned hash ranges, a constructed zero hash, sampler state/key behavior, unchanged registration ordering, layout/upload overflows, all 256 swizzle byte values, cube bounds, cache corruption/repair/collisions/quota and cache-name compatibility. The median network also passes all 512 binary inputs and 10,000 deterministic finite floating-point inputs against an independent sort.

Native sampler and image-view probes exercise actual Vulkan objects with Khronos synchronization validation. The shader comparison matrix checks horizontal, diagonal, fractional, HUD, occlusion, flat-old, flat-new and palette fixtures at ordinary and non-workgroup-aligned dimensions, plus the complete 1080p factor/quality matrix. Retained shader outputs and intermediate motion fields match the preceding production modules in those cases.

The reserved isolated cache-request benchmark alternated original, hashing-only, reservation-only and combined implementations. One-pass hashing helps larger requests; reservation helps small requests and is negligible at 1 MiB. An eight-state warmed sampler lookup benchmark alternated canonical lookup with/without its front cache, with no sampler creation in the timed region. These CPU operation measurements are not FPS measurements.

The motion-search candidate reduces GPU work in the moving synthetic workloads; still/cut workloads show much smaller differences and some small regressions. The median network reduces time in an isolated batch of 64 production-sized median dispatches with normal inter-pass barriers. Its whole-frame contribution is small. See the [aggregate evidence](data/amd-twenty-renderer-20261119.json) for sample arrays and scope.

Partial-SAD lower-bound pruning was rejected: it preserved output but substantially slowed the moving GPU workloads. It is not shipped or counted. No resolution, filtering, frame-generation multiplier or skipped-draw setting was reduced to obtain results.

Final integration checks and real-course results are recorded after the combined candidate completes. Rare hitches, other GPUs and unvisited courses remain outside certification.

## Repeating checks

Run `ctest --test-dir <build> -LE gpu --output-on-failure` for CPU regressions. Acquire the machine-wide game-test reservation before native/GPU or performance work, as described in [AGENT-WORKFLOW.md](AGENT-WORKFLOW.md).

With a prepared data root, `NX_SAMPLER_PROBE=1` exercises sampler limits/reuse; add `NX_SAMPLER_BENCH=1` for isolated lookup timing. `NX_IMAGE_VIEW_PROBE=1` tests subresource cache keys and cube/swizzle handling. `NX_TEXTURE_CONVERSION_PROBE=1` tests cold/warm conversion/cache equality; use a fresh cache root for genuine cold cases.

`NX_FRAMEGEN_PROBE=1`, `NX_FRAMEGEN_PROBE_SIZE=1920x1080`, `NX_FRAMEGEN_PROBE_DIGEST=1` and `NX_FRAMEGEN_PROBE_FLOW_DIGEST=1` check output/field digests. Probe-only `NX_FRAMEGEN_PROBE_FLOW_SPV` and `NX_FRAMEGEN_PROBE_MEDIAN_SPV` allow module comparisons. `NX_FRAMEGEN_PROBE_MEDIAN_BENCH=1` adds isolated repeated median dispatches; do not treat that timing as whole-game FPS.
