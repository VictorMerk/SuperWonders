# Streamed music prefetch

Short file reads in the game's music streaming path can stall on storage even after shader compilation is warm.
The Audio tab's **Audio prefetch (experimental)** option offers small, read-only `content:` BGM and `Env_` `.bwav` files to a
background RAM prefetcher. It defaults **off** and is independent of the texture conversion cache. Native byte checks
pass, but the complete six-run timing comparison failed the 1%/0.1% low regression gates. It is an opt-in experiment,
not an automatically enabled performance fix.

One worker reads at most 8 MiB per stream. On Windows it requests background resource scheduling for its own
thread, covering I/O and memory as well as CPU priority; a rejected request retains ordinary scheduling. It never
changes the priority of the game or other applications. The cache reserves at most 64 MiB across 16 entries, with four queued
jobs and one active job. Open streams retain their snapshots; closed, inactive entries can be evicted. Allocation,
I/O, metadata, queue or budget failures retain ordinary streaming. A guest read never waits for prefetch completion.
Until the complete snapshot is ready, it follows the existing positional file-read path.

This preserves the original compressed audio bytes and decoder. It does not transcode audio, preload all sound
effects, cache saves, write game-derived files to disk, or guarantee elimination of stutter. The initial background
read still consumes storage bandwidth. Disable the option if that competes with other work on your machine.
Changes affect future music opens; turning it off also immediately bypasses already prepared snapshots.

Private diagnostics:

- `NX_STREAM_CACHE=0` forces the feature off regardless of saved settings.
- `NX_STREAM_CACHE_VALIDATE=1` compares each completed RAM read with the original file bytes and reports PASS/FAIL.
  A mismatch uses the freshly read bytes. Validation adds I/O and must be disabled during timing comparisons.
- `NX_HEADLESS_AUDIO=1` explicitly permits the real audio device in otherwise hidden QA runs. Ordinary hidden runs
  remain silent. The presence of `NX_MUTE` always disables the device, including `NX_MUTE=0`.

The persistent setting remains `preload_music` for compatibility; it also controls the explicitly eligible ambience
files. Other sound effects and writable files do not qualify. Runtime logs identify the first RAM read of each open
stream, its completed prefetch count and the current bounded reservation.

The cache test covers exact bytes, EOF, invalid ranges, Unicode paths, metadata changes, active handles, eviction,
limits and shutdown. A Windows native game run with a real XAudio2 device produced 46 matching RAM/file read checks
with no mismatches. Six actual streams (title, two world-map tracks, ambience and two course tracks) were used from RAM,
reserving 19.3 MiB on this route. This is byte validation, not a listening test or a general FPS improvement claim. Linux code
shares the cache and settings, but its audio device path has not been exercised on this Windows machine.

Reference: [Microsoft background thread scheduling](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority).
