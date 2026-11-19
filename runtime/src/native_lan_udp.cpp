// Independently implemented BSD IPv4 UDP subset; no MKW source copied.
#include "native_lan_udp.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <mswsock.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nx::native_lan {
namespace {
#ifdef _WIN32
using Socket = SOCKET;
using NativePoll = WSAPOLLFD;
constexpr Socket invalid_socket = INVALID_SOCKET;
int last_error() { return WSAGetLastError(); }
void close_socket(Socket s) { closesocket(s); }
bool bootstrap() {
    struct Winsock {
        bool ok;
        Winsock() { WSADATA data{}; ok = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
        ~Winsock() { if (ok) WSACleanup(); }
    };
    static Winsock instance;
    return instance.ok;
}
int translate(int e) {
    switch (e) {
    case WSAEINTR: return Interrupted; case WSAEBADF: case WSAENOTSOCK: return BadDescriptor;
    case WSAEACCES: return Access; case WSAEFAULT: return Fault; case WSAEINVAL: return Invalid;
    case WSAEMFILE: return TooManyFiles; case WSAEWOULDBLOCK: return WouldBlock;
    case WSAEMSGSIZE: return MessageSize; case WSAENOPROTOOPT: return ProtocolOption;
    case WSAEPROTONOSUPPORT: return Protocol; case WSAESOCKTNOSUPPORT: return SocketType;
    case WSAEOPNOTSUPP: return Unsupported; case WSAEAFNOSUPPORT: return AddressFamily;
    case WSAEADDRINUSE: return AddressInUse; case WSAEADDRNOTAVAIL: return AddressUnavailable;
    case WSAENETDOWN: return NetworkDown; case WSAENETUNREACH: return NetworkUnreachable;
    case WSAECONNABORTED: return ConnectionAborted; case WSAECONNRESET: return ConnectionReset;
    case WSAENOBUFS: return NoBuffers; case WSAEISCONN: return IsConnected;
    case WSAENOTCONN: return NotConnected; case WSAESHUTDOWN: return ShutDown;
    case WSAETIMEDOUT: return TimedOut; case WSAECONNREFUSED: return ConnectionRefused;
    case WSAEHOSTUNREACH: return HostUnreachable; default: return Io;
    }
}
#else
using Socket = int;
using NativePoll = pollfd;
constexpr Socket invalid_socket = -1;
int last_error() { return errno; }
void close_socket(Socket s) { ::close(s); }
bool bootstrap() { return true; }
int translate(int e) {
    switch (e) {
    case EPERM: return Permission; case EINTR: return Interrupted;
    case EBADF: case ENOTSOCK: return BadDescriptor; case EACCES: return Access;
    case EFAULT: return Fault; case EINVAL: return Invalid; case EMFILE: case ENFILE: return TooManyFiles;
    case EAGAIN: return WouldBlock;
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK: return WouldBlock;
#endif
    case EMSGSIZE: return MessageSize; case ENOPROTOOPT: return ProtocolOption;
    case EPROTONOSUPPORT: return Protocol; case ESOCKTNOSUPPORT: return SocketType;
    case EOPNOTSUPP: return Unsupported; case EAFNOSUPPORT: return AddressFamily;
    case EADDRINUSE: return AddressInUse; case EADDRNOTAVAIL: return AddressUnavailable;
    case ENETDOWN: return NetworkDown; case ENETUNREACH: return NetworkUnreachable;
    case ECONNABORTED: return ConnectionAborted; case ECONNRESET: return ConnectionReset;
    case ENOBUFS: return NoBuffers; case ENOMEM: return NoBuffers; case EISCONN: return IsConnected;
    case ENOTCONN: return NotConnected; case ESHUTDOWN: return ShutDown;
    case ETIMEDOUT: return TimedOut; case ECONNREFUSED: return ConnectionRefused;
    case EHOSTUNREACH: return HostUnreachable; default: return Io;
    }
}
#endif
Result fail(int error) { return {-1, error, false}; }
Result host_failure() { return fail(translate(last_error())); }
bool good_span(const void* data, size_t size) { return size == 0 || data != nullptr; }
sockaddr_in native_address(uint32_t address, uint16_t port) {
    sockaddr_in result{}; result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(address); result.sin_port = htons(port); return result;
}
bool set_bool(Socket socket, int option, bool value) {
    const int flag = value ? 1 : 0;
    return setsockopt(socket, SOL_SOCKET, option, reinterpret_cast<const char*>(&flag), sizeof flag) == 0;
}
struct Entry {
    Socket socket;
    std::mutex mutex;
    std::atomic<bool> closed{false};
    explicit Entry(Socket s) : socket(s) {}
    ~Entry() { close_socket(socket); }
};
}

bool permitted_ipv4(uint32_t a, bool destination) noexcept {
    if (!a) return !destination;
    if ((a & 0xff000000u) == 0x0a000000u || (a & 0xff000000u) == 0x7f000000u ||
        (a & 0xfff00000u) == 0xac100000u || (a & 0xffff0000u) == 0xc0a80000u ||
        (a & 0xffff0000u) == 0xa9fe0000u) return true;
    return destination && (a == 0xffffffffu || (a & 0xffffff00u) == 0xe0000000u ||
                           (a & 0xff000000u) == 0xef000000u);
}
Address ipv4_address(uint32_t address, uint16_t port) noexcept {
    Address raw{}; raw[0] = std::byte{16}; raw[1] = std::byte{2};
    raw[2] = static_cast<std::byte>(port >> 8); raw[3] = static_cast<std::byte>(port & 255);
    for (int i = 0; i < 4; ++i) raw[4 + i] = static_cast<std::byte>((address >> (24 - 8 * i)) & 255);
    return raw;
}
bool decode_ipv4(std::span<const std::byte> raw, uint32_t& address, uint16_t& port) noexcept {
    if (raw.size() != 16 || !raw.data() || raw[0] != std::byte{16} || raw[1] != std::byte{2}) return false;
    uint32_t a = 0; for (int i = 0; i < 4; ++i) a = (a << 8) | std::to_integer<unsigned>(raw[4 + i]);
    address = a; port = uint16_t((std::to_integer<unsigned>(raw[2]) << 8) | std::to_integer<unsigned>(raw[3]));
    return true;
}
struct Udp::State {
    struct Slot { std::shared_ptr<Entry> entry; uint32_t generation = 1; };
    std::array<Slot, kDescriptors> slots{};
    std::mutex mutex;
    std::atomic<bool> cancelled{false};
    bool enabled;
    explicit State(bool enable) : enabled(enable && bootstrap()) {}
    std::shared_ptr<Entry> get(int fd) {
        if (fd < 0) return {};
        const auto index = unsigned(fd) % kDescriptors, generation = unsigned(fd) / kDescriptors;
        std::lock_guard lock(mutex);
        auto& slot = slots[index];
        return slot.generation == generation ? slot.entry : nullptr;
    }
    int unavailable() const {
        return !enabled ? NetworkDown : cancelled.load(std::memory_order_relaxed) ? Interrupted : 0;
    }
};
Udp::Udp(bool enabled) : state_(std::make_unique<State>(enabled)) {}
Udp::~Udp() { cancel(); } // Caller must join users before object destruction.
void Udp::cancel() noexcept { state_->cancelled.store(true, std::memory_order_relaxed); }

Result Udp::socket(int family, int type, int protocol) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (family != 2) return fail(AddressFamily);
    if (type != 2) return fail(SocketType);
    if (protocol != 0 && protocol != 17) return fail(Protocol);
    const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == invalid_socket) return host_failure();
    std::shared_ptr<Entry> entry;
    try { entry = std::make_shared<Entry>(socket); }
    catch (const std::bad_alloc&) { close_socket(socket); return fail(NoBuffers); }
#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking)) return host_failure();
    // Connectionless, multi-peer UDP policy: a closed peer must not poison a
    // later receive from a live peer via Windows' asynchronous ICMP reset.
    // Keep other syscall errors intact and fail creation if this policy cannot
    // be installed. No service/guest operation is claimed to have succeeded.
    BOOL report_resets = FALSE; DWORD returned = 0;
    if (WSAIoctl(socket, SIO_UDP_CONNRESET, &report_resets, sizeof report_resets,
                 nullptr, 0, &returned, nullptr, nullptr)) return host_failure();
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) return host_failure();
    const int fdflags = fcntl(socket, F_GETFD, 0);
    if (fdflags < 0 || fcntl(socket, F_SETFD, fdflags | FD_CLOEXEC) < 0) return host_failure();
#endif
    // Multicast is scoped even if a caller forgets to configure its hop limit.
    const int ttl = 1;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl)) return host_failure();
    std::lock_guard lock(s.mutex);
    if (int e = s.unavailable()) return fail(e);
    for (size_t i = 0; i < s.slots.size(); ++i) if (!s.slots[i].entry && s.slots[i].generation) {
        auto& slot = s.slots[i]; slot.entry = std::move(entry);
        return {int64_t(slot.generation * kDescriptors + i), 0, false};
    }
    return fail(TooManyFiles);
}
Result Udp::bind(int fd, std::span<const std::byte> raw) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    uint32_t address; uint16_t port;
    if (!decode_ipv4(raw, address, port)) return fail(Invalid);
    if (!permitted_ipv4(address, false)) return fail(Access);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    auto native = native_address(address, port);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    if (::bind(entry->socket, reinterpret_cast<const sockaddr*>(&native), sizeof native)) return host_failure();
    return {0, 0, false};
}
Result Udp::local_address(int fd, Address& output) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    sockaddr_in address{};
#ifdef _WIN32
    int length = sizeof address;
#else
    socklen_t length = sizeof address;
#endif
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    if (getsockname(entry->socket, reinterpret_cast<sockaddr*>(&address), &length)) return host_failure();
    output = ipv4_address(ntohl(address.sin_addr.s_addr), ntohs(address.sin_port));
    return {0, 0, false};
}
Result Udp::send_to(int fd, std::span<const std::byte> bytes, int flags, std::span<const std::byte> raw) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (flags & ~DontWait) return fail(Unsupported);
    if (bytes.size() > kMaxDatagram) return fail(MessageSize);
    if (!good_span(bytes.data(), bytes.size())) return fail(Fault);
    uint32_t address; uint16_t port;
    if (!decode_ipv4(raw, address, port) || !port) return fail(Invalid);
    if (!permitted_ipv4(address, true)) return fail(Access);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    const auto native = native_address(address, port);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    const char dummy = 0;
    const auto* data = bytes.empty() ? &dummy : reinterpret_cast<const char*>(bytes.data());
    const auto sent = ::sendto(entry->socket, data, int(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&native), sizeof native);
    return sent < 0 ? host_failure() : Result{int64_t(sent), 0, false};
}
Result Udp::receive_from(int fd, std::span<std::byte> bytes, int flags, Address* sender) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (flags & ~(DontWait | Peek)) return fail(Unsupported);
    if (bytes.size() > kMaxDatagram) return fail(MessageSize);
    if (!good_span(bytes.data(), bytes.size())) return fail(Fault);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    // Bound work if wildcard-bound sockets receive packets from public sources.
    // Receive into scratch until policy passes: rejected payloads never reach the
    // caller's buffer. Peek cannot discard a rejected packet, so return Access.
    std::array<std::byte, kMaxDatagram> scratch;
    for (int attempt = 0; attempt < 16; ++attempt) {
        sockaddr_in source{}; int64_t received = -1; bool truncated = false;
#ifdef _WIN32
        int length = sizeof source;
        WSABUF buffer{ULONG(scratch.size()), reinterpret_cast<char*>(scratch.data())};
        DWORD count = 0, native_flags = flags & Peek ? MSG_PEEK : 0;
        const int result = WSARecvFrom(entry->socket, &buffer, 1, &count, &native_flags,
            reinterpret_cast<sockaddr*>(&source), &length, nullptr, nullptr);
        if (result && WSAGetLastError() != WSAEMSGSIZE) return host_failure();
        received = count; truncated = result != 0;
#else
        iovec vector{scratch.data(), scratch.size()};
        msghdr message{}; message.msg_name = &source; message.msg_namelen = sizeof source;
        message.msg_iov = &vector; message.msg_iovlen = 1;
        received = recvmsg(entry->socket, &message, flags & Peek ? MSG_PEEK : 0);
        if (received < 0) return host_failure();
        truncated = message.msg_flags & MSG_TRUNC;
#endif
        const uint32_t address = ntohl(source.sin_addr.s_addr);
        if (!permitted_ipv4(address, false) || !address) {
            if (flags & Peek) return fail(Access);
            continue;
        }
        const size_t copied = std::min(bytes.size(), size_t(received));
        if (copied) std::memcpy(bytes.data(), scratch.data(), copied);
        if (sender) *sender = ipv4_address(address, ntohs(source.sin_port));
        return {int64_t(copied), 0, truncated || size_t(received) > bytes.size()};
    }
    return fail(WouldBlock);
}
Result Udp::set_option(int fd, Option option, bool enabled) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (option != Option::Broadcast && option != Option::ReuseAddress && option != Option::ReusePort) return fail(ProtocolOption);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    int native_option;
    switch (option) {
    case Option::Broadcast: native_option = SO_BROADCAST; break;
    case Option::ReuseAddress: native_option = SO_REUSEADDR; break;
    case Option::ReusePort:
#if defined(SO_REUSEPORT) && !defined(_WIN32)
        native_option = SO_REUSEPORT; break;
#else
        return fail(Unsupported);
#endif
    default: return fail(ProtocolOption);
    }
    return set_bool(entry->socket, native_option, enabled)
        ? Result{0, 0, false} : host_failure();
}
Result Udp::get_option(int fd, Option option) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (option != Option::Broadcast && option != Option::ReuseAddress && option != Option::ReusePort) return fail(ProtocolOption);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    int native_option;
    switch (option) {
    case Option::Broadcast: native_option = SO_BROADCAST; break;
    case Option::ReuseAddress: native_option = SO_REUSEADDR; break;
    case Option::ReusePort:
#if defined(SO_REUSEPORT) && !defined(_WIN32)
        native_option = SO_REUSEPORT; break;
#else
        return fail(Unsupported);
#endif
    default: return fail(ProtocolOption);
    }
    int value = 0;
#ifdef _WIN32
    int length = sizeof value;
#else
    socklen_t length = sizeof value;
#endif
    if (getsockopt(entry->socket, SOL_SOCKET, native_option, reinterpret_cast<char*>(&value), &length)) return host_failure();
    if (length != sizeof value) return fail(Io);
    return {value != 0, 0, false};
}
Result Udp::set_unicast_ttl(int fd, int ttl) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (ttl < 1 || ttl > 255) return fail(Invalid);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    return setsockopt(entry->socket, IPPROTO_IP, IP_TTL,
        reinterpret_cast<const char*>(&ttl), sizeof ttl) == 0 ? Result{0, 0, false} : host_failure();
}
Result Udp::get_unicast_ttl(int fd) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    int ttl = 0;
#ifdef _WIN32
    int length = sizeof ttl;
#else
    socklen_t length = sizeof ttl;
#endif
    if (getsockopt(entry->socket, IPPROTO_IP, IP_TTL, reinterpret_cast<char*>(&ttl), &length)) return host_failure();
    if (length != sizeof ttl || ttl < 1 || ttl > 255) return fail(Io);
    return {ttl, 0, false};
}
Result Udp::shutdown(int fd, int how) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (how < 0 || how > 2) return fail(Invalid);
    auto entry = s.get(fd); if (!entry) return fail(BadDescriptor);
    std::lock_guard lock(entry->mutex);
    if (entry->closed.load()) return fail(BadDescriptor);
    return ::shutdown(entry->socket, how) == 0 ? Result{0, 0, false} : host_failure();
}
Result Udp::close(int fd) {
    auto& s = *state_;
    if (!s.enabled) return fail(NetworkDown);
    if (fd < 0) return fail(BadDescriptor);
    std::shared_ptr<Entry> entry;
    {
        std::lock_guard lock(s.mutex);
        auto& slot = s.slots[unsigned(fd) % kDescriptors];
        if (slot.generation != unsigned(fd) / kDescriptors || !slot.entry) return fail(BadDescriptor);
        entry = std::move(slot.entry);
        // Exhausted slots retire permanently: an ancient fd never aliases a
        // fresh socket even after the finite descriptor generation is used up.
        slot.generation = slot.generation == 0x3ffffffu ? 0 : slot.generation + 1;
    }
    std::lock_guard lock(entry->mutex);
    entry->closed.store(true);
    // OS handle closes when in-flight operations release their shared reference.
    return {0, 0, false};
}
Result Udp::poll(std::span<PollFd> fds, int timeout_ms) {
    auto& s = *state_; if (int e = s.unavailable()) return fail(e);
    if (fds.size() > kDescriptors || !good_span(fds.data(), fds.size()) || timeout_ms < 0 || timeout_ms > kMaxPollMs) return fail(Invalid);
    std::array<std::shared_ptr<Entry>, kDescriptors> entries;
    std::array<NativePoll, kDescriptors> native{};
    for (size_t i = 0; i < fds.size(); ++i) {
        fds[i].revents = 0;
        if (fds[i].events & ~(Readable | Writable)) return fail(Invalid);
        if (fds[i].fd >= 0) entries[i] = s.get(fds[i].fd);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (int e = s.unavailable()) return fail(e);
        int ready = 0; size_t count = 0;
        std::array<size_t, kDescriptors> indices{};
        for (size_t i = 0; i < fds.size(); ++i) {
            auto& entry = entries[i];
            fds[i].revents = 0;
            if (fds[i].fd < 0) continue;
            if (!entry || entry->closed.load()) { fds[i].revents = InvalidFd; ++ready; continue; }
            auto& p = native[count]; indices[count++] = i;
            p.fd = entry->socket; p.events = 0; p.revents = 0;
            if (fds[i].events & Readable) p.events |= POLLIN;
            if (fds[i].events & Writable) p.events |= POLLOUT;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        const int wait_ms = ready || now >= deadline ? 0 : int(std::clamp<int64_t>(remaining + 1, 1, 10));
        int result = 0;
        if (count) {
#ifdef _WIN32
            result = WSAPoll(native.data(), ULONG(count), wait_ms);
#else
            result = ::poll(native.data(), count, wait_ms);
#endif
            if (result < 0) return host_failure();
        } else if (wait_ms) std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        if (int e = s.unavailable()) return fail(e);
        for (size_t j = 0; j < count; ++j) {
            const size_t i = indices[j]; const int events = native[j].revents;
            if (entries[i]->closed.load()) fds[i].revents = InvalidFd;
            else {
                if (events & POLLIN) fds[i].revents |= Readable;
                if (events & POLLOUT) fds[i].revents |= Writable;
                if (events & POLLERR) fds[i].revents |= PollError;
                if (events & POLLHUP) fds[i].revents |= Hangup;
                if (events & POLLNVAL) fds[i].revents |= InvalidFd;
            }
            if (fds[i].revents) ++ready;
        }
        if (ready || std::chrono::steady_clock::now() >= deadline) return {ready, 0, false};
    }
}
} // namespace nx::native_lan
