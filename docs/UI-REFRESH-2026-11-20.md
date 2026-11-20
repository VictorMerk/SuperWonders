# Launcher and settings refresh

The PC launcher and the Esc/F1 settings menu share the runtime interface. Both
original Wonder and the upgraded-edition installation remain project targets;
each prepared edition still needs a compatible runtime and game library.

The current source implements the Dark Games Hub design: charcoal surfaces,
off-white type and one cool action accent. Play is a cover-style page rather than
a settings table: a substantial typographic Wonder hero contains one primary
Play action, or one Set up game files action for an unprepared installation.
Settings, Controls and Profiles are quiet secondary routes below it; local co-op
and explicitly experimental LAN remain accessible. There is no permanently blank
description pane on Play. The cover uses runtime-owned typography and shapes,
not bundled game artwork or a gameplay screenshot.

| Before | After | Why |
| --- | --- | --- |
| Play repeats a settings list and help pane | Large game-identity cover and one primary action | Make starting the selected adventure the main task |
| Green gradients and repeated filled controls | Quiet charcoal, divider rows and underline navigation | Keep emphasis on the current action and game identity |
| Controls shortcuts mixed into general settings | Dedicated launcher Controls page | Find player assignments and remapping directly |
| Small fixed buttons and shrinking values | Wrapped labels, bounded widths and measured row heights | Keep settings readable in smaller windows |
| Settings tabs can exceed the available width | Responsive tab and subsection buttons | Keep every page reachable |
| Keyboard options precede the actual bindings | Binding rows first, secondary options below | Show movement, jump and sprint immediately |
| Binding B or a shoulder can also navigate | Capture blocks menu navigation until release | Avoid closing or switching pages while remapping |
| Capture can survive a rapid close/reopen | Menu transitions cancel listening | Prevent an unexpected later binding |
| Unbounded footer text | Measured, wrapped device-aware hints | Preserve content space and current button prompts |

The [initial launcher capture](images/ui-launcher-2026-11-20.png) and
[initial controller capture](images/ui-controls-2026-11-20.png) document an earlier
layout, not the final hero. Those native Vulkan captures use intentionally empty,
isolated test data; they contain no game assets or personal connection details.
Final prepared and unprepared hero captures are still pending.

The five bundled Noto fallback subsets now include the localized Controls
captions. Their sources are pinned to the authoritative Google Fonts repository,
with the corresponding OFL notices retained; see
[font provenance](../third_party/fonts/NotoFallback/README.md).
The coverage test decodes C++ Unicode escapes and checks CJK punctuation as well
as ordinary text, covering 875 checked characters in both atlas ranges and
bundled font cmaps. Character availability does not certify shaping or layout.

## Validation and limits

The final hero and refreshed embedded fonts compile in the current Windows
runtime. Windows CPU regressions pass, including the shared geometry,
font coverage and launcher/library checks. Earlier native tours cover preceding
layout iterations; the final hero still requires its native capture pass, so
rendered approval is not yet established.
Current source checks pass: font coverage, all 21 library tests and all three
pure library geometry tests. The hidden Tk construction/resize smoke passed at
960 x 740, 640 x 440 and 520 x 420 while the window remained withdrawn; it is not
a screenshot or visual approval.

These checks do not establish every language/device combination, accessibility,
correct gameplay graphics, multiplayer synchronization or an FPS improvement.
The Win32 fallback has CPU geometry/syntax checks; it needs its own visual test.
