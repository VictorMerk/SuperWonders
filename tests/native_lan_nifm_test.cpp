#include "native_lan_nifm.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <stdexcept>
#include <thread>

using namespace nx::native_lan;
namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "native nifm: %s\n", message); std::exit(1); }
}
HostInterface interface(std::string id, uint32_t address) {
    HostInterface item;
    item.id = std::move(id); item.address = address; item.mask = 0xffffff00;
    item.up = true; item.gateway_known = item.dns_known = true;
    item.gateway = (address & 0xffffff00) | 1;
    item.dns_servers = {0xc0a80201, 0x08080808}; // Injected metadata; never queried.
    return item;
}
}
int main() {
    check(valid_ipv4_mask(0xffffff00) && valid_ipv4_mask(0xffffffff) && valid_ipv4_mask(0xfffffffe) &&
          !valid_ipv4_mask(0) && !valid_ipv4_mask(0xff00ff00), "contiguous mask validation, including /31 and /32");
    auto ethernet = interface("ethernet", 0xc0a8025b);
    InterfaceInventory inventory{NifmStatus::Ok, {ethernet}};
    auto selection = select_interface(inventory);
    check(selection.status == NifmStatus::Ok && selection.selected->address == ethernet.address, "select real eligible private interface");
    for (uint32_t address : {0x0a000005u, 0xac100005u, 0xac1f0105u, 0xa9fe0105u}) {
        auto item = interface("private", address);
        check(select_interface({NifmStatus::Ok, {item}}).status == NifmStatus::Ok, "supported private/link-local ranges");
    }
    for (uint32_t address : {0x08080808u, 0xac200005u, 0xefffffffu, 0u, 0xc0a80200u, 0xc0a802ffu}) {
        auto item = interface("invalid", address);
        check(select_interface({NifmStatus::Ok, {item}}).status != NifmStatus::Ok, "public, wildcard, multicast, subnet endpoint rejected");
    }
    auto down = ethernet; down.up = false;
    auto tunnel = ethernet; tunnel.point_to_point = true;
    auto loopback = interface("loopback", 0x7f000001); loopback.loopback = true; loopback.mask = 0xff000000;
    check(select_interface({NifmStatus::Ok, {down, tunnel, loopback}}).status == NifmStatus::NoInterface, "down/tunnel/loopback excluded");
    check(select_interface({NifmStatus::Ok, {loopback}}, {}, true).status == NifmStatus::Ok,
          "loopback accepted only through explicit test-fixture policy");
    check(select_interface(inventory, 0x7f000001).status == NifmStatus::InvalidAddress &&
          select_interface(inventory, 0x08080808).status == NifmStatus::InvalidAddress, "explicit public/loopback overrides rejected");
    auto broken = ethernet; broken.mask = 0xffff00ff;
    check(select_interface({NifmStatus::Ok, {broken}}).status == NifmStatus::InvalidMask, "invalid subnet metadata identified");
    auto wifi = interface("wifi", 0xc0a8025c);
    inventory.interfaces.push_back(wifi);
    selection = select_interface(inventory);
    check(selection.status == NifmStatus::Ambiguous && !selection.selected && selection.candidates.size() == 2 &&
          selection.candidates[0].id == "ethernet", "multiple interfaces reported deterministically, never guessed");
    std::reverse(inventory.interfaces.begin(), inventory.interfaces.end());
    check(select_interface(inventory).candidates[0].id == "ethernet", "OS enumeration order cannot change candidate order");
    check(select_interface(inventory, wifi.address).selected->id == "wifi", "explicit private address resolves ambiguity");
    check(select_interface(inventory, 0xc0a80299).status == NifmStatus::NoInterface, "explicit address must actually exist");
    check(select_interface({NifmStatus::Ok, {ethernet, ethernet}}).status == NifmStatus::Ok, "exact duplicate enumeration records do not invent ambiguity");
    auto conflicting = ethernet; conflicting.gateway = 0xc0a802fe;
    check(select_interface({NifmStatus::Ok, {ethernet, conflicting}}).status == NifmStatus::Ambiguous, "conflicting metadata is not silently discarded");
    check(select_interface({NifmStatus::EnumerationFailed, {ethernet}}).status == NifmStatus::EnumerationFailed, "enumerator failures cannot expose partial candidates");
    unsigned enumerations = 0;
    {
        Nifm disabled({}, [&] { ++enumerations; return inventory; });
        check(disabled.initialize() == NifmStatus::Disabled && disabled.submit_request() == NifmStatus::Disabled &&
              disabled.config().status == NifmStatus::Disabled && !disabled.available() && enumerations == 0, "default-off service never enumerates or advertises an interface");
    }
    inventory.interfaces = {ethernet};
    Nifm service({true, {}, false}, [&] { ++enumerations; return inventory; });
    check(service.submit_request() == NifmStatus::NotInitialized && service.set_local_mode(true) == NifmStatus::NotInitialized,
          "operations require initialized provider context");
    check(service.initialize() == NifmStatus::Ok && service.snapshot().initialized && !service.snapshot().available,
          "initialize precedes local mode and does not fabricate network availability");
    check(service.initialize() == NifmStatus::Ok && enumerations == 1, "idempotent initialize");
    check(service.submit_request() == NifmStatus::InternetOffline && service.config().status == NifmStatus::InternetOffline,
          "internet mode remains offline despite private physical interface");
    check(service.set_local_mode(true) == NifmStatus::Ok && service.config().status == NifmStatus::NoRequest && !service.snapshot().available,
          "local mode still requires explicit request");
    check(service.submit_request() == NifmStatus::Ok && service.snapshot().available && service.available(), "local request succeeds on actual selected fixture interface");
    auto config = service.config();
    check(config.status == NifmStatus::Ok && config.config.address == ethernet.address && config.config.mask == ethernet.mask &&
          config.config.gateway == ethernet.gateway && config.config.dns_secondary == 0x08080808 && config.config.known_fields == ConfigAll,
          "full config returns exact supplied real metadata, public DNS is metadata only");
    check(service.config(32).status == NifmStatus::InvalidArgument, "unknown config field masks rejected");
    inventory.interfaces[0].dns_known = false;
    check(service.refresh() == NifmStatus::Ok && service.snapshot().available && service.config().status == NifmStatus::UnsupportedMetadata &&
          service.config().config.known_fields == 0, "unknown DNS fails full config without fabricated zero fields");
    check(service.config(ConfigAddress | ConfigMask | ConfigGateway).status == NifmStatus::Ok,
          "supported partial config remains available without pretending DNS is known");
    inventory.interfaces[0].gateway_known = false;
    check(service.refresh() == NifmStatus::Ok && service.config(ConfigGateway).status == NifmStatus::UnsupportedMetadata,
          "unknown gateway is not invented");
    inventory.interfaces[0].gateway_known = inventory.interfaces[0].dns_known = true;
    inventory.interfaces[0].gateway = 0; inventory.interfaces[0].dns_servers.clear();
    check(service.refresh() == NifmStatus::Ok && service.config().status == NifmStatus::Ok &&
          service.config().config.gateway == 0 && service.config().config.dns_primary == 0,
          "known absence represented truthfully, distinct from unsupported metadata");
    inventory.interfaces[0].up = false;
    check(service.refresh() == NifmStatus::NoInterface && !service.snapshot().available && service.config().status == NifmStatus::NoInterface,
          "link loss invalidates availability on status refresh");
    inventory.interfaces[0] = ethernet;
    check(service.refresh() == NifmStatus::Ok && service.snapshot().available, "real interface recovery satisfies existing request");
    check(service.cancel_request() == NifmStatus::Ok && !service.snapshot().available && service.config().status == NifmStatus::NoRequest,
          "cancel clears request and availability");
    check(service.refresh() == NifmStatus::Ok && !service.snapshot().available, "refresh cannot resurrect cancelled request");
    check(service.submit_request() == NifmStatus::Ok && service.set_local_mode(false) == NifmStatus::Ok &&
          !service.snapshot().available && !service.snapshot().requested, "mode transition cancels local request");
    {
        Nifm empty({true, {}, false}, [] { return InterfaceInventory{}; });
        check(empty.initialize() == NifmStatus::Ok && empty.snapshot().initialized && empty.set_local_mode(true) == NifmStatus::Ok &&
              empty.submit_request() == NifmStatus::NoInterface && !empty.snapshot().available,
              "provider initializes honestly with no interface while network stays offline");
        Nifm bad({true, {}, false}, []() -> InterfaceInventory { throw std::runtime_error("OS enumeration failure"); });
        check(bad.initialize() == NifmStatus::EnumerationFailed && !bad.snapshot().initialized, "enumeration exception is not fabricated initialization success");
        Nifm ambiguous({true, {}, false}, [&] { return InterfaceInventory{NifmStatus::Ok, {ethernet, wifi}}; });
        check(ambiguous.initialize() == NifmStatus::Ok && ambiguous.set_local_mode(true) == NifmStatus::Ok &&
              ambiguous.submit_request() == NifmStatus::Ambiguous && !ambiguous.snapshot().available, "ambiguity remains offline throughout lifecycle");
    }
    {
        std::atomic<bool> block{false}; std::promise<void> entered, release;
        auto gate = release.get_future().share();
        Nifm concurrent({true, {}, false}, [&] {
            if (block.load()) { entered.set_value(); gate.wait(); }
            return InterfaceInventory{NifmStatus::Ok, {ethernet}};
        });
        check(concurrent.initialize() == NifmStatus::Ok && concurrent.set_local_mode(true) == NifmStatus::Ok, "concurrent fixture initialized");
        block = true; NifmStatus request_status = NifmStatus::Ok;
        std::thread worker([&] { request_status = concurrent.submit_request(); });
        entered.get_future().wait();
        check(concurrent.cancel_request() == NifmStatus::Ok && !concurrent.available() && !concurrent.snapshot().available,
              "cancel/snapshot never block behind host enumeration");
        release.set_value(); worker.join();
        check(request_status == NifmStatus::Cancelled && !concurrent.snapshot().available && !concurrent.snapshot().requested,
              "late enumeration cannot resurrect cancelled request");
    }
    std::puts("native nifm injected capability and lifecycle CPU tests passed");
}
