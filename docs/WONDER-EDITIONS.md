# Wonder edition support investigation

This checkout adds an external-runtime library for the two Wonder editions and a checked NUP reader. It does **not** yet implement or certify Switch 2 Edition gameplay. Mario Kart World is a technical reference, not a third launcher entry.

## Shared launcher

Run `Start Wonder Library.cmd` on Windows with Python 3 and Tk, or `python tools/wonder_library.py`.
Choose each edition's prepared runtime executable and its data folder. The library starts the chosen executable with that folder as `NX_DATA_ROOT`. Both editions must use different data folders. Existing saves are not copied, converted or migrated. An unconfigured edition cannot be started. An arbitrary NUP is not a prepared runtime.

The library stores its selection in `%LOCALAPPDATA%/SuperWonders/library.json` on Windows, or `~/.local/share/SuperWonders/library.json` on Linux. `--config PATH` selects a task-local configuration. Closing the library leaves a started game running; it does not kill external games. The library prevents a second game that it owns from starting while its first is still running.

## Package inspection

With Python `lz4` and `zstandard` available:

```text
python tools/inspect_wonder_package.py GAME.nup --json private/package-report.json
python tools/inspect_wonder_package.py GAME.nup --verify-all --json private/verified-package.json
python tools/inspect_wonder_package.py GAME.nup --extract-exefs private/new-source/exefs
```

Inspection does not modify the package. The report enumerates every title rather than silently selecting the last one. Extraction requires a new destination, validates paths and verifies decoded lengths and recorded nonzero SHA-256 digests. Multiple-title extraction requires an explicit `--title-index`. Compressed file inspection needs `zstandard`; SDK inspection additionally needs `lz4`. All game-derived reports and extracted files belong in `private/`, or outside the repository.

Neither a `.nup` extension, a title ID, an NVN SDK marker nor a filename establishes that a package contains all of a Switch 2 Edition's content. The supplied package was measured to contain an NVN/Maxwell executable, `Product.120` resource tables and upgrade-store prompts. A full title-file verification passed. Comparing its stage/actor tables to the existing base-game dump found identical record counts and no added string values. The package's English messages advertise purchasing/downloading Bellabel Park. These observations strongly suggest the export contains the updated base portion; they do not establish playable upgraded content. Preserve the file and investigate the complete upgrade/native title export before treating it as a complete second edition.

The measured main executable differs from 1.0.0. At initial inspection, the runtime's timing, camera, menu and deterministic-seed sites were tied to 1.0.0. Do not pass this source through that old patch table, mix its assets with a 1.0.0 generated library, or mark it supported because a package check passes.

## Executable port in progress

The continuation adds an explicit layout for the supplied Product.120 main build. `recomp/nso.py` preserves the original NSO identity; generated module tables carry it to the runtime. The game-library ABI is raised to 2, so a runtime cannot load an old library with a different module-table layout. Existing installed runtimes and their libraries are not overwritten.

The new timing patch preserves Product.120's interleaved `ADRP x11`; the old eight-word replacement is not used at shifted addresses. All timing and camera sites are validated before writing any instructions. Menu callbacks were located from enum registration and the title wait state. Wonder Seed hooks were matched to their body-switch implementations and contact-check helper. All six runtime hook signatures pass against the supplied executable. The LAN adapter selects this source's framework boundary and five exact RNG initialization sites, whose branches all target `GetSystemTick`; ordinary clocks remain unchanged. These static checks do not establish native multiplayer synchronization.

The builder retains each module's recompiler coverage report in the game-data folder and refuses a build with unhandled CPU instructions. Unit tests cover this failure condition, unknown source identities and atomic patch rejection. Both supplied modules passed with zero unhandled instructions. Matching shaders, game code and runtime have been built.

Native testing exposed two compatibility defects. The cold-start account API now explicitly returns no retained user contexts rather than leaving its output uninitialized. The newer SDK moved its complete, unchanged 208-entry texture-format table from `0xd34198` to `0xd351a8`; the runtime selects the new location by SDK identity and checks representative format geometry before allocating pools. The old address produced incorrect block sizes and texture-upload access violations.

After these corrections, a reserved 12,000-frame run exited deliberately with code 0 and 400 fresh captures. Visual inspection confirmed the title screen, user/player/character selection, opening Flower Kingdom scene, movement, jumping and intro cutscene. New-scene frame drops occurred; this is not a stable-performance certification. Saving/restarting, full courses, native Switch 2 features and Bellabel Park still require separate verification. Vulkan validation layers were not enabled in this run.

The first visual run did not establish working sound: its log reported no recognized audio-renderer instance. The SDK also moved the renderer vtable and execution function. These are now resolved by their exported symbol names; the renderer's execution-mode and device member offsets were checked against both SDKs. Subsequent native runs produced non-silent, ten-second 48 kHz stereo captures and advanced through the opening cutscene. The smoke harness writes `audio.wav`; `-RequireAudio` makes a missing capture a failure. Hardware output was muted, so these checks do not establish audible quality or the audio device path.

Later gameplay cleared the first course and exercised the Wonder Flower effect. A new process loaded the saved world-map position and both recorded Wonder Seeds. However, one longer run crashed in texture untiling when re-entering the first course after its first clear. The following traced reload/replay run exited cleanly after 12,000 frames, but that does **not** establish that the intermittent crash is fixed. Opt-in source tracing now records texture/pool metadata and diagnoses unreadable guest source ranges before dereferencing them. A separate fresh-save fixture is used to reproduce the first-clear transition. Full support remains unverified while this fault is unresolved.

A separate startup fixture remained at a blank title scene while music and presents continued. Controls with an unchanged table, disabled menu overrides and an empty save folder reproduced it. Comparing file traces identified required event resources that opened under the shorter successful fixture path but were reported missing under the longer failing path. A native CRT probe reproduced 18 failed reads beyond Windows' ordinary path limit; the same probe using extended absolute paths had zero failures. Guest filesystem roots and Windows CRT opens now use extended paths, normalizing guest separators after each path join. Regression tests cover metadata and content reads on a long Unicode path and joins onto an already extended mount root. The previously failing fixture then completed 7,200 native frames with 60 fresh captures, non-silent audio and deliberate exit code 0. Visual checks confirmed user selection and the opening Flower Kingdom scene; the file trace confirmed formerly missing event resources opened successfully. Resource-budget growth also preserves the original table's eight-byte alignment, but alignment alone did not repair startup. Windows crash reports include plain thread-local metadata for an active texture upload without requiring full upload logging; timed thread dumps include wait ownership as well as call chains. These diagnostics do not repair the separate course re-entry fault.

The user's visible session on the pre-integration runtime closed normally with exit code 0. After integrating the repository's newer AMD/pipeline changes, the runtime rebuilt and CPU checks passed. A subsequent 12,000-frame native run with menu overrides disabled reached the title and intro, captured non-silent audio and exited deliberately with code 0. The path repair above subsequently addressed the startup failure; the original course re-entry crash requires separate coverage. Frame generation and user-selected settings prevent treating the manual session as a controlled performance comparison.

After the path repair, a fresh-user run with all generated menu assets completed 40,000 native frames and exited deliberately with code 0, preserving 133 fresh captures and non-silent audio. Visual checks covered the intro, world map, first course, Wonder effect and a death/checkpoint reload. The reconstructed input route diverged and did not finish the course before the frame limit, so this run does not certify the first-clear/re-entry transition.

An actively reviewed follow-up cleared the first course with Nabbit on the one-seed route, saved the result, returned to the map and re-entered the cleared course to its playable starting area in the same process. It completed 98,477 native frames, 328 fresh captures and non-silent audio, then exited with code 0 through the explicit shutdown request. The generated pause menu, including its Quit Game entry, and Restart Course were visually checked in a separate run. That separate run timed out during navigation and is not a passing smoke. The successful Nabbit transition does not establish that the original two-seed/Elephant re-entry fault is repaired, nor does it certify player-coordinate tracking, all courses or native Switch 2 content.

`tools/perf/check_wonder_captures.py RUN/frames` checks a spread of fresh native PPM captures for non-flat content in the centre, outside the metrics window. It rejected the blank-title captures and accepted a visually inspected gameplay run. This detects flat output, not correct rendering or gameplay. Pass `-ContentPython PATH_TO_PYTHON` to the PowerShell smoke runner to require this check in addition to process/capture/audio checks; without it, the result explicitly reports that content was not checked.

`tools/perf/run_wonder_source_smoke.ps1` is the actual native-game test harness for follow-up runs. It acquires the machine reservation, starts the specified runtime on a task-owned fixture, records native logs/frame traces/captures, checks deliberate frame-keyed shutdown and preserves each run under a new output directory. The library CMD opens the GUI; it is not this test. A native smoke success still does not certify all courses, visuals, multiplayer or the contents of the upgraded edition.

For an actively reviewed scenario, `-QuitFile NEW_MARKER_PATH` selects request-driven shutdown: create that file only after completing the scenario. The runtime polls it and uses the normal render-thread exit path. An existing marker is rejected before acquiring a reservation. With this option, reaching the fallback frame limit does not satisfy the shutdown check; the result must contain the explicit request-driven exit. The marker does not establish that any gameplay objective was completed.

A further run used a copy of an existing save with both first-course Wonder Seeds collected. On the runtime integrated through `71b0b4f`, Blue Toad loaded that course, returned to the map and repeatedly re-entered its playable starting area without a crash. The run completed 42,685 native frames with 142 fresh captures, non-silent audio and an explicit request-driven exit code 0. The course card and pause screen visibly retained both seeds. This covers saved two-seed re-entry; it does not reproduce the original Elephant-form first-clear sequence or establish a fix for that earlier fault.

The library's original-edition launch configuration was also exercised with the installed original runtime and an isolated copy of its save, settings and caches. It reached a rendered title screen and closed through its ordinary window-close request. This confirms that the original slot starts the separate compatible runtime. It is a title-screen check, not an original-edition gameplay regression or a test of the new runtime against an ABI-1 game library.

## MKW reference and AMD fixes

[mkw-recomp](https://github.com/adriadam10/mkw-recomp) implements static AArch64 recompilation, NVN2 over Vulkan and ahead-of-time SM86-to-SPIR-V translation. That is a substantial reusable starting point **if** the target executable/shaders need NVN2/SM86. It is not a replacement for inspecting Wonder's imports, assets and game-specific hooks.

On 18 November 2026, the connected GitHub API reports the user's [graphics compatibility PR #458](https://github.com/adriadam10/mkw-recomp/pull/458) and [resource lifetime PR #459](https://github.com/adriadam10/mkw-recomp/pull/459) merged. The [Ellesmere static alpha-to-coverage PR #560](https://github.com/adriadam10/mkw-recomp/pull/560) remains open. These are correctness fixes, without a demonstrated Wonder speedup.

The inspected Wonder runtime already bakes alpha-to-coverage into its pipeline (`nvn_pipeline.cpp`) and applies cube compatibility only to cube targets (`nvn_image.cpp`). Those two MKW remedies cannot simply be copied here as new fixes. Logical warp handling also exists in Wonder's separate Maxwell translation path. Resource-lifetime changes need matching ownership analysis, not a wholesale cherry-pick between unlike backends.

It is reasonable to aim for the upgraded edition as the eventual main option if it includes the desired base content. Keep the original available until the new edition is verified. Better console visuals do not prove better PC performance, especially on an RX 570; measure base frame rate, simulation speed, memory use and final rendered images on the actual build.

## Validation

`python tests/nup_package_test.py` checks synthetic raw/compressed packages, content hashes, malformed tables, paths, cycles, extraction isolation and read limits. `python tests/wonder_library_test.py` checks isolated launch arguments, separate data roots, missing installations, configuration persistence and developer-environment cleanup without starting a game. UI construction was checked with both unconfigured Play buttons disabled. No native gameplay or FPS claim follows from these checks.

NUP layout reference: `adriadam10/mkw-recomp` commit `dd9297545beb9d2c90170eea93a01030961bf7cf`, `tools/prepare/nup.py` and `runtime/sdk/fs/nup.c`, MIT. The bounded reader here is independently implemented from that documented layout. The reference's notice is preserved in `third_party/mkw-recomp-LICENSE`.
