# Official upstream synchronization

The official repository was fetched over HTTPS from `https://github.com/vfjoo888/SuperWonders.git`.
The reviewed main tip is `19c25c4e5c228720bcefe4823b4ea45d04a86fd1`, including 13 commits absent from the fork at
`ae02698`. A local backup branch, `backup/pre-upstream-20261117-ae02698`, retains that pre-merge state.
The merge retains upstream ancestry instead of replacing the fork with a downloaded release.

## Incoming changes and review

The diff has 45 source/documentation/licence files, with no added host binaries, dependency manifests, Git hooks,
CI workflows or submodules. Review covered the new layout-generation scripts and their local file publication,
game/camera hooks, renderer/pipeline changes, startup configuration and setup integration. No added download,
remote-code execution, credential collection, startup persistence or antivirus-disabling behavior was found.
This is a scoped source review, not a malware-free guarantee or a full vulnerability audit. No new full-package
antivirus scan was started; the prior r8 scan results do not certify a future changed binary.

Notable incoming behavior: experimental 21:9/32:9 camera/render-target/HUD support, generated wide layouts,
first-house high-FPS dialogue protection, launcher recipe-cache preservation, reduced periodic draw logging,
and Linux aspect startup configuration. The Intel depth workaround was added then reverted upstream; the final
upstream state is retained rather than re-enabling that reverted change.

Ultrawide remains opt-in with original 16:9 as default. Its camera search includes a background raw guest-heap
scan and writes to matched camera fields; performance, thread safety and scene coverage need focused validation.
In particular, camera/culling differences with LAN have not been established safe. The feature is not promoted
to a stable all-course or LAN default simply because it is now in upstream main.

## Preserving the fork

Seven conflicted files were resolved by combining the changes. The merge keeps fixed 60 Hz LAN simulation and
the native two-instance/save-isolation path; 2x–8x frame generation; capture diagnostics; Fit/Stretch and crop
validation; texture conversion/cache bounds; targeted upload/clear synchronization; Circle-confirm/Esc controls;
one-folder setup and literal bracket paths; busy-sidebar scope balancing; FPS/LAN metrics; mod/save/cache UI;
and experimental audio prefetch off by default.

Additional integration adjustments preserve saved custom window heights in the original profile, respect explicit
Windows aspect/resolution environment overrides, account for wide source dimensions in the fork's Fit/FSR/blit
rectangle, feed aspect changes through the shared pending-settings path, and mark aspect resources as requiring
restart. The new layout-preparation wildcard uses an escaped literal root, and runtime packing retains the added
ultrawide licence. Generated interpolation images already have physical capture dimensions; they are not widened
again by blindly inheriting the guest target flag.

## Validation status

Conflict markers and whitespace checks are clear. A restart-aspect regression has been added to the settings tests.
Build, CPU suite and native checks are deferred until the user's active r8 recompilation finishes. The installed
r8 launcher, its build workers, the immutable ZIPs and the brother's current test installation are untouched.
No merged runtime has been deployed or handed to either LAN PC. A future complete kit must regenerate its kit ID
and game code because upstream changed the recompiler/camera patch; old r8 results do not validate that new kit.
