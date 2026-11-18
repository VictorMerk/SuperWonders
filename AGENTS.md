# Independent-agent work

The user assigns each agent a task. Work only on that task; do not take over another workstream.

- Do not message, delegate to, or coordinate through other agents. Coordinate machine testing only through the shared reservation file and process inventory described below.
- Read [the agent workflow](docs/AGENT-WORKFLOW.md). If present, read `private/AGENT_HANDOFF.md` for this machine's current state, paths, dirty changes, evidence and unfinished work.
- Before launching any game, GPU probe, native UI tour or performance test, acquire the machine-wide reservation with `tools/perf/game_test_reservation.ps1`. A process check alone is not a reservation. Start nothing unless Acquire returns `state: acquired`.
- If another game or reservation exists, continue independent editing/documentation/CPU work or wait. Do not kill another agent's or the user's processes, release their reservation, or contact their agent.
- Keep a reservation for the complete test and release only your own lease ID in `finally`. Continue checking for games launched by older tools that do not use reservations; stop only your own test if a foreign game starts.
- Use separate build/data/output folders per task. Do not overwrite another task's executable, fixture save, settings or evidence. Avoid heavy CPU builds while another agent measures performance.
- Preserve unrelated dirty files. Never use blanket staging, reset, clean, checkout or conflict resolution to discard another agent's work. Prefer a separate checkout/worktree when the environment supports it.
- Do not commit game data, generated game libraries, keys, firmware, saves, private logs, pairing codes, local install paths or private ZIPs. `private/` is intentionally ignored.
- Link a PR with the T3 `link_pull_request` tool immediately after creating/editing/pushing to it. Do not merge, publish a release or create upstream PRs unless the user assigns that action.
- Report actual test scope. Loopback, partial player-state checks, API validation and synthetic transport throughput do not certify physical LAN play, all actors, stable FPS, zero latency or correct rendering.

User instructions take precedence. Do not infer a new task from the handoff's backlog.
