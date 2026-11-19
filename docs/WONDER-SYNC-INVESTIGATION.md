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
sampling number. Framework events also record their invocation sequence and
present counter before/after the call, including calls skipped by the current
present-based input exchange. A call without a present is not by itself proof
that gameplay advanced. The delivered pad and sampling number are exact; the associated
current commit tick is observed separately after the pad read, and may change
between those operations on different threads. A pipeline sample timestamp is
before insertion, and a submit-return event is after insertion, not its exact time.

Tracing is disabled by default. Enabled tracing retains a session prefix and
recent circular tail in RAM, with counters and an explicit omitted-event count.
There is no per-frame file output. The default capacity is 131,072 events
(approximately 38 MiB on the current Windows build). Override it with
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
It also reports a changed delivered pad under the same player/HID sample number
as a candidate inconsistency, while counting identical repeated reads separately.
It streams the CSV and keeps compact indexes rather than retaining all event
dictionaries. Gaps can hide changes; cross-PC sampling-number offsets are not
treated as a desync verdict.

The next controlled gameplay experiment is the same save, character choices,
route and settings with input pipeline enabled versus disabled. This tests a
specific buffering hypothesis; disabling it is not an established fix. Do not
rewrite all guest/kernel clocks or discard additional barriers without evidence.

## Native game networking census

`NX_NATIVE_NET_TRACE=1` enables an independent, default-off SDK call census for
Wonder's existing network functions. It forwards the original SDK handlers and
preserves their return values, CPU effects and exceptions. It enables no host
network access, changes no offline service result and supplies no account data.
Wrappers are installed after module linking and before guest constructors or
threads execute, so installation does not race existing guest SDK calls.

The `native-net experimental sdk-census` native-log entries contain allowlisted
scalar parameters, module-relative callers, return/error classification and
duration. Logging samples early calls and powers of two, capped at 256 events.
Normal process exit also logs at most 13 cumulative operation summaries. Their
atomic counters are non-transactional; a concurrent call may still be finishing.
The SDK allowlist covers local-network-mode selection, IP configuration, UDP socket
operations, polling, errno and selected SSL/friend availability entry points.
Direct HLE calls bypassing these SDK entries are excluded; absence of a census
event does not prove absence of all networking activity.

Three additional Product120 main-module probes observe the LAN state factory,
the common network-state constructor and network preparation. They require the
exact source build identity and matching 32-byte code-prefix hashes before any
main entry is wrapped. Unknown/mismatched builds keep their original entries.
The constructor/preparation probes report only an integer mode from 0 through 3;
they read no guest object and suppress opaque return values. The factory probe
observes entry without forcing selection. This separates an unselected factory
from later SDK setup failure without changing game behavior.

The purpose is to identify the reachable game-native path before implementing
an adapter. This does not provide Nintendo friends, native LAN rooms or GameShare.
One-host streamed local co-op remains a separate option that needs a physical
controller/streaming test.

## Native UDP adapter core

`native_lan_udp` is a separately tested transport foundation. It is compiled on
Windows/Linux desktop, but has no game caller, environment activation, HLE hooks
or service integration. Constructing it without explicit enablement creates no
host sockets. It cannot currently enable a native Wonder room.

The core implements bounded IPv4 UDP descriptors, BSD address conversion,
network byte order, private/local destination checks, bounded receive copies,
datagram truncation reporting, nonblocking send/receive/peek, polling, broadcast
and address-reuse options, mapped errors and cancellation. Shared socket ownership
protects pending polls from OS-handle reuse during close. Tests use loopback only.

Important integration boundaries remain: guest-memory validation, exact imported
ABI wrappers, nifm lifecycle and interface selection, blocking-operation retry,
multicast membership and any additional socket options actually used by Pia.
TCP, IPv6, connected UDP and Nintendo account/cloud services are unsupported.
The core always uses nonblocking host sockets, including when send/receive flags
are zero. Poll accepts finite timeouts from 0 through 1,000 ms; a future adapter
must implement longer/infinite guest waits with cancellation-aware retries.
Windows tests do not establish Linux execution or physical LAN gameplay.

The BSD address layout does not imply FreeBSD error numbers. Verified Product120
call sites expect would-block error 11. The other Linux-numbered error mappings
follow the matching SDK/MKW ABI and still need confirmation against the native
game path before guest handlers are installed.

## Renderer changes in the same follow-up

Texture uploads check their real source span, including a valid final pitched
row without requiring unread allocation padding. Opt-in background draw skipping
waits for blur/capture, shadow, transparent and stencil-sensitive pipelines.
Deferred replay values are published under the GPU replay mutex, preventing a
concurrent inline submit from replacing them during another replay.

Descriptor-pool finalization/reuse now retires separately owned snapshots and
removes stale reverse registrations. Replacing a slot with an absent texture
invalidates its previous snapshot key. Old descriptor maps and deferred objects
remain available to in-flight replays. CPU tests exercise repeated pool/slot churn
without registry accumulation; this addresses a demonstrated ownership leak.

Command-list epoch capture also shares the object-registry lock. Previously it
could observe an intermediate point in a multi-object retirement: a snapshot had
retired but its referring pool had not, allowing replay to retain the pool and
lose the snapshot. A CPU regression uses the actual incarnation implementation
to reproduce that intermediate mark and verify coherent capture. This does not
require a GPU, and it does not prove that this race caused the reported blur.

These changes have CPU coverage where applicable. The new replay lock ordering
and the reported reopened-menu blur/level-map shadow symptoms still require
reserved GPU and visual testing. No FPS gain is claimed from these source fixes.
