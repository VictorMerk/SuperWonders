#pragma once
#include <cstdint>
#include <cstddef>
#include <map>
#include <stdexcept>

namespace nx::nvn::typed_pipeline {
using Kinds = std::map<uint32_t,uint32_t>;
// Caller supplies its existing records lock. Tokens identify COMPLETE maps,
// independently of ShaderInfo addresses or the order bindings were discovered.
// No guest shader inherits another shader's observations through this table.
class SemanticTokens {
    std::map<Kinds,uint64_t> tokens_;
    uint64_t next_ = 0;
public:
    static constexpr size_t kMaxMaps=4096,kMaxBindings=256;
    uint64_t intern(const Kinds& kinds) {
        if(kinds.empty())return 0;
        if(auto it=tokens_.find(kinds);it!=tokens_.end())return it->second;
        if(tokens_.size()>=kMaxMaps || kinds.size()>kMaxBindings)
            throw std::length_error("integer texture variant identity bound exceeded");
        for(auto [binding,kind]:kinds)if(kind!=1 && kind!=2)
            throw std::invalid_argument("invalid integer texture kind");
        if(next_==UINT64_MAX)throw std::overflow_error("integer texture variant identities exhausted");
        const auto token=next_+1;
        tokens_.emplace(kinds,token);next_=token;return token;
    }
    size_t size() const noexcept {return tokens_.size();}
};
}
