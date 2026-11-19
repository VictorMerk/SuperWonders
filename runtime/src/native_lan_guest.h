#pragma once
#include "native_lan_udp.h"
#include <nx/cpu.h>
#include <span>

namespace nx::native_lan {
enum class GuestCall { Socket, Bind, SendTo, RecvFrom, Poll, Close, GetLastErrno, SetLastErrno };
enum class MemoryAccess { Read, Write, ReadWrite };
// Injected numeric-address access only: no guest pointer is cast/dereferenced.
// The owner must pin mappings/permissions for the whole invoke() call. validate
// must cover the entire span; read/write report failures even after validation.
class GuestMemory {
public:
    virtual ~GuestMemory() = default;
    virtual bool validate(uint64_t address,size_t bytes,MemoryAccess access) noexcept = 0;
    virtual bool read(uint64_t address,std::span<std::byte> bytes) noexcept = 0;
    virtual bool write(uint64_t address,std::span<const std::byte> bytes) noexcept = 0;
};
// Explicit caller assertion, NOT an initialization operation or state check.
// Future integration must establish SDK socket-client initialized state and
// coherent errno accessor aliases, or own a complete separate backend lifecycle.
// Original SDK uninitialized getter22/setter-no-op behavior is NOT modeled by
// this direct TLS helper. The tag cannot fabricate a service/client success.
struct EstablishedGuestContext {};
// Uninstalled ABI adapter. No hooks, services or fabricated connectivity. The
// Udp instance must separately be explicitly enabled. Socket/Poll scalar ints
// use low32 register bits; data lengths and Poll nfds use full64; socklen is u32.
// Product120 SDK errno lives at CPU.tpidrro+0x1d8 (per guest thread). WouldBlock11
// is verified; other errno mappings inherit the core's provisional ABI mapping.
class GuestAdapter {
public:
    static constexpr uint64_t kErrnoOffset=0x1d8;
    GuestAdapter(Udp& udp,GuestMemory& memory,EstablishedGuestContext):udp_(udp),memory_(memory) {}
    // true: completed raw guest call, including x0=-1 + guest errno failures.
    // Invalid argument mappings with valid TLS complete as guest Fault14.
    // false: unavailable TLS or unexpected adapter/output-copy failure; x0 stays
    // unchanged. Caller MUST NOT resume/fall back as though no call occurred:
    // unexpected output-copy failure can follow a completed host operation.
    // IPv4 sin_len0 and16 are accepted; only a private copy is normalized.
    // Every I/O call prevalidates TLS errno RW and all destination spans before
    // network side effects. Success preserves errno and all CPU state except x0.
    // SetLastErrno is void: writes low32(x0), preserves every CPU register.
    // Verified SDK boundary differs from generic BSD: zero-sized SendTo with a
    // destination and zero-sized RecvFrom return0 before host I/O. Missing SendTo
    // destination delegates to connected Send in SDK; unsupported here.
    // RecvFrom ignores sender outputs if address/length pointer/input length is0.
    // Otherwise it copies min(input length,16), writes actual length16 only on
    // success (caller may supply28). Data truncation returns bytes copied and
    // discards remainder unless Peek; no MSG_TRUNC extension. Nonzero NULL data
    // gives Invalid22; lengths over32bits give Fault14, with NULL-buffer check preceding width check.
    bool invoke(CPU& cpu,GuestCall call);
private:
    Udp& udp_;
    GuestMemory& memory_;
    bool valid(uint64_t address,size_t size,MemoryAccess access) noexcept;
    bool finish(CPU& cpu,uint64_t tls,const Result& result) noexcept;
};
}
