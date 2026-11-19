# LAN input freshness and transport regression checks — 2026-11-19

Protocol 6 polls the next controller input after the previous buffered exchange finishes, and removes the separate per-tick application acknowledgement. The local regression checks passed. Physical two-PC verification and permanent installation remain pending fresh access to the second PC. This is a candidate, not a claim that the reported home-network lag is resolved.

## Changes and compatibility

The one-frame pipeline previously sampled input before waiting for the outstanding network exchange. A button change during that wait therefore missed the next submitted sample. `submit_sampled` receives the previous result before polling and queueing the next input. A blocking regression test changes input while that wait is outstanding and verifies that the new value is queued. Failed exchanges do not poll another input. Simulation still uses the agreed prior result; this adds no prediction or rollback.

The host sends Commit N without waiting for a separate Ack N. A conforming guest Session can send authenticated Input N+1 only after decoding Commit N. Exact role/tick/nonce checks and HMAC authentication remain enforced. TCP supplies ordered reliable bytes ([RFC 9293 §2.2](https://www.rfc-editor.org/rfc/rfc9293.html#section-2.2)); the application state machine supplies the acknowledgement implication. Neither property proves full game determinism. Save-transfer acknowledgements remain unchanged. Protocols 3, 4 and 5 are rejected rather than mixed with protocol 6.

For two players the measured application game traffic is 3 → 2 messages and 504 → 432 bytes per tick, a 14.3% byte reduction. These counts exclude startup and TCP/IP overhead; messages are not necessarily individual network packets. The protocol-only matrix did not establish a substantial throughput or latency improvement.

The matching r8 branch also retains the completed renderer/cache fixes from compatibility branch `56ba608d7f0b601e0e9734f6b795a23e934b1d97`. This is compatibility preservation, not a new measured renderer improvement. Main retains its newer content fingerprint implementation. Main and r8 have different game-library ABIs; the runtime-only candidate is built from the r8 branch and must not be replaced by a main executable against an old game library.

## Timing evidence

Two separate 24-run ABBA campaigns used 120 exchanges/run, 8 ms simulated work/tick, serial and buffered modes, and 0/5/15 ms configured delay in each relay direction. Each configuration has two runs/build. The first campaign isolates Ack removal; the second compares the old protocol/sampling baseline with the final protocol-6 fresh-sampling candidate. Scoped Windows timer resolution and high-resolution work waits avoid the earlier coarse Sleep quantization. The discarded early timer smoke is excluded. Complete aggregate means, observed delays and actual work durations are in [the data file](data/lan-performance-20261119.json).

Software sample-to-commit age, milliseconds (baseline / final candidate):

| Mode | Configured one-way delay | Host sample age | Guest sample age | Host ms/tick |
|---|---:|---:|---:|---:|
| Serial | 0 | 0.269 / 0.320 | 0.384 / 0.218 | 8.629 / 8.574 |
| Serial | 5 | 11.683 / 11.538 | 11.648 / 11.554 | 20.032 / 19.834 |
| Serial | 15 | 31.765 / 31.383 | 31.555 / 31.351 | 40.162 / 39.715 |
| One-frame overlap | 0 | 1.138 / 1.613 | 0.820 / 0.259 | 8.215 / 8.238 |
| One-frame overlap | 5 | 14.778 / 11.471 | 14.562 / 11.472 | 11.630 / 11.553 |
| One-frame overlap | 15 | 54.165 / 31.127 | 53.679 / 31.127 | 31.439 / 31.209 |

The buffered delayed cases show fresher samples; zero-delay results do not show a consistent gain. Throughput is mostly unchanged. Moving the sampling timestamp later does not by itself establish button-to-display improvement; the blocking input-change regression establishes the functional improvement. These synthetic values are neither game FPS nor physical controller latency. Two runs per variant are a limited sample.

## Correctness and native scope

- Complete builds and CPU suites: matching r8 51/51, main 61/61. The transport test includes ten serial/buffered fault cases: valid-message replay, authenticated-payload tampering, missing Input, missing Commit, and truncated stream. All reject or time out as expected without standalone game Acks.
- Three GPU CTests passed: warp32, SPIR-V NaN, ASTC.
- Final frozen r8 runtime SHA-256: `103bbf612ce0085aaf28ccfc21e3eb5f7b11e85c7bfda767c0ebfef22966e88d`.
- Two local native pairs tested a 12-second loader delay on the host, then on the guest. Both reached Course004, movement and pause/resume, with 4,095 matching retained commits each and respectively 2,495 / 2,734 matching sampled player positions. The host-delay pair loaded Vulkan synchronization validation on both peers with zero validation errors. The guest-delay pair did not enable validation.
- A third local pair replayed the reported house dialogue/movement route: 4,095 matching retained commits and 1,982 matching sampled player positions. Final captures show Mario and Daisy in the house on both peers; a world-map return was not established. No artificial loader delay was used.
- Each native case ended with host exit 0 through intentional shutdown and guest exit 2 for the expected peer disconnect. Guest exit 2 is not a normal menu exit. Reviewed final captures show no obvious black screen or missing geometry; still images cannot exclude transient rendering errors.

The trace comparator checks committed inputs and one detected player position, not every actor, RNG, character identity or complete world state. All native pairs ran on one PC over loopback. No new physical LAN test, Wi-Fi loss/jitter campaign, all-course test, NVIDIA test or end-to-end latency measurement was completed. The previous installed protocol-4 physical house divergence remains part of the failure history; the new local passes do not erase it. The previous prepared LAN installations and original singleplayer installation have not been replaced by this candidate.

The loading-delay hook is enabled only for an active LAN session with `NX_TEST_VIRTUAL_ONLY=1` and explicit `NX_LAN_TEST_LOADING_DELAY_MS`/`NX_LAN_TEST_LOADING_EVENT`. It holds one matching file-open completion once, with a maximum of 20 seconds. Normal play has a cached disabled fast path.
