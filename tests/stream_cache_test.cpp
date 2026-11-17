#include "stream_cache.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

void check(bool ok, const char* text) { if (!ok) { std::fprintf(stderr,"%s\n",text); std::exit(1); } }
int main() {
    namespace fs = std::filesystem;
    using namespace nx::streaming;
    check(eligible_music("content://Sound/Resource/Stream/BGM_Test.bwav") &&
          eligible_music("content://Sound/Resource/Stream/Env_Grassland.bwav") &&
          !eligible_music("save:/Sound/Resource/Stream/BGM_Test.bwav") &&
          !eligible_music("content:/Model/BGM_Test.bwav") && !eligible_music("content:/Sound/Resource/Stream/SE_Test.bwav"),
          "prefetch is limited to immutable content music streams");
    const auto root = fs::temp_directory_path() / ("wonder-stream-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto first = root / fs::path(u8"music-ä.bwav"), second = root / "second.bwav";
    std::vector<uint8_t> source(65536);
    for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 13);
    auto write = [&](const fs::path& path, const std::vector<uint8_t>& bytes) {
        std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    write(first,source); write(second,source);
    MusicCache cache(65536,65536,2,1);
    auto token = cache.open(first);
    check(bool(token), "a bounded music file can prefetch without making a disk copy");
    auto await = [&](const MusicCache::Token& entry) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        uint8_t value; uint64_t read = 0;
        while (!cache.read(entry,0,&value,1,read) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return read == 1;
    };
    check(await(token), "prefetch publishes a complete snapshot");
    std::vector<uint8_t> output(source.size()); uint64_t read = 0;
    check(cache.read(token,0,output.data(),output.size(),read) && read == output.size() && output == source,
          "all prefetched bytes match the file exactly");
    check(cache.read(token,65530,output.data(),16,read) && read == 6 &&
          std::equal(output.begin(),output.begin()+6,source.end()-6), "EOF clips reads correctly");
    check(cache.read(token,70000,output.data(),16,read) && read == 0, "reads beyond EOF return zero");
    check(cache.read(token,0,nullptr,0,read) && read == 0, "zero-byte reads are valid");
    check(!cache.read(token,-1,output.data(),1,read) &&
          !cache.read(token,std::numeric_limits<int64_t>::max(),output.data(),2,read) &&
          !cache.read(token,0,nullptr,1,read), "invalid ranges/buffers leave the normal fallback in control");
    check(!cache.open(second) && cache.stats().reserved_bytes <= 65536, "active handles cannot exceed the total byte budget");
    token.reset();
    MusicCache::Token replacement;
    for (int i=0;i<100 && !replacement;++i) { replacement=cache.open(second); if(!replacement) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    check(replacement && await(replacement) && cache.stats().entries == 1, "inactive snapshots can be evicted for a new stream");
    source[0] = 77; write(second,source);
    fs::last_write_time(second,fs::last_write_time(second)+std::chrono::seconds(1));
    check(!cache.open(second), "a changed file never reuses a stale active snapshot for a new open");
    replacement.reset();
    replacement=cache.open(second);
    check(replacement && await(replacement) && cache.read(replacement,0,output.data(),1,read) && output[0] == 77,
          "reopened modified content gets a fresh complete snapshot");
    cache.stop();
    check(!cache.open(first), "shutdown rejects new background work");
    check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
          root.filename().string().starts_with("wonder-stream-cache-test-"), "cleanup stays in the owned test directory");
    fs::remove_all(root);
    std::puts("stream prefetch: exact bytes, EOF, invalid ranges, quotas, eviction, metadata and shutdown passed");
}
