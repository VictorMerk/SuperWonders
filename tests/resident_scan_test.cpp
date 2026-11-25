#include "resident_scan.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static void check(bool ok,const char* text) { if(!ok) { std::fprintf(stderr,"%s\n",text); std::exit(1); } }
int main() {
#ifdef _WIN32
    constexpr size_t page=4096;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*3,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
#else
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
    auto* memory=static_cast<unsigned char*>(mmap(nullptr,page*3,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if(memory==MAP_FAILED) memory=nullptr;
#endif
    check(memory!=nullptr,"allocate a cold committed range");
    memory[0]=41; memory[page*2]=99;
    std::vector<unsigned char> output(page*3,255);
    size_t reads=0;
    auto read=[&](uint64_t at,void* out,size_t bytes) {
        ++reads; std::memcpy(out,reinterpret_cast<void*>(at),bytes); return true;
    };
    const auto copied=nx::probe::read_resident(reinterpret_cast<uint64_t>(memory),output.data(),output.size(),read);
    check(copied>=page*2 && output[0]==41 && output[page*2]==99,"resident pages remain discoverable across cold holes");
    check(output[page]==0,"cold memory never leaves stale scanner-buffer contents");
#ifdef _WIN32
    PSAPI_WORKING_SET_EX_INFORMATION middle{}; middle.VirtualAddress=memory+page;
    check(QueryWorkingSetEx(GetCurrentProcess(),&middle,sizeof middle) && !middle.VirtualAttributes.Valid,
          "diagnostic scanning did not page in the untouched middle page");
#else
    unsigned char middle=0;
    check(mincore(memory+page,page,&middle)==0 && !(middle&1),"diagnostic scanning did not page in the untouched middle page");
#endif
    std::fill(output.begin(),output.end(),255);
    nx::probe::read_resident(reinterpret_cast<uint64_t>(memory),output.data(),output.size(),[](uint64_t,void* out,size_t bytes) {
        std::memset(out,123,bytes); return false;
    });
    check(std::all_of(output.begin(),output.end(),[](auto b) { return b==0; }),"partial failed reads cannot create phantom objects");
    const auto before=reads;
    check(nx::probe::read_resident(reinterpret_cast<uint64_t>(memory)+1,output.data(),page,read)==0 && reads==before,
          "unaligned scan cannot read outside resident pages");
#ifdef _WIN32
    VirtualFree(memory,0,MEM_RELEASE);
#else
    munmap(memory,page*3);
#endif
    std::puts("resident probe: cold pages untouched, resident objects visible, failed reads cleared");
}
