#include "texture_upload_bounds.h"
#include <cstdlib>
#include <cstdio>
static void check(bool ok) { if(!ok) std::abort(); }
int main() {
    using namespace nx::nvn;
    const uint32_t big=std::numeric_limits<uint32_t>::max();
    TextureCopyShape s{128,4,4,1,1,0,0,1,1,4};
    check(texture_copy_end(s,128)==64);
    s.offset=127;check(!texture_copy_end(s,128));s.offset=128;
    s.width=big;check(texture_copy_end(s,128)==uint64_t(big)*4*4);
    s.height=s.depth=s.layers=big;check(!texture_copy_end(s,128));
    s={0,5,5,1,2,8,8,4,4,16};check(texture_copy_end(s,0)==128);
    s.row_length=1;check(!texture_copy_end(s,0));s.row_length=8;s.block_width=0;check(!texture_copy_end(s,0));
    check(astc_level_bytes(1,1,2,4,4,16)==32);
    check(texture_span_fits(8,32,40) && !texture_span_fits(9,32,40));
    check(!texture_span_fits(std::numeric_limits<uint64_t>::max(),32,40));
    check(!astc_level_bytes(big,big,big,1,1,16));
    check(!astc_level_bytes(4,4,2,4,4,std::numeric_limits<uint64_t>::max()));
    check(!astc_level_bytes(4,4,1,0,4,16));
    // A pitched R8 atlas may not read beyond a short row or the last layer.
    check(!texture_copy_end({0,33,2,1,1,32,0,1,1,1},0));
    auto atlas=texture_copy_end({0,31,2,1,2,32,0,1,1,1},0);
    check(atlas==127 && texture_span_fits(0,*atlas,127) && !texture_span_fits(0,*atlas,126));
    std::puts("upload bounds: underflow, overflow, padded compressed slices and ASTC source bounds pass");
}
