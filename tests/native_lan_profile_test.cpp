#include "native_lan_profile.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace nx;
using namespace nx::native_lan::profile;
namespace {
void check(bool ok,const char* text) {if(!ok){std::cerr<<text<<'\n';std::exit(1);}}
void original(CPU&) {}
void offline(CPU&) {}
void backend(CPU&) {}
void other(CPU&) {}
struct Fixture {
    std::array<uint8_t,160> main_bytes{},sdk_bytes{};
    std::array<Fn,40> table{};
    std::array<Symbol,3> imports{{
        {"nnsocketSocket",0,0,2,1,1},
        {"_ZN2nn6socket12GetLastErrorEv",0,0,2,1,1},
        {"nnsocketInetHtonl",0,0,2,1,1}}};
    std::array<Symbol,3> definitions{{
        {"nnsocketSocket",4,32,2,1,0},
        {"_ZN2nn6socket12GetLastErrorEv",44,32,2,1,0},
        {"nnsocketInetHtonl",84,32,2,1,0}}};
    ModuleInfo main{},sdk{};
    std::array<Guard,1> guards{};
    std::array<Alias,3> aliases{};
    std::map<std::string,Fn> hle;
    std::map<std::string,bool> cached;
    size_t queries=0,handler_queries=0;
    Fixture() {
        for(size_t i=0;i<main_bytes.size();++i){main_bytes[i]=uint8_t(i*3);sdk_bytes[i]=uint8_t(i*7);}
        table.fill(original);
        main.text_size=main_bytes.size();main.build_id="fixture-main";main.symbols=imports.data();main.symbol_count=imports.size();
        sdk.text_size=sdk_bytes.size();sdk.build_id="fixture-sdk";sdk.symbols=definitions.data();sdk.symbol_count=definitions.size();
        guards[0]={8,hash_prefix(std::span(main_bytes).subspan(8,32)),32};
        aliases={{{"nnsocketSocket",Operation::Socket,ReturnKind::Signed32,{4,hash_prefix(std::span(sdk_bytes).subspan(4,32)),32},true},
                  {"_ZN2nn6socket12GetLastErrorEv",Operation::GetErrno,ReturnKind::Signed32,{44,hash_prefix(std::span(sdk_bytes).subspan(44,32)),32},true},
                  {"nnsocketInetHtonl",Operation::PreserveOriginal,ReturnKind::Preserved,{84,hash_prefix(std::span(sdk_bytes).subspan(84,32)),32},true}}};
        hle[std::string(aliases[1].name)]=offline;
    }
    Profile profile() {return {"fixture-main","fixture-sdk",guards,aliases};}
    Lookup lookup(bool missing_handler=false) {
        return {[&](std::string_view name){++queries;auto it=hle.find(std::string(name));return it==hle.end()?nullptr:it->second;},
                [&](std::string_view name){++queries;auto it=cached.find(std::string(name));return it!=cached.end()&&it->second;},
                [&,missing_handler](Operation op,ReturnKind result){++handler_queries;
                    check(result==ReturnKind::Signed32,"resolver receives explicit return contract");
                    return missing_handler&&op==Operation::GetErrno?nullptr:backend;}};
    }
    Plan plan(bool enabled=true,Phase phase=Phase::BeforeRelocations,bool missing=false) {
        return prepare(enabled,profile(),{&main,main_bytes,{}},{&sdk,sdk_bytes,table},phase,lookup(missing));
    }
};
struct AtomicCommitter:Committer {
    Fixture& fixture;bool reject=false,throws=false;size_t calls=0;
    explicit AtomicCommitter(Fixture& f):fixture(f){}
    bool commit(std::span<const Binding> bindings) override {
        ++calls;
        auto next=fixture.hle;
        for(const auto& binding:bindings)next[std::string(binding.name)]=binding.replacement;
        if(throws)throw std::runtime_error("staging allocation failure");
        if(reject)return false;
        // All fallible staging finished: this publication is nonthrowing.
        for(const auto& binding:bindings)*binding.sdk_entry=binding.replacement;
        fixture.hle.swap(next);return true;
    }
};
}
int main() {
    check(prepare(false,{}, {}, {},Phase::AfterRelocations,{}).status==Status::Disabled,
          "disabled backend needs no profile, mapped modules or queries");
    {
        Fixture f;const auto p=f.plan(false);
        check(p.status==Status::Disabled&&f.queries==0&&f.handler_queries==0,"default-off plan does not query or change anything");
        check(f.table[1]==original&&f.hle.at("_ZN2nn6socket12GetLastErrorEv")==offline,"default offline handlers preserved");
    }
    {
        Fixture f;auto p=f.plan();check(p.status==Status::Ready&&p.bindings.size()==2&&p.preserved_aliases==1,"complete guarded plan ready");
        check(p.bindings[1].bypassed_sdk_via_hle&&p.bindings[1].original_hle==offline,"records existing HLE bypass and SDK original together");
        check(f.table[1]==original&&f.table[11]==original,"preparation never publishes wrappers");
        AtomicCommitter commit(f);commit.reject=true;
        check(!apply(p,Phase::BeforeRelocations,f.lookup(),commit)&&f.table[1]==original&&f.hle.size()==1,"failed atomic staging leaves every alias untouched");
        commit.reject=false;commit.throws=true;
        try {apply(p,Phase::BeforeRelocations,f.lookup(),commit);check(false,"exception expected");}
        catch(const std::runtime_error&){}
        check(f.table[1]==original&&f.hle.size()==1,"exception before publication leaves originals intact");
        commit.throws=false;check(apply(p,Phase::BeforeRelocations,f.lookup(),commit),"one coherent HLE+SDK transaction commits");
        check(f.table[1]==backend&&f.table[11]==backend&&f.table[21]==original,"SDK entries replaced while pure conversion preserved");
        check(f.hle.at("nnsocketSocket")==backend&&f.hle.at("_ZN2nn6socket12GetLastErrorEv")==backend,"raw/C and existing offline aliases share backend");
        check(!apply(p,Phase::BeforeRelocations,f.lookup(),commit),"stale captured plan cannot overwrite a later binding");
    }
    for(unsigned kind=0;kind<10;++kind) {
        Fixture f;
        switch(kind) {
        case 0:f.main.build_id="other-main";break;
        case 1:f.sdk.build_id="other-sdk";break;
        case 2:f.main_bytes[8]^=1;break;
        case 3:f.sdk_bytes[50]^=1;break;
        case 4:f.definitions[1].name="missing-required-alias";break;
        case 5:f.imports[1].name="nnsocketFutureOwnedDescriptorApi";break;
        case 6:f.cached["nnsocketSocket"]=true;break;
        case 7:f.definitions[1].value=48;break;
        case 8:f.table[11]=nullptr;break;
        case 9:f.imports[1].imported=0;break;
        }
        const auto p=f.plan();check(p.status==Status::Rejected&&p.bindings.empty()&&!p.error.empty(),"identity/code/alias/cache mismatch rejects entire plan");
        check(f.table[1]==original&&f.hle.at("_ZN2nn6socket12GetLastErrorEv")==offline,"rejected plans never partially mutate defaults");
    }
    {
        Fixture f;check(f.plan(true,Phase::AfterRelocations).status==Status::Rejected,"late registry replacement rejected before cached thunks can be stale");
        check(f.plan(true,Phase::BeforeRelocations,true).status==Status::Rejected,"explicit unsupported/failure handler required for every operation");
        const auto p=f.plan();AtomicCommitter commit(f);
        f.cached["nnsocketSocket"]=true;
        check(!apply(p,Phase::BeforeRelocations,f.lookup(),commit)&&commit.calls==0,"thunk caching after planning prevents commit");
        f.cached.clear();f.table[1]=other;
        check(!apply(p,Phase::BeforeRelocations,f.lookup(),commit)&&commit.calls==0,"changed SDK original prevents commit");
        f.table[1]=original;f.hle["nnsocketSocket"]=other;
        check(!apply(p,Phase::BeforeRelocations,f.lookup(),commit)&&commit.calls==0,"changed HLE original prevents commit");
        f.hle.erase("nnsocketSocket");auto changed=f.lookup();changed.handler=[](Operation,ReturnKind){return other;};
        check(!apply(p,Phase::BeforeRelocations,changed,commit)&&commit.calls==0,"changed backend operation resolver prevents commit");
    }
    const auto& actual=product120();
    check(actual.aliases.size()>150&&actual.main_guards.size()==3,"complete production profile carries source guards and coherent alias families");
    bool raw=false,typed=false,c=false,detail=false,init22=false;
    for(const auto& alias:actual.aliases) {
        raw|=alias.name=="_ZN2nn6socket6SocketEiii";
        typed|=alias.name=="_ZN2nn6socket6SocketENS0_6FamilyENS0_4TypeENS0_8ProtocolE";
        c|=alias.name=="nnsocketSocket";
        detail|=alias.name=="_ZN2nn6socket6detail6SocketEiii";
        init22|=alias.name=="nnsocketInitialize"&&alias.result==ReturnKind::Result22;
    }
    check(raw&&typed&&c&&detail&&init22,"profile covers raw/typed/C/detail imports and C result-mask contract");
    std::cout<<"Native LAN binding plan: identity, alias completeness, HLE bypass/cache, transactional publication and default preservation passed\n";
}
