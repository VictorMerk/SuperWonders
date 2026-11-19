// Independently implemented host-interface/provider layer. No MKW code copied.
#include "native_lan_nifm.h"
#include <algorithm>
#include <limits>
#include <tuple>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <fstream>
#include <sstream>
#include <map>
#endif

namespace nx::native_lan {
namespace {
constexpr size_t kMaxInterfaces = 256;
bool private_address(uint32_t address) {
    return (address & 0xff000000u) == 0x0a000000u ||
        (address & 0xfff00000u) == 0xac100000u ||
        (address & 0xffff0000u) == 0xc0a80000u ||
        (address & 0xffff0000u) == 0xa9fe0000u;
}
bool loopback_address(uint32_t address) { return (address & 0xff000000u) == 0x7f000000u; }
bool usable_address(uint32_t address, uint32_t mask) {
    // /31 point-to-point subnet arithmetic and /32 host masks are valid masks;
    // point-to-point interfaces themselves are excluded separately.
    const uint32_t host = ~mask;
    return host < 3 || ((address & host) != 0 && (address & host) != host);
}
InterfaceInventory enumeration_failure() { return {NifmStatus::EnumerationFailed, {}}; }
#ifndef _WIN32
struct DefaultRoute { uint32_t gateway = 0; uint64_t metric = 0; bool ambiguous = false; };
bool default_routes(std::map<std::string, DefaultRoute>& routes) {
    // Linux kernel routing metadata only, no socket or DNS request. Missing or
    // malformed metadata stays unknown rather than becoming an invented route.
    std::ifstream file("/proc/net/route");
    if (!file) return false;
    std::string line;
    if (!std::getline(file, line)) return false;
    size_t count = 0;
    while (std::getline(file, line)) {
        if (++count > 4096 || line.size() > 4096) return false;
        std::istringstream stream(line);
        std::string name; uint64_t destination, gateway, flags, refs, uses, metric, mask;
        if (!(stream >> name >> std::hex >> destination >> gateway >> flags >> std::dec >> refs >> uses >> metric >> std::hex >> mask)) return false;
        if (destination || mask || !(flags & 1)) continue;
        if (gateway > std::numeric_limits<uint32_t>::max()) return false;
        const uint32_t address = ntohl(uint32_t(gateway));
        auto found = routes.find(name);
        if (found == routes.end() || metric < found->second.metric) routes[name] = {address, metric, false};
        else if (metric == found->second.metric && address != found->second.gateway) found->second.ambiguous = true;
    }
    return !file.bad();
}
#endif
}
bool valid_ipv4_mask(uint32_t mask) noexcept {
    const uint32_t inverse = ~mask;
    return mask != 0 && (inverse & (inverse + 1)) == 0;
}
InterfaceSelection select_interface(const InterfaceInventory& inventory,
    std::optional<uint32_t> requested, bool fixture) {
    InterfaceSelection result;
    if (inventory.status != NifmStatus::Ok || inventory.interfaces.size() > kMaxInterfaces) {
        result.status = NifmStatus::EnumerationFailed; return result;
    }
    if (requested && !private_address(*requested) && !(fixture && loopback_address(*requested))) {
        result.status = NifmStatus::InvalidAddress; return result;
    }
    for (const auto& item : inventory.interfaces) {
        if (!item.up || item.point_to_point || (requested && item.address != *requested)) continue;
        if (item.loopback || loopback_address(item.address)) {
            if (!fixture || !loopback_address(item.address)) continue;
        } else if (!private_address(item.address)) continue;
        if (!valid_ipv4_mask(item.mask) || !usable_address(item.address, item.mask)) {
            ++result.invalid_masks; continue;
        }
        result.candidates.push_back(item);
    }
    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) {
        return std::tie(a.id, a.address, a.mask, a.gateway_known, a.gateway, a.dns_known, a.dns_servers) <
            std::tie(b.id, b.address, b.mask, b.gateway_known, b.gateway, b.dns_known, b.dns_servers);
    });
    // Duplicate records from one enumerator don't create artificial ambiguity.
    result.candidates.erase(std::unique(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) {
        return std::tie(a.id, a.address, a.mask, a.gateway_known, a.gateway, a.dns_known, a.dns_servers) ==
            std::tie(b.id, b.address, b.mask, b.gateway_known, b.gateway, b.dns_known, b.dns_servers);
    }), result.candidates.end());
    if (result.candidates.size() == 1) {
        result.selected = result.candidates.front(); result.status = NifmStatus::Ok;
    } else if (result.candidates.size() > 1) result.status = NifmStatus::Ambiguous;
    else if (result.invalid_masks) result.status = NifmStatus::InvalidMask;
    return result;
}
InterfaceInventory enumerate_host_interfaces() {
#ifdef _WIN32
    ULONG size = 16 * 1024;
    std::vector<unsigned char> storage(size);
    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        status = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST |
            GAA_FLAG_SKIP_MULTICAST, nullptr, adapters, &size);
        if (status != ERROR_BUFFER_OVERFLOW) break;
        if (!size || size > 16 * 1024 * 1024) return enumeration_failure();
        storage.resize(size);
    }
    if (status == ERROR_NO_DATA) return {};
    if (status != NO_ERROR) return enumeration_failure();
    InterfaceInventory inventory;
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); adapter; adapter = adapter->Next) {
        std::vector<uint32_t> gateways, dns;
        for (auto* gateway = adapter->FirstGatewayAddress; gateway; gateway = gateway->Next)
            if (gateway->Address.lpSockaddr && gateway->Address.iSockaddrLength >= int(sizeof(sockaddr_in)) && gateway->Address.lpSockaddr->sa_family == AF_INET)
                gateways.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(gateway->Address.lpSockaddr)->sin_addr.s_addr));
        std::sort(gateways.begin(), gateways.end());
        gateways.erase(std::unique(gateways.begin(), gateways.end()), gateways.end());
        for (auto* server = adapter->FirstDnsServerAddress; server && dns.size() < 2; server = server->Next)
            if (server->Address.lpSockaddr && server->Address.iSockaddrLength >= int(sizeof(sockaddr_in)) && server->Address.lpSockaddr->sa_family == AF_INET)
                dns.push_back(ntohl(reinterpret_cast<const sockaddr_in*>(server->Address.lpSockaddr)->sin_addr.s_addr));
        for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.iSockaddrLength < int(sizeof(sockaddr_in)) || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
            if (inventory.interfaces.size() == kMaxInterfaces) return enumeration_failure();
            HostInterface item;
            item.id = adapter->AdapterName ? adapter->AdapterName : "";
            item.address = ntohl(reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr.s_addr);
            const unsigned prefix = unicast->OnLinkPrefixLength;
            item.mask = prefix > 32 || prefix == 0 ? 0 : uint32_t(0xffffffffu << (32 - prefix));
            item.up = adapter->OperStatus == IfOperStatusUp &&
                (unicast->DadState == IpDadStatePreferred || unicast->DadState == IpDadStateDeprecated);
            item.loopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
            item.point_to_point = adapter->IfType == IF_TYPE_PPP || adapter->IfType == IF_TYPE_TUNNEL;
            item.gateway_known = gateways.size() <= 1;
            if (item.gateway_known && !gateways.empty()) item.gateway = gateways.front();
            item.dns_known = true; item.dns_servers = dns;
            inventory.interfaces.push_back(std::move(item));
        }
    }
    return inventory;
#else
    ifaddrs* addresses = nullptr;
    if (getifaddrs(&addresses)) return enumeration_failure();
    struct Cleanup { ifaddrs* value; ~Cleanup() { freeifaddrs(value); } } cleanup{addresses};
    std::map<std::string, DefaultRoute> routes;
    const bool routes_known = default_routes(routes);
    InterfaceInventory inventory;
    for (auto* entry = addresses; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) continue;
        if (inventory.interfaces.size() == kMaxInterfaces) return enumeration_failure();
        HostInterface item;
        item.id = entry->ifa_name ? entry->ifa_name : "";
        item.address = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
        if (entry->ifa_netmask && entry->ifa_netmask->sa_family == AF_INET)
            item.mask = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask)->sin_addr.s_addr);
        item.up = (entry->ifa_flags & IFF_UP) && (entry->ifa_flags & IFF_RUNNING);
        item.loopback = entry->ifa_flags & IFF_LOOPBACK;
        item.point_to_point = entry->ifa_flags & IFF_POINTOPOINT;
        // Absence from the main table does not prove absence from policy-route
        // tables; only report a gateway actually found for this interface.
        item.gateway_known = false;
        if (auto found = routes.find(item.id); found != routes.end()) {
            item.gateway = found->second.gateway;
            item.gateway_known = routes_known && !found->second.ambiguous;
        }
        // resolv.conf is not authoritative per-interface DNS. Do not report a
        // global stub resolver or an unrelated interface's DNS as this one.
        item.dns_known = false;
        inventory.interfaces.push_back(std::move(item));
    }
    return inventory;
#endif
}
Nifm::Nifm(NifmOptions options, Enumerator enumerate) : options_(options), enumerate_(std::move(enumerate)) {}
InterfaceSelection Nifm::enumerate_selection() const {
    try {
        return enumerate_ ? select_interface(enumerate_(), options_.address, options_.allow_loopback_fixture)
                          : select_interface(enumeration_failure());
    } catch (...) {
        InterfaceSelection result; result.status = NifmStatus::EnumerationFailed; return result;
    }
}
void Nifm::update_available() {
    state_.available = state_.initialized && state_.local_mode && state_.requested &&
        state_.selection.status == NifmStatus::Ok && state_.selection.selected.has_value();
}
NifmStatus Nifm::initialize() {
    if (!options_.enabled) return NifmStatus::Disabled;
    uint64_t epoch;
    {
        std::lock_guard lock(mutex_);
        if (state_.initialized) return NifmStatus::Ok;
        epoch = ++epoch_;
    }
    auto selection = enumerate_selection();
    std::lock_guard lock(mutex_);
    if (epoch != epoch_) return NifmStatus::Cancelled;
    state_.selection = std::move(selection);
    if (state_.selection.status == NifmStatus::EnumerationFailed) return state_.selection.status;
    state_.initialized = true; update_available();
    return NifmStatus::Ok;
}
NifmStatus Nifm::set_local_mode(bool enabled) {
    if (!options_.enabled) return NifmStatus::Disabled;
    std::lock_guard lock(mutex_);
    if (!state_.initialized) return NifmStatus::NotInitialized;
    if (state_.local_mode != enabled) { state_.local_mode = enabled; state_.requested = false; ++epoch_; }
    update_available(); return NifmStatus::Ok;
}
NifmStatus Nifm::submit_request() {
    if (!options_.enabled) return NifmStatus::Disabled;
    uint64_t epoch;
    {
        std::lock_guard lock(mutex_);
        if (!state_.initialized) return NifmStatus::NotInitialized;
        if (!state_.local_mode) return NifmStatus::InternetOffline;
        state_.requested = true; state_.available = false; epoch = ++epoch_;
    }
    return refresh_at(epoch);
}
NifmStatus Nifm::cancel_request() {
    if (!options_.enabled) return NifmStatus::Disabled;
    std::lock_guard lock(mutex_);
    if (!state_.initialized) return NifmStatus::NotInitialized;
    state_.requested = false; ++epoch_; update_available(); return NifmStatus::Ok;
}
NifmStatus Nifm::refresh() {
    if (!options_.enabled) return NifmStatus::Disabled;
    uint64_t epoch;
    {
        std::lock_guard lock(mutex_);
        if (!state_.initialized) return NifmStatus::NotInitialized;
        epoch = ++epoch_;
    }
    return refresh_at(epoch);
}
NifmStatus Nifm::refresh_at(uint64_t epoch) {
    auto selection = enumerate_selection();
    std::lock_guard lock(mutex_);
    if (epoch != epoch_) return NifmStatus::Cancelled;
    state_.selection = std::move(selection); update_available(); return state_.selection.status;
}
bool Nifm::available() const noexcept {
    std::lock_guard lock(mutex_); return state_.available;
}
NifmSnapshot Nifm::snapshot() const {
    std::lock_guard lock(mutex_); return state_;
}
NifmConfigResult Nifm::config(uint32_t required) const {
    if (!options_.enabled) return {NifmStatus::Disabled};
    if (required & ~ConfigAll) return {NifmStatus::InvalidArgument};
    std::lock_guard lock(mutex_);
    if (!state_.initialized) return {NifmStatus::NotInitialized};
    if (!state_.local_mode) return {NifmStatus::InternetOffline};
    if (!state_.requested) return {NifmStatus::NoRequest};
    if (!state_.available) return {state_.selection.status};
    const auto& selected = *state_.selection.selected;
    NifmConfig config;
    config.address = selected.address; config.mask = selected.mask;
    config.known_fields = ConfigAddress | ConfigMask;
    if (selected.gateway_known) { config.gateway = selected.gateway; config.known_fields |= ConfigGateway; }
    if (selected.dns_known) {
        config.known_fields |= ConfigDnsPrimary | ConfigDnsSecondary;
        if (!selected.dns_servers.empty()) config.dns_primary = selected.dns_servers[0];
        if (selected.dns_servers.size() > 1) config.dns_secondary = selected.dns_servers[1];
    }
    if ((config.known_fields & required) != required) return {NifmStatus::UnsupportedMetadata};
    return {NifmStatus::Ok, config};
}
} // namespace nx::native_lan
