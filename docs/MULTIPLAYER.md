# Local and remote co-op

The PC input backend provides four independent controller slots, input histories,
connection events and rumble routes. Player 1 can share a gamepad with the keyboard,
or **F1 > Controls > Keyboard as player 1** can reserve the keyboard for player 1
and assign gamepads to players 2–4. The player rows show each connected device.
Select a connected device row to test rumble.

Connect controllers before selecting the player count in Wonder. Choose two,
three or four players in the game's local player menu. Unsupported player counts
return an error instead of inventing connected controllers. Disconnecting a pad
does not shift the other active slots; its replacement fills the vacancy.
Changing the keyboard-player option deliberately reassigns the pads.

## Native LAN (experimental)

Open **Launcher > Play > LAN co-op**. On the host, choose 2-4 players and open a
room. Share its private IPv4 address, port and pairing code with the other PCs.
On each other PC choose Join and enter those details. Use the same runtime build,
game version and mods on every PC. The room starts when all PCs connect. Choose
the same player count in Wonder's local player menu and select each character.

Each PC runs its own native game and contributes its primary gamepad/keyboard to
one real co-op character. Fixed 60 Hz simulation steps, a common entropy seed/epoch,
authenticated TCP input commits and a complete-framework-frame barrier keep peers
aligned. This is separate from Nintendo matchmaking and Wonder's online ghost mode.
The native launcher stays alive as the game process so its connected sockets are
preserved when gameplay starts.

The host sends only its bounded save snapshot, authenticated and checked before
installation. Joining mounts a fresh profile under `data/lan-sessions`; the client's
normal save is preserved. Host progress saves normally. The save decoder rejects
links, traversal, absolute/reserved paths, duplicate names and oversized archives.
No game assets or credentials are sent by this save transfer.

The partial desync guard compares the primary player's course and position with an
explicit tolerance of 1/1024 world unit. It does not checksum every actor, physics
object or RNG state. Larger differences stop the session. A regular graphical
session returns to the launcher with the reason; headless checks exit with code 2.
There is no live reconnect or rollback. Lockstep follows the slowest peer/network.

Local verification used two independent native Wonder instances with separate save
profiles. Both peers controlled real characters in Scram, Skedaddlers!, including
running, jumping, deaths and restarts. The launcher-to-game run recorded more than
34,000 matching course-position samples at nine-digit precision, including a test
with the client limited to two logical CPUs. Separate physical-PC/network testing
and broader course/world-state coverage remain necessary before calling LAN stable.

Developer launch variables, as an alternative to the launcher lobby:

- `NX_LAN_MODE=host` or `join`
- `NX_LAN_ADDRESS=<private IPv4>` (host defaults to 0.0.0.0; join to 127.0.0.1)
- `NX_LAN_PORT=27846`
- `NX_LAN_PLAYERS=2` (2-4)
- `NX_LAN_CODE=<same private pairing code on every peer>` (4-64 characters)

For launcher-path automation, `NX_LAN_LOBBY_AUTOSTART=host|join` uses the address,
port, player-count and pairing variables above. `NX_LAN_LOBBY_TOUR=1` previews the
native popup for 120 UI frames without connecting. Pairing codes must stay out of
shared diagnostics. Connection attempts time out after three minutes; pending
rooms can be cancelled and retried. The fork changes no firewall or router rules.

## Why the Mario Kart LAN branch is different

The owner's [MKW LAN branch](https://github.com/VictorMerk/mkw-recomp/tree/feat/fork-lan-peers)
uses Mario Kart's built-in LAN lobby and forwards its discovery broadcasts to
private/VPN peers. Wonder's runtime deliberately reports offline and its socket
operations fail. There is no corresponding Wonder LAN lobby to expose by copying
those socket changes. Its friends-service crash is fixed here, but that does not
provide Nintendo online services. This fork implements native input synchronization
separately.

The input tests cover four SDL virtual pads, separation of player inputs, stick
limits, per-player rumble, disconnect/replacement events and keyboard-plus-pad
assignment. Physical multi-controller and remote sessions should also be checked
on the intended hardware before calling a release fully verified.
