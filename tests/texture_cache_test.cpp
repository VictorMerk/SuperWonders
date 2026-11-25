#include "texture_cache.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>

void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
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
    {
        std::fstream corrupt(folder / request.name, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(-1, std::ios::end); corrupt.put(0);
    }
    check(!cache.load(request, source, loaded), "corrupt output checksum falls back to real conversion");
    check(cache.store(request, source, converted), "damaged entry can be replaced with a complete conversion");
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
