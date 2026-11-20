# Wonder clock policy

Wonder's fixed simulation step does not make every guest clock deterministic. Legacy lockstep LAN uses the agreed 30 or 60 Hz step and replaces five verified RNG-constructor tick reads per supported source version with shared-session seed values. Other tick reads, SDK polling deadlines, kernel waits and GPU timestamps retain real host time. Freezing those clocks globally could prevent polling or timeouts from completing.

## Shared-clock ABI correction

Both supported Wonder SDKs consume an adjustment record that the time service previously left uninitialized:

| Source | SDK build prefix | Steady-clock consumer | System-clock calculation |
|---|---|---|---|
| Original 1.0.0 | `3053b22935e32ddf` | `sdk+0x3d0300` | `sdk+0x3d0440` |
| Product.120 | `e4bd58cac28cf13a` | `sdk+0x3d0ae0` | `sdk+0x3d0c20` |

The independently checked consumers read a 32-bit sequence at shared offset `0xd0`. Its low bit selects one of two 56-byte records at `0xd8` and `0x110`. Each record contains five signed 64-bit adjustment fields followed by a 128-bit source UUID at offsets `+0x28` and `+0x30`. A changed sequence causes the SDK to retry its read.

The SDK compares this UUID with the system-clock context's source UUID. Leaving the new record zero made that comparison fail with result `0xcc74`. The shared user-clock helper then cleared the time output, while its public getter could report success: an initialized SDK could return POSIX time zero.

The service now initializes both records with matching source identity and neutral adjustment. Zero multiplier, offset and limit preserve real steady-clock progression. The older steady record at `0x0` and user/network contexts at `0x38` and `0x80` remain intact. Production-service CPU tests use independently derived readers for both SDKs, exercise both sequence parities and clock contexts, and reproduce the old source-identity failure by clearing the UUID. These tests do not execute a guest SDK or certify gameplay.

## Legacy LAN limitation

The correction applies only when **legacy `lan::active()` is false**. This includes ordinary play and the separate experimental native socket provider; that provider is distinct from legacy lockstep LAN.

Legacy LAN's direct system-clock IPC uses the session epoch plus presented-frame count divided by the agreed simulation rate. The SDK shared-memory fast path instead adds real host elapsed time to a cached offset. Enabling that path alone would expose conflicting clock policies. Consequently legacy LAN retains its previous shared-clock behavior, including the known source-identity failure. This correction is **not a LAN clock or desync fix**.

The mode is settled before the singleton shared page is allocated: startup completes the launcher and `lan::initialize()` before kernel initialization, module loading or guest constructors. Production lobby starts are confined to that preboot launcher. A failed gameplay session exits and opens a new launcher process. There is no supported same-process transition from ordinary play into legacy LAN; adding one requires revisiting this initialization gate.

A future legacy clock correction must cover all relevant system-clock aliases and SDK fast paths coherently while preserving real steady clocks for scheduling. A hook for one public getter would leave other paths inconsistent. Actual simulation-clock involvement must be established from the first divergent gameplay branch before changing broader timer behavior.
