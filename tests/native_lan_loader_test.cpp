// Exercises the actual loader/linker/dispatch implementation with synthetic
// metadata and byte images only. No original game bytes or guest instructions.
#include <nx/runtime.h>
#include <nx/game.h>
#include "frametrace.h"
#include "platform.h"
#include "native_lan_profile.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
using namespace nx;
namespace prof=nx::native_lan::profile;
namespace {
constexpr uint64_t main_base=kModuleBase,sdk_base=kModuleBase+0x10000;
constexpr const char* socket_alias="nnsocketSocket";
constexpr const char* other_alias="nnsocketClose";
std::string mode,fixture_directory;
unsigned offline_calls=0,new_calls=0,callbacks=0,commits=0;
void check(bool ok,const char* why) { if(!ok) { std::fprintf(stderr,"native loader: %s\n",why);std::exit(1); } }
void offline(CPU& cpu) { ++offline_calls;cpu.x[0]=0x1111; }
void replacement(CPU& cpu) { ++new_calls;cpu.x[0]=0x2222; }
void fixture_main(CPU& cpu) { call(cpu,ld<uint64_t>(main_base+0x208)); }
std::array<uint8_t,0x300> main_image{},sdk_image{};
prof::Lookup queries() {
    return {[](std::string_view alias) { return hle_lookup(std::string(alias).c_str()); },
        [](std::string_view alias) { return hle_thunk_cached(std::string(alias).c_str()); },
        [](prof::Operation,prof::ReturnKind)->Fn { return replacement; }};
}
class Commit final:public prof::Committer {
public:
    bool commit(std::span<const prof::Binding> bindings) override {
        ++commits;
        std::vector<std::string> names;for(const auto& b:bindings)names.emplace_back(b.name);
        std::vector<HleBinding> batch;
        for(size_t i=0;i<bindings.size();++i)batch.push_back({names[i].c_str(),bindings[i].replacement});
        if(mode=="reject")batch.push_back(batch.front()); // Real batch duplicate rejection.
        if(!hle_replace_before_relocations(batch))return false;
        // Same non-throwing startup publication sequence as the runtime: all
        // allocation/checks precede HLE publication; SDK stores cannot fail.
        for(const auto& binding:bindings)*binding.sdk_entry=binding.replacement;
        return true;
    }
};
void before_relocations() {
    ++callbacks;
    const auto* main=module_at(main_base);const auto* sdk=module_at(sdk_base);
    check(main && sdk && main->table && sdk->table,"all modules and function tables mapped before callback");
    check(main->table[0x40/4]==fixture_main && sdk->table[0x20/4]==offline,"original function tables present");
    check(find_symbol(socket_alias)==sdk_base+0x20,"SDK public exports collected before callback");
    check(ld<uint64_t>(main_base+0x208)==0 && ld<uint64_t>(main_base+0x210)==0,"callback precedes import and relative relocation writes");
    const auto view=[](const LoadedModule& m) {
        return prof::ModuleView{m.info,{reinterpret_cast<const uint8_t*>(m.base),size_t(m.info->text_size)},
            {m.table,size_t(m.info->text_size/4)}};
    };
    const std::array guards{prof::Guard{0x40,prof::hash_prefix(std::span(main_image).subspan(0x40,32)),32}};
    const std::array aliases{
        prof::Alias{socket_alias,prof::Operation::Socket,prof::ReturnKind::Signed32,{0x20,prof::hash_prefix(std::span(sdk_image).subspan(0x20,32)),32},true},
        prof::Alias{other_alias,prof::Operation::Close,prof::ReturnKind::Signed32,{0x60,prof::hash_prefix(std::span(sdk_image).subspan(0x60,32)),32},false}};
    const prof::Profile profile{"synthetic-main-v1","synthetic-sdk-v1",guards,aliases};
    const auto lookup=queries();
    const auto plan=prof::prepare(true,profile,view(*main),view(*sdk),prof::Phase::BeforeRelocations,lookup);
    if(mode=="cached") {
        check(plan.status==prof::Status::Rejected && hle_thunk_cached(socket_alias),"actual cached HLE thunk rejects checked startup plan");
        check(hle_lookup(socket_alias)==offline && sdk->table[0x20/4]==offline,"cached rejection leaves HLE and SDK defaults intact");return;
    }
    check(plan.status==prof::Status::Ready,"synthetic image prefixes/identities/imports produce checked plan");
    if(mode=="throw")throw std::runtime_error("intentional startup callback rejection");
    Commit commit;
    const bool applied=prof::apply(plan,prof::Phase::BeforeRelocations,lookup,commit);
    check(applied==(mode=="success"),"real batch publication returns correct outcome");
    const auto expected=applied?replacement:offline;
    check(hle_lookup(socket_alias)==expected && hle_lookup(other_alias)==expected &&
        sdk->table[0x20/4]==expected && sdk->table[0x60/4]==expected,"HLE and SDK table stores agree; failure cannot publish half a backend");
}
ModuleInfo module(const char* name,uint64_t base,const FuncEntry* functions,size_t function_count,
                  const Symbol* symbols,size_t symbol_count,const Reloc* relocations,size_t relocation_count,const char* identity) {
    ModuleInfo info{};info.name=name;info.base=base;info.text_size=0x100;info.ro=0x100;info.ro_size=0x100;
    info.data=0x200;info.data_size=0x100;info.bss=0x300;info.bss_size=0xd00;
    info.funcs=functions;info.func_count=function_count;info.symbols=symbols;info.symbol_count=symbol_count;
    info.relocs=relocations;info.reloc_count=relocation_count;info.build_id=identity;return info;
}
}
// Only non-loader facilities are stubbed; actual loader/memory/profile code is
// linked. Frame timers/logging never start renderer or application subsystems.
namespace nx::game { const Info kInfo{"synthetic-loader-test","synthetic",0,"","",0}; }
namespace nx::ft {
std::atomic<uint64_t> g_counter[kCount]{};
CallTimer::CallTimer(TimedCall value):kind(value),started(0) {}
CallTimer::~CallTimer() { (void)kind;(void)started; }
void CallTimer::finish() noexcept { (void)kind;(void)started; }
}
namespace nx {
void backtrace(CPU&) {}
namespace plat {
std::string data_root() { return fixture_directory; }
FILE* fopen_utf8(const std::string&,const char*) { return nullptr; }
void log_line(const char*) {}
[[noreturn]] void exit_process(int status) { std::exit(status); }
bool reserve(uint64_t address,uint64_t size) {
#ifdef _WIN32
    return VirtualAlloc(reinterpret_cast<void*>(address),size,MEM_RESERVE,PAGE_NOACCESS)!=nullptr;
#else
    return false;
#endif
}
bool commit(uint64_t address,uint64_t size) {
#ifdef _WIN32
    const auto begin=address&~uint64_t(0xfff),end=(address+size+0xfff)&~uint64_t(0xfff);
    return VirtualAlloc(reinterpret_cast<void*>(begin),end-begin,MEM_COMMIT,PAGE_READWRITE)!=nullptr;
#else
    return false;
#endif
}
void decommit(uint64_t address,uint64_t size) {
#ifdef _WIN32
    check(VirtualFree(reinterpret_cast<void*>(address),size,MEM_DECOMMIT)!=0,"private arena decommit");
#endif
}
}
}
int main(int argc,char** argv) {
#ifndef _WIN32
    std::puts("Native loader: private VirtualAlloc fixture requires Windows");return 0;
#else
    check(SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS)!=0,"BelowNormal CPU test");
    mode=argc>1?argv[1]:"success";
    check(mode=="success"||mode=="reject"||mode=="cached"||mode=="throw","known fresh-process test mode");
    HleRegistrar original_socket(socket_alias,offline),original_other(other_alias,offline);
    check(!hle_thunk_cached(socket_alias),"fresh process starts with no cached socket thunk");
    if(mode=="cached")hle_address(socket_alias);
    check(plat::reserve(main_base,0x20000),"reserve synthetic modules' private guest arena");
    const auto directory=std::filesystem::temp_directory_path()/("native-loader-test-"+std::to_string(GetCurrentProcessId()));
    check(std::filesystem::create_directory(directory),"fixture directory must be fresh");fixture_directory=directory.string();
    // loader log() rotates NX_LOG before fopen_utf8; never inherit a player's
    // log target even though this fixture intentionally opens no log handle.
    check(_putenv_s("NX_LOG",(directory/"test.log").string().c_str())==0,"isolate loader log rotation inside fixture directory");
    for(size_t i=0;i<0x100;++i) {main_image[i]=uint8_t(i*3+7);sdk_image[i]=uint8_t(i*5+11);}
    {std::ofstream out(directory/"main.bin",std::ios::binary);out.write(reinterpret_cast<const char*>(main_image.data()),main_image.size());check(bool(out),"write synthetic main image");}
    {std::ofstream out(directory/"sdk.bin",std::ios::binary);out.write(reinterpret_cast<const char*>(sdk_image.data()),sdk_image.size());check(bool(out),"write synthetic SDK image");}
    const FuncEntry main_functions[]={{0x40,fixture_main}},sdk_functions[]={{0x20,offline},{0x60,offline}};
    const Symbol main_symbols[]={{socket_alias,0,0,2,1,1},{"fixtureData",0,0,1,1,1}};
    const Symbol sdk_symbols[]={{socket_alias,0x20,4,2,1,0},{other_alias,0x60,4,2,1,0},{"fixtureData",0x240,8,1,1,0}};
    const Reloc relocations[]={{0x208,R_AARCH64_JUMP_SLOT,0,0},{0x210,R_AARCH64_RELATIVE,0,0x80},{0x218,R_AARCH64_GLOB_DAT,1,0}};
    const auto main_info=module("main",main_base,main_functions,1,main_symbols,2,relocations,3,"synthetic-main-v1");
    const auto sdk_info=module("sdk",sdk_base,sdk_functions,2,sdk_symbols,3,nullptr,0,"synthetic-sdk-v1");
    const ModuleInfo* modules[]={&main_info,&sdk_info};bool threw=false;
    try {load_modules(modules,2,fixture_directory,before_relocations);}
    catch(const std::runtime_error&) {threw=true;}
    check(threw==(mode=="throw") && callbacks==1,"actual loader callback executes once and propagates failure");
    if(!threw) {
        check(ld<uint64_t>(main_base+0x208)==hle_address(socket_alias) && hle_thunk_cached(socket_alias),"real import relocation captures installed HLE thunk");
        check(ld<uint64_t>(main_base+0x210)==main_base+0x80 && ld<uint64_t>(main_base+0x218)==sdk_base+0x240,"ordinary relative/data relocations run after callback");
    } else {
        check(ld<uint64_t>(main_base+0x208)==0 && hle_lookup(socket_alias)==offline && module_at(sdk_base)->table[0x20/4]==offline,"throwing callback leaves defaults and never enters relocation stage");
    }
    const HleBinding late[]={{"uncached-late-fixture",replacement}};
    check(!hle_thunk_cached(late[0].name) && !hle_replace_before_relocations(late) && !hle_lookup(late[0].name),"late HLE publication rejected even for uncached name; exception restores phase guard");
    CPU cpu{};
    if(threw)call(cpu,hle_address(socket_alias));else call(cpu,main_base+0x40);
    check(cpu.x[0]==(mode=="success"?0x2222u:0x1111u),"actual imported HLE call reaches expected handler");
    call(cpu,sdk_base+0x20);call(cpu,hle_address(other_alias));
    check(offline_calls==(mode=="success"?0u:3u) && new_calls==(mode=="success"?3u:0u),"HLE and direct SDK dispatch never reach old offline handler after successful publication");
    check(commits==((mode=="success"||mode=="reject")?1u:0u),"rejected/cached/throwing plans do not execute unintended commit");
    nx::decommit(main_base,0x20000);
    check(VirtualFree(reinterpret_cast<void*>(main_base),0,MEM_RELEASE)!=0,"release synthetic module arena");
    check(std::filesystem::remove(directory/"main.bin"),"remove exact synthetic main fixture");
    check(std::filesystem::remove(directory/"sdk.bin"),"remove exact synthetic SDK fixture");
    check(std::filesystem::remove(directory),"remove empty owned fixture directory");
    std::printf("Native loader: actual mapping/callback/HLE batch/SDK table/link/dispatch %s passed (synthetic only)\n",mode.c_str());
#endif
}
