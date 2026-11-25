#include "texture_cache.h"
#include "cache_file.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace nx::nvn {
namespace fs = std::filesystem;
namespace {
constexpr uint64_t kMaxPart = 32ull << 20;
constexpr std::array<uint8_t,8> kMagic{'S','W','B','C','3',0,1,0};
uint64_t fingerprint(std::span<const uint8_t> bytes, uint64_t hash = 0xcbf29ce484222325ull) {
    size_t at = 0;
    for (; at + 8 <= bytes.size(); at += 8) {
        uint64_t word; std::memcpy(&word, bytes.data() + at, 8);
        hash ^= word; hash *= 0x9e3779b97f4a7c15ull;
        hash = (hash << 27) | (hash >> 37);
    }
    for (; at < bytes.size(); ++at) hash = (hash ^ bytes[at]) * 0x100000001b3ull;
    return hash ^ (hash >> 29);
}
void put(std::vector<uint8_t>& out, uint64_t value, size_t count = 8) {
    for (size_t i = 0; i < count; ++i) { out.push_back(uint8_t(value)); value >>= 8; }
}
uint64_t get(const uint8_t* bytes, size_t count = 8) {
    uint64_t value = 0;
    for (size_t i = 0; i < count; ++i) value |= uint64_t(bytes[i]) << (i * 8);
    return value;
}
bool owned_name(const std::string& name) {
    return name.size() == 36 && name.substr(32) == ".bin" &&
        std::all_of(name.begin(), name.begin() + 32, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string text(const fs::path& path) {
    auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()};
}
}

TextureCacheRequest texture_cache_request(uint32_t format, uint32_t layers,
    std::span<const uint8_t> source, std::span<const LinearLevel> levels) {
    TextureCacheRequest request;
    if (format < 0x79 || format > 0x94 || !layers || layers > 2048 || source.empty() || source.size() > kMaxPart ||
        levels.empty() || levels.size() > 32) return request;
    put(request.metadata, format, 4); put(request.metadata, layers, 4); put(request.metadata, levels.size(), 4);
    for (const auto& level : levels) {
        if (!level.width || !level.height || level.width > 16384 || level.height > 16384 ||
            level.offset > source.size() || level.layer_size > (source.size() - level.offset) / layers) return {};
        put(request.metadata, level.offset); put(request.metadata, level.layer_size); put(request.metadata, level.tiled_offset);
        put(request.metadata, level.width, 4); put(request.metadata, level.height, 4);
        put(request.metadata, level.width_blocks, 4); put(request.metadata, level.height_blocks, 4); put(request.metadata, level.block_height_gobs, 4);
        auto output = level;
        output.width_blocks = (level.width + 3) / 4; output.height_blocks = (level.height + 3) / 4;
        output.layer_size = uint64_t(output.width_blocks) * output.height_blocks * 16;
        output.offset = request.output_bytes;
        request.output_bytes += (output.layer_size * layers + 15) & ~15ull;
        if (request.output_bytes > kMaxPart) return {};
        request.output_levels.push_back(output);
    }
    const auto first = fingerprint(source, fingerprint(request.metadata));
    const auto second = fingerprint(source, fingerprint(request.metadata, 0x84222325cbf29ce4ull));
    char name[40]; std::snprintf(name, sizeof name, "%016llx%016llx.bin", (unsigned long long)first, (unsigned long long)second);
    request.name = name;
    return request;
}

TextureCache::TextureCache(fs::path directory, uint64_t bytes, size_t files) : root(std::move(directory)), budget(bytes), max_files(files) {
    std::error_code error;
    // Index only: opening every absent texture separately on a slow disk can itself stall loading.
    size_t visited = 0;
    for (fs::directory_iterator it(root, error), end; !error && it != end && visited++ < 8192; it.increment(error)) {
        const auto name = it->path().filename().string();
        if (owned_name(name) && !it->is_symlink(error) && it->is_regular_file(error)) {
            const auto size = it->file_size(error);
            if (!error) index.emplace(name, size), known_bytes += size;
        }
    }
}

bool TextureCache::load(const TextureCacheRequest& request, std::span<const uint8_t> source, std::vector<uint8_t>& output) noexcept {
    try {
        if (!owned_name(request.name) || request.metadata.size() > 2048 || source.empty() || source.size() > kMaxPart || !request.output_bytes || request.output_bytes > kMaxPart) return false;
        { std::lock_guard lock(index_lock); if (!index.contains(request.name)) return false; }
        const auto path = root / request.name;
        std::error_code error;
        const uint64_t expected = 40 + request.metadata.size() + source.size() + request.output_bytes;
        if (fs::is_symlink(path, error) || fs::file_size(path, error) != expected || error) return false;
        std::ifstream file(path, std::ios::binary);
        std::array<uint8_t,40> header{};
        if (!file.read(reinterpret_cast<char*>(header.data()), header.size()) ||
            !std::equal(kMagic.begin(), kMagic.end(), header.begin()) || get(header.data()+16) != source.size() ||
            get(header.data()+24) != request.output_bytes || get(header.data()+32) != request.metadata.size()) return false;
        std::vector<uint8_t> metadata(request.metadata.size());
        if (!file.read(reinterpret_cast<char*>(metadata.data()), metadata.size()) || metadata != request.metadata) return false;
        std::array<uint8_t,16384> compare;
        for (size_t at = 0; at < source.size();) {
            const size_t count = std::min(compare.size(), source.size() - at);
            if (!file.read(reinterpret_cast<char*>(compare.data()), count) ||
                std::memcmp(compare.data(), source.data() + at, count)) return false;
            at += count;
        }
        std::vector<uint8_t> converted(request.output_bytes);
        if (!file.read(reinterpret_cast<char*>(converted.data()), converted.size()) || fingerprint(converted) != get(header.data()+8)) return false;
        output = std::move(converted);
        return true;
    } catch (...) { return false; }  // caches must not turn memory/disk problems into game failures
}

bool TextureCache::store(const TextureCacheRequest& request, std::span<const uint8_t> source, std::span<const uint8_t> output) noexcept {
    try {
        const uint64_t bytes = 40 + request.metadata.size() + source.size() + output.size();
        if (!owned_name(request.name) || request.metadata.size() > 2048 || source.empty() || source.size() > kMaxPart || output.size() != request.output_bytes ||
            !request.output_bytes || output.size() > kMaxPart || bytes > budget || !max_files) return false;
        std::error_code error;
        fs::create_directories(root, error);
        if (error) return false;
        std::vector<uint8_t> header(kMagic.begin(), kMagic.end());
        put(header, fingerprint(output)); put(header, source.size()); put(header, output.size()); put(header, request.metadata.size());
        CacheFile pending(text(root / request.name));
        const auto pending_name = pending.path();
        std::ofstream file(fs::path(reinterpret_cast<const char8_t*>(pending_name.c_str())), std::ios::binary);
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        file.write(reinterpret_cast<const char*>(request.metadata.data()), request.metadata.size());
        file.write(reinterpret_cast<const char*>(source.data()), source.size());
        file.write(reinterpret_cast<const char*>(output.data()), output.size());
        file.close();
        if (!file || !pending.commit()) return false;
        { std::lock_guard lock(index_lock); known_bytes -= index[request.name]; index[request.name] = bytes; known_bytes += bytes; }
        trim();
        return true;
    } catch (...) { return false; }
}

void TextureCache::trim() {
    { std::lock_guard lock(index_lock); if (known_bytes <= budget && index.size() <= max_files) return; }
    struct Entry { fs::path path; uint64_t bytes; fs::file_time_type written; };
    std::vector<Entry> entries;
    std::error_code error;
    uint64_t total = 0;
    size_t visited = 0;
    for (fs::directory_iterator it(root, error), end; !error && it != end && visited++ < 8192; it.increment(error)) {
        if (!owned_name(it->path().filename().string()) || it->is_symlink(error) || !it->is_regular_file(error)) continue;
        const auto bytes = it->file_size(error); if (error) break;
        const auto written = it->last_write_time(error); if (error) break;
        entries.push_back({it->path(), bytes, written}); total += bytes;
    }
    { std::lock_guard lock(index_lock); known_bytes = total; }
    size_t remaining = entries.size();
    if (total <= budget && remaining <= max_files) return;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.written < b.written; });
    for (const auto& entry : entries) {
        if (total <= budget && remaining <= max_files) break;
        if (fs::remove(entry.path, error)) {
            total -= entry.bytes;
            --remaining;
            std::lock_guard lock(index_lock); index.erase(entry.path.filename().string()); known_bytes = total;
        }
    }
}
}
