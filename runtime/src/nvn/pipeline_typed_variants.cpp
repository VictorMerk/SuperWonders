#include "pipeline_typed_variants.h"
#include <algorithm>
#include <cstring>

namespace nx::nvn::typed_pipeline {
namespace {
constexpr std::array<uint8_t,8> magic={'W','O','N','T','Y','P','0','1'};
uint64_t checksum(std::span<const uint8_t> bytes) {
    uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}return h;
}
void put(Bytes& out,uint32_t v) {for(unsigned i=0;i<4;++i)out.push_back(uint8_t(v>>(i*8)));}
struct Reader {
    std::span<const uint8_t> input;
    bool take(void* dst,size_t n) {
        if(n>input.size())return false;
        std::memcpy(dst,input.data(),n);input=input.subspan(n);return true;
    }
    bool word(uint32_t& value) {
        if(input.size()<4)return false;
        value=uint32_t(input[0])|uint32_t(input[1])<<8|uint32_t(input[2])<<16|uint32_t(input[3])<<24;
        input=input.subspan(4);return true;
    }
};
void stage(Bytes& out,const ShaderVariant& s) {
    put(out,s.stage);put(out,s.shared);for(auto n:s.local)put(out,n);
    out.insert(out.end(),s.spirv_sha1.begin(),s.spirv_sha1.end());put(out,uint32_t(s.kinds.size()));
    for(auto [binding,kind]:s.kinds){put(out,binding);put(out,kind);}
}
bool stage(Reader& in,ShaderVariant& s) {
    if(!in.word(s.stage)||!in.word(s.shared))return false;
    for(auto& n:s.local)if(!in.word(n))return false;
    uint32_t count=0;if(!in.take(s.spirv_sha1.data(),s.spirv_sha1.size())||!in.word(count)||count>kMaxBindings)return false;
    uint32_t previous=0;
    for(uint32_t i=0;i<count;++i) {
        uint32_t binding,kind;if(!in.word(binding)||!in.word(kind)||(i&&binding<=previous))return false;
        s.kinds.emplace(binding,kind);previous=binding;
    }
    return true;
}
}
bool valid(const ShaderVariant& s,bool vertex) {
    if(!vertex && s.stage==0)return s.kinds.empty() && s.shared==0 &&
        s.local==std::array<uint32_t,3>{1,1,1} &&
        std::all_of(s.spirv_sha1.begin(),s.spirv_sha1.end(),[](char c){return !c;});
    if(s.stage!=(vertex?1u:2u)||s.kinds.size()>kMaxBindings)return false;
    for(auto n:s.local)if(!n || n>1024)return false;
    for(char c:s.spirv_sha1)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
    for(auto [binding,kind]:s.kinds)if(binding>4095 || (kind!=1 && kind!=2))return false;
    return true;
}
bool same_source(const ShaderVariant& a,const ShaderVariant& b) noexcept {
    return a.stage==b.stage && a.shared==b.shared && a.local==b.local && a.spirv_sha1==b.spirv_sha1;
}
bool Catalog::add(const Bytes& recipe,const Variant& variant) {
    if(recipe.empty()||recipe.size()>kMaxRecipeBytes||!valid(variant.vs,true)||!valid(variant.fs,false)||
       (variant.vs.kinds.empty()&&variant.fs.kinds.empty()))return false;
    if(auto it=records_.find(recipe);it!=records_.end())
        if(std::find(it->second.begin(),it->second.end(),variant)!=it->second.end())return true;
    if(size_>=kMaxVariants)return false;
    if(auto it=records_.find(recipe);it!=records_.end())it->second.push_back(variant);
    else {
        std::vector<Variant> staged;staged.push_back(variant);
        records_.emplace(recipe,std::move(staged));
    }
    ++size_;return true;
}
const std::vector<Variant>* Catalog::find(const Bytes& recipe) const {
    auto it=records_.find(recipe);return it==records_.end()?nullptr:&it->second;
}
bool encode(const Catalog& catalog,Format format,Bytes& out) {
    if(!format.recipe_size||format.recipe_size>kMaxRecipeBytes||catalog.size()>kMaxVariants)return false;
    Bytes encoded(magic.begin(),magic.end());put(encoded,1);put(encoded,format.vendor);put(encoded,format.device);
    put(encoded,format.recipe_size);put(encoded,uint32_t(catalog.size()));
    for(const auto& [recipe,variants]:catalog.records()) {
        if(recipe.size()!=format.recipe_size)return false;
        for(const auto& variant:variants) {
            encoded.insert(encoded.end(),recipe.begin(),recipe.end());stage(encoded,variant.vs);stage(encoded,variant.fs);
            if(encoded.size()>kMaxBytes)return false;
        }
    }
    const auto hash=checksum(encoded);for(unsigned i=0;i<8;++i)encoded.push_back(uint8_t(hash>>(8*i)));
    out=std::move(encoded);return true;
}
bool decode(std::span<const uint8_t> bytes,Format format,Catalog& out) {
    if(bytes.size()>kMaxBytes||bytes.size()<8)return false;
    uint64_t stored=0;for(unsigned i=0;i<8;++i)stored|=uint64_t(bytes[bytes.size()-8+i])<<(8*i);
    bytes=bytes.first(bytes.size()-8);if(checksum(bytes)!=stored)return false;
    Reader in{bytes};std::array<uint8_t,8> found{};uint32_t version,vendor,device,size,count;
    if(!in.take(found.data(),found.size())||found!=magic||!in.word(version)||version!=1||!in.word(vendor)||
       !in.word(device)||!in.word(size)||!in.word(count)||vendor!=format.vendor||device!=format.device||
       size!=format.recipe_size||!size||size>kMaxRecipeBytes||count>kMaxVariants)return false;
    Catalog parsed;
    for(uint32_t i=0;i<count;++i) {
        if(size>in.input.size())return false;
        Bytes recipe(size);Variant variant;
        if(!in.take(recipe.data(),size)||!stage(in,variant.vs)||!stage(in,variant.fs))return false;
        const auto before=parsed.size();
        if(!parsed.add(recipe,variant)||parsed.size()!=before+1)return false; // duplicates are corruption
    }
    if(!in.input.empty())return false;
    out=std::move(parsed);return true;
}
}
