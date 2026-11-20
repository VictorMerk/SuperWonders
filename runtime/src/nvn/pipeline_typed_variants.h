#pragma once
#include "integer_texture_variants.h"
#include <array>
#include <span>
#include <vector>

namespace nx::nvn::typed_pipeline {
using Bytes=std::vector<uint8_t>;
struct ShaderVariant {
    uint32_t stage=0, shared=0;
    std::array<uint32_t,3> local{1,1,1};
    std::array<char,40> spirv_sha1{};
    Kinds kinds;
    bool operator==(const ShaderVariant&) const = default;
};
struct Variant {
    ShaderVariant vs,fs;
    bool operator==(const Variant&) const = default;
};
struct Format {uint32_t vendor=0,device=0,recipe_size=0;};
constexpr size_t kMaxVariants=4096,kMaxBindings=32,kMaxBytes=16u<<20,kMaxRecipeBytes=2048;
class Catalog {
    std::map<Bytes,std::vector<Variant>> records_;
    size_t size_=0;
public:
    bool add(const Bytes& exact_recipe,const Variant&);
    const std::vector<Variant>* find(const Bytes& exact_recipe) const;
    size_t size() const noexcept {return size_;}
    const auto& records() const noexcept {return records_;}
};
// Endian-defined, bounded and transactional. Any corruption leaves `out`
// unchanged. The legacy recipe file is a separate input and is never rewritten.
bool encode(const Catalog&,Format,Bytes& out);
bool decode(std::span<const uint8_t>,Format,Catalog& out);
bool valid(const ShaderVariant&,bool vertex);
bool same_source(const ShaderVariant&,const ShaderVariant&) noexcept;
}
