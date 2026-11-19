// Developer-only metadata preflight. No SDK/game code, sockets or packet I/O.
#include "native_lan_nifm.h"
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
using namespace nx::native_lan;
namespace {
const char* name(NifmStatus status) {
    switch(status) {
    case NifmStatus::Ok:return "ok";
    case NifmStatus::Disabled:return "disabled";
    case NifmStatus::NotInitialized:return "not_initialized";
    case NifmStatus::EnumerationFailed:return "enumeration_failed";
    case NifmStatus::NoInterface:return "no_interface";
    case NifmStatus::Ambiguous:return "ambiguous";
    case NifmStatus::InvalidAddress:return "invalid_address";
    case NifmStatus::InvalidMask:return "invalid_mask";
    case NifmStatus::InvalidArgument:return "invalid_argument";
    case NifmStatus::UnsupportedMetadata:return "unsupported_metadata";
    case NifmStatus::InternetOffline:return "internet_offline";
    case NifmStatus::NoRequest:return "no_request";
    case NifmStatus::Cancelled:return "cancelled";
    }
    return "unknown";
}
bool literal(std::string_view text,uint32_t& result) {
    uint32_t parsed=0;
    for(unsigned i=0;i<4;++i) {
        const auto dot=text.find('.');const auto part=text.substr(0,dot);
        if(part.empty() || part.size()>3 || (part.size()>1 && part.front()=='0'))return false;
        unsigned octet=0;const auto converted=std::from_chars(part.data(),part.data()+part.size(),octet);
        if(converted.ec!=std::errc{} || converted.ptr!=part.data()+part.size() || octet>255)return false;
        parsed=(parsed<<8)|octet;
        if(i<3) {if(dot==text.npos)return false;text.remove_prefix(dot+1);}
        else if(dot!=text.npos)return false;
    }
    result=parsed;return true;
}
std::string ipv4(uint32_t address) {
    return std::to_string(address>>24)+'.'+std::to_string((address>>16)&255)+'.'+
        std::to_string((address>>8)&255)+'.'+std::to_string(address&255);
}
struct Evidence {
    NifmStatus initialized=NifmStatus::NotInitialized;
    NifmSnapshot snapshot;
    NifmConfigResult config{NifmStatus::NotInitialized};
    InterfaceSelection eligible;
    std::optional<uint32_t> requested;
    uint64_t captured_ms=0;
};
Evidence inspect(const InterfaceInventory& inventory,std::optional<uint32_t> requested={}) {
    // Only the initial caller enumerates the OS. Provider's explicit request
    // boundaries consume the same captured inventory for a coherent preflight.
    Nifm provider({true,requested,false},[&] { return inventory; });
    Evidence result;result.eligible=select_interface(inventory);result.requested=requested;
    result.captured_ms=uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    result.initialized=provider.initialize();
    if(result.initialized==NifmStatus::Ok) {
        provider.set_local_mode(true);provider.submit_request();
        result.config=provider.config(ConfigAddress|ConfigMask);
    }
    result.snapshot=provider.snapshot();return result;
}
void interface_json(std::ostream& out,const HostInterface& chosen) {
    // Do not expose adapter IDs (Windows GUIDs), MAC addresses or account data.
    out << "{\"ipv4\":\"" << ipv4(chosen.address) << "\",\"mask\":\"" << ipv4(chosen.mask)
        << "\",\"up\":" << (chosen.up?"true":"false")
        << ",\"loopback\":" << (chosen.loopback?"true":"false")
        << ",\"point_to_point\":" << (chosen.point_to_point?"true":"false")
        << ",\"gateway_known\":" << (chosen.gateway_known?"true":"false") << ",\"gateway\":";
    if(chosen.gateway_known)out << '\"' << ipv4(chosen.gateway) << '\"';else out << "null";
    out << ",\"dns_known\":" << (chosen.dns_known?"true":"false") << '}';
}
void output(std::ostream& out,const Evidence& evidence) {
    const auto& selection=evidence.snapshot.selection;
    out << "{\"schema\":1,\"source\":\"host_os_metadata\",\"status\":\"" << name(selection.status)
        << "\",\"initialize_status\":\"" << name(evidence.initialized)
        << "\",\"captured_utc_unix_ms\":" << evidence.captured_ms
        << ",\"eligible_candidates\":" << evidence.eligible.candidates.size()
        << ",\"matching_candidates\":" << selection.candidates.size()
        << ",\"invalid_masks\":" << evidence.eligible.invalid_masks << ",\"requested_address\":";
    if(evidence.requested)out << '\"' << ipv4(*evidence.requested) << '\"';else out << "null";
    out << ",\"candidates\":[";
    for(size_t i=0;i<evidence.eligible.candidates.size();++i) {
        if(i)out << ',';
        interface_json(out,evidence.eligible.candidates[i]);
    }
    out << "],\"selected\":";
    if(selection.selected)interface_json(out,*selection.selected);else out << "null";
    out << ",\"config_status\":\"" << name(evidence.config.status)
        << "\",\"requested_config_fields\":[\"address\",\"mask\"],\"scope\":\"Host interface metadata and local-provider selection only; no sockets or packets; does not certify room discovery, multiplayer, latency or FPS\"}\n";
}

void check(bool ok,const char* why) { if(!ok) {std::cerr << "preflight self-test: " << why << '\n';std::exit(1);} }
HostInterface fixture(std::string id,uint32_t address) {
    HostInterface item;item.id=std::move(id);item.address=address;item.mask=0xffffff00;item.up=true;
    return item;
}
int self_test() {
    uint32_t parsed=0;
    check(literal("192.168.2.3",parsed) && parsed==0xc0a80203 && ipv4(parsed)=="192.168.2.3","strict literal and endian formatting");
    for(auto bad:{"01.2.3.4","1.2.3","1.2.3.4.5","1.2.3.256","1.2.3.-1","1.2.3.4 ",""})
        check(!literal(bad,parsed),"malformed address rejected");
    auto a=fixture("fixture-a",0xc0a80203),b=fixture("fixture-b",0xc0a80204);
    auto evidence=inspect({NifmStatus::Ok,{a}});
    check(evidence.snapshot.available && evidence.snapshot.selection.selected && evidence.config.status==NifmStatus::Ok &&
        evidence.config.config.known_fields==(ConfigAddress|ConfigMask),"actual provider supports address/mask without fabricated gateway/DNS");
    std::ostringstream text;output(text,evidence);
    check(text.str().find("\"gateway_known\":false,\"gateway\":null,\"dns_known\":false")!=std::string::npos,"unknown metadata flags remain explicit");
    a.gateway_known=true;a.dns_known=true;
    evidence=inspect({NifmStatus::Ok,{a,b}});
    check(evidence.snapshot.selection.status==NifmStatus::Ambiguous && !evidence.snapshot.available && !evidence.snapshot.selection.selected,"ambiguity never guesses an interface");
    std::ostringstream ambiguous;output(ambiguous,evidence);
    check(ambiguous.str().find("\"candidates\":[")!=std::string::npos &&
        ambiguous.str().find("192.168.2.3")!=std::string::npos && ambiguous.str().find("192.168.2.4")!=std::string::npos &&
        ambiguous.str().find("fixture-a")==std::string::npos && ambiguous.str().find("\"id\"")==std::string::npos,
        "ambiguous report lists useful private address choices without adapter identifiers");
    evidence=inspect({NifmStatus::Ok,{a,b}},a.address);
    check(evidence.eligible.candidates.size()==2 && evidence.snapshot.selection.candidates.size()==1,
        "explicit selection preserves full eligible candidate report");
    check(evidence.snapshot.available && evidence.config.config.address==a.address,"explicit address resolves eligible candidate");
    evidence=inspect({NifmStatus::EnumerationFailed,{a}});
    check(evidence.initialized==NifmStatus::EnumerationFailed && !evidence.snapshot.available,"OS enumeration failure cannot become readiness");
    evidence=inspect({NifmStatus::Ok,{}});
    check(evidence.snapshot.selection.status==NifmStatus::NoInterface && evidence.config.status==NifmStatus::NoInterface,"no-interface case remains offline");
    evidence=inspect({NifmStatus::Ok,{a,b}},0xc0a80299);
    check(evidence.snapshot.selection.status==NifmStatus::NoInterface && !evidence.snapshot.available && evidence.eligible.candidates.size()==2,
        "unassigned explicit address fails while retaining valid alternatives");
    evidence=inspect({NifmStatus::Ok,{a}},0x08080808);
    check(evidence.snapshot.selection.status==NifmStatus::InvalidAddress,"public override is rejected by actual selector");
    std::cout << "{\"schema\":1,\"source\":\"injected_self_test_only\",\"status\":\"passed\",\"scope\":\"No host enumeration, sockets or packets\"}\n";
    return 0;
}
}
int main(int argc,char** argv) {
#ifdef _WIN32
    if(!SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS)) {
        std::cout << "{\"schema\":1,\"status\":\"priority_setup_failed\"}\n";return 2;
    }
#endif
    std::optional<uint32_t> requested;
    bool test=false;
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg=="--self-test" && !test)test=true;
        else if(arg=="--local-address" && i+1<argc && !requested) {
            uint32_t address;
            if(!literal(argv[++i],address)) {std::cout << "{\"schema\":1,\"status\":\"invalid_address_literal\"}\n";return 2;}
            requested=address;
        } else if(arg=="--help") {
            std::cout << "native_lan_preflight [--local-address IPv4] | --self-test\nReads host interface metadata only; no packets, sockets, guest code or renderer.\n";return 0;
        } else {std::cout << "{\"schema\":1,\"status\":\"invalid_arguments\"}\n";return 2;}
    }
    if(test) {
        if(requested) {std::cout << "{\"schema\":1,\"status\":\"invalid_arguments\"}\n";return 2;}
        return self_test();
    }
    try {
        const auto evidence=inspect(enumerate_host_interfaces(),requested);
        output(std::cout,evidence);
        return evidence.snapshot.available && evidence.config.status==NifmStatus::Ok?0:2;
    } catch(...) {
        std::cout << "{\"schema\":1,\"source\":\"host_os_metadata\",\"status\":\"enumeration_failed\"}\n";return 2;
    }
}
