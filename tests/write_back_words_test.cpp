#include "nvn/write_back_words.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

static void legacy(uint8_t* to,const uint8_t* from,const uint8_t* was,size_t size) {
    size_t k=0;
    for (;k+8<=size;k+=8) {
        if (!std::memcmp(from+k,was+k,8)) continue;
        for (size_t b=k;b<k+8;b+=4)
            if (std::memcmp(from+b,was+b,4)) std::memcpy(to+b,from+b,4);
    }
    for (;k<size;++k) if (from[k]!=was[k]) to[k]=from[k];
}
int main() {
    std::mt19937 rng(0x5742494f);
    for (size_t size=0;size<=4097;++size) for (unsigned offset=0;offset<4;++offset) {
        std::vector<uint8_t> before(size+8),gpu(size+8),guest(size+8);
        for (auto& b:before) b=uint8_t(rng());
        gpu=before;
        for (size_t i=0;i<size;++i) if ((rng()&31)==0) gpu[i+offset]^=uint8_t(1+rng()%255);
        for (auto& b:guest) b=uint8_t(rng());  // guest writes after recording must survive where the GPU did not write
        auto expected=guest,actual=guest;
        legacy(expected.data()+offset,gpu.data()+offset,before.data()+offset,size);
        nx::nvn::copy_changed_words(actual.data()+offset,gpu.data()+offset,before.data()+offset,size);
        if (actual!=expected) {std::fprintf(stderr,"write-back mismatch: size=%zu offset=%u\n",size,offset);return 1;}
    }
    std::puts("write-back: wider reads preserve legacy changed-word, tail-byte and untouched-guest semantics");
}
