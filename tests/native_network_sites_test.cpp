#include "native_network_trace.h"
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
    std::array<Fn,32> table{};table[1]=table[10]=original;
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
    std::cout<<"Guarded native main probes: identity, all-or-none publication, forwarding, redaction and exception tests passed\n";
}
