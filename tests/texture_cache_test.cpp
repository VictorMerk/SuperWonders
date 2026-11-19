#include "texture_cache.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <cstring>
#include <iterator>

void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
uint64_t reference_hash(std::span<const uint8_t> bytes,uint64_t hash=0xcbf29ce484222325ull) {
    size_t at=0;
    for(;at+8<=bytes.size();at+=8) {
        uint64_t word;std::memcpy(&word,bytes.data()+at,8);
        hash^=word;hash*=0x9e3779b97f4a7c15ull;hash=(hash<<27)|(hash>>37);
    }
    for(;at<bytes.size();++at) hash=(hash^bytes[at])*0x100000001b3ull;
    return hash^(hash>>29);
}
int main() {
    using namespace nx::nvn;
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("wonder-texture-cache-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto folder = root / fs::path(u8"cache-ä");
    std::vector<uint8_t> source(16, 7), converted(16, 32), loaded{99};
    LinearLevel level{}; level.width = level.height = 4;
    level.width_blocks = level.height_blocks = 1; level.layer_size = 16;
    std::vector<LinearLevel> levels{level};
    const auto request = texture_cache_request(0x79, 1, source, levels);
    for(size_t size=16;size<=4097;++size) {
        std::vector<uint8_t> pattern(size);
        for(size_t i=0;i<size;++i) pattern[i]=uint8_t(i*37+size);
        const auto r=texture_cache_request(0x79,1,pattern,levels);
        const auto a=reference_hash(pattern,reference_hash(r.metadata));
        const auto b=reference_hash(pattern,reference_hash(r.metadata,0x84222325cbf29ce4ull));
        char expected[40];std::snprintf(expected,sizeof expected,"%016llx%016llx.bin",(unsigned long long)a,(unsigned long long)b);
        check(r.name==expected,"single-pass dual fingerprint preserves existing cache names, including tails");
    }
    check(request.name.size() == 36 && request.output_bytes == 16, "deterministic bounded BC3 request");
    TextureCache cache(folder);
    check(!cache.load(request, source, loaded) && loaded == std::vector<uint8_t>{99}, "miss preserves original conversion path");
    check(cache.store(request, source, converted), "Unicode cache file stored atomically");
    check(cache.load(request, source, loaded) && loaded == converted, "cached conversion is byte-identical");
    TextureCache restart(folder);
    check(restart.load(request, source, loaded) && loaded == converted, "a second process-style index reuses the file");
    auto modified = source; modified[7]++;
    auto collision = texture_cache_request(0x79, 1, modified, levels); collision.name = request.name;
    check(!cache.load(collision, modified, loaded), "even a forced fingerprint collision cannot reuse different source bytes");
    auto srgb = texture_cache_request(0x87, 1, source, levels); srgb.name = request.name;
    check(!cache.load(srgb, source, loaded), "different decode metadata cannot reuse an entry");
    auto longer=source;longer.push_back(1);
    auto length_collision=texture_cache_request(0x79,1,longer,levels);length_collision.name=request.name;
    check(!cache.load(length_collision,longer,loaded) && cache.stats().invalid_files==0,
          "a valid different-length collision must not poison the original cache entry");
    check(cache.load(request,source,loaded),"valid colliding request remains usable");
    std::vector<char> original;
    { std::ifstream file(folder/request.name,std::ios::binary);original.assign(std::istreambuf_iterator<char>(file),{}); }
    {
        std::fstream corrupt(folder / request.name, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(-1, std::ios::end); corrupt.put(0);
    }
    check(!cache.load(request, source, loaded), "corrupt output checksum falls back to real conversion");
    check(cache.stats().invalid_files==1 && cache.stats().bytes==original.size(),"corrupt data is skipped but remains counted against quota");
    { std::ofstream file(folder/request.name,std::ios::binary|std::ios::trunc);file.write(original.data(),original.size()); }
    check(!cache.load(request,source,loaded),"known corrupt record bypasses repeat filesystem reads for this index generation");
    check(cache.store(request, source, converted), "damaged entry can be replaced with a complete conversion");
    check(cache.load(request,source,loaded) && cache.stats().invalid_files==0,"successful store repairs the negative entry");
    fs::resize_file(folder / request.name, 40);
    check(!cache.load(request, source, loaded), "truncated record is never used");
    fs::create_directories(root / "budget");
    { std::ofstream note(root / "budget/keep.txt"); note << "unrelated file"; }
    TextureCache small(root / "budget", 150);
    check(small.store(request, source, converted), "first budgeted entry");
    const auto other = texture_cache_request(0x79, 1, modified, levels);
    check(small.store(other, modified, converted), "second budgeted entry triggers bounded eviction");
    uint64_t bytes = 0; size_t records = 0;
    for (const auto& entry : fs::directory_iterator(root / "budget")) if (entry.path().extension() == ".bin") bytes += entry.file_size(), ++records;
    check(bytes <= 150 && records == 1 && fs::exists(root / "budget/keep.txt"), "eviction only removes owned regular cache files");
    TextureCache count_limited(root / "count", 1 << 20, 1);
    check(count_limited.store(request, source, converted) && count_limited.store(other, modified, converted), "file count is bounded independently of byte quota");
    records = 0;
    for (const auto& entry : fs::directory_iterator(root / "count")) if (entry.path().extension() == ".bin") ++records;
    check(records == 1, "many tiny textures cannot grow an unbounded index");
    TextureCache reconcile(root/"reconcile",1<<20,2);
    check(reconcile.store(request,source,converted) && reconcile.store(other,modified,converted),"two indexed entries");
    fs::remove(root/"reconcile"/request.name);
    auto third_source=source;third_source[0]++;
    const auto third=texture_cache_request(0x79,1,third_source,levels);
    check(reconcile.store(third,third_source,converted) && reconcile.stats().files==2,
          "complete quota rescan removes stale externally deleted entries from the index");
    fs::remove(root/"reconcile"/third.name);
    check(!reconcile.load(third,third_source,loaded) && reconcile.stats().files==1,"missing lookup removes stale bookkeeping");
    auto bad = request; bad.name = "../outside.bin";
    check(!small.store(bad, source, converted) && !small.load(bad, source, loaded), "cache names cannot escape the root");
    { std::ofstream obstacle(root / "not-a-directory"); obstacle << "keep"; }
    TextureCache unwritable(root / "not-a-directory/child");
    check(!unwritable.store(request, source, converted), "filesystem failure does not become a game failure");
    levels[0].width = std::numeric_limits<uint32_t>::max();
    check(texture_cache_request(0x79, 1, source, levels).name.empty(), "unbounded dimensions are not cacheable");
    check(fs::weakly_canonical(root).parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
          root.filename().string().starts_with("wonder-texture-cache-test-"), "cleanup is confined to this temporary test folder");
    fs::remove_all(root);
    std::puts("texture cache: restart, exact source, metadata, corruption, truncation, quota, paths and I/O failure passed");
}
