#include "nvn/pipeline_typed_variants.h"
#include <cstdio>
#include <cstdlib>
using namespace nx::nvn::typed_pipeline;
void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"typed sidecar: %s\n",why);std::exit(1);}}
Variant fixture() {
    Variant v;v.vs.stage=1;v.vs.spirv_sha1.fill('a');v.vs.kinds={{1,1},{3,2}};
    v.fs.stage=2;v.fs.spirv_sha1.fill('b');return v;
}
void word(Bytes& b,size_t offset,uint32_t value){for(unsigned i=0;i<4;++i)b[offset+i]=uint8_t(value>>(8*i));}
void seal(Bytes& b) {
    uint64_t h=14695981039346656037ull;for(size_t i=0;i<b.size()-8;++i){h^=b[i];h*=1099511628211ull;}
    for(unsigned i=0;i<8;++i)b[b.size()-8+i]=uint8_t(h>>(8*i));
}
int main() {
    Bytes legacy(776);for(size_t i=0;i<legacy.size();++i)legacy[i]=uint8_t(i*17);
    const auto saved_legacy=legacy;auto first=fixture();auto second=first;second.vs.kinds={{1,2}};
    Catalog source;check(source.add(legacy,first)&&source.add(legacy,second)&&source.size()==2,"same exact recipe retains unsigned/signed variants separately");
    check(source.add(legacy,first)&&source.size()==2,"runtime duplicate observation deduplicated");
    Format format{0x1002,0x67df,776};Bytes encoded;
    check(encode(source,format,encoded),"versioned sidecar encodes");Catalog decoded;
    check(decode(encoded,format,decoded)&&decoded.size()==2,"mixed variant roundtrip");
    check(*decoded.find(legacy)==*source.find(legacy) && legacy==saved_legacy,"legacy bytes and every complete map remain unchanged");
    auto other=legacy;other[400]^=1;check(!decoded.find(other),"one differing recipe byte never matches");
    auto stale=first.vs;stale.spirv_sha1[0]='c';check(!same_source(first.vs,stale),"changed base SPIR-V checksum rejects source identity");
    stale=first.vs;++stale.shared;check(!same_source(first.vs,stale),"changed shared controls reject source identity");
    stale=first.vs;++stale.local[0];check(!same_source(first.vs,stale),"changed local controls reject source identity");
    stale=first.vs;stale.stage=2;check(!same_source(first.vs,stale),"changed stage rejects source identity");
    for(size_t size=0;size<encoded.size();++size) {
        Catalog retained=decoded;check(!decode(std::span(encoded).first(size),format,retained)&&retained.size()==2,"truncation fails transactionally");
    }
    for(size_t byte=0;byte<encoded.size();++byte) {
        auto damaged=encoded;damaged[byte]^=1;Catalog retained=decoded;
        check(!decode(damaged,format,retained)&&retained.size()==2,"payload/header/checksum bit corruption rejected without publication");
    }
    for(unsigned kind=0;kind<3;++kind) {
        auto damaged=encoded;
        if(kind==0)word(damaged,24,uint32_t(kMaxVariants+1));
        if(kind==1)word(damaged,20,777);
        if(kind==2)word(damaged,28+776+20+40+4+4,3);
        seal(damaged);Catalog retained=decoded;check(!decode(damaged,format,retained)&&retained.size()==2,"structurally invalid but checksummed sidecar rejected");
    }
    {
        auto damaged=encoded;word(damaged,28+776+64+8,1);seal(damaged);
        Catalog kept=decoded;check(!decode(damaged,format,kept),"duplicate/unsorted bindings rejected even with valid file checksum");
    }
    {
        Catalog one;one.add(legacy,first);Bytes encoded_one;check(encode(one,format,encoded_one),"single variant fixture");
        Bytes duplicate(encoded_one.begin(),encoded_one.end()-8);
        duplicate.insert(duplicate.end(),encoded_one.begin()+28,encoded_one.end()-8);duplicate.resize(duplicate.size()+8);
        word(duplicate,24,2);seal(duplicate);Catalog kept=decoded;
        check(!decode(duplicate,format,kept) && kept.size()==2,"duplicate complete variants rejected transactionally");
    }
    auto bad_vendor=format;++bad_vendor.vendor;Catalog retained=decoded;
    check(!decode(encoded,bad_vendor,retained)&&retained.size()==2,"different GPU namespace rejected transactionally");
    auto invalid=first;invalid.vs.kinds={{0,3}};check(!source.add(legacy,invalid),"invalid sampled kind not retained");
    invalid=first;invalid.vs.kinds.clear();invalid.fs.kinds.clear();check(!source.add(legacy,invalid),"untyped legacy variants need no supplemental record");
    Catalog bounded;for(uint32_t i=0;i<kMaxVariants;++i){auto key=legacy;word(key,100,i);check(bounded.add(key,first),"bounded population");}
    other=legacy;word(other,100,0xffffffff);check(!bounded.add(other,first)&&bounded.size()==kMaxVariants,"catalog bound refuses new entries without destroying prior records");
    Bytes excessive(kMaxBytes+1);check(!decode(excessive,format,retained),"oversized input rejected before parsing");
    std::puts("Typed sidecar: exact v1 identity, mixed variants, source checks, corruption/truncation and bounds passed");
}
