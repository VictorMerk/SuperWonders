#pragma once
#include "netplay_wire.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace nx::lan {
// Monotonic session-lifetime transport diagnostics. Message slots use Message's
// numeric value (slot zero is unused). Received message counts require a valid
// packet MAC; this does not imply an accepted role/tick or verified game state.
// Byte counters include partial/invalid traffic and the SWLN framing/MAC, but
// exclude TCP/IP headers. Snapshots are non-transactional and never take io locks.
constexpr size_t kDiagnosticMessageTypes=size_t(Message::SaveAck)+1;
struct SessionDiagnostics {
    std::array<uint64_t,kDiagnosticMessageTypes> sent_messages{},received_messages{};
    uint64_t sent_bytes=0,received_bytes=0;
    uint64_t send_waits=0,receive_waits=0,wait_ns=0,peak_wait_ns=0;
    uint64_t timeouts=0,receive_timeouts=0,cancellations=0;
    uint64_t disconnects=0,socket_errors=0,invalid_packets=0;
};
struct SessionOptions {
    std::string address = "127.0.0.1";
    uint16_t port = 27846;
    uint8_t players = 2;
    uint8_t probe_warmup = 0;  // zero: trusted state; runtime heap probe requires an agreeing baseline
    uint8_t frame_rate = 60, input_lead = 0;  // host choice, adopted by every joining peer
    Key key{};
    Fingerprint build{}, save{};
    bool allow_save_transfer = false;
    std::vector<uint8_t> initial_save;  // host snapshot, bounded by kMaxSaveBytes
    std::function<void(std::span<const uint8_t>)> install_save;  // client: isolated session profile only
    uint64_t epoch = 0, seed = 0;
    std::chrono::milliseconds timeout{15000};
    // Lobby/save transfer can take minutes; an established game must detect a
    // silent peer separately rather than appearing frozen for that whole time.
    std::chrono::milliseconds frame_timeout{10000};
    // Shader preparation can outlast the lobby wait before the first game frame.
    std::chrono::milliseconds startup_timeout{600000};
    std::function<bool()> idle;  // false cancels; no session mutation from this callback
};
// TCP transports authenticated, ordered inputs. The caller supplies one local
// input per simulation tick; all participants receive the same complete frame.
// This class does not claim that the game's simulation is deterministic.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void host(const SessionOptions&, const std::function<void(uint16_t)>& listening = {});
    void join(const SessionOptions&);
    FrameInputs exchange(const plat::PadState& local, uint64_t previous_state_hash = 0, const PlayerState& = {}, uint64_t frame_work_ns=0, uint64_t loading_event=0);
    void stop() noexcept;  // interrupts waits; join caller threads before destroying the object
    uint8_t role() const;
    uint8_t players() const;
    uint64_t epoch() const;
    uint64_t seed() const;
    bool position_probe_ready() const;
    uint8_t frame_rate() const;
    uint8_t input_lead() const;
    SessionDiagnostics diagnostics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
