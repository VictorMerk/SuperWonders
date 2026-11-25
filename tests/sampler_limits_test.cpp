#include "sampler_limits.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool pass) { if(!pass) std::abort(); }
int main() {
    using nx::nvn::sampler_lod;
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    auto a=sampler_lod(5,-1000,1000,6,16,true,2,4);
    check(a.bias==2 && a.minimum==0 && a.maximum==1000 && a.anisotropy==4);
    a=sampler_lod(5,8,2,-6,16,false,2,16);
    check(a.bias==-2 && a.minimum==8 && a.maximum==8 && a.anisotropy==1);
    a=sampler_lod(1,8,32,0,1,true,2,16);
    check(a.minimum==0 && a.maximum==0.25f);
    for(float min:{nan,inf,-inf,-2.0f,0.0f,4.0f})
    for(float max:{nan,inf,-inf,-2.0f,0.0f,4.0f})
    for(float bias:{nan,inf,-inf,-8.0f,8.0f})
    for(float aniso:{nan,inf,-inf,-8.0f,32.0f}) {
        a=sampler_lod(5,min,max,bias,aniso,true,2,4);
        check(std::isfinite(a.bias) && a.bias>=-2 && a.bias<=2);
        check(std::isfinite(a.minimum) && a.minimum>=0 && a.maximum>=a.minimum);
        check(std::isfinite(a.maximum) && a.anisotropy>=1 && a.anisotropy<=4);
    }
    std::puts("sampler limits: device caps, no-mip filtering, reversed intervals and nonfinite inputs pass");
}
