# Profiles, saves and PC menu quality of life

## Profiles and progress

Open **Play > Profiles & saves** or **Settings > Manage profiles & progress** before starting a game. Select an existing profile, save its nickname, or create a new adventure with fresh progress. Up to 16 profiles are supported. Nicknames accept valid UTF-8 up to 31 bytes; empty names, control characters and leading/trailing spaces are rejected.

The original profile keeps `data/user/` and the existing guest account UID. Renaming it does not move or reset progress. Additional profiles store their user/save and cache trees under `data/profiles/<id>/user/`. Video, audio and controller settings remain shared across profiles. No automatic import, save-format conversion or deletion is performed.

**Back up now** makes a new completed backup in the selected profile's `backups/` directory. Existing backups are never overwritten. **Restore selected backup** requires confirmation, copies and validates the selected backup first, then creates a safety backup of current progress before replacing the user tree. A failed replacement attempts to put the original tree back. Interrupted/incomplete `.partial` folders are excluded from the picker. Backups reject links/junctions, unsupported file types, more than 16,384 entries or over 1 GiB. Keep backups private; they contain save data.

Automatic backups are enabled before solo Play and before manually opening a host room, and can be disabled in the profile manager. A failed automatic backup leaves you in the launcher to resolve it. Backups are retained until you remove them; they are not silently rotated. Joining LAN continues to use a separate session copy; hosting uses the selected profile's progress. Profile changes and restoration are disabled while a room is preparing, waiting or ready.

On Windows, this runtime holds an exclusive data-folder lock for its whole lifetime. A second copy using the same data root cannot open the launcher or restore its live saves. Launcher/game/update transitions wait for their parent to release the lock. Older runtimes do not participate in this lock; close them before managing saves with this build. Linux/macOS have the profile functionality but do not yet enforce the Windows instance lock.

## Launcher and controllers

The launcher scales its sidebar against both window dimensions. Opening a settings panel or modal disables the controls underneath it. Settings rows wrap long labels and stack values when needed; narrower panels place help below the scrollable options. The Play page shows the selected profile and connected player slots. Controls shows a live face-button readout, player slots with rumble tests, binding presets and a reset for custom controller mappings.

The LAN dialog fits smaller windows, identifies the selected profile and explains the host/join steps. The transport, buffering and host-negotiated simulation settings belong to the separate LAN workstream. This change does not certify physical LAN synchronization or performance.

## Escape / F1 menu

The settings panel opens centered and supports moving by its background and resizing from its edges/corners. It stays within the current display bounds after resizing. **System > Full-window settings** fills the window; returning to floating mode restores the panel size from this session. **Menu opacity** and **Reduced menu motion** are saved in `data/usability.ini`. Reduced motion removes menu fades/slides and moving highlights. Floating position/size are session preferences, not persisted across program launches.

The header provides Resume/Back, audio mute and FPS visibility shortcuts. Esc, F1, controller B or Minus + Plus close settings. L/R switch tabs. The settings panel does not pause simulation; use Wonder's own pause button before opening it. LAN continues while settings are open.

On Windows, resizing the game window by its border remembers the client size after dragging ends. Minimum client size is 640 x 360. Maximizing/minimizing does not replace the remembered windowed size.

**System > Return to launcher** requires a second selection within four seconds. It uses the normal shutdown/flush path and reopens configuration for profile/save/mod changes. Progress since the game's last save may be lost, and leaving a LAN host ends its room.

## Comfort and installation

The Mods page includes reversible comfort presets: Quiet play (mute + no rumble), Clean screen (hide metrics), and Restore sound & rumble. These are runtime options, not game-code cheats. Existing RomFS mod discovery, load order and conflict reporting remain available.

Game files shows package/tool readiness and installed/package game-kit IDs. A complete setup contains first-build tools; a runtime update requires prepared game data. The Updates page explains this distinction and can open the installation directory. Updating is refused while a LAN room is active/preparing, and the selected profile is backed up before installation.

Update manifests reject player-owned paths, including data, saves, profiles, backups, mods and local settings. A manifest rejected for naming such paths cannot be used to install or remove those files. Runtime-only deliveries must be labelled clearly and record their binary hashes and source snapshot; they do not contain game data, generated game libraries, keys or saves.

## Verification

`profile_account_test` verifies that guest IPC receives the selected nickname while retaining the legacy UID and profile layout. `profiles_test` covers legacy path compatibility, profile separation, UTF-8/name validation, corrupt selection fallback, Windows data locking, restore content, pre-restore preservation, incomplete/outside backup rejection, and usability settings persistence. `save_backup_test` covers separate backup copies and preservation of active saves. `update_core_test` covers protected player-data paths in manifests. `netplay_lobby_test` exercises the lobby with the profile-root integration.

`tools/perf/run_qol_ui.ps1 -Runtime <build-directory> -Output <new-directory>` acquires the machine-wide reservation, uses its own synthetic data roots and runtime copies, captures native launcher/settings at 1280 x 720 and 640 x 360, profiles at both sizes, the co-op dialog, and live window resizing. It also checks fullscreen/reduced-motion settings and uses a virtual controller to select the second profile. Resize validation requires captures at all three requested image dimensions; a process exiting successfully alone is insufficient. It stops only its own process if a foreign game starts. Test hooks `NX_PROFILE_MANAGER_TOUR` and `NX_QOL_RESIZE_TOUR` are opt-in QA only and must not be set in a delivered launch environment.

## Dedicated PC gameplay actions

**Controls > Keyboard controls > Dedicated PC actions** provides named, remappable actions:

| Action | Default key |
| --- | --- |
| Move / crouch / look up or enter | WASD |
| Jump / swim | Space |
| Sprint / grab / throw | Shift (either side) |
| Spin / badge ability | E |
| Ground pound | Ctrl (either side) |
| Use stored item / confirm | F |
| Greet / emote | Q |
| Place standee | V |
| Pause | Enter |
| Badges / map menu | Backspace |
| PC settings | Esc or F1 |

Ground pound sends Down and ZL together, covering Wonder's ground-pound settings. Standee briefly crouches before pressing use, so a single tap supplies the combination; stand on the ground where the game allows a standee. These shortcuts translate keyboard input, not game physics or ability rules. Sprint/grab/throw remain one contextual game action. Optional original shoulder/stick inputs are under **Advanced keyboard bindings**; L defaults to Z, duplicate ZR spin and stick clicks are unbound.

Match **Wonder's game layout** to the game's own **Dash and Jump Controls** setting: standard B jump/Y dash, or alternate A jump/B dash. The PC action keys stay consistent. This selector does not change a save or gamepad bindings. Prompts use the matching keyboard actions, including the alternate face-button layout and shared R/ZR spin key. Restart after changing keys/layout to refresh game prompts.

Select an action and press its new key; Delete unbinds it and Esc cancels capture. Movement uses the assigned keys, including analog movement; action mode has no hidden fixed WASD/arrow aliases. PC launcher/settings navigation continues to use Enter, Esc and arrows.

**Reset keyboard layout** restores the selected mode's defaults. **Legacy Switch buttons** retains direct button mapping without the PC combinations. Existing custom layouts stay in legacy mode, unchanged. Only complete untouched old presets (including the first versioned PC preset) migrate automatically to the corrected actions. Settings record layout version 2 and the chosen mode, so deliberate saved custom/legacy bindings remain intact. Windows and Linux support the action menu; macOS's existing fixed keyboard path uses the standard action defaults.

`keyboard_actions_test` checks actual B-jump/Y-dash masks, dedicated combinations and standee sequencing, both game layouts, movement, rebinding, unbound keys and settings migration. `keyboard_defaults_test` checks defaults, modifier aliases and custom preservation. `button_font_test` checks action-aware prompt mapping. These CPU checks do not replace native course testing.
