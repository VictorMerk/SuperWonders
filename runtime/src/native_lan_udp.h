#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace nx::native_lan {

// Standalone experimental transport. No environment switch, guest hooks, service
// registration, or account integration. A future adapter must validate guest
// memory before constructing these host spans. It must opt in explicitly.
// Guest errno uses Linux numbers despite its BSD sockaddr layout. Product120
// main+f5005c, +f501b0 and +f502cc compare GetLastErrno to11 for would-block.
// Other numeric mappings follow the matching SDK/MKW ABI and require further
// guest-callsite validation before installing a Fn adapter.
enum Error : int {
    Permission = 1, Interrupted = 4, Io = 5, BadDescriptor = 9, Access = 13,
    Fault = 14, Invalid = 22, TooManyFiles = 24, WouldBlock = 11,
    MessageSize = 90, ProtocolOption = 92, Protocol = 93, SocketType = 94,
    Unsupported = 95, AddressFamily = 97, AddressInUse = 98,
    AddressUnavailable = 99, NetworkDown = 100, NetworkUnreachable = 101,
    ConnectionAborted = 103, ConnectionReset = 104, NoBuffers = 105,
    IsConnected = 106, NotConnected = 107, ShutDown = 108, TimedOut = 110,
    ConnectionRefused = 111, HostUnreachable = 113,
};
struct Result {
    int64_t value = -1;
    int error = 0; // Linux/guest errno numbers, never a raw Winsock error.
    bool truncated = false;
    explicit operator bool() const { return error == 0; }
};
using Address = std::array<std::byte, 16>; // sin_len=16, AF_INET=2, network-order port/address.
struct PollFd { int fd; int16_t events; int16_t revents = 0; };
constexpr int16_t Readable = 1, Writable = 4, PollError = 8, Hangup = 16, InvalidFd = 32;
constexpr int Peek = 2, DontWait = 0x80;
enum class Option { Broadcast, ReuseAddress };

// Address is a canonical integer with first IPv4 octet in the high byte.
// Bind permits wildcard/private/link-local/loopback; destinations additionally
// permit limited broadcast and local/admin-scoped multicast, always TTL=1.
bool permitted_ipv4(uint32_t address, bool destination) noexcept;
Address ipv4_address(uint32_t address, uint16_t port) noexcept;
bool decode_ipv4(std::span<const std::byte> raw, uint32_t& address, uint16_t& port) noexcept;

class Udp {
public:
    // Generation-tagged IDs; slots retire rather than wrapping a generation.
    static constexpr size_t kDescriptors = 32, kMaxDatagram = 65507;
    static constexpr int kMaxPollMs = 1000;
    explicit Udp(bool enabled = false);
    ~Udp();
    Udp(const Udp&) = delete;
    Udp& operator=(const Udp&) = delete;
    Result socket(int family, int type, int protocol);
    Result bind(int fd, std::span<const std::byte> address);
    Result local_address(int fd, Address& address);
    Result send_to(int fd, std::span<const std::byte> bytes, int flags,
                   std::span<const std::byte> address);
    // Pointer-only output is host-owned; sender bytes are not logged/stored.
    Result receive_from(int fd, std::span<std::byte> bytes, int flags, Address* sender = nullptr);
    Result set_option(int fd, Option option, bool enabled);
    // Real OS shutdown result: unconnected UDP commonly returns NotConnected;
    // this API never reports fabricated shutdown success.
    Result shutdown(int fd, int how);
    Result close(int fd);
    // All host sockets stay nonblocking. Flags=0 never implies blocking here.
    // Poll accepts 0..1000ms, <=32 entries, Readable/Writable only; fd<0 ignored.
    // Invalid/stale descriptors return InvalidFd readiness, as poll does.
    Result poll(std::span<PollFd> fds, int timeout_ms);
    // Permanent cancellation: new operations return Interrupted; an already
    // running nonblocking syscall may complete. Poll notices in
    // <=10ms plus scheduler delay. close remains available after cancellation.
    void cancel() noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace nx::native_lan
