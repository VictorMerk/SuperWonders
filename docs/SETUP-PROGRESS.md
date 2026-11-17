# Setup progress during long recompilation stages

The displayed percentage is weighted overall setup progress, not elapsed time and not a live percentage through
function analysis. The original main-analysis stage could sit at 47% while a Python worker used one CPU core and
several GiB of RAM, making a healthy build look frozen. A launcher using little CPU does not imply its child worker
is idle; later compilation uses several Clang workers.

The recompiler now publishes reading, analysis and code-generation phase markers. The build wrapper forwards those
as concrete stage labels and includes actual `generated/total` function counts once generation reports them. Analysis
does not invent a completion percentage or an ETA before a total is available. The native progress card identifies
the percentage as overall progress, keeps elapsed time visible and explains that one stage can take several minutes.
Captured error output is bounded to the last 40 lines.

The last one percent is reserved for upstream's ultrawide layout preparation. Previously the new upstream sequence
could show 100% after code compilation and return to 99% for layouts. The updated weighting ends the main steps at
99%, so completion is shown after that final preparation attempt.

Two regressions in `setup_paths_test.py` check actual forwarded phase/count labels and monotonic weighted completion
through the layout step with stub workers. These changes apply to a future complete kit/runtime. The already
delivered r8 EXEs/scripts and an active first build are not rewritten; the new kit needs a regenerated ID.
