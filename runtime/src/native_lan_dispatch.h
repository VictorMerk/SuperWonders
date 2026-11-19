#pragma once
#include "native_lan_backend.h"
#include "native_lan_guest.h"
#include "native_lan_profile.h"
#include <string_view>

namespace nx::native_lan {
bool parse_local_ipv4(std::string_view text,uint32_t& address) noexcept;
// The binding planner supplies the exact operation/return contract. The memory
// owner keeps its leases alive until return. false is an unrecoverable mapping
// failure; the caller must stop rather than retry an already performed call.
bool dispatch(Backend& backend,GuestMemory& memory,CPU& cpu,
              profile::Operation operation,profile::ReturnKind result);
}
