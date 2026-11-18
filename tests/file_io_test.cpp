#include "file_io.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>

void check(bool ok,const char* message) {
    if (!ok) { std::fprintf(stderr,"%s\n",message); std::exit(1); }
}
int main() {
    using namespace nx::file_io;
    FILE* file = std::tmpfile(); check(file!=nullptr,"temporary file");
    std::array<uint8_t,4096> initial{};
    initial.fill(0x5a);
    check(write_at(file,0,initial.data(),initial.size()),"initial contents");
    std::array<uint8_t,32> data{};
    check(read_at(file,1024,data.data(),data.size())==data.size(),"positional read");
    const auto position = std::ftell(file);
    check(size(file)==4096 && std::ftell(file)==position,"size never changes the stream position");
    check(read_at(file,-1,data.data(),1)==0 && !write_at(file,-1,data.data(),1),"negative offsets rejected");
    check(write_at(file,0,nullptr,0) && read_at(file,0,nullptr,0)==0,"zero-length operations accept no buffer");
    check(!write_at(file,0,nullptr,1) && read_at(file,0,nullptr,1)==0,"nonempty operations require a buffer");
    check(read_at(file,1,data.data(),UINT64_MAX)==0 && !write_at(file,INT64_MAX,data.data(),2),"overflowing ranges rejected");
    std::atomic<bool> ok{true};
    auto writer = [&] {
        std::array<uint8_t,32> bytes{};
        for (unsigned i = 0; i < 3000; ++i) {
            bytes.fill(uint8_t(i));
            if (!write_at(file,128,bytes.data(),bytes.size())) ok = false;
        }
    };
    auto reader = [&] {
        std::array<uint8_t,32> bytes{};
        for (unsigned i = 0; i < 3000; ++i) {
            if (read_at(file,2048,bytes.data(),bytes.size())!=bytes.size()) ok = false;
            for (auto b : bytes) if (b != 0x5a) ok = false;
        }
    };
    auto statter = [&] { for (unsigned i = 0; i < 6000; ++i) if (size(file)!=4096) ok = false; };
    std::thread a(writer),b(reader),c(statter);
    a.join(); b.join(); c.join();
    check(ok,"parallel reads/writes/size queries preserve disjoint data and length");
    std::array<uint8_t,4096> final{};
    check(read_at(file,0,final.data(),final.size())==final.size(),"final full read");
    for (size_t i = 0; i < final.size(); ++i)
        check(final[i] == (i >= 128 && i < 160 ? uint8_t(2999) : 0x5a),"writes stay at the requested offset");
    check(std::fclose(file)==0,"temporary file cleanup");
    std::puts("concurrent file I/O tests passed");
}
