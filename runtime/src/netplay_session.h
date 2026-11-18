#pragma once
#include "netplay_wire.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace nx::lan {
struct SessionOptions {
    std::string address = "127.0.0.1";
    uint16_t port = 27846;
    uint8_t players = 2;
    Key key{};
    Fingerprint build{}, save{};
    bool allow_save_transfer = false;
    std::vector<uint8_t> initial_save;  // host snapshot, bounded by kMaxSaveBytes
    std::function<void(std::span<const uint8_t>)> install_save;  // client: isolated session profile only
    uint64_t epoch = 0, seed = 0;
    std::chrono::milliseconds timeout{15000};
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
    FrameInputs exchange(const plat::PadState& local, uint64_t previous_state_hash = 0, const PlayerState& = {});
    void stop() noexcept;  // interrupts waits; join caller threads before destroying the object
    uint8_t role() const;
    uint8_t players() const;
    uint64_t epoch() const;
    uint64_t seed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
