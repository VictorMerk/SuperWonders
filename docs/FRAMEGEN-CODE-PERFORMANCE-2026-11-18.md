# Frame-synthesis code optimization

The synthesis compute shader previously evaluated all eleven motion candidates for every generated pixel. Each candidate samples both full-resolution input pictures. It now stops when the selected score is at most the fixed penalty charged to every remaining candidate.

This preserves the selection rule: sample disagreement is non-negative, every later candidate adds 0.01, and equal scores retain the earlier candidate. Consequently, no later candidate can replace a winner scoring at most 0.01. Pixels with uncertain motion continue through the original search. Resolution, frame-generation multiplier, texture quality and frame pacing are unchanged.

The embedded synthesis SPIR-V was regenerated with `tools/gen_shaders.py --only framegen`. Recompiling the original shader with the same compiler reproduced its committed SPIR-V exactly; only the synthesis module changes in the generated header.

## Reproducing the checks

The existing native frame-generation probe accepts two additional opt-in QA variables:

- `NX_FRAMEGEN_PROBE_SIZE=1920x1080` overrides the larger probe images; the original default remains 1280x720. Both dimensions must be between 64 and 4096.
- `NX_FRAMEGEN_PROBE_DIGEST=1` prints a full RGBA readback hash for each generated image, allowing ordered baseline/candidate comparisons.

Run the executable with `NX_FRAMEGEN_PROBE=1`, an isolated valid data fixture and these variables. On this machine, all game/GPU tests also require the machine reservation described in `AGENT-WORKFLOW.md`. Compare complete ordered digest lists, exit codes and all 87 image checks. Use the Khronos validation layer with synchronization validation for correctness runs; disable validation for timing runs.

The probe's GPU timestamp includes two captures, optical flow, all requested intermediate pictures and GPU readback. It is not a measurement of synthesis alone, an ordinary game frame, or gameplay FPS. CPU image construction, validation and hashing sit outside that timestamp.

## Correctness

Baseline and optimized builds each passed all 87 image checks with the Vulkan validation layer loaded and zero validation errors. Their complete ordered output hashes matched. The cases cover small edge sizes and 1920x1080 moving, still and hard-cut images at 2x through 8x presentation. The candidate SPIR-V also passed validation for Vulkan 1.0.

These checks cover the tested RX 570 and synthetic inputs. They do not certify every driver, motion/occlusion pattern, game course or long-session glitch.

## Synthetic GPU timing

Radeon RX 570, Windows Vulkan 1.3, reported driver value `0x00800117`. Baseline and candidate executables use the same instrumentation, compiler and fixture; only the embedded synthesis shader differs. Timing runs disable the validation layer and use order A, B, B, A, A, B. Values below are medians of three complete runs per build. All eight correctness/timing runs passed all 87 checks and produced identical ordered output hashes (696 image checks in total).

| Input | Multiplier | Baseline GPU ms | Optimized GPU ms | Time reduction |
|---|---:|---:|---:|---:|
| moving | 2x | 8.547 | 7.752 | 9.3% |
| moving | 4x | 11.341 | 9.355 | 17.5% |
| moving | 8x | 16.272 | 13.226 | 18.7% |
| still | 2x | 4.901 | 4.093 | 16.5% |
| still | 4x | 8.687 | 6.232 | 28.3% |
| still | 8x | 14.395 | 10.459 | 27.3% |
| cut | 2x | 7.606 | 7.544 | 0.8% |
| cut | 4x | 10.748 | 10.774 | -0.2% |
| cut | 8x | 16.084 | 16.245 | -1.0% |

Moving/still median reductions across all tested multipliers span 9.3-28.3%. Hard cuts generally cannot use this early exit: their median changes range from a 2.0% reduction to a 1.4% increase. Individual timings are noisy: the first candidate 2x cut sample was 15.221 ms, versus 7.440 and 7.544 ms in its repeats. Three samples are insufficient to certify hitch distributions or rare regressions. Preserve the raw values rather than treating the median as proof of zero regressions.

[All sanitized timing samples](FRAMEGEN-CODE-PERFORMANCE-2026-11-18.csv). These measurements include captures, flow, synthesis and readback; they are not gameplay FPS gains.

## Course runs and remaining hitches

Four hidden-window native Vulkan runs used the same scripted title/map/Scram, Skedaddlers! route in order A, B, B, A. The isolated fixture used native 1920x1080 rendering, uncompressed textures, 2x generation, a 60 FPS cap and the same pacing settings in both builds. These fixture settings were restored afterward; the user installation settings were not changed. Audio was muted in both builds. Each run reached course frame 1500 and quit normally through the controller-driven settings menu (exit 0).

The table reports **original-picture cadence at host presentation**, reconstructed by the frame-time suite across generated pictures. Each measurement spans course frames 200-1100 (900 original intervals), excluding screenshot readback. Generated pictures do not count as simulation ticks.

| Run | Build | Original FPS | 1% low FPS | Maximum interval ms | New pipelines | Skipped draws |
|---|---|---:|---:|---:|---:|---:|
| a1 | baseline | 55.06 | 11.19 | 214.87 | 142 | 0 |
| b1 | optimized | 57.64 | 36.89 | 27.76 | 6 | 0 |
| b2 | optimized | 56.94 | 18.95 | 143.25 | 89 | 0 |
| a2 | baseline | 57.97 | 32.65 | 34.08 | 10 | 0 |

**No attributable gameplay FPS or hitch-rate improvement is established.** A1 compiled new pipelines and serves as a warm-up, not a fair FPS baseline. Later movement outcomes/player positions differed despite identical scripted inputs; B2 also encountered new pipelines and replay hitches. These short, differently evolving scenes cannot isolate a small performance change. The average cadence stayed close to the existing pacing limit. The 9-28% synthetic GPU-time reduction must not be described as 9-28% higher game FPS.

Twenty title/map/course captures were inspected. No black blocks or missing geometry were observed in those images. Both builds retained complete drawing, and all measured windows recorded zero skipped draws. This is limited visual evidence, not certification of every transient glitch. GPU-busy telemetry returned no valid samples in this r8 submission mode and is not used as evidence.

The optimized B2 run still contained 143.25 ms maximum original-picture intervals, with replay/compilation activity. This change reduces interpolation GPU work; it does not repair first-use pipeline compilation or every CPU/load hitch. Raw logs, traces and machine-specific installation details remain private.

[Sanitized course measurements](FRAMEGEN-COURSE-PERFORMANCE-2026-11-18.csv).
