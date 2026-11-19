#include "native_lan_memory.h"
#include <nx/runtime.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace {
void check(bool ok,const char* why) { if(!ok) { std::fprintf(stderr,"native memory: %s\n",why);std::exit(1); } }
std::atomic<unsigned> os_decommits{0},os_commits{0};
std::atomic<uint64_t> fail_commit_address{0};
}
// Test links actual memory.cpp with private-arena OS stubs, not the application
// platform/window implementation. No game, renderer or remote process involved.
namespace nx {
[[noreturn]] void fatal(const char*,...) { std::fputs("unexpected runtime fatal\n",stderr);std::exit(1); }
namespace plat {
bool reserve(uint64_t address,uint64_t size) {
#ifdef _WIN32
    return VirtualAlloc(reinterpret_cast<void*>(address),size,MEM_RESERVE,PAGE_NOACCESS)!=nullptr;
#else
    return false;
#endif
}
bool commit(uint64_t address,uint64_t size) {
    ++os_commits;
    if(address==fail_commit_address.load())return false;
#ifdef _WIN32
    const uint64_t begin=address&~uint64_t(0xfff),end=(address+size+0xfff)&~uint64_t(0xfff);
    return VirtualAlloc(reinterpret_cast<void*>(begin),end-begin,MEM_COMMIT,PAGE_READWRITE)!=nullptr;
#else
    return false;
#endif
}
void decommit(uint64_t address,uint64_t size) {
    ++os_decommits;
#ifdef _WIN32
    check(VirtualFree(reinterpret_cast<void*>(address),size,MEM_DECOMMIT)!=0,"private arena decommit");
#endif
}
}
}
using namespace nx;
using namespace nx::native_lan;
int main() {
#ifndef _WIN32
    check(!RuntimeGuestMemory::supported(),"non-Windows fails closed until write-watch participates");
    RuntimeGuestMemory memory;check(!memory.validate(0x1000,4,MemoryAccess::Read),"unsupported mapper rejects memory");
    std::puts("Native memory: non-Windows fail-closed passed");
#else
    check(SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS)!=0,"BelowNormal CPU test");
    constexpr uint64_t arena=kAliasBase+0x10000000, page=0x1000;
    check(nx::plat::reserve(arena,0x10000),"reserve private guest alias arena");
    check(nx::plat::reserve(kKernelBase,0x10000),"reserve private kernel allocation arena");
    check(nx::commit(arena,4*page),"commit owned test pages");
    vm_set(arena,page,kMemHeap,3);vm_set(arena+page,page,kMemCode,1);
    vm_set(arena+3*page,page,kMemHeap,3);
    const std::array<std::byte,8> value{std::byte{1},std::byte{2},std::byte{3},std::byte{4}};
    {
        RuntimeGuestMemory memory;std::array<std::byte,8> output{};
        check(!memory.read(arena,output),"copy cannot cast an address before a successful pin");
        check(memory.validate(arena,8,MemoryAccess::ReadWrite) && memory.write(arena,value) && memory.read(arena,output) && output==value,"actual owned mapped read/write");
        check(memory.validate(arena+page-4,8,MemoryAccess::Read),"read spans adjacent guest regions with distinct permissions");
        check(!memory.validate(arena+page-4,8,MemoryAccess::Write),"cross-region write denied by readonly second region");
        check(!memory.write(arena+page,std::span(value).first(4)),"read pin never authorizes a write");
        check(!memory.validate(arena+2*page-4,8,MemoryAccess::Read),"cross-region gap rejected");
        check(!memory.validate(~uint64_t(3),8,MemoryAccess::Read),"overflow rejected before cast");
        check(!memory.validate(uint64_t(&memory),4,MemoryAccess::Read),"ordinary host object is not runtime guest ownership");
    }
    // VmInfo alone is insufficient: it may publish an uncommitted or already
    // decommitted region. The host ledger must independently cover the span.
    vm_set(arena+5*page,page,kMemHeap,3);
    { RuntimeGuestMemory memory;check(!memory.validate(arena+5*page,4,MemoryAccess::Read),"fake VmInfo without host commit denied"); }
    { RuntimeGuestMemory memory;check(!memory.validate(arena+2*page,4,MemoryAccess::Read),"host commit without guest mapping denied"); }
    // A failed OS commit must not resurrect accessibility from stale allocated
    // VmInfo after decommit. Injection lives only in this test's platform stub.
    nx::decommit(arena+3*page,page);
    check(vm_query(arena+3*page).perm==3,"failed-commit fixture retains stale accessible metadata");
    fail_commit_address=arena+3*page;
    check(!nx::commit(arena+3*page,page),"injected host commit failure is returned");
    fail_commit_address=0;
    { RuntimeGuestMemory memory;check(!memory.validate(arena+3*page,4,MemoryAccess::Read),"failed OS commit cannot publish committed ledger despite stale VmInfo"); }
    check(nx::commit(arena+3*page,page),"successful retry commits actual host pages");
    { RuntimeGuestMemory memory;check(memory.validate(arena+3*page,4,MemoryAccess::ReadWrite),"successful retry publishes accessibility only after host success"); }
    const uint64_t tls=kernel_alloc(page);
    check(tls==kKernelBase,"test private kernel allocation");
    const auto prior=vm_query(tls);
    {
        RuntimeGuestMemory memory;check(memory.validate(tls+0x1d8,4,MemoryAccess::ReadWrite),"runtime-owned unpublished kernel TLS is validated");
        check(vm_query(tls).state==prior.state,"private kernel ownership does not change QueryMemory baseline");
    }
    vm_set(tls+0x1d8,4,kMemInaccessible,0);
    { RuntimeGuestMemory memory;check(!memory.validate(tls+0x1d8,4,MemoryAccess::Read),"explicit later kernel revocation wins over ownership");check(memory.validate(tls+0x1dc,4,MemoryAccess::ReadWrite),"revocation cuts only its explicit span"); }
    // Establish a live pin, launch a mutation, then observe its pending gate by
    // rejection of new conflicting pins. No timing-only proof of blocking.
    const auto wait_pending=[&](uint64_t address) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        for(;;) {
            RuntimeGuestMemory probe;
            if(!probe.validate(address,4,MemoryAccess::ReadWrite))break;
            check(std::chrono::steady_clock::now()<deadline,"mutation publishes pending gate");
            std::this_thread::yield();
        }
    };
    auto live=std::make_unique<RuntimeGuestMemory>();
    check(live->validate(arena,8,MemoryAccess::ReadWrite),"lease for concurrent decommit test");
    const auto before=os_decommits.load();std::atomic<bool> finished{false};
    std::thread unmap([&] { nx::decommit(arena,page);finished=true; });
    wait_pending(arena);
    check(!finished && os_decommits.load()==before,"decommit blocked BEFORE any OS unmapping call");
    check(live->write(arena,value),"existing lease stays accessible while unmap waits");
    std::atomic<bool> query_done{false};
    std::thread query([&] { (void)vm_query(arena+page);query_done=true; });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(!query_done && std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    check(query_done,"global VM mutex is not held while waiting for lease release");query.join();
    check(nx::commit(arena+6*page,page),"unrelated host mapping proceeds while overlapping unmap waits");
    vm_set(arena+6*page,page,kMemHeap,3);
    live.reset();unmap.join();
    check(finished && os_decommits.load()==before+1,"OS unmap resumes after lease release");
    check(vm_query(arena).perm==3,"exercise actual caller ordering: VM metadata still old after decommit");
    { RuntimeGuestMemory memory;check(!memory.validate(arena,4,MemoryAccess::Read),"stale accessible VmInfo cannot authorize decommitted host memory"); }
    check(nx::commit(arena,page),"recommit private arena after release");
    // Guest permission changes also wait without mutating the old permission
    // while the lease is still active. New callers see the changed permission.
    live=std::make_unique<RuntimeGuestMemory>();check(live->validate(arena,8,MemoryAccess::ReadWrite),"lease before permission revoke");
    finished=false;std::thread protect([&] { vm_set(arena,page,kMemHeap,1);finished=true; });
    wait_pending(arena);check(!finished,"permission publication waits for active writer lease");
    live.reset();protect.join();
    { RuntimeGuestMemory memory;check(memory.validate(arena,4,MemoryAccess::Read) && !memory.validate(arena,4,MemoryAccess::Write),"revoked writer permission enforced after release"); }
    vm_set(arena,page,kMemHeap,3);
    live=std::make_unique<RuntimeGuestMemory>();check(live->validate(arena,8,MemoryAccess::ReadWrite),"lease before recommit");
    const auto commits_before=os_commits.load();finished=false;
    std::thread recommit([&] { check(nx::commit(arena,page),"recommit after pin");finished=true; });
    wait_pending(arena);check(!finished && os_commits.load()==commits_before,"recommit also gates before OS operation");
    live.reset();recommit.join();
    // Windows decommits the whole containing page even for an unaligned one-
    // byte request. A pin elsewhere on that page must gate BEFORE the OS call.
    live=std::make_unique<RuntimeGuestMemory>();
    check(live->validate(arena+8,8,MemoryAccess::ReadWrite),"pin away from one-byte decommit range on same OS page");
    const auto unaligned_before=os_decommits.load();finished=false;
    std::thread one_byte([&] { nx::decommit(arena+page-1,1);finished=true; });
    wait_pending(arena);
    check(!finished && os_decommits.load()==unaligned_before,"one-byte decommit waits for pin anywhere in containing host page");
    check(live->write(arena+8,value),"pinned bytes outside requested one-byte interval remain accessible until release");
    live.reset();one_byte.join();
    check(finished && os_decommits.load()==unaligned_before+1,"unaligned OS decommit resumes after page pin release");
    { RuntimeGuestMemory memory;check(!memory.validate(arena+8,4,MemoryAccess::Read),"one-byte decommit removes whole host page from committed ledger"); }
    nx::decommit(arena,7*page);vm_set(arena,7*page,kMemFree,0);
    nx::decommit(kKernelBase,page);
    check(VirtualFree(reinterpret_cast<void*>(arena),0,MEM_RELEASE)!=0,"release private alias reservation");
    check(VirtualFree(reinterpret_cast<void*>(kKernelBase),0,MEM_RELEASE)!=0,"release private kernel reservation");
    std::puts("Native memory: actual mapping/perms, cross-regions, TLS ownership, overflow, unmap/recommit/permission pins and unlocked waits passed (Windows)");
#endif
}
