#pragma once
#include "platform.h"
#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace nx::lan {
constexpr uint16_t kProtocol = 3;
constexpr size_t kHeaderBytes = 40, kMacBytes = 32, kMaxPayload = 4096, kPlayers = 4;
using Key = std::array<uint8_t,32>;
using Nonce = std::array<uint8_t,16>;
using Fingerprint = std::array<uint8_t,32>;
enum class Message : uint16_t { Hello=1, Welcome, Start, Reject, Input, Commit, Ack, Leave, SaveBegin, SaveChunk, SaveEnd, SaveAck };
struct Packet {
    Message type = Message::Hello;
    uint16_t sender = 0;
    uint64_t tick = 0;
    Nonce session{};
    std::vector<uint8_t> payload;
};
struct PlayerState {
    uint64_t scene = 0;
    bool valid = false;
    std::array<float,3> position{};
};
// Floating point simulation may differ below one 1/1024 world unit. Larger
// differences stop the session; this partial guard never repairs unknown state.
bool compatible_state(const PlayerState&,const PlayerState&);
struct FrameInputs {
    uint64_t tick = 0, state_hash = 0;
    uint8_t players = 0;
    std::array<plat::PadState,kPlayers> pads{};
    PlayerState state;
};
Key pairing_key(std::string_view code);
std::vector<uint8_t> encode_packet(const Packet&, const Key&);
Packet decode_packet(std::span<const uint8_t>, const Key&);
size_t packet_payload_size(std::span<const uint8_t> header);
std::vector<uint8_t> encode_input(const plat::PadState&, uint64_t state_hash, const PlayerState& = {});
plat::PadState decode_input(std::span<const uint8_t>, uint64_t& state_hash, PlayerState* = nullptr);
std::vector<uint8_t> encode_commit(const FrameInputs&);
FrameInputs decode_commit(std::span<const uint8_t>);
bool private_ipv4(uint32_t host_order_address);
}
