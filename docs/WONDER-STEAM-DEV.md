# Wonder Steam developer prototype

[Project overview](../README.md) · [Playing guide](PLAYING-WONDER.md) · [Multiplayer](MULTIPLAYER.md)

**Status:** optional runtime bootstrap implemented and CPU tested with mocks.
The official Steamworks SDK was not found in the bounded local inventory, and
no real Steam API initialization, overlay, Remote Play Together or P2P session
has been tested. This is a developer experiment, not a ready multiplayer mode.

This is an isolated Windows development fixture for Valve's **Spacewar AppID
480**. It is not a Steam release, an entitlement substitute, or a working
Remote Play/P2P implementation. Valve documents 480 as the Steamworks sample's
AppID and uses `steam_appid.txt` for direct developer launches.
[Steamworks Spacewar sample](https://partner.steamgames.com/doc/sdk/api/example?l=english)

`tools/play/Start-Wonder-SteamDev.ps1` defaults to a read-only preflight. It checks
supplied file paths and whether a Steam process is running. It does not call a
Steam API, start Steam, edit Steam configuration, read account files, send an
invitation, or start Wonder during preflight.

## Prerequisites and isolated preparation

Use a prepared Wonder runtime and existing game DLL/data. Obtain the Steamworks
SDK through Valve's official download; the launcher never downloads an SDK or
borrows a Steam DLL from another installed game. Expected SDK files are:

- `public/steam/steam_api.h`
- `public/steam/isteamremoteplay.h`
- `public/steam/isteamnetworkingsockets.h`
- `redistributable_bin/win64/steam_api64.dll`

These layout checks do not cryptographically certify a DLL's origin. Valve's API
overview describes the headers/redistributables, successful API initialization,
regular callback pumping and shutdown required for actual Steam integration.
The optional runtime bootstrap implements initialization, callback pumping and
shutdown; its injectable CPU tests exercise those lifecycles without loading
Steam. An AppID file alone does not perform them, and mock success does not
establish that the real Steam client accepts this development launch.
[Steamworks API overview](https://partner.steamgames.com/doc/sdk/api?l=english)

The runtime must also have its generated `wonder-features.json` beside it. The
launcher requires schema 1, game `wonder`, a true `steam_dev_bootstrap` capability,
and a SHA-256 matching the selected executable. Missing, disabled or stale
capability files reject preparation/launch. This prevents older builds that
silently ignore the Steam opt-in flag from appearing to run a Steam test.

Example preflight, with paths adjusted to your local files:

```powershell
& tools/play/Start-Wonder-SteamDev.ps1 `
  -Runtime 'C:/Wonder/Runtime/wonder.exe' -DataRoot 'D:/WonderData' `
  -DevRoot 'C:/Wonder/private/steam-dev-case' -SdkRoot 'C:/SDKs/Steamworks/sdk' -DryRun
```

`-SdkRoot` may also come from `STEAMWORKS_SDK` or `STEAM_SDK_ROOT`. Missing SDK files
are reported explicitly; readiness stays false. The normal Steam client
installation is not a Steamworks SDK installation.

With a complete caller-provided SDK, `-Prepare` creates a **fresh** DevRoot. It
copies the runtime and known support DLLs, then writes AppID 480 next to the
isolated executable. It records the exact runtime/DLL hashes in a private
manifest and copies `wonder-features.json` unchanged, even though the staged
runtime is named `wonder.exe`. Both source and staged capabilities are rechecked
before launch. Existing developer files are never overwritten. DevRoot must be
separate from the source runtime, data and SDK folders, lie under a `private`
directory, and have no linked ancestors.

The game DLL and game assets are neither copied nor compiled. The prepared data
root and absolute game-library path are passed to the child. Preparation does
not modify the normal runtime's AppID/DLL, settings or saves. A later normal
gameplay session uses the selected data and may save progress/settings normally.

By default the SDK DLL remains at its explicit SDK path. `-CopySteamApi` copies
only that caller-provided redistributable into the isolated DevRoot. A changed
runtime, AppID or SDK DLL is rejected when reusing a prepared fixture.

## Later manual test

Only after other games have closed, Steam is already running under the same
Windows user, and the official SDK is present, use `-Launch`. This acquires the
game reservation and starts one visible local-co-op Wonder process. It does not
launch the Spacewar sample game or change Steam's installed files.

The child receives `NX_STEAM_DEV=1`, the explicit `NX_STEAM_API_LIBRARY` path,
AppID 480 and the existing data/game library. Legacy input-LAN, game-native LAN,
replay, headless and test environment settings are cleared. The optional runtime
Steam bootstrap must be compiled into that runtime; the hash-bound build
capability checks its declared presence, while actual API initialization remains
untested by preflight. Runtime initialization logs provide the next evidence.
No friend messages/invitations are sent by this launcher.

## Per-run diagnostics

A real developer launch creates a fresh `runs/<run-id>/` directory inside its
isolated DevRoot. It retains `session.json`, `native.log`, `frametrace.csv`,
`result.json` and, when the bootstrap runs, `steam-dev-stats.json`. A missing
statistics file is missing evidence, not successful Steam initialization.

The statistics record actual initialization attempts, whether initialization ever
succeeded, callback calls/completions/failures and shutdown calls/completions.
The startup snapshot is replaced by a shutdown snapshot when that path executes.
A callback still in flight can report `busy`; a shutdown checkpoint alone does
not prove completed teardown. Normal Windows window-close explicitly runs cleanup
before `ExitProcess`; an OS kill or crash can leave only the startup snapshot.
No account or friend identifiers are collected by these counters. They do not
measure packets, remote players, RTT, streaming quality or controller delivery.

## Two separate multiplayer paths

**One-host Remote Play Together:** run a single local-co-op simulation and let
Steam stream its picture/audio and guest controllers. Valve describes this as
friends joining the host's local game, with capability controlled by the app's
Steam settings. AppID 480 does not by itself establish that those features are
available for this prototype. Check actual sessions and independent controller
slots before calling it working. Any invitation should be an explicit manual
user action.
[Steam Remote Play](https://partner.steamgames.com/doc/features/remoteplay?l=english),
[ISteamRemotePlay](https://partner.steamgames.com/doc/api/ISteamRemotePlay?l=english)

For Wonder, this is the nearer candidate for avoiding two divergent simulations:
there is only one simulation. Streaming encode/decode, bandwidth, input latency,
controller routing and actual Remote Play availability still require a physical
test. This is a project inference, not a Steam performance guarantee.

**Two-process Steam P2P:** integrate Steam networking identity, connection/session
management, callbacks, message send/receive, cancellation and disconnect handling.
Valve's `ISteamNetworkingSockets` provides message-oriented reliable/unreliable
connections and relay support; using Valve's network requires the appropriate
Steamworks integration. This is different from video/controller streaming.
[ISteamNetworkingSockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets?l=english),
[Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay?l=english)

Changing packet transport cannot establish deterministic Wonder gameplay by
itself. The existing shared-input LAN mode has demonstrated simulation/loading
divergence despite matching inputs. A P2P transport must therefore accompany a
verified game-native networking path or another demonstrated synchronization
solution, not merely replace TCP and claim the desync fixed.

## CPU validation

`tests/wonder_steam_dev_test.ps1` uses fake SDK/runtime files and an injected
process recorder. It checks dry-run behavior, isolated AppID/DLL paths, original
file preservation, environment cleanup, unavailable Steam/SDK blocking and
capability/manifest tampering and per-run diagnostic path selection. Old/missing/disabled/stale build capabilities are
checked to fail before the mocked process recorder. It never executes a Steam
DLL, Steam client, or game. Such
tests do not verify overlay, Remote Play, P2P, FPS or latency.
