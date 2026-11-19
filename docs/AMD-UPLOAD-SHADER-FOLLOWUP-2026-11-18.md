# AMD upload and shader follow-up - 18 November 2026

This pass continues the [renderer performance work](AMD-RENDERER-PERFORMANCE-2026-11-18.md) on a Radeon RX 570 Series. Graphics quality and the user's settings are preserved. Operation timings below are separate from whole-game FPS.

## Retained CPU renderer changes

Compute storage uploads need a reference snapshot so retirement copies back only words the GPU changed. Previously the renderer copied guest bytes into the mapped upload ring, then constructed that reference by reading the mapping. On this AMD device the upload ring's coherent memory type is not host-cached.

The renderer now constructs the reference in normal CPU memory and uploads those exact bytes. It does not reread mutable guest memory to produce a second, potentially different copy. Merged ranges still restore their original references before GPU copies combine the live results, and unchanged GPU words still preserve later guest writes.

Overlap lookup now returns a half-open map iterator range. It visits the same disjoint compute regions without constructing a heap-allocated iterator vector per binding. The caller records its GPU copies before erasing the old range; region and snapshot lifetimes are preserved.

## Mapped upload preparation

A native probe uses the production snapshot helper and the same first coherent Vulkan memory type as the upload ring (`HOST_VISIBLE | HOST_COHERENT`, flags `0x6` here). Both implementations produce identical CPU snapshots and GPU-copied bytes. The correctness run loads the Khronos layer with synchronization validation and reports zero validation errors.

Timings use a separate validation-disabled run, four samples per implementation in ABBAABBA order. Each sample processes 8 MiB of guest payload in ranges of the stated size, **plus the production 65,552-byte zero tail for each range**. Snapshot allocation, CPU copying, mapped uploading and padding are included. Vulkan submission, GPU copy and fence waits are outside the CPU timer. Compilation uses the runtime's `-O3 -mavx2` settings.

| Guest range | Earlier median CPU ms | New median CPU ms |
|---|---:|---:|
| 256 bytes | 143.893 | 68.210 |
| 4 KiB | 65.458 | 4.710 |
| 64 KiB | 60.881 | 0.658 |
| 1 MiB | 62.907 | 2.076 |

These are upload-preparation operation measurements, not FPS multipliers. In particular, small ranges repeat the zero tail many times; the payload size is not the total mapped-memory traffic. Raw aggregate samples are in [the data file](data/amd-upload-followup-20261118.json).

Scratch uploads also stop clearing payload bytes immediately before overwriting them. Missing inputs still clear the complete padded range, and supplied inputs clear only their unused tail. A second ABBAABBA comparison processes 64 MiB of payload without extra padding; its median times are:

| Scratch range | Earlier median CPU ms | New median CPU ms |
|---|---:|---:|
| 256 bytes | 43.158 | 21.370 |
| 4 KiB | 3.750 | 2.017 |
| 64 KiB | 3.901 | 1.833 |
| 1 MiB | 3.820 | 1.943 |

## Coarse motion-search shader

The coarse optical-flow specialization shares its two image tiles within each workgroup. All invocations reach the load/barrier before out-of-bounds output lanes return, including partial edge groups. Fine and full-resolution search calculations retain their sampling path. The generated embedded module is the exact module tested with the production `-Os` compiler option.

Eight synthetic patterns at 320x180 and 127x75, plus 1920x1080 checks, cover still images, moving detail and cuts. Across the production candidate matrix, 68 validation processes passed 1,052 image comparisons and 1,728 finite-field checks with identical reference/candidate hashes and zero Vulkan validation errors. Synchronous, pipelined and asynchronous embedded-module probes then passed separately for the main and r8 builds.

Timing uses separate validation-disabled processes in ABBAAB order, three samples per implementation. GPU timestamps include two captures, interpolation and readback. They measure this workload, not whole-game FPS:

| Factor / scene | Earlier median GPU ms | Shared-tile median GPU ms |
|---|---:|---:|
| 2x / cut | 12.623 | 12.621 |
| 2x / moving | 7.128 | 6.915 |
| 2x / still | 11.360 | 11.135 |
| 4x / cut | 16.330 | 14.816 |
| 4x / moving | 9.268 | 9.273 |
| 4x / still | 13.255 | 13.067 |
| 8x / cut | 22.281 | 22.082 |
| 8x / moving | 12.638 | 12.564 |
| 8x / still | 14.755 | 14.496 |

The 2x moving case improves about 3%. Some cases are effectively unchanged and sample ranges overlap; the variable 4x cut result is not a promised 9% gameplay gain. Other candidates and earlier exploratory timings without production optimization were excluded.

## Course observations before the pacing follow-up

Six visible runs compare the previously installed renderer with the upload/shader changes in ABBAAB order. The same Course004 route uses native 1920x1080, the existing high-quality profile and 2x frame generation. Each measured window contains 900 original-picture host intervals, course-relative frames 200 through 1100, with the player stationary at the same position. Movement and captures occur outside that window.

| Run | Build | Average host original pictures/s | 1% low | p99 interval ms |
|---|---|---:|---:|---:|
| A1 | earlier | 58.19 | 47.88 | 18.25 |
| B1 | upload/shader | 58.35 | 56.28 | 17.64 |
| B2 | upload/shader | 58.34 | 56.08 | 17.69 |
| A2 | earlier | 58.36 | 56.02 | 17.70 |
| A3 | earlier | 55.53 | 27.80 | 28.58 |
| B3 | upload/shader | 56.94 | 35.78 | 23.19 |

No measured window skipped draws, created pipelines or contained an interval above 50 ms. Later samples vary more on both builds. These short windows do not establish a robust whole-game FPS or hitch-rate improvement.

The last candidate run continued longer. Its 7,000-frame diagnostic tail contained four approximately 0.4-second stalls inside Vulkan image acquisition or presentation. The corresponding frames did not create pipelines, decode ASTC, read files or spend comparable time logging/polling input. Measured presentation-sleep excess stayed below 1.3 ms. Those observations locate blocking calls; they do not establish a driver, cache-export or OS cause, and the upload/shader changes do not claim to fix them.

The visible start/middle/end course and pause/resume captures show no obvious black or missing geometry. The pause colour effect returns to normal on resume. Still captures cannot rule out transient artifacts between captures. A visible validation session exited normally before its requested duration and is recorded as incomplete. A subsequent hidden-window repeat completed 180 seconds of course validation with pause/resume, the Khronos layer loaded and zero validation errors.

## Presentation diagnostics and pacing validation

Optional frame traces distinguish the presentation wait's requested duration from its actual duration. The reporter sums these across generated pictures when reconstructing original-picture intervals, labels only excess wait and supports legacy CSV traces. Tracing disabled performs no extra post-wait clock read.

The interpolation path previously anchored the next original picture to the actual completion of its preceding present. Repeated small wait/present delays therefore accumulated into the requested period. Successful or suboptimal interpolated presents now retain their scheduled phase when completion is less than one period late. First/passthrough frames, clock reversals, failures and whole missed periods restart from actual completion, so a long stall does not produce an extended catch-up burst. The OS timer and graphics settings are unchanged.

The final visible comparison runs ABBA against the previously installed renderer, with the same settings and route. Each measured window contains 7,000 original-picture host intervals at course-relative frames 1500 through 8500, after movement and before the final capture. Small final-position differences from physics timing are recorded in the aggregate data. These are host presentation intervals; physical scanout timing is unavailable on this test path.

| Run | Build | Average host original pictures/s | 1% low | Median interval ms | p99 ms | Maximum ms | Intervals >50 ms |
|---|---|---:|---:|---:|---:|---:|---:|
| A1 | earlier | 58.10 | 47.54 | 17.16 | 18.74 | 98.10 | 1 |
| B1 | final | 59.75 | 39.32 | 16.66 | 19.39 | 228.16 | 1 |
| B2 | final | 59.99 | 50.94 | 16.68 | 18.93 | 21.83 | 0 |
| A2 | earlier | 58.29 | 53.27 | 17.14 | 17.77 | 33.74 | 0 |

The candidate's steady median approaches the requested 16.67 ms cadence. Worst-frame and 1% low results remain variable: B1 includes a 228 ms unknown/main-thread hitch, whereas B2 contains no interval above 25 ms. This supports removing cumulative pacing drift, not a claim that all FPS drops are fixed. No measured long-course window skipped draws or created pipelines. The earlier acquire/present stalls remain an unresolved diagnostic observation.

All 16 long-course start/middle/end/late captures and the final validation pause/resume captures were inspected. They show no obvious black or missing geometry, and resume restores the normal colour effect. Captures do not exclude transient artifacts between frames. The final hidden-window synchronization-validation course completed its requested 180 seconds with repeated pause/resume, normal exit, the Khronos layer loaded and zero validation errors.

## Final validation scope

The integrated main build passes all 57 CPU-only CTests; the r8-compatible build passes six targeted tests. The pacing regression exercises 3,000 steady frames across caps 30/60/90/120/144, plus first frames, clock reversal and missed-period resets. Upload/reference, tail-clear and overlap cases preserve the prior bytes and writeback semantics. Trace tests cover aggregation and legacy CSVs.

The final embedded-module probes pass synchronous (87 image / 144 field), pipelined (11 / 18) and asynchronous (11 / 18) checks separately for both build variants, matching the selected tested shader combination with zero validation errors. The asset-free upload diagnostic independently confirms the upload ring's first coherent memory selection (type 1, flags `0x6` on this RX 570), snapshot/GPU-copy correctness and zero synchronization-validation errors.

Latest-main integration includes unrelated controller/launcher changes. Those changes received the integrated build and CPU checks; the actual-course renderer tests use the separately built r8-compatible runtime matching the installed game ABI. No NVIDIA hardware or complete course/effect catalogue was tested. The report preserves the incomplete visible validation session rather than treating it as a pass; the two completed hidden validation sessions are separate evidence.

The first final-pacing validation attempt could not export its completion evidence after the system drive filled. Its logs are preserved as an incomplete run, with zero recorded validation errors; it is excluded from passing counts. Hash-verified duplicate test-cache copies were removed while retaining the original, and the complete validation was repeated before acceptance.

The game validation logs contain unused fragment-output warnings where the shader writes a colour location without an attached target. Zero validation errors does not mean zero warnings or prove every game effect correct.
