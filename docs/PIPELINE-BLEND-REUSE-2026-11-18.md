# Reuse pipelines across irrelevant blend state

The renderer previously included all eight NVN blend records in every graphics-pipeline identity, including disabled blending and targets beyond the current attachment count. Draws could therefore create another Vulkan pipeline solely because stale blend factors changed, despite identical output. Recorded startup recipes retained the same redundant variants.

The cache now clears blend parameters when blending is disabled, clears blend/write-mask metadata beyond the attachment count, and excludes the NVN target-routing tag after routing has selected an array entry. Active blend factors/equations, live colour-write masks, attachment formats, depth state and all unrelated pipeline fields remain distinct. Driver creation and persisted recipes use the same canonical values. Existing recipe files are compacted before prewarming, and new recipes use the same identity.

This follows the [Vulkan blending rule](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineColorBlendAttachmentState.html): disabled blending passes the source fragment colour through unchanged. The canonical parameters still translate to valid Vulkan blend factors and operations. No shader, graphics-quality, frame-generation or pacing setting is changed.

## CPU checks and recipe audit

The `pipeline_blend_key` CTest passes on the integration and r8-compatible source. It covers inactive-state equivalence, idempotence, depth-only passes and preservation of active blending/write masks at every attachment index for one through eight targets. Format and depth changes also remain distinct. The integration pipeline translation unit compiled, and the full r8 comparison executables build with identical opt-in probe instrumentation.

A read-only audit using the compiled canonicalization helper examined 3,449 recorded r8 recipes. Their identities reduce to 2,218: 1,231 entries (35.7%) differ only in ignored blend metadata. This is an offline recipe-identity count, not measured compilation time, gameplay FPS or proof that every hitch is fixed. Raw recipes and machine-specific paths remain private.

## Native replay probe

`NX_PIPELINE_RECIPE_PROBE=1` initializes Vulkan, prepares the shader catalogue, replays recorded pipeline recipes, saves any compacted recipes and exits before the guest game starts. Use only an isolated valid cache fixture, and acquire the machine-wide reservation described in `AGENT-WORKFLOW.md` before launching it. The probe can rewrite fixture caches. Check the ready/total/skipped counts and Vulkan diagnostics; a process exit alone does not certify every pipeline was created.

For A/B timing, restore identical shader/recipe/driver cache inputs before every process, use identical probe instrumentation in both executables, enable synchronization validation for correctness runs and disable validation for timing runs. Prewarm duration measures host pipeline preparation, not an ordinary game frame or GPU rendering time.

Native replay completed on the AMD Vulkan device. Validation-enabled baseline and candidate runs prepared all 3,449 and 2,218 recipes respectively, with no skipped recipes or Vulkan validation errors. Validation runs are correctness checks and are excluded from timing comparisons.

Three restored-cache samples per build, in ABBAAB order with validation disabled, gave baseline prewarm durations of 1,202.4 / 1,193.2 / 1,154.3 ms and candidate durations of 1,112.5 / 1,092.2 / 1,169.8 ms. The medians were 1,193.2 and 1,112.5 ms. The sample ranges overlap; this is a limited warmed host-preparation measurement, not gameplay FPS or cold driver compilation.

A synthetic draw/readback probe also checks opaque output, live alpha/additive blending, channel write masks and independent multiple-render-target blending. The baseline and canonicalized implementation produced identical pixel digests. The candidate passed cache-identity checks in synchronous, pipelined and asynchronous submission modes, with synchronization validation enabled and no validation errors. The baseline reproduced the redundant creations while retaining the same pixels.

These results apply to blend-state normalization alone. A subsequent audit found uninitialized depth-state padding; its additional fix and validation are documented separately. The installed runtime and source branch must still be distinguished when testing gameplay.
