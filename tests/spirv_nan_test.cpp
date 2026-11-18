#include "nvn_spirv_nan.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main(int argc, char** argv) {
    using nx::nvn::patch_nan_minmax;
    std::vector<uint32_t> code{0x07230203, 0x10000, 0, 32, 0,
        (6u << 16) | 11, 1, 0x4c534c47, 0x6474732e, 0x3035342e, 0,
        (7u << 16) | 12, 2, 3, 1, 37, 4, 5,
        (7u << 16) | 12, 2, 6, 1, 40, 4, 5,
        (8u << 16) | 12, 2, 7, 1, 43, 4, 5, 8,
        (7u << 16) | 12, 2, 9, 10, 37, 4, 5};
    const auto original = code;
    std::vector<uint32_t> patched;
    check(patch_nan_minmax(code, patched), "patch legacy float operations");
    check(code == original && patched.size() == code.size() && patched[3] == code[3], "preserve input and IDs");
    check(patched[15] == 79 && patched[22] == 80 && patched[29] == 81, "use numeric NaN semantics");
    check(patched[37] == 37, "leave another instruction set alone");
    std::vector<uint32_t> sentinel{123};
    check(!patch_nan_minmax(patched, sentinel) && sentinel == std::vector<uint32_t>{123}, "idempotent");
    auto broken = code; broken.push_back(0xffff0000);
    check(!patch_nan_minmax(broken, sentinel) && sentinel == std::vector<uint32_t>{123}, "reject truncation atomically");
    broken = code; broken[5] = 11;
    check(!patch_nan_minmax(broken, sentinel), "reject zero length instruction");
    check(!patch_nan_minmax({0, 0, 0, 1, 0}, sentinel), "reject wrong magic");
    if (argc == 3) {
        std::ifstream f(argv[1], std::ios::binary | std::ios::ate);
        check(bool(f) && f.tellg() >= 20 && f.tellg() % 4 == 0, "read real shader");
        code.resize(size_t(f.tellg()) / 4); f.seekg(0);
        f.read(reinterpret_cast<char*>(code.data()), code.size() * 4);
        check(patch_nan_minmax(code, patched), "real shader contains legacy float operations");
        std::ofstream output(argv[2], std::ios::binary);
        output.write(reinterpret_cast<const char*>(patched.data()), patched.size() * 4);
        check(bool(output), "write real patched shader for spirv-val");
    }
    std::puts("SPIR-V numeric min/max/clamp patch: PASS");
}
