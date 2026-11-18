# Agent workflow: independent tasks and shared testing

## Start here

Work only on the task the user gives you. Do not spawn/message other agents or ask another agent to review your work. The user wants independent agents, with file-based coordination only to avoid simultaneous game tests.

Read `private/AGENT_HANDOFF.md` when using the original shared workspace. It contains the actual local paths, current commit, uncommitted LAN checkpoint, evidence, package status and remaining issues. This private file is deliberately not published to GitHub. A clean checkout will not contain it; ask the user to point you at the existing handoff rather than assuming the backlog is your assignment.

Do not switch/reset the shared working tree while another task has dirty changes. Use your environment's supported separate-checkout/worktree workflow when possible, and separate `private/build-<task>`, data roots and output names. Never stage unrelated changes with `git add -A`.

## Machine-wide game/GPU reservation

All checkouts on this Windows account use:

```text
%USERPROFILE%\.t3\shared\game-tests\current.json
```

Inspect it through the helper, which also checks running Wonder/MKW processes:

```powershell
& tools/perf/game_test_reservation.ps1 -Action Status
```

To run a test, acquire and release **in the same long-lived PowerShell invocation**:

```powershell
$claim = & tools/perf/game_test_reservation.ps1 -Action Acquire `
    -Task 'YOUR ASSIGNED TASK: specific test' -Workspace (Get-Location).Path -OwnerPid $PID |
    ConvertFrom-Json
if ($claim.state -ne 'acquired') {
    # No game/GPU test. Return to editing, light CPU work or a later retry.
    return
}
try {
    # Recheck processes immediately before launch; use your own test/data paths.
    # Run your assigned test here. Track only the processes you launch.
} finally {
    & tools/perf/game_test_reservation.ps1 -Action Release -LeaseId $claim.lease.lease_id
}
```

Do not acquire from a short-lived shell and then run the test from another shell: the recorded owner must remain alive. `state: free` from Status is informational; only successful Acquire authorizes starting. Create-new semantics resolve two simultaneous acquisitions. Owner PID **and process start time** distinguish stale reservations from PID reuse. A stale reservation can be archived only when its owner is gone and no known game runs. Unreadable reservations stay busy. Never manually remove another owner's file.

The helper never kills games. Known process names include `wonder`, `wonder-launcher`, `mkw` and `mkw-launcher`. A Wonder LAN game can run **inside `wonder-launcher.exe`**; that name does not mean the game is idle. Existing scripts may not yet acquire the reservation, so continue checking actual processes during tests. If a foreign game starts, stop only your own test, preserve evidence and defer. Do not stop or contact the other agent.

Use the default reservation directory/process names for real tests. Alternate directories/process names are only for isolated helper unit tests. If busy, retry after useful independent work or a 30–60 second wait, not a busy loop. CPU builds may continue at low parallelism for correctness tests, but avoid competing heavy work during another agent's performance measurements.

The helper's isolated CPU check is:

```powershell
& tests/game_test_reservation_test.ps1
```

Native runners already check processes, but not all old runners automatically acquire this new reservation. Wrap them as above. Two instances belonging to **one** LAN test share a single reservation.

## Build and test

Read the private handoff for this machine's existing CMake/compiler paths. Do not install a new toolchain unnecessarily. Example using the current workspace's prepared tools:

```powershell
$env:PATH = (Resolve-Path '../.tools/llvm-mingw-20261104-ucrt-x86_64/bin').Path + ';' + $env:PATH
& '../.tools/cmake-4.5.0-windows-x86_64/bin/cmake.exe' --build private/build-native --parallel 2
& '../.tools/cmake-4.5.0-windows-x86_64/bin/ctest.exe' --test-dir private/build-native `
    -E 'warp32_gpu|astc_gpu' --output-on-failure
```

That existing build directory belongs to the ongoing LAN work; other tasks should use an isolated build directory. The exclusion above avoids the two standalone GPU targets; native GPU probes and real game tests also require a reservation. Preserve complete log files and use new evidence/output names. Do not label an interrupted, cleanup-failed or metadata-incomplete run as passing.

## GitHub

- Fork/origin: `https://github.com/VictorMerk/SuperWonders.git`.
- Official/upstream: `https://github.com/vfjoo888/SuperWonders.git`.
- Existing work branch: `fix/pc-stability-and-diagnostics`.
- Existing draft PR: <https://github.com/VictorMerk/SuperWonders/pull/1>.
- Official code was merged through `64bebcc7bf42b31d0b3ee83cb04b4dbcf5b5b084`; see the [reviewed integration](UPSTREAM-SYNC-2026-11-17.md).

The user has not decided whether the project stays in the fork or eventually sends upstream PRs. Do not merge the draft, push to the default branch, create upstream PRs or publish binary releases as part of an unrelated task. A task-specific branch/PR is appropriate when assigned; describe the final change and meaningful validation in detail, using actual screenshots where useful.

Use explicit paths when staging. Write multiline commit/PR text to a local file and use `git commit -F` / `gh pr edit --body-file`. The GitHub CLI is installed; private local paths are in the handoff. After each PR creation/edit/push, the **next tool call** must link the full PR URL with T3's `link_pull_request` tool (discover its normalized tool name if necessary). Linking persists across pushes. Watching is required only for an assigned monitoring task or an action that actually waits on PR checks/reviews.

Never publish game dumps, generated `wonder_game.dll`, keys, firmware, saves, raw private logs, pairing codes or private installation paths. Native screenshots may be used only after visual inspection and checking for private information. Keep version-specific tests distinct: the shipped r8 package, committed source and newer uncommitted source are different builds.
