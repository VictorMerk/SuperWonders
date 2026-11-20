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
(approximately 43 MiB on the current Windows build). Override it with
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

## Framework and selected file observations

With the same opt-in application trace enabled, `framework_enter`,
`framework_exit` and `framework_throw` pair actual wrapped framework calls using
local invocation numbers and parent/thread ordinals. `loading_file` observes
successful physical opens of selected stage-load and BGM/environment resources
before the guest OpenFile API returns. This excludes virtual opens and does not
measure loader readiness. It records a noncryptographic resource-path hash and,
when validated inside main/sdk text, a guest module-relative caller offset.
Raw paths, guest TLS tokens and host callers are not exported by these additions.

The fixed registry supports 64 local thread identities and eight nested calls;
unknown/exhausted identities remain explicit. Invocation/thread numbers are local
to one process and cannot be compared numerically across PCs. Exception exits
preserve the original exception; observations perform no filesystem work. Older
CSV files without the trailing observation columns remain readable. Retention can
omit an entry or exit: an unpaired retained event alone is not evidence of a hang.
These observations change no frame cadence, clocks, protocol or pause behavior.

## Existing LAN timing guard

Active shared-input LAN already bypasses Wonder's adaptive wall-clock frame step
and uses the host-agreed 30 or 60 Hz step. The actual-production-code
`wonder_lan_timing` CPU regression checks initialization before the first present
and irregular presentation timestamps, including a long gap and a conflicting
local interval. The stored simulation step stays fixed; the offline adaptive
branch still works independently. The adaptive-step log from an ordinary local
co-op run is therefore not evidence that active LAN uses adaptive timing.

This covers that timing branch only. It does not establish deterministic guest
thread scheduling, loading progression, all clocks, actor state or world sync.

## Loading publication asymmetry

The CPU regression `netplay_game_loading` exercises the actual runtime paths
with bounded fake transport replies. A peer inside `loading_event()` exchanges
an unmatched marker without publishing it, then exposes only the final neutral
commit after both markers match. An ordinary peer's `next_frame()` can publish
and return from an exchange containing the other peer's unmatched loading
marker. Matching transport ticks therefore do not establish matching framework
returns or input publication.

This test executes no guest simulation and does not prove the historical
course divergence's cause. Simply waiting inside the ordinary path could prevent
that peer from reaching its own loading event. A correction needs a verified
loading/frame progression policy, with paired application and framework traces;
an unconditional extra barrier is not an established fix. The existing analyzer
reports `gameplay_applies_with_peer_loading_marker` for retained examples.

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

## Product120 room and mode reachability: static evidence

The inspected `Bootup.Nin_NX_NVN.pack.zs` screen parameters
`NetworkSetting.game__UIExtraScreenParam.bgyml` and
`FriendMatchTop.game__UIExtraScreenParam.bgyml` bind the friends action (X) and
create/search results. English `LayoutMsgRef/NetworkSettingRef.msbt`,
`FriendMatchTopRef.msbt` and `GameMsg/OnlineManual.msbt` identify the ordinary
Online Status / Play with Friends route. Manual entries `FriendMatch_01_Text`,
`LiveGhost_00_Text` and `LiveGhost_01_Text` establish room navigation and independent
world/online-shadow semantics. No assets or message dumps are distributed here.

Do not conflate number domains: the high-level enum is Invalid=0, NotUse=1,
Internet=2, Emulation=3, LANMatch=4. The separate backend state 1 uses nifm Internet
mode; backend 2 initializes local wireless; backend 3 selects nifm local mode.
Original-NSO-checked main offsets +0x1db7d34/+0x1db7d38 derive mode==3 for
`SetLocalNetworkMode` at +0x1db7d3c. Backend 2 instead calls `ldn::Initialize`
at +0x1db7da4. The UI-to-backend-3 activation path remains unproved; construction
of all three states is not evidence of selection. Pia transport alone cannot
establish shared simulation rather than ghosts.

Compiled online gates are concrete: +0x1da8dc0 calls
`IsNetworkServiceAccountAvailable`, with false returning before UID publication
at +0x1da8dc8; +0x1dadacc has a second availability gate. +0x1ce1780 calls
`EnsureNetworkServiceAccountAvailable` and tests Result==0. +0x6d7434 calls
`CheckFriendListAvailability`, exiting on an error at +0x6d7440; a later path calls
`EnsureFriendListAvailable` at +0x6d747c. Their exact order on the room UI is not
execution-verified. The existing offline account/friend providers cannot satisfy
these honestly. No additional fake-success capability is justified by this audit.

Next reserved observation: open the ordinary friends menu once, correlate its
activation with the existing source-guarded probes, and retain the first actual
service failure and local-network argument. Stop at an account requirement. Only
natural local-backend activation warrants a physical discovery/join test. No mode,
resource, account or room fabrication is part of that test. This bounded static
evidence applies to Product120 and certifies neither rooms nor world synchronization.


## Confirmed input-publication asymmetry in a CPU reproduction

A two-process, localhost reproduction links the actual runtime netplay code,
OneAhead pipeline and production Session transport. It runs no guest engine and
uses mocked host/device probes. Both sides authenticate the same nonneutral
commit. The host enters the loader and records that commit as
`loading_drain_discard`; the other process consumes it through ordinary
`next_frame`, publishes it to the HID snapshot and records `apply`. Both later
agree on the same loading marker and publish a final neutral commit.

This proves different publication decisions are possible despite identical
committed input payloads. It does not prove a guest simulation step occurred or
establish the cause of a historical physical-PC failure. The newer rejection of
conflicting loading identities does not detect this case: its identities agree.
A fix needs agreement about simulation boundaries and each commit's fate, not
only matching input values. Blocking ordinary progression on a remote loading
marker alone can prevent the guest from reaching its own matching file request;
applying then overwriting a drained snapshot under the loader lock does not
establish guest consumption. Neither shortcut is shipped as a synchronization fix.


The application comparer now reports `commit_fate_mismatches` separately from
payload differences on shared applied ticks. It compares only explicit retained
publication/discard/loading-exchange events for the same committed tick; an
exchange followed by final neutral publication is classified as published. A
loading-exchange row without a later retained publication is incomplete, since
the export might end before its final neutral event.
Conflicting local dispositions are ambiguous, and absent rows remain unknown.
The combined multiplayer report raises `legacy_commit_fate_mismatch` when this
evidence differs. The actual pipeline and serial CPU reproductions both produce
that warning-worthy mismatch; twenty comparer tests and twenty combined-report
tests cover the reporting path. This is diagnostic coverage, not a runtime fix.


## Optional native task-phase observations

With legacy application tracing enabled, `NX_LAN_TRACE_PHASES=1` additionally
observes the source-verified pre/core/post task callbacks in both supported builds.
The three hooks remain untouched by default. Before installing any hook, the
runtime checks source version, text bounds, instruction fingerprints and all
original function pointers. Each wrapper calls the original exactly once and
preserves its CPU results and exception. It does not read or write guessed actor
fields, change readiness, publish inputs, pause the game or perform exchanges.

The nine optional `phase_pre/core/post_enter/exit/throw` events retain schema 1
and use the existing bounded local invocation/thread identities. Phase tick and
sampling identity come from an atomic publication-identity mirror without taking
the input mutex; the loading marker and presentation count are separate local
observations. This is not the complete atomic pad snapshot used for HID delivery.
The parser keeps phase counts separate from framework counts and attributes file
observations through retained, paired, same-thread ancestry only. Missing or
truncated parents remain unknown. Explicit tracing adds work and is not an FPS
benchmark mode.

These are source-named lifecycle callbacks. In particular, the core callback can
return without advancing actors. Invocation counts or cross-PC ordinal equality
therefore do not establish shared simulation generations or scene readiness.
The next physical test must correlate these observations with actual input/HID
publication, loading and visible gameplay before choosing a new exchange gate.

See [Wonder clock policy](WONDER-CLOCK-POLICY.md) for fixed-step versus ordinary
clocks, the shared-clock ABI correction and the remaining legacy LAN limitation.


### Actual two-instance follow-up

A same-PC Product120 run reached course, pause and resume on both peers with the
new runtime and both observation flags. Actual pre/post callbacks paired on both
sides with no observation eviction; the registered core callback had zero events
on both. The requested three-phase criterion therefore failed. A real-DLL
metadata check found all hook targets, but target availability does not establish
that the game invokes a particular delegate. Core dispatch remains under audit.

The pair had 14,329 matched applied ticks with no retained applied-pad mismatch,
but three explicit publication-fate mismatches. The first was a published commit
on one peer versus loading-only exchange on the other. No usable position pairs
were available, so this is not world-state verification. All loading-file
observations lacked a retained phase/framework ancestor and remain unknown.

At requested paired shutdown, the host quit normally and the client received a
socket-close error before its own render-thread quit poll. Both exported their
diagnostics; this was a shutdown race in that test, not a demonstrated spontaneous
course crash. Corrected derivative session declarations are distinguished from
the preserved original malformed metadata; correlation does not independently
prove the loaded library hash or physical-PC provenance.
