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
that gameplay advanced. The current HID path captures the delivered pad's source,
exposed commit tick, player count/connectivity and local loading marker together
under the existing input mutex. It adds analog-stick direction buttons before
recording the actual delivery. LAN sample identity is exposed commit tick plus
one: lobby tick zero is sample one, repeated reads retain their identity, and a
new publication can change it without a present. Transport-only loading pumps
do not publish samples; only the existing normal input application and final
loading-neutral application do. Non-LAN sampling and ordinary clocks are unchanged.
The signed HID identity bound is checked before publication; exhausted/regressing
identities or reuse with different pads fail rather than silently wrap.

Exports from this path declare `hid_identity=exposed_commit_plus_one` in the
existing schema-one metadata. Older or unknown-contract traces retain their
original limitation: the callback observed a commit tick separately after the
pad read, so another publication could intervene. Presentation counters and log
timestamps remain later observations in either version. A pipeline sample timestamp is
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
treated as a desync verdict. Recognized atomic-contract traces additionally check
the tick-plus-one identity and report contract mismatches explicitly. Absence of
the declaration means legacy/unknown behavior, not an inferred atomic snapshot.

The snapshot change fixes a source-proven inconsistency: the old path wrote
`g_frame + 1` before acquiring the input lock, although a loading transition could
publish a different neutral pad without a present. The retained physical failure
traces do not contain the HID history needed to prove this was their first cause.
CPU publication/read regressions cover repeated reads, loading publication,
signed identity bounds and concurrent metadata/pad coherence. A fresh controlled
gameplay reproduction is still required; this is not a world-synchronization fix
certification.

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
Normal Windows shutdown explicitly emits at most 15 cumulative operation
summaries before `ExitProcess`, with an idempotent `atexit` fallback. Abrupt
termination can still lose summaries. Sampled entries are logged as calls finish.
The atomic counters are non-transactional; a concurrent call may still be finishing.
The SDK allowlist covers local-network-mode selection, IP configuration, UDP socket
operations, polling, errno and selected SSL/friend availability entry points.
Direct HLE calls bypassing these SDK entries are excluded; absence of a census
event does not prove absence of all networking activity.

Five additional Product120 main-module probes observe LAN state allocation,
the common network-state constructor, network preparation, activation entry and
selector entry. They require the
exact source build identity and matching 32-byte code-prefix hashes before any
main entry is wrapped. Unknown/mismatched builds keep their original entries.
The constructor/preparation probes report only an integer mode from 0 through 3;
they read no guest object and suppress opaque return values. The factory probe
observes allocation without forcing selection. Ordinary setup
constructs all three modes, so allocation alone is not proof of LAN selection.
Activation and selector probes copy only a checked four-byte mode scalar under
a guest-memory mapping lease; unreadable/unknown values are reported as unavailable.
An entry observation does not prove activation completed. These probes preserve
the original handlers and never select or force a networking mode.

The census still observes the original path; it is skipped when the owned native
provider below is installed. It does not provide Nintendo friends, native LAN
rooms or GameShare. A matching call trace does not establish room creation or
gameplay synchronization.

## Opt-in native networking provider

`NX_NATIVE_LAN=1` now installs a Windows runtime provider for the verified Wonder
Product120 source. Both module identities, code-prefix hashes, aliases and
existing handlers are checked before an atomic startup transaction, ahead of
import relocation and guest execution. The checked profile replaces 180 aliases
and preserves 39 others. Disabled startup leaves the existing handlers unchanged.
Native mode is exclusive with shared-input LAN and the Steam developer mode; it
does not force the game to select its native LAN factory.

The installed provider connects the bounded UDP core, guest ABI adapter, mapped
memory access and local-interface/nifm lifecycle. Its socket initialization owns
real host resources and coherent guest TLS errno aliases; it does not fabricate
Nintendo's original SDK IPC client or modify that client's initialization state.
Provider initialization can succeed while the network remains unavailable.
Availability requires local mode, an explicit request and a real, unambiguous
eligible private interface. Internet mode stays offline. The actual-host
preflight found four eligible interfaces and correctly reported ambiguity;
this host requires an explicit `NX_NATIVE_LAN_ADDRESS` selection. That preflight
does not establish a connection to the second PC.

Windows interface enumeration supplies actual IPv4 address, prefix, gateway and
DNS metadata. Unknown required metadata fails configuration rather than becoming
an invented value. Reported public DNS addresses are metadata only; the provider
performs no DNS queries. Linux transport/interface foundations exist, but the
runtime guest-memory mapper currently fails closed outside Windows; per-interface
Linux DNS metadata also remains unsupported.

The guest boundary validates numeric addresses through `RuntimeGuestMemory`.
Successful validations pin the covered mappings and permissions through the
whole call, including a poll wait. Overlapping runtime VM mutations wait for the
pins; unrelated VM operations can proceed. Caller CPU state is preserved except
for the declared return value; errors use the calling guest thread's TLS.
Unexpected output-copy failure after host I/O stops dispatch without falling back
to an original handler that could repeat the operation.

Source-specific behavior is retained: raw IPv4 input can have length byte zero,
normalized only in a private copy; sender capacity can be 28 bytes; optional
sender outputs avoid unused pointer reads. SDK zero-sized send/receive returns
zero without transmitting or consuming a datagram. The underlying host core
separately supports real zero-length UDP packets. Verified Product120 call sites
expect would-block errno 11 despite the BSD address layout. Other Linux-numbered
error mappings follow the matching SDK/MKW ABI and still need native-path checks.

The core implements bounded descriptors, private/local IPv4 policy, nonblocking
send/receive/peek, truncation, cancellation and poll. Shared ownership protects
pending polls from OS-handle reuse during close. The adapter provides real scalar
socket-option setting/readback, socket-name queries and shutdown. Unicast TTL
accepts 1 through 255; reuse-port uses the real platform capability and is
explicitly unsupported on Windows. Fcntl exposes the actual always-nonblocking
contract: requests for blocking behavior are unsupported. Poll accepts finite
0 through 1,000 ms waits; longer/infinite guest waits, multicast membership,
additional options, TCP, IPv6 and connected sends remain unsupported. Nintendo
accounts, online authentication, cloud services and GameShare are not provided.

The native log contains bounded call diagnostics. Explicit Windows shutdown now
stops this provider and exports `native-lan-stats.json` before `ExitProcess`, with
`atexit` retained as a fallback. Its counters are non-transactional; durations
include waits and are not RTT. Returned receive bytes include peeks and do not
measure wire traffic or prove world-state agreement. Forced termination can
still prevent export.

## Steam AppID480 developer prototype

`NX_STEAM_DEV=1` enables a separate Windows API-bootstrap prototype for one-host
local co-op experimentation. The developer launcher prepares an isolated fixture;
the runtime checks executable-directory/CWD agreement, an AppID480 file and an
explicit caller-provided `steam_api64.dll` path. The controlled loader avoids
implicit current-directory/PATH DLL searches. Normal startup neither loads Steam
nor changes AppID files, the working directory or the normal build's DLLs.

The bootstrap calls initialization before the launcher, pumps callbacks from
Wonder's presented-frame hook and shuts down explicitly in the Windows exit path.
Callback execution holds no bootstrap state mutex; reentrant shutdown is deferred
until the active callback returns, with serialized, exactly-once teardown.
It does not restart Steam, read friends or send invitations. The official SDK is
not available in the current setup, so real SDK/client initialization has not
been tested. Injected CPU mocks cover the lifecycle and failures. Initialization
success alone would not certify the overlay, Remote Play Together, lobbies, P2P
transport or usable streamed multiplayer.

## Current validation boundary

CPU regressions cover transport/ABI loopback, guest VM pins, provider
lifecycle/profile dispatch and Steam mocks. These checks do not certify native
room creation, physical two-PC gameplay, synchronized actor/world state, latency,
FPS or the reported graphics symptoms. The next native/streaming playtest remains
pending. Local renderer checks require the machine-wide reservation and an idle
GPU; a second-PC session also requires that PC to be available.

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
