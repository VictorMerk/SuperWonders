#include "present_geometry.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
int main() {
    using namespace nx::nvn;
    auto box = fit_picture(1920,1080,2560,1080,true);
    check(box.x == 320 && box.y == 0 && box.width == 1920 && box.height == 1080, "ultrawide is pillarboxed");
    box = fit_picture(1920,1080,1280,1024,true);
    check(box.x == 0 && box.y == 152 && box.width == 1280 && box.height == 720, "taller output is letterboxed");
    box = fit_picture(1280,720,1920,1080,true);
    check(box.x == 0 && box.y == 0 && box.width == 1920 && box.height == 1080, "matching aspect preserves full area");
    box = fit_picture(1920,1080,2560,1080,false);
    check(box.x == 0 && box.width == 2560 && box.height == 1080, "stretch remains selectable");
    box = fit_picture(1920,1080,1,1,true);
    check(box.width == 1 && box.height == 1, "tiny nonzero outputs never produce zero-sized copies");
    for (uint32_t w : {1u,640u,1366u,1920u,3440u,UINT_MAX})
        for (uint32_t h : {1u,720u,1024u,1440u,UINT_MAX}) {
            box = fit_picture(1920,1080,w,h,true);
            check(box.width && box.height && uint64_t(box.x) + box.width <= w &&
                  uint64_t(box.y) + box.height <= h, "fit rectangle always stays in the output without integer overflow");
        }
    const int32_t valid[] = {100,50,640,360}, negative[] = {-1,0,640,360}, zero[] = {0,0,0,360};
    const int32_t overflow[] = {INT_MAX,0,INT_MAX,360}, outside[] = {1000,0,640,360};
    check(valid_picture_crop(valid,1280,720), "in-bounds crop accepted");
    check(!valid_picture_crop(negative,1280,720) && !valid_picture_crop(zero,1280,720) &&
          !valid_picture_crop(overflow,1280,720) && !valid_picture_crop(outside,1280,720) &&
          !valid_picture_crop(nullptr,1280,720), "invalid crops rejected consistently without signed overflow");
    std::puts("presentation fit, stretch, extreme sizes and crop bounds passed");
}
