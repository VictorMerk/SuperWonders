# Playing Wonder together

[Project overview](../README.md) · [Playing guide](PLAYING-WONDER.md) · [Synchronization investigation](WONDER-SYNC-INVESTIGATION.md)

**Shared-input LAN currently has known gameplay desync.** Two players can see different level progress despite agreeing on controller packets. Do not rely on it for a normal multiplayer session or treat its older successful samples as proof of synchronization.

## Choose a path

| Path | How it works | Current status |
| --- | --- | --- |
| Local co-op | One game process, separate controllers. | Four runtime controller slots; verify your physical devices and assignments. |
| Streamed local co-op | One host runs Wonder; a streaming app carries picture, audio and guest controls. | Dedicated launch script available. Guest controller routing, quality and latency still need a physical test. |
| Shared-input LAN | Each PC simulates local co-op from exchanged controller inputs. | Known desync; diagnostic use only. |
| Game-native LAN experiment | Product.120 SDK socket/network calls use an opt-in host provider. | Source guarded and CPU tested; in-game room reachability and multiplayer gameplay **untested**. |
| Steam developer prototype | Isolated AppID 480 launch with optional Steam API bootstrap. | CPU mocks/preflight only. No verified overlay, Remote Play Together or Steam P2P. |

## Local and streamed co-op

Connect controllers, check their independent player rows in **F1 → Controls**, then select two, three or four players in Wonder. Reserving the keyboard for player 1 places connected gamepads in the remaining slots. Replacing a disconnected pad fills its vacancy rather than shifting other active players.

For a prepared Windows installation, this launcher starts one ordinary local-co-op simulation:

```powershell
& tools/play/Start-Wonder-LocalCoop.ps1 `
  -Runtime 'C:/Wonder/wonder.exe' -DataRoot 'D:/WonderData'
```

The scripts are run from the repository root. Launch scripts honor the shared game-test reservation and check for recognized game processes. Close other games before running an experiment. They do not configure a streaming application, approve a connection or send an invitation. Confirm that a guest's virtual controller is a separate player before starting a course.

A single simulation avoids disagreement between two independently running games. Streaming still adds encoding, network and decoding costs; it is not a zero-latency promise. Steam Remote Play Together availability is not established by adding a non-Steam shortcut or writing an AppID file. See [the Steam prototype](WONDER-STEAM-DEV.md).

## Game-native LAN experiment

The new Windows backend is **off by default** and restricted to the inspected Product.120 source profile. It routes selected socket and local-network SDK calls to a bounded host implementation. Initialization checks module identities, code prefixes, imported aliases and entry points before installing the selected bindings together. Unsupported operations fail rather than claiming a successful platform service.

This is different from the launcher's older shared-input room. It does not force the game into a network mode, supply a Nintendo account or establish that a usable room menu exists. Real metadata checks and CPU/loopback tests validate pieces of the implementation; they do not validate a Wonder multiplayer session.

Start with the read-only capability check:

```powershell
& tools/play/Start-Wonder-NativeLan.ps1 `
  -Runtime 'C:/Wonder/wonder.exe' -DataRoot 'D:/WonderData' -DryRun
```

The executable must have a matching `wonder-features.json` declaring the native provider. Missing/stale manifests reject launch. Removing `-DryRun` requests the experiment with `NX_NATIVE_LAN=1`; optional `-LocalAddress` selects a literal IPv4 address assigned to that PC. This is a local interface address, not a host/join room code. The launcher keeps diagnostics under the selected data/output folder. It does not change router or firewall configuration.

The optional `native_lan_preflight.exe` beside the runtime lists eligible private IPv4 addresses without adapter IDs or MAC addresses. When it is present, the launcher rejects ambiguous, offline, stale or mismatched results before starting the game. `-LocalAddress` must belong to this PC; do not copy the other player's address. If the helper is absent, an explicit address is checked against local interface metadata; an omitted address is left to the runtime, with a warning.

For a paired diagnostic run, pass the same opaque `-SessionId` on both PCs and a different `-PeerId` on each. Both arguments are optional together. They label evidence; they do not authenticate a peer or create a room. For example, add `-SessionId test-01 -PeerId left` on one PC and `-SessionId test-01 -PeerId right` on the other. Keep both runtimes and their capability manifests matched.

The next meaningful test must establish the reachable game menu, real room discovery/joining and gameplay across two physical PCs. Until then, this mode is a development experiment.

## What the inspected game room actually does

Product120 resources identify an existing **Online Status > Play with Friends >
Create Room / Find Room** flow. `NetworkSetting` binds the friends action to X;
`FriendMatchTop` supplies the create/search actions. `GameMsg/OnlineManual.msbt`
entries `FriendMatch_01_Text` and `LiveGhost_01_Text` describe that route and its
online-shadow semantics: players retain independent enemy/block state. A friends
room therefore does not establish shared local-co-op simulation.

These resources prove menu definitions and intended behavior, not that the PC
provider can reach or join the room. The game-level `Internet` enum has value 2;
the separate backend value 2 initializes local wireless (`nn::ldn`). Backend 3
selects local-IP mode. Its existence does not establish a retail LAN menu or the
application's synchronization model. Current offline account/friends services do
not supply Nintendo matchmaking or authentication.

The next reserved test is the ordinary menu action with existing native probes:
observe the activated backend, actual local-network argument and first unavailable
service. Stop at an authentication requirement; do not force a mode or report room
success from a factory/socket counter. This conclusion applies to the inspected
Product120 content, not unverified Switch 2 exclusive features.

## Why matching packets did not fix LAN

The older mode synchronizes input rather than the full game world. Loading, scheduling, HID delivery and game state can differ while retained input traces still match. Its partial primary-player position guard does not checksum every actor or object. Earlier same-PC tests therefore did not predict the later physical-PC failure.

Replacing TCP with UDP or Steam transport alone does not establish a deterministic simulation. The [synchronization investigation](WONDER-SYNC-INVESTIGATION.md) tracks applied-input traces, bounded retention, native SDK probes and the evidence needed for the next controlled test. Keep both peers on the same runtime, version, save baseline and mod set when collecting evidence. Preserve private diagnostics locally and redact pairing codes before sharing.

For protocol 6, one controller Input is 152 bytes and one Commit is 280 bytes,
including the 40-byte protocol header and 32-byte MAC. At 60 exchanges per second,
a two-PC gameplay pair therefore carries about 25.3 KiB/s in both directions
combined. This is a calculation from the current encoder, excluding TCP/IP,
transport acknowledgements, retransmissions, setup/save transfer and loading
pumps; it is not a measured network rate. Gameplay sends no separate game-level
Ack per tick, and TCP_NODELAY is enabled. Waiting at an input barrier can still
add latency even when the byte rate is small. Lowering traffic alone does not
repair different input application or world state.

## Compare diagnostic evidence

`tools/perf/summarize_wonder_multiplayer.py` accepts explicit session and artifact
paths from each participant. For a native experiment:

```powershell
py tools/perf/summarize_wonder_multiplayer.py `
  --left-session left/session.json --left-native-stats left/native-lan-stats.json `
  --right-session right/session.json --right-native-stats right/native-lan-stats.json `
  --output combined-report.json
```

The tool also accepts Steam lifecycle statistics and legacy packet/application
traces. It keeps the modes separate, checks declarations and evidence freshness,
and requires matching runtime/session identities plus distinct peer IDs before
correlating the pair. Old runs without those fields remain uncorrelated evidence.
Session JSON may additionally declare `source_build_id` (the 64 hexadecimal NSO
build ID) and `game_library_sha256` (the generated game library's SHA256). These
identify game code separately from the shared runtime executable, which can run
both Wonder editions. Conflicting declarations block comparison; malformed ones
are errors. Missing declarations in older sessions remain explicitly `unknown`,
and a declaration on one PC cannot establish the other PC's identity. Matching
declarations are reported as `declared_match`, not measured loaded-module proof.
The launchers do not automatically record those two optional loaded-code
declarations. The native developer launcher instead records
`planned_game_library_sha256` plus a scoped selection record: it hashes the first
determinable file from the child process's override, runtime-adjacent and prepared
data search order. Locked/unreadable or ambiguous paths stay unknown. This does
not execute or validate the DLL, infer its source edition, or prove which bytes
were loaded later. Planned conflicts also block pair comparison; a
`planned_match` never fills unknown loaded-code declarations. Preserve original
session evidence and measure identities rather than guessing from folder names.
Counts of API calls and returned bytes are not wire throughput or round-trip time.
Exit code zero means the report parsed without malformed-input errors; it is not
a passing multiplayer test. Keep original traces private and inspect the report's
warnings and scope before drawing a conclusion.

## Nintendo and Mario Kart features

Nintendo friends, Game Rooms, online shadows, GameShare and GameChat require their corresponding content/services; a PC socket backend does not supply them. Complete Switch 2 Edition features remain subject to the [edition investigation](WONDER-EDITIONS.md).

[mkw-recomp](https://github.com/adriadam10/mkw-recomp) is a useful reference for bridging an existing game's networking path to the host OS. Wonder has different game-specific entry points and service behavior, so a working Mario Kart session does not certify Wonder's adapter. The current work investigates those entry points without claiming Nintendo online support.
