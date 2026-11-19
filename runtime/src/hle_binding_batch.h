#pragma once
#include <nx/runtime.h>
#include <set>
#include <span>
#include <string>

namespace nx {
// Startup only. Copy/validate before publication: no partially changed registry
// on allocation, duplicate, phase or cached-thunk failure. The caller owns the
// startup exclusion; no guest or concurrent registry reader may execute here.
template<class Registry,class Cached>
bool replace_hle_batch(Registry& registry,std::span<const HleBinding> bindings,
                       bool before_relocations,Cached&& cached) {
    if(!before_relocations || bindings.empty())return false;
    std::set<std::string> names;
    for(const auto& binding:bindings) {
        if(!binding.name || !*binding.name || !binding.function ||
           !names.emplace(binding.name).second || cached(binding.name))return false;
    }
    auto replacement=registry;
    for(const auto& binding:bindings)replacement[binding.name]=binding.function;
    registry.swap(replacement);
    return true;
}
}
