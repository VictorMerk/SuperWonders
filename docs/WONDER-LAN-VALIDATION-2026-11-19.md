# Wonder LAN streaming-music fix and diagnostics

A physical two-PC session stopped presenting frames on one peer while both
continued exchanging inputs. The stalled peer opened the Savanna rhythm track
four times; its partner opened it three times. The fourth file open incorrectly
created another loading barrier. One presented frame persisted through 3,407
exchanges. Matching input packets alone did not establish playable synchronization.

The runtime now coalesces music reopens within the current title/world/course
category. A scene load or death rearms it, and cached map/course transitions still
synchronize. This is covered by a real two-peer authenticated TCP regression with
asymmetric audio opens followed by independent controller input.

The verified frame-governor return site is also selected by executable identity,
including the Product120 source. Unknown builds do not receive that memory patch.

## Validation

- All 72 CPU tests passed after the final diagnostic export change.
- Two physical PCs completed two 7,200-frame routes and a 600-frame shader warmup.
  Captures show the badge house and world map. Shared-input comparisons found no
  mismatch or large presentation-progress discrepancy. The counterpart's normal
  requested shutdown can result in exit code 2 for the peer.
- A final 1,800-frame physical run checked the extended diagnostics and structured
  transport export on both peers. Both produced captures; no transport timeout,
  invalid packet was recorded. One peer observed a socket reset when the other
  ended the session; this is retained in the error counters rather than hidden.
  Shutdown cancellation is counted separately from a network failure.
- The sampled map/house windows averaged approximately 55 FPS with frame generation
  off. Frame-time spikes remain. These runs do not establish an FPS improvement,
  locked 60 FPS, zero input delay, all-actor synchronization or a completed level.
- The exact Savanna freeze was reproduced from its saved logs and in the CPU TCP
  regression; that stage still needs a full physical gameplay regression.

## Diagnostic files

`lan-input-trace.csv` keeps a bounded 4,096-exchange ring. It includes local/peer
loading markers and exchange timestamps. The comparison tool separates loading,
steady gameplay, failed exchanges and older traces with unknown phase. It reports
presentation progress and input age without treating either as whole-world proof.

`lan-transport-stats.json` records protocol/role/rate, message counts, wire bytes,
socket waits and errors. Snapshots are atomic but non-transactional. Received
message counts mean MAC-valid packets; byte counts exclude TCP/IP overhead.
Peak socket wait describes one select call, not network RTT or a whole exchange.
Timeout totals include the receive-timeout subset.

The native log emits bounded transport summaries every 600 ticks and on shutdown.
No keys, save payloads or packet contents are added to those diagnostics. Long
manual reproductions should enable the existing `NX_INPUT_RECORD` file so a long
freeze cannot evict the entire route from the bounded trace ring.
