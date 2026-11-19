# Wonder synchronization investigation

The current shared-input LAN mode can diverge during real gameplay even when
both peers agree on every retained controller packet. Do not describe a matching
transport trace as a synchronized game. See the later manual failure in
[the physical validation report](WONDER-LAN-VALIDATION-2026-11-19.md).

## Applied input and HID timeline

Set `NX_LAN_APPLY_TRACE=1` on both peers before launching a development session.
On diagnostic shutdown, each data root receives `lan-application-trace.csv`.
Preserve it together with the input trace, transport JSON, native log, input
recording, exact runtime identity, settings and session metadata.

The application trace distinguishes completed network exchanges, sampled jobs,
pipeline receive results, exposed input, loading barriers, discarded pipeline
drains, neutral resets and actual HID pad deliveries. It records the guest HID
sampling number. The delivered pad and sampling number are exact; the associated
current commit tick is observed separately after the pad read, and may change
between those operations on different threads. A pipeline sample timestamp is
before insertion, and a submit-return event is after insertion, not its exact time.

Tracing is disabled by default. Enabled tracing retains a session prefix and
recent circular tail in RAM, with counters and an explicit omitted-event count.
There is no per-frame file output. The default capacity is 131,072 events
(approximately 36 MiB on the current Windows build). Override it with
`NX_LAN_APPLY_TRACE_CAPACITY`, from 2 through 262,144 events. Evidence between the
prefix and tail may be evicted, so absence of a retained event is not proof that
it never occurred. Abrupt process termination may prevent shutdown export.

Compare two retained traces with:

```powershell
py tools/perf/compare_lan_application.py host/lan-application-trace.csv client/lan-application-trace.csv
```

The tool checks opposite roles and matching seeds, reports differing applied
pads or event kinds at common ticks, duplicate applications, loading-marker
application and retained drain discards. It keeps retention gaps explicit.
It does not infer actor or world equality from equal applications.

The next controlled gameplay experiment is the same save, character choices,
route and settings with input pipeline enabled versus disabled. This tests a
specific buffering hypothesis; disabling it is not an established fix. Do not
rewrite all guest/kernel clocks or discard additional barriers without evidence.

## Native game networking census

`NX_NATIVE_NET_TRACE=1` enables an independent, default-off SDK call census for
Wonder's existing network functions. It forwards the original SDK handlers and
preserves their return values, CPU effects and exceptions. It enables no host
network access, changes no offline service result and supplies no account data.

The `native-net experimental sdk-census` native-log entries contain allowlisted
scalar parameters, module-relative callers, return/error classification and
duration. Logging samples early calls and powers of two, capped at 256 events.
The allowlist covers local-network-mode selection, IP configuration, UDP socket
operations, polling, errno and selected SSL/friend availability entry points.
Direct HLE calls bypassing these SDK entries are excluded; absence of a census
event does not prove absence of all networking activity.

The purpose is to identify the reachable game-native path before implementing
an adapter. This does not provide Nintendo friends, native LAN rooms or GameShare.
One-host streamed local co-op remains a separate option that needs a physical
controller/streaming test.

## Renderer changes in the same follow-up

Texture uploads check their real source span, including a valid final pitched
row without requiring unread allocation padding. Opt-in background draw skipping
waits for blur/capture, shadow, transparent and stencil-sensitive pipelines.
Deferred replay values are published under the GPU replay mutex, preventing a
concurrent inline submit from replacing them during another replay.

These changes have CPU coverage where applicable. The new replay lock ordering
and the reported reopened-menu blur/level-map shadow symptoms still require
reserved GPU and visual testing. No FPS gain is claimed from these source fixes.
