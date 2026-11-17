# LAN input waits and compact metrics

The performance panel now shows **LAN input wait** while a native LAN session is active. It measures the complete
input-exchange barrier, including the partner's simulation/render scheduling and socket waits. It is not network
ping or controller-to-photon latency. A single slow PC can hold every participant back.

The value is smoothed over recent ticks. Native logs also record peak wait and waits exceeding 50 ms every 600 input
ticks. New sessions reset these values. The thread-safe accumulator test checks startup, smoothing, slow waits,
reset and extreme timestamp values; all currently configured 27 non-GPU targets pass.

Overlay settings include **Compact FPS counter** (FPS/frame time at top right, without the graph) and **Detailed
performance panel** (all metrics at the selected position). The compact panel keeps the LAN wait visible. With
frame generation, presented FPS is evaluated against the selected 2x–8x target, while game FPS uses the simulation
target rather than the generated-picture target.

The optional developer player probe's remembered address is now atomic. `NX_PROBE_PLAYER=ptr=<hex>` supplies a
previously observed fixture address, still subject to the normal vtable check. A bad hint retains the normal scan.
This does not expand the LAN guard: it still samples the primary player, not the complete world state.

The underlying LAN protocol and frame barrier are unchanged by these diagnostics. Prior two-instance tests used
two real characters with independent inputs. A new native route and a separate physical-PC test are still required
for this particular preview; the transport/unit tests alone do not prove full game synchronization.
