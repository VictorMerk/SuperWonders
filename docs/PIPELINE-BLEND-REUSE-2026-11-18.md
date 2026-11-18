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

Native timing and rendering validation are pending the shared GPU reservation. The previous verified shader/graphics changes are on main; this additional optimization is not yet installed or claimed to improve gameplay FPS.
