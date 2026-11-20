# Performance suite: frame times, not averages

An average of 60 fps can hold a 160 ms frame. Players feel the slow frames: stutter, hitches and poor 1% lows. This
suite records every presented frame and reports those. Use it for every performance claim and every change that could
affect frame pacing, and compare against a baseline.

Hitch labels are diagnostic indicators, not proof of a root cause. In particular, upload volume is not an upload
duration: an otherwise unexplained long frame stays `unknown` even if it uploaded several megabytes. Worst-frame
lines show upload volume and presenting-thread CPU time alongside the timed waits so unmeasured gaps remain visible.

The design follows Savage Wind's frame trace and hitch analysis (Revivedx/SavageWindRecomp: `runtime/perfcount.h`,
`tools/perf_routes.py`, `docs/PERF-ROUTES.md`): record what each frame did, then name the cause of each slow frame.

## Recording: `NX_FRAMETRACE=<file.csv>`

One row per frame presented to the display (`runtime/src/frametrace.h`). The counters are always on (one atomic add
at events that cost far more); nothing is written without the variable.

| column | meaning |
|---|---|
| `frame`, `t_ms`, `frame_ms` | row number, time since the first present, time since the previous present |
| `acquire_ms`, `queue_present_ms` | `vkAcquireNextImageKHR` and `vkQueuePresentKHR` in this frame (swapchain blocking) |
| `replay_ms`, `encode_ms`, `fence_ms` | the presenting thread recording the game's commands, `vkQueueSubmit`, waiting for the GPU |
| `upload_mb` | bytes through the upload buffer |
| `present_cpu_ms` | CPU time of the presenting thread |
| `guest_presents`, `guest_gap_ms`, `guest_cpu_ms` | the game's own presents since the last row, the gap between its last two, its thread's CPU time |
| `guest_replay_wait_ms`, `guest_pace_ms`, `guest_interval` | the game waiting for the replay (at most two frames ahead), waiting for the vsync pacing, and its present interval (2 = 30 fps) |
| `pipelines`, `pipeline_ms`, `skipped` | pipelines created (any thread) and their time, draws skipped while compiling |
| `astc`, `astc_ms` | ASTC textures decoded on the CPU |
| `file_reads`, `file_mb`, `file_ms` | guest file and RomFS reads |
| `minflt`, `majflt` | page faults (Windows: one count, in `minflt`) |
| `log_line` | log lines written so far: the report prints a hitch's log lines from the run's `NX_LOG` |
| `sample_to_present_ms` | first HID sample associated with this game frame to its host present; -1 when unavailable. Excludes controller polling and display scanout/response. |
| `original_picture` | 1 for the current original picture, 0 for an interpolated picture; generated pictures are not extra simulation ticks |

**Measurement boundary.** The NVN presentation path records host-present timestamps. The display-wait helper is
not wired into this backend, so this trace does not measure scanout, pixel response or full button-to-photon latency.
Do not describe the CSV as proof of zero input lag. With frame generation, each displayed submission is a separate
row; use `original_picture` to distinguish source pictures and disable frame generation for a simple stutter baseline.

On Windows, `tools/trace_performance.ps1 -GameRoot '<prepared installation>'` starts a normal measured game session
and writes `data/performance-<timestamp>.csv`. Quit normally to flush the final rows. The diagnostics collector includes
the two newest timing reports up to 32 MiB each. No trace is recorded during normal play unless explicitly enabled.

## Reporting: `tools/perf/frametrace_report.py`

The default `--pictures original` reports **original-picture cadence at host presentation**. When a trace contains
interpolated pictures, it reconstructs consecutive original-to-original intervals and sums work counters across
the intervening presents. Merely filtering the original rows would incorrectly retain an intermediate's short
`frame_ms` and lose work. This measures host output cadence, not simulation ticks or physical scanout.
Use `--pictures presented --target-fps 240` to inspect a 240-present target separately. Legacy traces without the
picture flag retain their old row interpretation. Baselines must carry the same explicit timing domain;
regenerate an old/unspecified baseline before using the default original-picture gate.

The report includes p50/p95/p99/max HID-sample-to-present proxy values from original pictures only, omitting
unavailable negative samples. It excludes controller transport and display response. The historical JSON field
`missed_vsyncs` counts missed **target intervals**, not observed physical monitor refreshes. Non-finite and
incomplete CSV rows are ignored.

    python tools/perf/frametrace_report.py run.csv --log run.log [--from-s 5] [--to-s 80] [--target-fps 60]
                                           [--json run.json] [--baseline base.json]

- **avg fps**, **1% low** and **0.1% low** (the average fps of the slowest 1% / 0.1% of frames), frame-time
  p50/p90/p99/p99.9/max.
- **Hitches** by size (frames over 25, 33, 50 and 100 ms), and frames over twice the median and 25 ms with a **cause**:
  - `swapchain_acquire` or `queue_present`: the swapchain blocked;
  - `gpu_wait`: waiting for the GPU;
  - `pipeline_build`, `astc_decode`, `replay_cpu`: the presenting thread's work;
  - `game_file_read`, `game_logic_cpu`, `game_waits_replay`, `game_late_other`: the game's own frame came late;
  - `game_asked_30fps`: the game's own present interval 2, as on the Switch, not a stall;
  - otherwise `pipelines_in_frame`, `page_faults`, `upload` or `unknown`.

  A stall names the cause when it explains a quarter of the frame's time over the median.
- **Stutter**: frames more than 4 ms longer than the one before, per minute.
- **Pacing**: the share of frames within 2 ms of a whole number of target intervals, and the missed vsyncs.
- The worst 10 s windows, and the worst hitches with their log lines.
- `--baseline` exits 1 when the run is worse: a 1% or 0.1% low more than 5% lower, more frames over 50 ms, or more
  stutter (over 20% plus 1 a minute).

## The suite: `tools/perf/perf_suite.sh`

    tools/perf/perf_suite.sh --out DIR [--runs 2] [--scenarios "warm cold idle"] [--display offscreen|window]
                             [--ini FILE] [--baseline DIR] [--env VAR=VALUE ...]

**Course scenarios (the default):** course 1-1 played to the goal, from `fixtures/save-course11`: the user's own run
(`tools/play/routes/course11.frames`, recorded on Ubuntu with `NX_INPUT_RECORD`, `docs/PLAY-TOOLS.md`) replayed per game
frame through the menus, the world map and the course. The "play" report covers the player's first move (`@go`) to the
goal (`tools/perf/game_window.py` maps the game frames onto the trace); `<run>.route.md` says where the player went and
whether the goal was reached. The game is not fully deterministic, so a replay can leave the recorded path: a run that
dies or misses the goal is marked **FAILED** (`<run>.FAILED` with the frame it died, and in the summary), never finished
by a bot. `--route FILE` replays another recording.

- `course11`: caches as they are; stops 12 s after the goal (at most 400 s; the trace is flushed every 600 rows and the
  game is killed, so a shorter wait loses the last rows).

**Frame check (every course run).** A run can reach the goal and still draw garbage; the user saw severe corruption
on screen in runs whose numbers looked fine, because nothing looked at the frames. Every course run now dumps five
shots as shown in the window (`NX_PLAY_SHOTS`, `NX_DUMP_FSR=1`): `title` (title+20), `map` (map+150), `start` (go+30),
`mid` (go+1450), `goal` (goal+90), into `<run>.frames/`, and `tools/play/frame_check.py` compares each with
`--reference DIR` (default `<data root>/reference/course11/<shot>.png`) at the best camera shift, and on its own for
black or magenta frames. A run that renders wrong, or misses a shot, is **FAILED** like one that misses the goal
(`<run>.frames.md` has the scores). Make the reference once from a run whose shots a person has looked at
(`frame_check.py --log <run>.log --frames <run>.frames --save-reference <data root>/reference/course11`); a shot without
a reference is reported as unchecked. The thresholds (more than 12% of pixels off by 60+, or a mean difference over 30)
are not calibrated on real runs yet: calibrate them on the first two good on-screen runs and one corrupted run.

Sound is on by default since 2026-11-13 (the user's live run stuttered more than muted replays); `--mute` for the old
muted runs. Compare a run only with runs of the same sound setting.
- `course11-cold`: no pipeline caches, as `cold` below (at most 460 s).

Windows, RX 9070 XT, warm, offscreen, two replays (2026-11-13): both reached the goal in 2899 and 2901 frames after `@go`
(the user's run: 2899); avg 59.5 fps, 1% low 23.8 / 26.6, 0.1% low 14.3 / 15.0, max 72 / 69 ms, 6 frames over 50 ms
each; hitches mostly `replay_cpu` and `page_faults`.

The older scenarios, each from `fixtures/save-course11` along the quick_course timeline (wall-clock input: title,
user, character, world map, course 1-1 to the right; the player mostly stands near the start):

- `warm`: caches as they are, 90 s.
- `cold`: no pipeline caches (put back afterwards) and, on Linux, Mesa's shader cache off; the input starts 30 s later
  for the startup preparation; 130 s.
- `idle`: the title screen, no input, 40 s.

Each run gets a report of its "play" window (from 5 s after the first frame) and of the whole run, then a summary (the
median over runs) and a baseline verdict. The player's save and `pc-settings.ini` are set aside and put back after
every run (checked byte for byte), and the suite refuses to start while the game runs. `--display window` shows the
game fullscreen on the desktop. **The default is `--display screen`** (2026-11-13, the user: tests on screen): a normal
visible window with real presents, so compositor and display timing count. `--display offscreen` is the old hidden
mode (Windows: `NX_HEADLESS_PRESENT=1`; Linux: SDL's offscreen driver). Take the GPU lock as for any game run.

## Optional call timing in frame traces

With `NX_FRAMETRACE` enabled, Windows and Linux input polling and the logger record completed-call elapsed time.
`log_ms` / `input_poll_ms` are totals; `log_max_ms` / `input_poll_max_ms` are the longest completed call since the
previous host present. Logging includes lock acquisition and output; polling includes controller reads and mapping.
Tracing off performs no added clock reads. Calls on different threads may overlap, and a call can span a present,
so these columns are correlations, not a critical-path decomposition or controller-to-photon latency measurement.
The reporter sums totals and takes maxima when reconstructing original pictures, and accepts older CSV headers.
It does not automatically blame a hitch on logging/input. Unit regressions check accumulation, interval reset,
maxima and legacy reports. Native hitch attribution with these new columns remains pending.

## Presentation wait attribution

Frame traces also append `present_pace_requested_ms` and `present_pace_actual_ms` for the frame-generation deadline wait before each host present. The actual duration includes the timer wait and scheduling before the thread resumes. Tracing off adds no post-wait clock read and leaves the pacing deadline unchanged.

The reporter sums both values when reconstructing original-picture intervals. Only actual time beyond the requested duration can contribute a `present_pace_oversleep` label; an intentional long wait is not automatically a hitch cause. This is measured wait correlation, not proof of an OS timer defect. Older traces without these fields remain supported, and their unknown hitches cannot be retrospectively attributed.

## Driver pipeline-cache export diagnostics

`NX_PIPELINE_CACHE_TRACE=1` logs each driver cache-size query with its revision, size and elapsed time. It also logs periodic polls skipped after an unchanged-size observation. This is optional background-export logging, separate from frame-trace attribution. A long query can correlate with a hitch without establishing that it blocked the presentation thread.

Successful same-size observations suppress further periodic queries until a pipeline changes the cache. They still owe a forced shutdown save, because serialized size alone cannot establish that the bytes are unchanged. Failed size queries, snapshots and file commits remain retryable; changes arriving during a successful save remain pending. Shutdown waits for the background writer before deciding whether a final export is owed.

## Source-smoke capture evidence

`tools/perf/run_wonder_source_smoke.ps1` monitors fresh PPM captures against
`-MaxCaptureMiB` (256 by default). Reaching that budget stops only the owned test
and fails the smoke, including when the final capture completes between polls.
The runner refuses a fixture with existing PPMs before launch because runtime
capture filenames restart and could overwrite those images.
The check is polled, so captures can exceed the threshold
between polls; it is not a filesystem quota. An interrupted final image can be
incomplete and must not be counted as valid visual evidence.

After the process stops, fresh captures are copied into the new output's
`frames` folder and SHA-256 checked before their exact source files are removed.
Older fixture captures and unrelated files remain untouched. This keeps one
verified copy instead of retaining duplicate capture trees. Reuse neither a live
save nor another test's output as the fixture. Dense readbacks add GPU and disk
cost and are unsuitable for an FPS comparison.

## Paired shadow diagnostic captures

For a controlled native named-shot test, `NX_TRACE_SHADOW_SHOT=<shot-name>`
uses that scheduled `NX_PLAY_SHOTS` shot to arm the next six original frames.
The arming image is excluded. Enable the existing shadow-only request mode and
set frame generation, pipelining and the replay worker off. Automatic periodic
frame dumps and the general trace mode are rejected for this experiment.

Each captured frame checks that both the actual guest-frame counter and the
presentation counter advanced by exactly one. A repeated/skipped counter or an
unsupported mode change invalidates the diagnostic. The log maps both counter
values; verify all six entries against the captured filenames and trace window.
External trace-file requests are deferred until the paired window ends. Detailed
fragment-filtered projection tracing uses a fresh bounded request budget so it
cannot silently spend that budget on earlier loading frames. Choose the actual
receiver shader filter for the scene; absent draws remain missing evidence.

This opt-in path changes no draw or pacing policy and is disabled in ordinary
play. It aligns diagnostic evidence; it does not fix shadows or certify motion,
visual correctness or performance. Readbacks and tracing are unsuitable for FPS
comparisons. Preserve a bounded output directory for each attempt.

## Rules

- Two runs per scenario at least; compare medians.
- Report the 1% and 0.1% lows and the hitch causes, never an average alone.
- A change that touches frame pacing, the present path, pipelines, uploads or loading runs the suite against the
  build before it, and must not be worse.
