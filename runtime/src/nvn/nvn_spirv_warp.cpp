// Same logical-warp correction as the GLSL translation in VictorMerk's mkw-recomp PR #458,
// applied to Wonder's already prepared SPIR-V, without requiring a game/shader rebuild.
#include "nvn_spirv_warp.h"
#include <map>
#include <set>
#include <cstring>

namespace nx::nvn {
bool patch_warp32(const std::vector<uint32_t>& in, std::vector<uint32_t>& out) {
    if (in.size() < 5 || in[0] != 0x07230203 || !in[3] || in[3] > 0x003fffff) return false;
    struct Inst { size_t at; uint32_t op, n; };
    std::vector<Inst> instructions;
    std::set<uint32_t> uint_types;
    std::map<uint32_t, uint32_t> pointers, variables, values, types;
    std::set<uint32_t> mask_vars;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> mask_components;  // pointer -> (mask variable, word)
    uint32_t lane_var = 0;
    size_t first_function = in.size();
    size_t metadata_at = in.size();
    constexpr char marker[] = "SuperWonders warp32 v1";
    bool needs_patch = false;
    for (size_t i = 5; i < in.size();) {
        const uint32_t n = in[i] >> 16, op = in[i] & 0xffff;
        if (!n || n > in.size() - i) return false;
        const uint32_t* w = in.data() + i;
        instructions.push_back({i, op, n});
        if (op == 330 && (n - 1) * 4 >= sizeof marker && std::memcmp(w + 1, marker, sizeof marker) == 0)
            return false;  // a module is normalized once, including its injected physical-lane reads
        if (metadata_at == in.size() && ((op >= 19 && op <= 39) || (op >= 71 && op <= 75))) metadata_at = i;
        if (op == 21 && n == 4 && w[2] == 32 && w[3] == 0) uint_types.insert(w[1]);
        if (op == 32 && n == 4 && w[2] == 1) pointers[w[1]] = w[3];  // Input pointer
        if (op == 59 && n >= 4 && w[3] == 1) variables[w[2]] = w[1];
        if (op == 71 && n == 4 && w[2] == 11 && w[3] == 41) lane_var = w[1], needs_patch = true;
        if (op == 71 && n == 4 && w[2] == 11 && w[3] >= 4416 && w[3] <= 4420) mask_vars.insert(w[1]);
        if (op == 43 && n == 4) values[w[2]] = w[3], types[w[2]] = w[1];
        if ((op == 65 || op == 66) && n == 5 && mask_vars.count(w[3]) && values.count(w[4]) && values[w[4]] < 4)
            mask_components[w[2]] = {w[3], values[w[4]]};
        if (op == 54 && first_function == in.size()) first_function = i;
        if (op == 339 || op == 345) {
            if (n != (op == 339 ? 5u : 6u)) return false;
            needs_patch = true;
        }
        i += n;
    }
    if (!needs_patch || !lane_var || first_function == in.size()) return false;
    const uint32_t u32 = pointers[variables[lane_var]];
    if (!uint_types.count(u32)) return false;
    // These translated operations must have Subgroup scope (3), not Workgroup scope.
    for (const auto& inst : instructions)
        if ((inst.op == 339 || inst.op == 345) &&
            (!values.count(in[inst.at + 3]) || values[in[inst.at + 3]] != 3)) return false;

    uint32_t next_id = in[3];
    std::vector<uint32_t> constants;
    auto constant = [&](uint32_t v) {
        for (const auto& [id, value] : values)
            if (value == v && types[id] == u32) return id;
        const uint32_t id = next_id++;
        constants.insert(constants.end(), {4u << 16 | 43u, u32, id, v});
        values[id] = v, types[id] = u32;
        return id;
    };
    const uint32_t zero = constant(0), five = constant(5), low_mask = constant(31), high_mask = constant(~31u);
    std::vector<uint32_t> metadata(1 + (sizeof marker + 3) / 4);
    metadata[0] = uint32_t(metadata.size()) << 16 | 330u;
    std::memcpy(metadata.data() + 1, marker, sizeof marker);
    std::vector<uint32_t> result(in.begin(), in.begin() + 5);
    auto emit = [&](uint32_t op, std::initializer_list<uint32_t> operands) {
        result.push_back(uint32_t(operands.size() + 1) << 16 | op);
        result.insert(result.end(), operands.begin(), operands.end());
    };
    for (const auto& inst : instructions) {
        const uint32_t* w = in.data() + inst.at;
        if (inst.at == metadata_at) result.insert(result.end(), metadata.begin(), metadata.end());
        if (inst.at == first_function) result.insert(result.end(), constants.begin(), constants.end());
        if (inst.op == 339) {  // OpGroupNonUniformBallot: normalize the caller's word into .x
            const uint32_t ballot = next_id++, lane = next_id++, warp = next_id++, word = next_id++;
            emit(339, {w[1], ballot, w[3], w[4]});
            emit(61, {u32, lane, lane_var});
            emit(194, {u32, warp, lane, five});  // ShiftRightLogical
            emit(77, {u32, word, ballot, warp});  // VectorExtractDynamic
            emit(80, {w[1], w[2], word, zero, zero, zero});  // CompositeConstruct
        } else if (inst.op == 345) {  // OpGroupNonUniformShuffle: source belongs to this logical warp
            const uint32_t lane = next_id++, base = next_id++, local = next_id++, source = next_id++;
            emit(61, {u32, lane, lane_var});
            emit(199, {u32, base, lane, high_mask});
            emit(199, {u32, local, w[5], low_mask});
            emit(197, {u32, source, base, local});
            emit(345, {w[1], w[2], w[3], w[4], source});
        } else if (inst.op == 61 && inst.n >= 4 && mask_components.count(w[3])) {
            const auto [variable, component] = mask_components[w[3]];
            if (component == 0) {
                const uint32_t vector = next_id++, lane = next_id++, warp = next_id++, word = next_id++;
                emit(61, {pointers[variables[variable]], vector, variable});
                emit(61, {u32, lane, lane_var});
                emit(194, {u32, warp, lane, five});
                emit(77, {u32, word, vector, warp});
                emit(83, {w[1], w[2], word});
            } else emit(83, {w[1], w[2], zero});
        } else if (inst.op == 61 && inst.n >= 4 && (w[3] == lane_var || mask_vars.count(w[3]))) {
            const uint32_t loaded = next_id++;
            const size_t at = result.size();
            result.insert(result.end(), w, w + inst.n);
            result[at + 2] = loaded;
            if (w[3] == lane_var) {
                // S2R LaneId also exposes the guest warp's 0..31 lane, not a raw host lane.
                emit(199, {u32, w[2], loaded, low_mask});
            } else {
                const uint32_t lane = next_id++, warp = next_id++, word = next_id++;
                emit(61, {u32, lane, lane_var});
                emit(194, {u32, warp, lane, five});
                emit(77, {u32, word, loaded, warp});
                emit(80, {w[1], w[2], word, zero, zero, zero});
            }
        } else result.insert(result.end(), w, w + inst.n);
    }
    result[3] = next_id;
    out = std::move(result);
    return true;
}
}
