// Older prepared shader packs predate the translator's spirv-nan.patch.
// Maxwell .SAT and FMNMX choose the numeric operand when the other is NaN.
// GLSL FMin/FMax/FClamp leave that result undefined; NMin/NMax/NClamp preserve
// the guest behavior on AMD as well as NVIDIA, without rebuilding game data.
#pragma once
#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace nx::nvn {
inline bool patch_nan_minmax(const std::vector<uint32_t>& in, std::vector<uint32_t>& out) {
    if (in.size() < 5 || in[0] != 0x07230203 || !in[3]) return false;
    std::set<uint32_t> glsl;
    constexpr char name[] = "GLSL.std.450";
    for (size_t i = 5; i < in.size();) {
        const uint32_t n = in[i] >> 16, op = in[i] & 0xffff;
        if (!n || n > in.size() - i) return false;
        if (op == 11 && n == 2 + (sizeof name + 3) / 4 &&
            std::memcmp(in.data() + i + 2, name, sizeof name) == 0) glsl.insert(in[i + 1]);
        i += n;
    }
    auto patched = in;
    bool changed = false;
    for (size_t i = 5; i < in.size(); i += in[i] >> 16) {
        const uint32_t n = in[i] >> 16, op = in[i] & 0xffff;
        if (op != 12 || n < 7 || !glsl.count(in[i + 3])) continue;
        uint32_t instruction = in[i + 4], replacement = 0;
        if (instruction == 37 && n == 7) replacement = 79;  // FMin -> NMin
        if (instruction == 40 && n == 7) replacement = 80;  // FMax -> NMax
        if (instruction == 43 && n == 8) replacement = 81;  // FClamp -> NClamp
        if (replacement) patched[i + 4] = replacement, changed = true;
    }
    if (changed) out = std::move(patched);
    return changed;
}
}  // namespace nx::nvn
