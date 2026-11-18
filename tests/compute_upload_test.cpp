#include "nvn/overlapping_regions.h"
#include "nvn/upload_snapshot.h"
#include "nvn/write_back_words.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <vector>

static void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr,"compute upload: %s\n",why); std::exit(1); }
}
int main() {
    std::mt19937 rng(0x534e4150);
    // Snapshot bytes must equal the upload, including partial/misaligned tails;
    // later guest changes survive where the simulated GPU left words alone.
    for (size_t n=0;n<=4097;++n) {
        std::vector<uint8_t> guest(n+4), upload(n+64,0xcd);
        for (auto& b:guest) b=uint8_t(rng());
        const auto original=guest;
        auto before=nx::nvn::snapshot_upload(upload.data()+1,guest.data()+1,n,n+32);
        require(std::equal(before->begin(),before->end(),original.begin()+1),"snapshot differs from guest input");
        require(std::equal(before->begin(),before->end(),upload.begin()+1),"upload differs from snapshot");
        require(std::all_of(upload.begin()+1+n,upload.begin()+1+n+32,[](auto v){return v==0;}),"padding not cleared");
        require(upload.front()==0xcd && upload[n+33]==0xcd,"upload crossed bounds");
        std::fill(guest.begin(),guest.end(),0xa5);
        nx::nvn::copy_changed_words(guest.data()+1,upload.data()+1,before->data(),n);
        require(std::all_of(guest.begin(),guest.end(),[](auto v){return v==0xa5;}),"unchanged GPU output overwrote later guest writes");
    }
    // Scratch uploads must preserve missing-source zero semantics and never
    // leave the descriptor's padded tail carrying older ring contents.
    for (size_t n=0;n<=4097;++n) for (bool present:{false,true}) {
        std::vector<uint8_t> source(n+4,0x5a), actual(n+64,0xcd), expected=actual;
        const size_t padded=n+32;
        std::memset(expected.data()+1,0,padded);
        if (present && n) std::memcpy(expected.data()+1,source.data()+1,n);
        nx::nvn::initialize_upload(actual.data()+1,present ? source.data()+1 : nullptr,n,padded);
        require(actual==expected,"scratch upload differs from full-zero-then-copy semantics");
    }
    struct Region { size_t size; };
    std::vector<uint8_t> bytes(8192);
    std::map<const uint8_t*,Region> regions;
    for (size_t at=0;at<4096;) {
        size_t n=1+rng()%97;
        regions.emplace(bytes.data()+at,Region{n}); at+=n+1+rng()%31;
    }
    for (unsigned trial=0;trial<20000;++trial) {
        size_t start=rng()%5000, n=1+rng()%1000;
        auto [first,last]=nx::nvn::overlapping_regions(regions,bytes.data()+start,n);
        std::vector<const uint8_t*> actual, expected;
        for (auto it=first;it!=last;++it) actual.push_back(it->first);
        for (const auto& [p,r]:regions)
            if (p<bytes.data()+start+n && p+r.size>bytes.data()+start) expected.push_back(p);
        require(actual==expected,"range query missed a live region or included a touching region");
        auto copy=regions;
        auto erase=nx::nvn::overlapping_regions(copy,bytes.data()+start,n);
        copy.erase(erase.first,erase.second);
        require(copy.size()+expected.size()==regions.size(),"range erase removed the wrong entries");
    }
    std::puts("compute upload: independent snapshots, protected guest writes, padding and 20000 overlap/erase cases pass");
}
