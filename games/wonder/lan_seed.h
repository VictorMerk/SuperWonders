#pragma once
#include <array>
#include <cstdint>
#include <span>

namespace nx::wonder::lan_seed {
struct Site { uint64_t call; std::array<uint32_t,8> words; };
// Wonder 1.0.0: GetSystemTick immediately seeds MT-style random state here.
// These selectors must never match frame pacing, timers or polling loops.
inline constexpr Site sites[] = {
    {0x41208c,{0x946ff259,0x4a407811,0x52912ca9,0x72ad80e9,0x320003eb,0x321f03ec,0xf904767f,0x1b092e2b}},
    {0x5ad2d8,{0x946985c6,0x4a407808,0x52912ca9,0x72ad80e9,0x320003ea,0x321f03eb,0x1b092908,0x320007ec}},
    {0xa3cfd4,{0x94574687,0xaa1f03e8,0x52800029,0xb9000260,0x4a40780a,0xf109b91f,0x1b147d4a,0x8b0a010a}},
    {0xfcde24,{0x944102f3,0x4a407808,0x52912ca9,0x72ad80e9,0x320003ea,0x321f03eb,0x1b092908,0x320007ec}},
    {0x1735b70,{0x942363a0,0x4a407808,0x52912ca9,0x72ad80e9,0x320003ea,0x321f03eb,0x1b092908,0x320007ec}},
};
// Product.120: each containing function matches the original's normalized
// instruction/register structure, and all five BLs resolve to GetSystemTick.
inline constexpr Site sites_120[] = {
    {0x44b3e8,{0x9473a486,0x4a407811,0x52912ca9,0x72ad80e9,0x320003eb,0x321f03ec,0xf904767f,0x1b092e2b}},
    {0x5a99a0,{0x946e2b18,0x4a407808,0x52912ca9,0x72ad80e9,0x320003ea,0x321f03eb,0x1b092908,0x320007ec}},
    {0xa4d6c4,{0x945b9bcf,0xaa1f03e8,0x52800029,0xb9000260,0x4a40780a,0xf109b91f,0x1b147d4a,0x8b0a010a}},
    {0xfde524,{0x94455837,0x4a407808,0x52912ca9,0x72ad80e9,0x320003ea,0x321f03eb,0x1b092908,0x320007ec}},
    {0x185acc0,{0x94236650,0x4a407808,0x52912ca9,0x72ad80e9,0x320003ea,0x321f03eb,0x1b092908,0x320007ec}},
};
inline std::span<const Site> table(bool product120=false) {
    return product120 ? std::span<const Site>(sites_120) : std::span<const Site>(sites);
}
inline const Site* find(uint64_t return_offset,bool product120=false) {
    for (const auto& site:table(product120)) if (return_offset==site.call+4) return &site;
    return nullptr;
}
inline bool verified(const Site& site,std::span<const uint32_t> actual) {
    if (actual.size()!=site.words.size()) return false;
    for (size_t i=0;i<actual.size();++i) if (actual[i]!=site.words[i]) return false;
    return true;
}
inline uint64_t value(uint64_t session_seed,const Site& site) {
    // Independent deterministic streams per constructor, with no host clock,
    // heap address, thread ID or scheduling-dependent global call counter.
    uint64_t value=(session_seed^site.call)+0x9e3779b97f4a7c15ull;
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
    value=(value^(value>>27))*0x94d049bb133111ebull;
    return value^(value>>31);
}
}
