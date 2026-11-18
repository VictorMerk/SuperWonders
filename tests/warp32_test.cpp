#include "nvn_spirv_warp.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main(int argc, char** argv) {
    using nx::nvn::patch_warp32;
    check(argc >= 2, "usage: warp32_test input.spv [patched.spv]");
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    check(bool(file), "open shader");
    auto size = file.tellg();
    check(size >= 20 && size % 4 == 0, "SPIR-V file size");
    std::vector<uint32_t> code(size_t(size) / 4), patched;
    file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), size);
    const auto original = code;
    check(patch_warp32(code, patched), "shader must contain supported subgroup operations");
    check(code == original && patched.size() > code.size() && patched[3] > code[3], "patch preserves input and allocates IDs");
    std::vector<uint32_t> twice{456};
    check(!patch_warp32(patched, twice) && twice == std::vector<uint32_t>{456}, "patch is idempotent");
    // Truncated instructions and non-SPIR-V input must not replace an existing output buffer.
    auto truncated = code; truncated.push_back(0xffff0000);  // instruction extends past EOF
    std::vector<uint32_t> sentinel{123};
    check(!patch_warp32(truncated, sentinel) && sentinel == std::vector<uint32_t>{123}, "reject malformed input");
    check(!patch_warp32({0, 0, 0, 0, 0}, sentinel), "reject wrong magic");
    check(!patch_warp32({0x07230203, 0x00010300, 0, 1, 0}, sentinel), "no operations is a no-op");
    if (argc > 2) {
        std::ofstream out(argv[2], std::ios::binary);
        out.write(reinterpret_cast<const char*>(patched.data()), patched.size() * 4);
        check(bool(out), "write patched shader");
    }
    std::printf("warp32 patch passed: %zu -> %zu words\n", code.size(), patched.size());
}
