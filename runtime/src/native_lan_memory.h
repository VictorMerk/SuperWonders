#pragma once
#include "native_lan_guest.h"

namespace nx::native_lan {
// Construct only after runtime memory_init (tests provide equivalent private
// reserved arenas). All host accessibility changes must use the runtime VM gate;
// an external/direct VirtualProtect/VirtualFree bypass is outside this contract.
// One instance per GuestAdapter::invoke call. Every successful validate pins the
// covered mapping/permissions until destruction; unrelated VM operations remain
// available while Poll waits. Caller must not share this instance concurrently.
// Windows only initially: POSIX write-watch mprotect is not coordinated with the
// VM mutation gate, so this mapper deliberately fails closed on other systems.
class RuntimeGuestMemory final:public GuestMemory {
public:
    RuntimeGuestMemory();
    ~RuntimeGuestMemory() override;
    RuntimeGuestMemory(const RuntimeGuestMemory&)=delete;
    RuntimeGuestMemory& operator=(const RuntimeGuestMemory&)=delete;
    static bool supported() noexcept;
    bool validate(uint64_t address,size_t bytes,MemoryAccess access) noexcept override;
    bool read(uint64_t address,std::span<std::byte> bytes) noexcept override;
    bool write(uint64_t address,std::span<const std::byte> bytes) noexcept override;
private:
    uint64_t lease_=0;
};
// Runtime VM coordination, implemented beside its map in memory.cpp. These
// functions never manufacture guest mappings; only committed, permitted runtime
// ranges can be pinned. Keeping them here avoids expanding the guest VM ABI.
namespace detail {
uint64_t create_vm_lease();
void release_vm_lease(uint64_t lease) noexcept;
bool pin_vm_span(uint64_t lease,uint64_t address,size_t bytes,MemoryAccess access) noexcept;
bool owns_vm_pin(uint64_t lease,uint64_t address,size_t bytes,MemoryAccess access) noexcept;
}
}
