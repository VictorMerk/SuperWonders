#include "nvn_tiling.h"
#include <cstdlib>
#include <cstdio>
#include <limits>
static void check(bool ok) { if(!ok) std::abort(); }
int main() {
    using namespace nx::nvn;
    const auto huge=std::numeric_limits<uint32_t>::max();
    check(block_linear_layout(4,4,1,33,1,1,4).levels.empty());
    check(block_linear_layout(4,4,1,huge,1,1,4).levels.empty());
    check(block_linear_layout(4,4,1,32,1,1,4).levels.size()==32);
    check(block_linear_size(huge,huge,huge,huge,32,32)==0);
    check(block_linear_layout(huge,huge,huge,1,1,1,huge).levels.empty());
    check(block_linear_layout(1,1,1,1,1,1,4).tiled_size==512);
    uint8_t source=1;std::vector<uint8_t> output{99};
    check(deswizzle_texture(&source,1,4,4,1,33,1,1,4,output).empty() && output==std::vector<uint8_t>{99});
    std::puts("tiling bounds: excessive mip shifts, overflowing layouts and rejected-output preservation pass");
}
