# Shader preparation and stutter

Recompiling Wonder's CPU executable does not finish the GPU driver's work.
The runtime translates the game's graphics operations to Vulkan. The driver
still compiles pipelines for the shaders, texture types and render states used
on your GPU.

Let startup preparation finish and keep the prepared installation's caches.
The runtime records pipelines encountered during play and prepares recorded
recipes on a later launch. New courses, effects and render-state combinations
can still introduce first-use work. A completed preparation screen means that
the recorded set was processed; it does not mean every possible game pipeline
is already available.

## Integer texture variants

Some translated shaders begin with floating-point texture declarations, while
the game binds an integer texture. The runtime specializes those declarations
to preserve the original texture semantics, including on AMD GPUs.

Observed complete binding maps are now recorded separately in a versioned
`.typed-v1` sidecar beside the legacy pipeline recipe file. Later preparation
can recreate those observed variants using private warmup shader objects.
Actual game shaders continue learning their types from the textures they bind.
One shader's observations are never copied into another live game shader merely
because their source hashes match.

Sidecar entries must match the complete legacy recipe, base SPIR-V checksum,
shader stage and compilation controls. Missing, stale or corrupt metadata falls
back to ordinary preparation. The legacy recipe format remains unchanged.
This cannot recover variants that were never recorded. Existing installations
need to encounter a variant with the new runtime before it can be prepared from
that sidecar on a subsequent launch.

CPU regressions cover map identity, the actual graphics pipeline key builder,
legacy layout, malformed metadata and the disabled path. These checks do not
measure stutter reduction or FPS; those require a controlled repeated route on
the same GPU and settings.

## Reporting a hitch

Keep the native log and frametime trace from the affected session. Record the
edition, course, settings and whether it was a first or repeated visit. Avoid
deleting caches before collecting the evidence.

For a developer source smoke, `run_wonder_source_smoke.ps1` accepts
`-TracePipelineMisses`. Its optional bounded trace records pipeline misses,
compile/wait observations and actual draws omitted while a pipeline is pending.
Use `tools/perf/summarize_pipeline_misses.py` to inspect the resulting CSV.
Tracing is off by default and writes its retained events at shutdown.

Pipeline timings can overlap and worker completion can be observed later.
Their frame association is diagnostic, not an exclusive breakdown of a frame.
Key fingerprints include process-local type identities and must not be used
alone to match pipelines across separate runs.

Shader work is one possible source of a hitch. Texture conversion, file access,
rendering, scheduling and logging can also contribute. Dense frame captures add
readback and disk costs, so capture runs are unsuitable for FPS comparisons.
