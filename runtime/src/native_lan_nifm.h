#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace nx::native_lan {

// Local capability provider, not a Nintendo SDK socket client or online service.
// No hooks, environment-variable activation, account state, or network traffic.
// IPv4 integers use canonical order: first octet in the high byte.
enum class NifmStatus {
    Ok, Disabled, NotInitialized, EnumerationFailed, NoInterface, Ambiguous,
    InvalidAddress, InvalidMask, InvalidArgument, UnsupportedMetadata,
    InternetOffline, NoRequest, Cancelled,
};
struct HostInterface {
    std::string id;
    uint32_t address = 0, mask = 0;
    bool up = false, loopback = false, point_to_point = false;
    // Known-and-absent is represented by0/empty. Unknown never becomes fake0
    // in a successful full-config result. DNS addresses are metadata only.
    bool gateway_known = false, dns_known = false;
    uint32_t gateway = 0;
    std::vector<uint32_t> dns_servers;
};
struct InterfaceInventory {
    NifmStatus status = NifmStatus::Ok;
    std::vector<HostInterface> interfaces;
};
struct InterfaceSelection {
    NifmStatus status = NifmStatus::NoInterface;
    std::optional<HostInterface> selected;
    // Sorted eligible candidates: ambiguity is reported, never hidden by
    // choosing the first interface returned by the OS.
    std::vector<HostInterface> candidates;
    size_t invalid_masks = 0;
};
bool valid_ipv4_mask(uint32_t mask) noexcept;
InterfaceInventory enumerate_host_interfaces();
InterfaceSelection select_interface(const InterfaceInventory& inventory,
    std::optional<uint32_t> address = {}, bool allow_loopback_fixture = false);

constexpr uint32_t ConfigAddress = 1, ConfigMask = 2, ConfigGateway = 4,
    ConfigDnsPrimary = 8, ConfigDnsSecondary = 16, ConfigAll = 31;
struct NifmConfig {
    uint32_t address = 0, mask = 0, gateway = 0, dns_primary = 0, dns_secondary = 0;
    uint32_t known_fields = 0;
};
struct NifmConfigResult { NifmStatus status; NifmConfig config{}; };
struct NifmOptions {
    bool enabled = false;
    std::optional<uint32_t> address;
    // Injection/testing only; production selection excludes127/8 and loopback.
    bool allow_loopback_fixture = false;
};
struct NifmSnapshot {
    bool initialized = false, local_mode = false, requested = false, available = false;
    InterfaceSelection selection;
};
class Nifm {
public:
    using Enumerator = std::function<InterfaceInventory()>;
    explicit Nifm(NifmOptions options = {}, Enumerator enumerate = enumerate_host_interfaces);
    // Establishes our provider context even if no interface exists; selection
    // status remains visible. Never sets any Nintendo SDK client/init flags.
    NifmStatus initialize();
    NifmStatus set_local_mode(bool enabled);
    NifmStatus submit_request();
    NifmStatus cancel_request();
    // Re-enumerate on explicit request/status boundaries; snapshot/config are
    // cached and never perform OS calls or hold a lock during enumeration.
    // A cancel or mode transition invalidates any in-flight refresh result.
    NifmStatus refresh();
    // Cached packet-path gate: no enumeration, selection copy or allocation.
    bool available() const noexcept;
    NifmSnapshot snapshot() const;
    NifmConfigResult config(uint32_t required_fields = ConfigAll) const;
private:
    NifmOptions options_;
    Enumerator enumerate_;
    mutable std::mutex mutex_;
    NifmSnapshot state_;
    uint64_t epoch_ = 0;
    InterfaceSelection enumerate_selection() const;
    NifmStatus refresh_at(uint64_t epoch);
    void update_available();
};
} // namespace nx::native_lan
