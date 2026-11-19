#include "native_lan_udp.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace nx::native_lan;
using Clock = std::chrono::steady_clock;
namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "native UDP: %s\n", message); std::exit(1); }
}
int socket(Udp& udp) {
    auto result = udp.socket(2, 2, 17);
    check(bool(result) && result.value >= 0, "create UDP socket");
    return int(result.value);
}
Address bind_loopback(Udp& udp, int fd) {
    check(bool(udp.bind(fd, ipv4_address(0x7f000001, 0))), "bind loopback ephemeral port");
    Address raw{}; check(bool(udp.local_address(fd, raw)), "query bound address");
    uint32_t address = 0; uint16_t port = 0;
    check(decode_ipv4(raw, address, port) && address == 0x7f000001 && port, "native/guest endian roundtrip");
    return raw;
}
void readable(Udp& udp, int fd) {
    PollFd poll{fd, Readable}; auto result = udp.poll({&poll, 1}, 1000);
    check(bool(result) && result.value == 1 && (poll.revents & Readable), "loopback datagram becomes readable");
}
}
int main() {
    check(WouldBlock == 11 && Invalid == 22 && BadDescriptor == 9 && NetworkDown == 100 &&
          AddressInUse == 98 && MessageSize == 90, "Linux errno ABI despite BSD sockaddr layout");
    for (uint32_t address : {0x0a000001u, 0xac100001u, 0xac1fffffu, 0xc0a80101u, 0x7f000001u, 0xa9fe0101u})
        check(permitted_ipv4(address, false) && permitted_ipv4(address, true), "private address allowed");
    for (uint32_t address : {0x08080808u, 0xac200001u, 0xac0fffffu, 0xc0a90001u, 0x01010101u, 0xe1000001u, 0xf0000001u})
        check(!permitted_ipv4(address, false) && !permitted_ipv4(address, true), "public/unscoped address denied");
    check(permitted_ipv4(0, false) && !permitted_ipv4(0, true), "wildcard binds only");
    for (uint32_t address : {0xffffffffu, 0xe0000001u, 0xefffffffu})
        check(permitted_ipv4(address, true) && !permitted_ipv4(address, false), "scoped discovery destinations only");
    auto raw = ipv4_address(0xc0a80102u, 0x1234);
    check(raw[2] == std::byte{0x12} && raw[3] == std::byte{0x34} && raw[4] == std::byte{0xc0}, "explicit network endian codec");
    uint32_t decoded = 77; uint16_t port = 88;
    check(!decode_ipv4(std::span(raw).first(15), decoded, port) && decoded == 77 && port == 88, "short address rejected without partial output");
    raw[0] = std::byte{0}; check(!decode_ipv4(raw, decoded, port), "invalid BSD sin_len rejected");
    raw[0] = std::byte{16}; raw[1] = std::byte{10}; check(!decode_ipv4(raw, decoded, port), "IPv6/other family rejected");
    {
        Udp disabled;
        check(disabled.socket(2, 2, 17).error == NetworkDown && disabled.close(32).error == NetworkDown,
              "default-off adapter never creates a host socket");
        check(disabled.set_unicast_ttl(32, 64).error == NetworkDown && disabled.get_unicast_ttl(32).error == NetworkDown &&
              disabled.set_option(32, Option::ReusePort, true).error == NetworkDown, "new options preserve disabled behavior");
    }
    {
        Udp udp(true);
        check(udp.socket(10, 2, 17).error == AddressFamily, "IPv4 only");
        check(udp.socket(2, 1, 6).error == SocketType && udp.socket(2, 2, 6).error == Protocol, "TCP/protocol denied");
        std::array<int, Udp::kDescriptors> handles{};
        for (auto& fd : handles) fd = socket(udp);
        check(udp.socket(2, 2, 17).error == TooManyFiles, "descriptor table bounded");
        const int old = handles[0]; check(bool(udp.close(old)), "close allocated slot");
        const int replacement = socket(udp);
        check(old != replacement && udp.close(old).error == BadDescriptor, "generation prevents stale-descriptor alias");
        handles[0] = replacement;
        for (int fd : handles) check(bool(udp.close(fd)), "all bounded handles close");
    }
    Udp udp(true);
    const int sender = socket(udp), receiver = socket(udp);
    const Address source = bind_loopback(udp, sender), destination = bind_loopback(udp, receiver);
    check(bool(udp.set_option(sender, Option::Broadcast, true)) && bool(udp.set_option(receiver, Option::ReuseAddress, true)), "supported boolean options");
    check(udp.get_option(sender, Option::Broadcast).value == 1 && udp.get_option(receiver, Option::ReuseAddress).value == 1,
          "boolean option getters query installed OS state");
    check(bool(udp.set_option(sender, Option::Broadcast, false)) && udp.get_option(sender, Option::Broadcast).value == 0,
          "boolean getter observes actual disabled state");
    for (int ttl : {1, 64, 255}) {
        check(bool(udp.set_unicast_ttl(sender, ttl)), "set valid unicast TTL");
        const auto actual = udp.get_unicast_ttl(sender);
        check(bool(actual) && actual.value == ttl, "read back actual OS unicast TTL");
    }
    check(udp.set_unicast_ttl(sender, 0).error == Invalid && udp.set_unicast_ttl(sender, 256).error == Invalid &&
          udp.get_unicast_ttl(sender).value == 255, "invalid TTL rejected without changing installed option");
    check(udp.set_unicast_ttl(-1, 64).error == BadDescriptor && udp.get_unicast_ttl(-1).error == BadDescriptor &&
          udp.set_option(-1, Option::ReusePort, true).error == BadDescriptor && udp.get_option(-1, Option::Broadcast).error == BadDescriptor,
          "new options reject invalid descriptors");
#if defined(SO_REUSEPORT) && !defined(_WIN32)
    {
        const auto native = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        check(native >= 0, "create native reuseport capability oracle");
        const int enabled = 1;
        const int oracle = setsockopt(native, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof enabled);
        ::close(native);
        const auto reuse = udp.set_option(receiver, Option::ReusePort, true);
        check(oracle == 0 ? bool(reuse) && reuse.value == 0 : !bool(reuse), "reuseport matches native capability, no fake success");
        if (oracle == 0) check(bool(udp.set_option(receiver, Option::ReusePort, false)), "disable supported native reuseport");
    }
#else
    check(udp.set_option(receiver, Option::ReusePort, true).error == Unsupported &&
          udp.set_option(receiver, Option::ReusePort, false).error == Unsupported && udp.get_option(receiver, Option::ReusePort).error == Unsupported,
          "reuseport explicitly unsupported without native capability, never aliased to reuseaddr");
#endif
    std::array<std::byte, 8> data{std::byte{1}, std::byte{2}, std::byte{0}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
    std::array<std::byte, 8> output{};
    check(udp.receive_from(receiver, output, 0).error == WouldBlock, "flags0 is still nonblocking");
    check(udp.send_to(sender, data, 0, ipv4_address(0x08080808, 53)).error == Access, "public destination rejected before OS send");
    check(udp.bind(sender, ipv4_address(0x08080808, 0)).error == Access, "public bind rejected");
    check(udp.send_to(sender, data, Peek, destination).error == Unsupported, "unsupported send flags rejected");
    check(udp.receive_from(receiver, output, 0x10).error == Unsupported, "unsupported receive flags rejected");
    check(udp.send_to(sender, data, 0, std::span(destination).first(8)).error == Invalid, "short sockaddr bounds");
    std::vector<std::byte> oversized(Udp::kMaxDatagram + 1);
    check(udp.send_to(sender, oversized, 0, destination).error == MessageSize &&
          udp.receive_from(receiver, oversized, 0).error == MessageSize, "oversized spans rejected");
    check(udp.send_to(sender, data, DontWait, destination).value == 8, "send loopback datagram");
    readable(udp, receiver);
    Address peer{};
    auto result = udp.receive_from(receiver, std::span(output).first(2), Peek | DontWait, &peer);
    check(bool(result) && result.value == 2 && result.truncated && output[0] == data[0], "peek reports bounded truncation");
    result = udp.receive_from(receiver, output, Peek, &peer);
    check(bool(result) && result.value == 8 && !result.truncated && output == data && peer == source, "peek keeps complete datagram and encodes sender");
    result = udp.receive_from(receiver, output, 0);
    check(bool(result) && result.value == 8 && output == data, "receive consumes one complete datagram");
    check(udp.receive_from(receiver, output, DontWait).error == WouldBlock, "datagram consumed exactly once");
    check(udp.send_to(sender, data, 0, destination).value == 8, "send second datagram"); readable(udp, receiver);
    result = udp.receive_from(receiver, std::span(output).first(2), 0);
    check(result.value == 2 && result.truncated, "nonpeek short receive reports discarded remainder");
    check(udp.receive_from(receiver, output, 0).error == WouldBlock, "short receive discards exactly one remainder");
    check(udp.send_to(sender, {}, 0, destination).value == 0, "zero-length UDP is transmitted"); readable(udp, receiver);
    result = udp.receive_from(receiver, {}, 0);
    check(bool(result) && result.value == 0 && !result.truncated, "zero-length UDP is received");
    PollFd waiting{receiver, Readable};
    const auto before = Clock::now(); result = udp.poll({&waiting, 1}, 20);
    check(bool(result) && result.value == 0 && waiting.revents == 0 && Clock::now() - before >= std::chrono::milliseconds(15), "finite poll timeout without busy wait");
    check(udp.poll({&waiting, 1}, -1).error == Invalid && udp.poll({&waiting, 1}, 1001).error == Invalid, "infinite and excessive timeouts rejected");
    std::array<PollFd, 33> too_many{};
    check(udp.poll(too_many, 0).error == Invalid, "poll descriptor bound");
    std::array<PollFd, 3> ready{{{receiver, Readable}, {sender, Writable}, {-1, Readable}}};
    result = udp.poll(ready, 0);
    check(bool(result) && result.value == 1 && ready[1].revents == Writable && ready[2].revents == 0, "write readiness and negative fd ignore");
    check(udp.shutdown(sender, 3).error == Invalid, "shutdown mode bounds");
    {
        const int old = socket(udp); bind_loopback(udp, old);
        std::atomic<bool> started{false}; Result polled; PollFd p{old, Readable};
        std::thread thread([&] { started = true; polled = udp.poll({&p, 1}, 1000); });
        while (!started.load()) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(bool(udp.close(old)), "close while poll owns socket reference");
        const int reused = socket(udp); bind_loopback(udp, reused);
        thread.join();
        check(bool(polled) && polled.value == 1 && p.revents == InvalidFd && reused != old,
              "close wakes poll with stale-generation invalid result");
        check(udp.receive_from(old, output, 0).error == BadDescriptor, "stale receive rejected after reuse");
        check(udp.set_unicast_ttl(old, 64).error == BadDescriptor && udp.get_unicast_ttl(old).error == BadDescriptor &&
              udp.set_option(old, Option::ReusePort, true).error == BadDescriptor, "new options cannot mutate replacement through stale descriptor");
        check(bool(udp.close(reused)), "close replacement socket");
    }
    {
        std::atomic<bool> started{false}; Result polled; PollFd p{receiver, Readable};
        std::thread thread([&] { started = true; polled = udp.poll({&p, 1}, 1000); });
        while (!started.load()) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto cancelled = Clock::now(); udp.cancel(); thread.join();
        check(polled.error == Interrupted && Clock::now() - cancelled < std::chrono::seconds(2), "cancellation interrupts bounded poll wait");
        check(udp.send_to(sender, data, 0, destination).error == Interrupted && udp.socket(2, 2, 17).error == Interrupted,
              "cancelled adapter cannot begin new traffic");
        check(udp.set_unicast_ttl(sender, 64).error == Interrupted && udp.get_unicast_ttl(sender).error == Interrupted &&
              udp.set_option(sender, Option::ReusePort, true).error == Interrupted && udp.get_option(sender, Option::Broadcast).error == Interrupted,
              "new options preserve permanent cancellation");
        check(bool(udp.close(sender)) && bool(udp.close(receiver)), "close still works after cancellation");
    }
    {
        Udp shutdown_test(true); const int fd = socket(shutdown_test);
        bind_loopback(shutdown_test, fd);
        // Windows can succeed for unconnected UDP where Linux returns ENOTCONN.
        // Compare the real host operation rather than fabricating one platform's
        // shutdown result as a supposedly portable guest contract.
        const auto native = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(0x7f000001);
        check(::bind(native, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0, "bind native shutdown oracle on loopback");
        const int oracle = ::shutdown(native, 2);
#ifdef _WIN32
        const int native_error = oracle ? WSAGetLastError() : 0;
        closesocket(native);
        check(!oracle || native_error == WSAENOTCONN, "known Windows UDP shutdown outcome");
#else
        const int native_error = oracle ? errno : 0;
        ::close(native);
        check(!oracle || native_error == ENOTCONN, "known POSIX UDP shutdown outcome");
#endif
        result = shutdown_test.shutdown(fd, 2);
        check(oracle == 0 ? bool(result) && result.value == 0 : result.error == NotConnected,
              "UDP shutdown forwards host success or mapped real error");
    }
    std::puts("native private-LAN UDP CPU/loopback tests passed");
}
