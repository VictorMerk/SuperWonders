# Driver-cache export follow-up - 18 November 2026

This follows the [AMD upload/shader pass](AMD-UPLOAD-SHADER-FOLLOWUP-2026-11-18.md). It changes renderer cache bookkeeping and shutdown synchronization, with the existing shader modules, frame-pacing code and graphics-quality settings retained.

## Change

Previously, the background exporter cleared its dirty flag before querying and writing the driver cache. A forced cache-save call arriving during that work could see a clean flag and return without waiting for its writer. The new revision tracker acknowledges persistence only after the temporary file is successfully committed. Shutdown drains background writes before deciding whether another export is owed. Other latest-main shutdown cleanup also waits for background writes. This makes the cache-save operation itself guarantee that wait; it is defensive hardening, not proof that a complete previous shutdown lost its cache.

A successful unchanged-size observation now suppresses periodic polling until another pipeline mutates the cache. Such an observation does not declare the cache persisted: equal size does not establish equal contents, so the final shutdown save remains owed. Failed driver queries, bounded snapshots and file commits remain retryable. Mutations arriving during a successful save remain pending for a later export.

The optional `NX_PIPELINE_CACHE_TRACE=1` diagnostics record the initial size-query revision, bytes and elapsed time, plus skipped unchanged polls. These are background-export observations, not frame-time attribution. Existing no-growth code already avoided the large data snapshot; this change additionally avoids the redundant size query and worker job.

## Actual cache persistence and reload

Two sequential hidden-window Radeon RX 570 runs use native 1920x1080, the existing high-quality profile and 2x frame generation, with synchronization validation, repeated pause/resume and orderly menu exits. The second starts from the driver cache captured after the first run. Both completed their requested course duration, loaded the Khronos layer, exited normally and reported zero validation errors. Existing unused fragment-output warnings remain.

| Run | Requested course seconds | Initial size-query attempts | Unchanged polls skipped | Forced exports at exit | Loaded bytes | Produced bytes |
|---|---:|---:|---:|---:|---:|---:|
| Initial | 180 | 5 | 0 | 0 | 189,795,868 | 195,904,540 |
| Saved-cache reload | 60 | 3 | 3 | 1 | 195,904,540 | 195,904,540 |

The traced initial size queries ranged from 0.006 to 0.035 ms. They were short in these runs, so this is a reduction of redundant driver work and a persistence fix, not evidence that cache polling caused the earlier major hitches. Counts include forced size-query attempts but exclude internal data-snapshot reads/retries. The initial run had already committed its latest revision periodically and needed no redundant final export. The warm run exercised the same-size dirty path and successfully exported at shutdown. Its saved cache was accepted on load.

## Same-settings gameplay comparison

Four visible ABBA runs compare the previously installed renderer with the cache change, using the same route, cache seed and quality settings. Each measured window contains 7,000 host original-picture intervals, course-relative frames 1500 through 8500. Movement and captures fall outside that window. Final player positions are recorded in the aggregate data because physics timing causes small differences.

| Run | Build | Average host original pictures/s | 1% low | p99 ms | Maximum ms | Intervals >50 ms |
|---|---|---:|---:|---:|---:|---:|
| A1 | previous | 59.68 | 36.26 | 19.10 | 318.08 | 1 |
| B1 | cache change | 60.00 | 54.84 | 17.70 | 21.78 | 0 |
| B2 | cache change | 60.00 | 54.50 | 17.77 | 21.51 | 0 |
| A2 | previous | 60.00 | 51.26 | 18.91 | 29.50 | 0 |

These host presentation measurements do not measure physical display scanout. They are a regression check, not a claimed whole-game FPS gain or proof that the prior large acquire/present or unknown main-thread stalls are fixed. All four measured windows skipped no draws and created no pipelines. Both candidate windows contained no interval above 50 ms; the baseline samples remain variable, so the absence of a rare hitch here is not evidence of a causal fix.

The 25 inspected captures (16 from the normal course runs, nine from validation) show no obvious black or missing geometry, and pause/resume restores the normal colours. One warm-run capture shows the settings menu during scripted quit navigation. Still captures cannot rule out transient artifacts between captures.

## CPU and integration checks

The latest-main integration build passes all 57 CPU-only tests; the r8-compatible build passes six targeted tests. Cache regressions cover 1,000 unchanged polls, retained same-size shutdown obligations, pending changes during a successful save and retries after failed exports. Existing header/driver UUID checks and bounded concurrent-growth snapshot tests also pass.

The r8 actual-course runtime preserves its installed game ABI and is maintained separately from latest main. Unrelated LAN updates merged into main receive the integrated build/CPU checks; these renderer runs do not certify physical LAN play. No NVIDIA hardware, all-course catalogue or universal zero-glitch result is claimed. No cache file, game data, saves or raw private logs are published.
