# Wonder multiplayer on PC

## Native LAN

Each PC runs its own game. The runtime exchanges authenticated controller inputs
at the selected simulation rate, with an optional one-frame input pipeline. This
is a shared local-co-op simulation, so slower rendering or a loading barrier on
either PC can hold up both players. Matching controller traces alone do not
verify all game state.

Music streaming can reopen a course track a different number of times on the two
PCs. Those reopens must not add new loading barriers. A scene change or death
allows the track to synchronize again. The trace comparison tool also reports
presented-frame progress so a frozen renderer is visible even when inputs match.

## Share one PC with friends

Run local multiplayer on the host and use a streaming application to carry its
picture/audio and the other player's controller. Only the host runs the game.
This avoids cross-PC simulation synchronization; video encoding, transport and
decoding still add latency. Actual streaming quality and controller routing need
a separate physical test.

Start the prepared game with:

```powershell
& tools/play/Start-Wonder-LocalCoop.ps1 -Runtime '<path to wonder.exe>' -DataRoot '<prepared data folder>'
```

Connect the host controller and guest's virtual controller, choose local
multiplayer in Wonder, and assign one character per controller. The launcher
clears inherited LAN/test/replay settings and keeps real controllers enabled.
It does not configure an account, approve a guest connection or send an invite.

Parsec supports Windows hosting and guest gamepads through its virtual USB
driver: [hosting and permissions](https://support.parsec.app/hc/en-us/articles/32381747079572-Hosting-and-Permissions),
[gamepad setup](https://support.parsec.app/hc/en-us/articles/32381705301908-Setup-Gamepad).
The host approves guest access in the streaming application.

Steam Remote Play also streams from one host, but Valve does not officially
support streaming non-Steam games. Adding a shortcut does not establish Remote
Play Together support: [Steam Remote Play support](https://help.steampowered.com/en/faqs/view/0689-74B8-92AC-10F2).

## Nintendo friends and GameShare

Nintendo's online Game Rooms, live-player shadows and GameShare are separate
features. They require compatible game content and the associated platform
services. A PC LAN room or streaming shortcut does not implement Nintendo
accounts, GameChat, GameShare or Nintendo's online backend.

[Nintendo's Wonder multiplayer guide](https://en-americas-support.nintendo.com/app/answers/detail/a_id/63381/)
describes the different modes. Main-course live-player shadows do not provide
the same interaction as shared local multiplayer.

MKW recomp instead bridges the game's existing Pia LAN sockets to the host OS.
That is why its networking layer is not a direct replacement for Wonder's
shared-input synchronization. The implementation was inspected, not copied:
`adriadam10/mkw-recomp`, commit `bd367ee`, `runtime/sdk/ov/ov_socket.c` and `ov_net.c`.
