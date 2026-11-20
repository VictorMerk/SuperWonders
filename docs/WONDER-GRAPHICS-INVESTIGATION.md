# Wonder graphics investigation

Intermittent black polygons on the stage map are reproducible in a controlled
Product.120 test after a short direction reversal. Six native captures were
paired with exactly the same six guest-frame traces: the first three showed
broad dark patches and the following three were bright. A natural shadow-map
transition from 1912 to 2048 pixels occurred in that window. No resolution was
forced, and frame generation and draw skipping were disabled.

## Shadow binding evidence

Early terrain and the map receiver sampled a shadow handle from roughly two
frames earlier during resizing. Later terrain in the same frame selected the
current shadow writer. All those images had been rendered; the problem was not
simply an empty texture. At stable 2048-pixel size, image-memory sharing could
mask an old handle by resolving it to current content.

The relevant shader resource is an ordinary sampler binding at fragment slot 9.
The original Deferred API path copied its handle array at command recording.
A separate texture-only candidate retains the Deferred texture array and reads
it later, preserving ordinary binds and the other Deferred API classes.
Submit snapshots also collect the maximum required word span before copying a
shared array, including requests reached through nested command lists.

## Experimental controls and limits

The candidate remains **off by default**. A developer experiment uses
`NX_DEFERRED_TEXTURE_BINDS=1` with `NX_DEFERRED_SNAPSHOT=1`. The older
`NX_DEFERRED_BINDS` flag affects all four Deferred classes and is a different
experiment; it must remain absent for a texture-only comparison. No global
recording/resource epoch override is part of this candidate.

In the first same-runtime comparison, the early receiver matched the current
writer in 3/6 control frames and 6/6 candidate frames, including natural
1504-to-1640-to-1776 resizing. Bounded provenance logs confirmed that actual
recorded handle values changed before submit and that the submitted values were
used. Both clips were visually clean, so this does **not** establish that the
intermittent black-polygon symptom is fixed. The six-frame windows also had
different draw counts and resize phases; their raw pass counters are not an FPS
comparison. No default change or performance improvement is certified.

Resource-slot reuse after recording and nested-list lifetime remain separate
review concerns. The candidate does not claim to solve them by switching all
lookups to a later epoch.

Further checks need a visible failing control, a matching candidate sequence,
course actors and transparent passes, map/course transitions and repeated
pause/gear reopening. Both original Wonder and Product.120 remain targets;
a Product.120 experiment does not certify every original-edition scene or native
Switch 2-exclusive content.

See [capture and timing tools](PERF-SUITE.md),
[shader preparation](SHADER-PREPARATION.md), and the
[playing guide](PLAYING-WONDER.md) for the current supported behavior.


## Gear-menu follow-up with frame generation

A controlled clean-mod comparison exercised ten real gear-menu close/reopen
cycles per renderer variant at 1280 x 720 with FG2 and draw skipping enabled.
All 46 sampled pictures were confirmed generated presentations; the adjacent
original presentations were not captured. Main Menu returns retained their blur
and Settings reopens showed the actual game page. No draw omissions occurred,
so this does not test the effect of an actual skipped draw. Both processes exited
normally. The default renderer and texture-only Deferred candidate both passed
this bounded check; it does not establish a universal blur cure or FPS gain.

The user's ordinary setup includes the built-in recomp-menu override, which the
clean comparison intentionally excluded. A byte-level archive comparison confirms
its Bootup matches Product120 except for the intended Pause menu edit; it is not
an original-edition archive accidentally overlaid on the upgraded game. Three
resource-table allocation entries predate the current generator's alignment.
Isolated regeneration and a modded-menu test sampling both original and generated
presentations remain pending. No live mod was replaced on the strength of the
clean comparison.


The regenerated Product120 menu was subsequently tested on the newer runtime:
five reopen cycles, with adjacent original/generated pictures verified for all
13 stable menu states. The sampled blurred map detail remained consistent and
both picture kinds showed Settings correctly. The background was mostly blue
before the first reopen, unlike the earlier clean comparison's terrain view;
full background correctness and old/new-mod causality remain unverified. The
live mod has not been replaced while that scene difference is investigated.
