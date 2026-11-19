#include "native_lan_memory.h"
#include <cstring>

namespace nx::native_lan {
bool RuntimeGuestMemory::supported() noexcept {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}
RuntimeGuestMemory::RuntimeGuestMemory() { if(supported())lease_=detail::create_vm_lease(); }
RuntimeGuestMemory::~RuntimeGuestMemory() { if(lease_)detail::release_vm_lease(lease_); }
bool RuntimeGuestMemory::validate(uint64_t address,size_t bytes,MemoryAccess access) noexcept {
    return lease_ && detail::pin_vm_span(lease_,address,bytes,access);
}
bool RuntimeGuestMemory::read(uint64_t address,std::span<std::byte> bytes) noexcept {
    if(!lease_ || !detail::owns_vm_pin(lease_,address,bytes.size(),MemoryAccess::Read))return false;
    // This is the first guest-pointer conversion: the runtime map, host commit
    // ledger and a live lease already prove ownership/accessibility. All runtime
    // Windows decommit/remap/permission changes wait until this lease ends.
    if(!bytes.empty())std::memcpy(bytes.data(),reinterpret_cast<const void*>(address),bytes.size());
    return true;
}
bool RuntimeGuestMemory::write(uint64_t address,std::span<const std::byte> bytes) noexcept {
    if(!lease_ || !detail::owns_vm_pin(lease_,address,bytes.size(),MemoryAccess::Write))return false;
    if(!bytes.empty())std::memcpy(reinterpret_cast<void*>(address),bytes.data(),bytes.size());
    return true;
}
}
