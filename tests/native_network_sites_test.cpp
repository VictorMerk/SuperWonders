#include "native_network_trace.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace nx;
using namespace nx::native_net;
namespace {
Event last;
bool fail=false;
unsigned calls=0;
uint64_t requested_address=0;
uint32_t observed_mode=3;
unsigned read_calls=0;
bool readable=true;
bool read_scalar(uint64_t address,uint32_t& value) noexcept {
    ++read_calls; requested_address=address;
    value=observed_mode; return readable;
}
void check(bool value,const char* message) { if(!value) {std::cerr<<message<<'\n';std::exit(1);} }
void original(CPU& cpu) {
    ++calls; cpu.x[0]=0xffff123456789000ull; cpu.x[12]=321;
    if(fail)throw std::runtime_error("original main exception");
}
void sink(const Event& event) {last=event;}
}
int main() {
    std::array<uint8_t,128> image{};
    for(size_t i=0;i<image.size();++i)image[i]=uint8_t(i*3);
    std::array<Fn,40> table{};table[1]=table[10]=original;
    ModuleInfo main{};main.base=0x10000;main.text_size=image.size();main.build_id="fixture-build";
    std::array<GuardedSite,2> sites{{
        {4,prefix_hash(std::span(image).subspan(4,32)),"fixture.factory",false},
        {40,prefix_hash(std::span(image).subspan(40,32)),"fixture.prepare",true}}};
    {
        Census census;
        check(!census.install_main(false,main,table.data(),image,"fixture-build",sites,sink),"disabled sites do not install");
        check(!census.install_main(true,main,table.data(),image,"other-build",sites,sink),"wrong build rejected");
        image[45]^=1;
        check(!census.install_main(true,main,table.data(),image,"fixture-build",sites,sink),"one wrong prefix rejects the whole set");
        check(table[1]==original&&table[10]==original,"failure cannot partially publish wrappers");
        image[45]^=1;
        auto invalid=sites;invalid[1].offset=124;
        check(!census.install_main(true,main,table.data(),image,"fixture-build",invalid,sink),"truncated code span rejected");
        invalid=sites;invalid[1]=invalid[0];
        check(!census.install_main(true,main,table.data(),image,"fixture-build",invalid,sink),"duplicate entry rejected");
        check(census.install_main(true,main,table.data(),image,"fixture-build",sites,sink)==2,"valid complete site set installs");
        CPU cpu{};cpu.x[1]=3;cpu.x[30]=main.base+80;
        table[10](cpu);
        check(cpu.x[0]==0xffff123456789000ull&&cpu.x[12]==321&&calls==1,"main handler CPU effects preserved");
        check(last.fields[0]==3&&last.caller_offset==80&&!last.has_result,"mode observed, opaque return never logged");
        cpu.x[1]=0x123456789abcdef0ull;table[10](cpu);
        check(last.fields[0]==-1,"unexpected pointer-sized mode redacted");
        table[1](cpu);check(std::string(last.operation)=="fixture.factory"&&last.fields[0]==-1,"factory requires no guest object read");
        fail=true;
        try {table[10](cpu);check(false,"exception expected");}
        catch(const std::runtime_error& error) {check(std::string(error.what())=="original main exception","original exception preserved");}
        check(last.exception&&!last.has_result,"exception evidence has no fabricated result");
        check(census.snapshot()[1].calls==3&&census.snapshot()[1].exceptions==1,"main site counters retained");
    }
    check(table[1]==original&&table[10]==original,"quiescent destruction restores main entries");
    {
        table[18]=table[26]=original;
        std::array<GuardedSite,4> selected{{
            sites[0],sites[1],
            {72,prefix_hash(std::span(image).subspan(72,32)),"fixture.activation-entry",false,ModeSource::StateX0},
            {104,0,"fixture.selector-entry",false,ModeSource::ControlX1}}};
        // An independently guarded second image gives every entry a full prefix.
        std::array<uint8_t,160> selected_image{};
        std::copy(image.begin(),image.end(),selected_image.begin());
        main.text_size=selected_image.size();
        selected[3].prefix_hash=prefix_hash(std::span(selected_image).subspan(104,32));
        Census census;
        check(!census.install_main(true,main,table.data(),selected_image,"fixture-build",selected,sink),
            "memory observation without safe reader fails closed");
        auto invalid=selected;invalid[2].mode_from_x1=true;
        check(!census.install_main(true,main,table.data(),selected_image,"fixture-build",invalid,sink,read_scalar),
            "ambiguous register/object scalar source rejected");
        invalid=selected;invalid[2].mode_source=static_cast<ModeSource>(99);
        check(!census.install_main(true,main,table.data(),selected_image,"fixture-build",invalid,sink,read_scalar),
            "unknown scalar source rejected");
        selected_image[111]^=1;
        check(!census.install_main(true,main,table.data(),selected_image,"fixture-build",selected,sink,read_scalar),
            "bad selection prefix prevents every probe");
        check(table[1]==original&&table[10]==original&&table[18]==original&&table[26]==original,
            "selection guard failure preserves complete default table");
        selected_image[111]^=1;
        check(census.install_main(true,main,table.data(),selected_image,"fixture-build",selected,sink,read_scalar)==4,
            "allocation and selection observations coexist");
        fail=false;CPU cpu{};cpu.x[0]=0x20000;cpu.x[1]=0x30000;
        table[18](cpu);
        check(requested_address==0x2000c&&last.fields[0]==3&&last.fields[1]==1&&!last.has_result,
            "activation records bounded entry scalar without opaque return");
        check(cpu.x[0]==0xffff123456789000ull&&cpu.x[12]==321,"activation original CPU effects preserved");
        observed_mode=2;table[26](cpu);
        check(requested_address==0x3003c&&last.fields[0]==2&&last.fields[1]==1,
            "selector reads its own control object offset");
        observed_mode=0xffffffffu;cpu.x[0]=0x20000;table[18](cpu);
        check(last.fields[0]==-1&&last.fields[1]==1,"unexpected memory scalar redacted");
        readable=false;table[26](cpu);
        check(last.fields[0]==-1&&last.fields[1]==0&&!last.exception,
            "unavailable mapping does not fabricate a mode");
        const auto before=read_calls;cpu.x[0]=UINT64_MAX;table[18](cpu);
        check(read_calls==before&&last.fields[0]==-1&&last.fields[1]==0,
            "overflow rejected before memory callback");
        cpu.x[1]=0;table[26](cpu);
        check(read_calls==before&&last.fields[0]==-1,"null object not probed");
        fail=true;cpu.x[0]=0x20000;readable=true;
        try {table[18](cpu);check(false,"activation exception expected");}
        catch(const std::runtime_error&) {}
        check(last.exception&&!last.has_result&&last.fields[0]==-1,
            "original activation exception retained with redacted scalar");
        check(census.snapshot()[2].calls==4&&census.snapshot()[2].exceptions==1,
            "selection cumulative counters retain every call");
    }
    check(table[18]==original&&table[26]==original,"selection probes restore after quiescence");
    std::cout<<"Guarded native main probes: identity, all-or-none publication, forwarding, redaction and exception tests passed\n";
}
