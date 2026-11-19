// Guest address space on the host: fixed reserved ranges, commit on demand, and a Horizon-style memory map
// for svcQueryMemory.
#include <map>
#include <algorithm>
#include <condition_variable>
#include <limits>
#include <unordered_map>
#include <vector>
#include <mutex>

#include <nx/runtime.h>

#include "platform.h"
#include "native_lan_memory.h"

namespace nx {

namespace {

std::mutex g_vm_lock;
std::map<uint64_t, VmInfo> g_vm;  // start -> block (non-overlapping; gaps are free)
uint64_t g_kernel_next = kKernelBase;
#ifdef _WIN32
struct Pin { uint64_t begin,end; uint32_t perm; };
std::condition_variable g_vm_changed;
std::unordered_map<uint64_t,std::vector<Pin>> g_vm_leases;
std::map<uint64_t,Pin> g_vm_mutations;
uint64_t g_next_lease=0,g_next_mutation=0;
// Actual successful host commits, independently of QueryMemory's publication
// order. kernel_alloc's trusted private ownership avoids changing QueryMemory.
std::map<uint64_t,uint64_t> g_committed,g_kernel_owned;
bool overlap(uint64_t a,uint64_t b,uint64_t c,uint64_t d) { return a<d && c<b; }
bool extent(uint64_t a,uint64_t n,uint64_t& end) {
    if(!a || !n || n>std::numeric_limits<uint64_t>::max()-a)return false;
    end=a+n;return true;
}
bool pages(uint64_t a,uint64_t n,uint64_t& begin,uint64_t& end) {
    if(!extent(a,n,end) || end>std::numeric_limits<uint64_t>::max()-0xfff)return false;
    begin=a&~uint64_t(0xfff);end=(end+0xfff)&~uint64_t(0xfff);return true;
}
void add_span(std::map<uint64_t,uint64_t>& spans,uint64_t begin,uint64_t end) {
    auto it=spans.lower_bound(begin);
    if(it!=spans.begin() && std::prev(it)->second>=begin)it=std::prev(it);
    while(it!=spans.end() && it->first<=end) {
        begin=std::min(begin,it->first);end=std::max(end,it->second);it=spans.erase(it);
    }
    spans[begin]=end;
}
void erase_span(std::map<uint64_t,uint64_t>& spans,uint64_t begin,uint64_t end) {
    auto it=spans.lower_bound(begin);
    if(it!=spans.begin() && std::prev(it)->second>begin)it=std::prev(it);
    while(it!=spans.end() && it->first<end) {
        const auto a=it->first,b=it->second;it=spans.erase(it);
        if(a<begin)spans[a]=begin;
        if(b>end) { spans[end]=b;break; }
    }
}
uint64_t covered_end(const std::map<uint64_t,uint64_t>& spans,uint64_t address) {
    auto it=spans.upper_bound(address);
    return it!=spans.begin() && std::prev(it)->second>address?std::prev(it)->second:address;
}
bool guest_arena(uint64_t a,uint64_t end) {
    const auto inside=[&](uint64_t base,uint64_t size) {return a>=base && end<=base+size;};
    return inside(kModuleBase,0x20000000) || inside(kHeapBase,kHeapMax) ||
        inside(kAliasBase,kAliasSize) || inside(kStackBase,kStackSize) || inside(kKernelBase,kKernelSize);
}
bool pinned(const std::vector<Pin>& pins,uint64_t begin,uint64_t end,uint32_t perm) {
    while(begin<end) {
        uint64_t next=begin;
        for(const auto& pin:pins)if(pin.begin<=begin && pin.end>begin && (pin.perm&perm)==perm)next=std::max(next,pin.end);
        if(next==begin)return false;
        begin=next;
    }
    return true;
}
// Pending mutations reject new conflicting pins, avoiding starvation while an
// existing lease finishes. CV wait releases g_vm_lock; unrelated operations and
// QueryMemory proceed during an adapter's bounded Poll.
void wait_unpinned(std::unique_lock<std::mutex>& lock,uint64_t begin,uint64_t end) {
    if(g_vm_leases.empty())return; // no diagnostic bridge: no pending-node allocation
    const auto ticket=++g_next_mutation;
    g_vm_mutations[ticket]={begin,end,0};
    g_vm_changed.wait(lock,[&] {
        for(const auto& [owner,pins]:g_vm_leases)
            for(const auto& pin:pins)if(overlap(begin,end,pin.begin,pin.end))return false;
        return true;
    });
    g_vm_mutations.erase(ticket);
}
bool commit_pages(uint64_t addr,uint64_t size,bool kernel_owned) {
    uint64_t begin,end;
    if(!pages(addr,size,begin,end))return false;
    std::unique_lock lock(g_vm_lock);
    wait_unpinned(lock,begin,end);
    if(!plat::commit(addr,size))return false;
    add_span(g_committed,begin,end);
    if(kernel_owned)add_span(g_kernel_owned,addr,addr+size);
    return true;
}
#endif

void reserve(uint64_t addr, uint64_t size, const char* what) {
    if (!plat::reserve(addr, size))
        fatal("cannot reserve %s at %llx (+%llx): the range is in use", what, (unsigned long long)addr,
              (unsigned long long)size);
}

}  // namespace

void memory_init() {
    reserve(kModuleBase, 0x20000000, "modules");
    reserve(kHeapBase, kHeapMax, "heap");
    reserve(kAliasBase, kAliasSize, "alias");
    reserve(kStackBase, kStackSize, "stack");
    reserve(kKernelBase, kKernelSize, "kernel");
    reserve(kThunkBase, kThunkSize, "thunks");
    // Only the alias and stack regions are free for the guest to pick addresses in (nn::os maps shared memory,
    // transfer memory and thread stacks at random free addresses); everything else is reported inaccessible.
    uint64_t end = kAddressSpaceBase + kAddressSpaceSize;
    vm_set(kAddressSpaceBase, kAliasBase - kAddressSpaceBase, kMemInaccessible, 0);
    if (kAliasBase + kAliasSize < kStackBase)  // Android: the alias sits below the modules (runtime.h)
        vm_set(kAliasBase + kAliasSize, kStackBase - (kAliasBase + kAliasSize), kMemInaccessible, 0);
    vm_set(kStackBase + kStackSize, end - (kStackBase + kStackSize), kMemInaccessible, 0);
}

bool commit(uint64_t addr, uint64_t size) {
#ifdef _WIN32
    return commit_pages(addr,size,false);
#else
    return plat::commit(addr,size);
#endif
}

void decommit(uint64_t addr, uint64_t size) {
#ifdef _WIN32
    uint64_t begin,end;
    if(!pages(addr,size,begin,end))return;
    std::unique_lock lock(g_vm_lock);
    wait_unpinned(lock,begin,end); // BEFORE VirtualFree, not the later vm_set.
    erase_span(g_committed,begin,end);
    erase_span(g_kernel_owned,begin,end);
    plat::decommit(addr,size); // void platform failure stays fail-closed in ledger.
#else
    plat::decommit(addr,size);
#endif
}

uint64_t kernel_alloc(uint64_t size, uint64_t align) {
    uint64_t a;
    {
        std::lock_guard lk(g_vm_lock);
        a=(g_kernel_next+align-1)&~(align-1);
        size=(size+0xFFF)&~0xFFFull;
        if(a+size>kKernelBase+kKernelSize)fatal("kernel region exhausted");
        g_kernel_next=a+size;
    }
    // commit now coordinates pins under g_vm_lock; never call it recursively.
#ifdef _WIN32
    // Commit + trusted publication form one transaction; a later vm_set cannot
    // be accidentally overridden between the host commit and ownership ledger.
    if(!commit_pages(a,size,true))fatal("kernel_alloc commit failed");
#else
    if(!commit(a,size))fatal("kernel_alloc commit failed");
#endif
    return a;
}

void vm_set(uint64_t addr, uint64_t size, uint32_t state, uint32_t perm) {
    if(!size || size>std::numeric_limits<uint64_t>::max()-addr)return;
    uint64_t end=addr+size;
#ifdef _WIN32
    std::unique_lock lk(g_vm_lock);
    wait_unpinned(lk,addr,end);
    // Any explicit later publication/revocation supersedes trusted ownership.
    erase_span(g_kernel_owned,addr,end);
#else
    std::lock_guard lk(g_vm_lock);
#endif
    // Cut overlapping blocks.
    auto it = g_vm.lower_bound(addr);
    if (it != g_vm.begin()) {
        auto p = std::prev(it);
        if (p->second.addr + p->second.size > addr) it = p;
    }
    while (it != g_vm.end() && it->second.addr < end) {
        VmInfo b = it->second;
        it = g_vm.erase(it);
        if (b.addr < addr) g_vm[b.addr] = {b.addr, addr - b.addr, b.state, b.perm};
        if (b.addr + b.size > end) g_vm[end] = {end, b.addr + b.size - end, b.state, b.perm};
    }
    if (state != kMemFree) g_vm[addr] = {addr, size, state, perm};
}

VmInfo vm_query(uint64_t addr) {
    std::lock_guard lk(g_vm_lock);
    auto it = g_vm.upper_bound(addr);
    uint64_t gap_end = it == g_vm.end() ? kAddressSpaceBase + kAddressSpaceSize : it->first;
    if (it != g_vm.begin()) {
        auto p = std::prev(it);
        if (addr < p->second.addr + p->second.size) return p->second;
        uint64_t gap_start = p->second.addr + p->second.size;
        return {gap_start, gap_end - gap_start, kMemFree, 0};
    }
    uint64_t gap_start = addr < kAddressSpaceBase ? 0 : kAddressSpaceBase;
    if (addr < kAddressSpaceBase) gap_end = kAddressSpaceBase;
    return {gap_start, gap_end - gap_start, kMemFree, 0};
}

namespace native_lan::detail {
uint64_t create_vm_lease() {
#ifdef _WIN32
    std::lock_guard lock(g_vm_lock);
    if(g_next_lease==std::numeric_limits<uint64_t>::max())return 0;
    const auto owner=++g_next_lease;g_vm_leases.emplace(owner,std::vector<Pin>{});return owner;
#else
    return 0;
#endif
}
void release_vm_lease(uint64_t owner) noexcept {
#ifdef _WIN32
    { std::lock_guard lock(g_vm_lock);g_vm_leases.erase(owner); }
    g_vm_changed.notify_all();
#endif
}
bool pin_vm_span(uint64_t owner,uint64_t address,size_t bytes,MemoryAccess access) noexcept {
#ifdef _WIN32
    const uint32_t perm=access==MemoryAccess::Read?1:access==MemoryAccess::Write?2:3;
    uint64_t end;
    if(!bytes)return true;
    if(!extent(address,bytes,end) || !guest_arena(address,end))return false;
    try {
        std::lock_guard lock(g_vm_lock);
        const auto lease=g_vm_leases.find(owner);
        if(lease==g_vm_leases.end())return false;
        auto& pins=lease->second;
        if(pinned(pins,address,end,perm))return true;
        for(const auto& [ticket,pending]:g_vm_mutations)
            if(overlap(address,end,pending.begin,pending.end))return false;
        for(uint64_t cursor=address;cursor<end;) {
            const auto host_end=covered_end(g_committed,cursor);
            auto guest_end=covered_end(g_kernel_owned,cursor);
            if(guest_end==cursor) {
                auto block=g_vm.upper_bound(cursor);
                if(block==g_vm.begin())return false;
                const auto& info=std::prev(block)->second;
                if(info.addr+info.size<=cursor || info.state==kMemFree || info.state==kMemInaccessible || (info.perm&perm)!=perm)return false;
                guest_end=info.addr+info.size;
            }
            if(host_end==cursor)return false;
            cursor=std::min({end,host_end,guest_end});
        }
        if(pins.size()>=32)return false; // adapter needs at most a handful of spans.
        pins.push_back({address,end,perm});return true;
    } catch(...) { return false; }
#else
    return false;
#endif
}
bool owns_vm_pin(uint64_t owner,uint64_t address,size_t bytes,MemoryAccess access) noexcept {
#ifdef _WIN32
    if(!bytes)return true;
    uint64_t end;if(!extent(address,bytes,end))return false;
    const uint32_t perm=access==MemoryAccess::Read?1:access==MemoryAccess::Write?2:3;
    std::lock_guard lock(g_vm_lock);
    const auto lease=g_vm_leases.find(owner);
    return lease!=g_vm_leases.end() && pinned(lease->second,address,end,perm);
#else
    return false;
#endif
}
} // namespace native_lan::detail

}  // namespace nx
