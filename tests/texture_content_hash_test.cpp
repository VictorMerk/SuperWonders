#include "texture_content_hash.h"
#include <cstdio>
#include <vector>

int main() {
    using nx::nvn::texture_content_hash;
    for (unsigned offset=0;offset<16;++offset) for (unsigned size=1;size<=257;++size) {
        std::vector<uint8_t> data(size+32);
        for (unsigned i=0;i<data.size();++i) data[i]=uint8_t(i*73+19);
        const auto original=texture_content_hash(data.data()+offset,size);
        for (unsigned i=0;i<size;++i) {
            data[offset+i]^=0x80;
            if (texture_content_hash(data.data()+offset,size)==original) {
                std::fprintf(stderr,"missed texture mutation: offset=%u size=%u byte=%u\n",offset,size,i);
                return 1;
            }
            data[offset+i]^=0x80;
        }
    }
    texture_content_hash(nullptr,0);
    const uint64_t seed=0x9E3779B97F4A7C15ull ^ 3ull ^ 10ull ^ 21ull,prime=0x100000001B3ull;
    uint64_t inverse=1;
    for(unsigned i=0;i<6;++i) inverse*=2-prime*inverse;
    uint64_t word=seed ^ ((0-(seed>>29))*inverse),previous=99;
    bool valid=false;
    const auto zero=texture_content_hash(reinterpret_cast<const uint8_t*>(&word),8);
    if(zero!=0 || nx::nvn::update_texture_content_hash(zero,previous,valid) || !valid) return 2;
    word^=1;
    if(!nx::nvn::update_texture_content_hash(texture_content_hash(reinterpret_cast<const uint8_t*>(&word),8),previous,valid)) return 3;
    std::puts("texture dirty hash: all bytes detected across short, tail and unaligned ranges");
}
