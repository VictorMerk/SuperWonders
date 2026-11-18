#include "nvn/pipeline_key.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace nx::nvn;
static void check(bool pass, const char* why) {
    if (!pass) { std::fprintf(stderr,"%s\n",why);std::exit(1); }
}
static PipelineKey canonical(PipelineKey key) { canonicalize_pipeline_key(key); return key; }
static bool same(const PipelineKey& a, const PipelineKey& b) { return std::memcmp(&a,&b,sizeof a)==0; }
int main() {
    DepthStencilInfo decoded;
    unsigned char zero[sizeof decoded] = {};
    check(std::memcmp(&decoded,zero,sizeof decoded)==0,"default decoded depth state must initialize every recorded byte");
    PipelineKey base;std::memset(&base,0,sizeof base);
    base.color_count=1;base.mask.mask[0]=15;base.color_formats[0]=VK_FORMAT_R8G8B8A8_UNORM;
    base.depth.depth_test=base.depth.stencil_test=1;base.depth.depth_func=3;
    base.depth.stencil_func[0]=2;base.depth.stencil_func[1]=4;
    base.depth.stencil_fail[0]=1;base.depth.stencil_fail[1]=3;
    base.depth.depth_fail[0]=5;base.depth.depth_fail[1]=7;
    base.depth.depth_pass[0]=2;base.depth.depth_pass[1]=6;
    const auto reference=canonical(base);
    for (unsigned old_padding=0;old_padding<256;++old_padding) {
        auto legacy=base;legacy.depth.reserved=uint8_t(old_padding);
        check(same(reference,canonical(legacy)),"all legacy padding values must share one pipeline identity");
    }
    auto changed=base;changed.depth.depth_test=0;
    check(!same(reference,canonical(changed)),"depth enable must remain distinct");
    changed=base;changed.depth.depth_write=1;
    check(!same(reference,canonical(changed)),"depth write/bias state must remain distinct");
    changed=base;changed.depth.depth_func=4;
    check(!same(reference,canonical(changed)),"depth comparison must remain distinct");
    changed=base;changed.depth.stencil_test=0;
    check(!same(reference,canonical(changed)),"stencil enable must remain distinct");
    for (unsigned face=0;face<2;++face) for (unsigned field=0;field<4;++field) {
        changed=base;
        uint32_t* fields[]={changed.depth.stencil_func,changed.depth.stencil_fail,changed.depth.depth_fail,changed.depth.depth_pass};
        fields[field][face]^=1;
        check(!same(reference,canonical(changed)),"all live stencil operations must remain distinct");
    }
    check(same(reference,canonical(reference)),"canonical key must be idempotent");
    std::puts("legacy depth padding: all 256 values merge, while live depth/stencil state survives");
}
