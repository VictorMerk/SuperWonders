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
| Held button changes meaning after a preset | Require a fresh physical release after effective bindings change | Prevent a selection press becoming an unintended Back action |
| Queued Esc survives a rapid reopen | Generation-tag events and clear previous held input | Keep old input from acting on a new menu |
| Preview hidden in ordinary desktop settings | Side-by-side preview when width and height permit | Keep the controller and its bindings visible together |
| Unbounded footer text | Measured, wrapped device-aware hints | Preserve content space and current button prompts |

The [launcher hero](images/ui-launcher-2026-11-20.png) and
[controller page](images/ui-controls-2026-11-20.png) are native Vulkan captures of
the current layout using isolated test data. The hero shows a prepared
installation and its Play action; the controller page uses empty test data.
Neither image contains game assets or personal connection details. The large
WONDER wordmark has its own six-letter font bake so the cover stays crisp
without duplicating all localized menu glyphs at cover size.

The five bundled Noto fallback subsets now include the localized Controls
captions. Their sources are pinned to the authoritative Google Fonts repository,
with the corresponding OFL notices retained; see
[font provenance](../third_party/fonts/NotoFallback/README.md).
The coverage test decodes C++ Unicode escapes and checks CJK punctuation as well
as ordinary text, covering 875 checked characters in both atlas ranges and
bundled font cmaps. Character availability does not certify shaping or layout.

## Validation and limits

The earlier input-navigation build passed all 112 CPU regressions and six native
overlay input cases, including stale polls/events, remapping capture, held
preset changes and rapid close/reopen. Six isolated native UI tours completed
cleanly: launcher and controller pages at 1280 x 720 and 640 x 360, German
controller settings at 800 x 600, and player/device settings at 1280 x 720.
Their captures were visually inspected. Wide controller panels now retain the
preview beside the bindings; small windows prioritize a scrollable binding list.
Current source checks pass: font coverage, all 21 library tests and all three
pure library geometry tests. The hidden Tk construction/resize smoke passed at
960 x 740, 640 x 440 and 520 x 420 while the window remained withdrawn; it is not
a screenshot or visual approval.

The cover-font follow-up passed nine focused CPU checks, six native overlay
input cases and seven native UI tours, including a 1920 x 1080 launcher.
Prepared-installation Play captures were also visually inspected. That private
capture helper encountered a post-exit log-sharing error; a separate postflight
verified all 18 complete images, ready status, no loaded game library and the
64 MiB final capture limit. This is not an uninterrupted helper-script pass.

These checks do not establish every language/device combination, accessibility,
correct gameplay graphics, multiplayer synchronization or an FPS improvement.
The Win32 fallback has CPU geometry/syntax checks; it needs its own visual test.
