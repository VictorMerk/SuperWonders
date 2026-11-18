#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace nx::probe {
// Best-effort diagnostic reads must not page the game's cold heap into RAM.
// Read only resident runs; zero holes and failed reads so old buffer contents
// cannot be mistaken for an object. A residency race may still fault a page.
template<class Read> size_t read_resident(uint64_t address, void* output, size_t bytes, Read read) {
    std::memset(output,0,bytes);
#ifdef _WIN32
    constexpr size_t page=4096;
#else
    const size_t page=size_t(sysconf(_SC_PAGESIZE));
#endif
    if (!page || address%page || bytes%page || bytes>1024*1024) return 0;
    const size_t count=bytes/page;
    if (!count) return 0;
#ifdef _WIN32
    std::vector<PSAPI_WORKING_SET_EX_INFORMATION> pages(count);
    for(size_t i=0;i<count;++i) pages[i].VirtualAddress=reinterpret_cast<void*>(address+i*page);
    if (!QueryWorkingSetEx(GetCurrentProcess(),pages.data(),DWORD(pages.size()*sizeof(pages[0])))) return 0;
    auto resident=[&](size_t i) { return pages[i].VirtualAttributes.Valid!=0; };
#else
    std::vector<unsigned char> pages(count);
    if (mincore(reinterpret_cast<void*>(address),bytes,pages.data())) return 0;
    auto resident=[&](size_t i) { return (pages[i]&1)!=0; };
#endif
    size_t copied=0;
    for(size_t i=0;i<count;) {
        if (!resident(i)) { ++i; continue; }
        const auto first=i++;
        while(i<count && resident(i)) ++i;
        auto* target=static_cast<unsigned char*>(output)+first*page;
        const size_t size=(i-first)*page;
        if (read(address+first*page,target,size)) copied+=size;
        else std::memset(target,0,size);
    }
    return copied;
}
}
