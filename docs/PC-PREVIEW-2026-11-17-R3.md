# PC follow-up: picture shape, cache robustness and useful diagnostics

This follows [preview r2](PC-PREVIEW-2026-11-17.md). Work remains in the fork's draft PR. It does not claim all
graphical defects, crashes or stutters are fixed.

## Picture shape

Video now includes **Preserve aspect ratio**, on by default. The normal PC output previously stretched the source
to every window rectangle. The fitted path centres it with black bars where required, while Off retains Stretch.
FSR 1 scales into the fitted content rectangle and copies at its correct offset; FXAA and the final overlay keep
working. The settings menu covers the whole window. This preserves the original picture shape; it does not expand
the game's field of view or implement an ultrawide camera mod.

Crop validation is shared by the blit, present-pass, FSR and frame-generation paths. Negative origins, empty sizes,
out-of-bounds rectangles and signed-overflow cases fall back to the complete picture consistently.

The new `NX_PRESENTATION_PROBE=1` renders an original synthetic picture through the real PC swapchain path without
game files. With `NX_HEADLESS_PRESENT=1`, `NX_DUMP_PRESENTED=1`, `NX_DUMP_FRAMES=1`, height 720 and window widths 1000
and 1720, [the readback checker](../tools/perf/check_presentation_probe.py) passes **20/20** cases. These cover Fit
and Stretch with FSR/FXAA off and on, a cropped picture and an invalid crop. It checks every picture row's colour
and completely black bars, rather than accepting a successful Vulkan call as proof of correct output.

## Settings that explain their state

After the launcher hands over to the game, the overlay snapshots the startup settings. Changes to resolution,
texture profile, anisotropic filtering, shader loading, game language or effective button prompts now produce a
persistent **Restart pending** banner listing the affected resources. Returning the settings to their original
values clears the banner. Live changes such as volume, interpolation or render-ahead do not create a false warning.
Unused custom controller bindings do not count as an effective prompt change. The banner is German in the German
menu; descriptions for the new latency/interpolation/aspect controls are also provided in German.

Description panes now retain a row identity and refresh its current text. Previously, a language switch or removed
row could leave stale descriptions visible until another row was hovered. The native input/layout probe exercises
the pending banner and the return to the startup configuration in addition to Esc, F1 and controller input isolation.

## Pipeline-cache robustness

The loader reads and validates the small Vulkan cache header before allocating for the whole file. It rejects
truncated/oversized headers, incompatible GPU/driver UUIDs and files above 512 MiB, retaining the existing file and
starting with an empty driver cache. A failed complete read is never passed to the driver as partial cache data.
This validates metadata and size, not every byte of the driver's private cache format.

A compile can grow the driver cache between its size query and data copy. The writer now retries `VK_INCOMPLETE`
at most three times and commits only a complete snapshot through the existing atomic writer. Continuous growth,
driver errors and allocation failures leave the previous warm file intact. This follows the
[Vulkan cache-data contract](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineCacheData.html).

## Measurements and validation

The timing reporter now reconstructs original-picture cadence separately from generated presents. It keeps the
work performed between originals and reports original-picture HID-to-host-present proxy percentiles. Neither
metric measures physical scanout or full controller-to-photon latency. Seven Python regressions cover 4x-style
timelines, work aggregation, window boundaries, legacy traces, missing samples and damaged CSV tails.

The configured Windows suite passes **26/26**, including the new cache-header/snapshot and presentation-geometry
tests and the Python reporter tests. The additional native presentation readbacks pass 20/20.

Two warm native route runs of r2 and two of this candidate used frame generation off, the same private test profile,
720p/60 target and the same repeat-pause/movement input route. No screenshots or concurrent build jobs ran during
these four measured cases. The play interval was aligned using the course anchor and guest-present counters,
covering course-relative frames 60..2100. The candidate medians passed the existing regression thresholds for 1%
and 0.1% lows, frames over 50 ms and frame-to-frame swings. This is a short route on one GPU, not a general speedup
claim; warmup order and normal run variation still matter.

Separate visual-capture runs are excluded from those timing results: saving a large swapchain screenshot itself
can create a visible hitch. The clean runs still show costly initial texture preparation, including CPU ASTC decode
in the low-VRAM BC3 path. That remains the next measured target, rather than attributing all pauses to shaders.
