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
std::array<uint64_t,2> fingerprints(std::span<const uint8_t> bytes, uint64_t a, uint64_t b) {
    size_t at = 0;
    for (; at + 8 <= bytes.size(); at += 8) {
        uint64_t word; std::memcpy(&word,bytes.data()+at,8);
        a = (a ^ word) * 0x9e3779b97f4a7c15ull;
        b = (b ^ word) * 0x9e3779b97f4a7c15ull;
        a = (a << 27) | (a >> 37); b = (b << 27) | (b >> 37);
    }
    for (; at < bytes.size(); ++at) {
        a = (a ^ bytes[at]) * 0x100000001b3ull;
        b = (b ^ bytes[at]) * 0x100000001b3ull;
    }
    return {a ^ (a >> 29),b ^ (b >> 29)};
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
    request.metadata.reserve(12 + 44 * levels.size());
    request.output_levels.reserve(levels.size());
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
    const auto hashes = fingerprints(source,fingerprint(request.metadata),fingerprint(request.metadata,0x84222325cbf29ce4ull));
    const auto first = hashes[0], second = hashes[1];
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

void TextureCache::reject(const std::string& name,uint64_t observed_generation,bool missing) {
    std::lock_guard lock(index_lock);
    if(generation!=observed_generation) return;  // a newer committed write must not be invalidated
    auto it=index.find(name);
    if(it==index.end()) return;
    if(missing) { known_bytes-=it->second;index.erase(it);invalid.erase(name); }
    else invalid.insert(name);  // corrupt bytes still count against the disk quota
}
TextureCache::Stats TextureCache::stats() {
    std::lock_guard lock(index_lock);return {known_bytes,index.size(),invalid.size()};
}
bool TextureCache::load(const TextureCacheRequest& request, std::span<const uint8_t> source, std::vector<uint8_t>& output) noexcept {
    try {
        if (!owned_name(request.name) || request.metadata.size() > 2048 || source.empty() || source.size() > kMaxPart || !request.output_bytes || request.output_bytes > kMaxPart) return false;
        uint64_t observed_generation;
        { std::lock_guard lock(index_lock); if (!index.contains(request.name) || invalid.contains(request.name)) return false; observed_generation=generation; }
        const auto path = root / request.name;
        std::error_code error;
        if (fs::is_symlink(path, error)) { reject(request.name,observed_generation); return false; }
        const auto actual=fs::file_size(path,error);
        if(error) {
            if(error==std::errc::no_such_file_or_directory) reject(request.name,observed_generation,true);
            return false;
        }
        if(actual<40) { reject(request.name,observed_generation);return false; }
        std::ifstream file(path, std::ios::binary);
        std::array<uint8_t,40> header{};
        if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) return false;
        const auto source_bytes=get(header.data()+16),output_bytes=get(header.data()+24),metadata_bytes=get(header.data()+32);
        if (!std::equal(kMagic.begin(), kMagic.end(), header.begin()) || !source_bytes || source_bytes>kMaxPart ||
            !output_bytes || output_bytes>kMaxPart || metadata_bytes>2048 || actual!=40+source_bytes+output_bytes+metadata_bytes) {
            reject(request.name,observed_generation); return false;
        }
        if(source_bytes!=source.size() || output_bytes!=request.output_bytes || metadata_bytes!=request.metadata.size()) return false;
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
        if (!file.read(reinterpret_cast<char*>(converted.data()), converted.size())) return false;
        if (fingerprint(converted) != get(header.data()+8)) {
            reject(request.name,observed_generation);return false;
        }
        output = std::move(converted);
        return true;
    } catch (...) { return false; }  // caches must not turn memory/disk problems into game failures
}

bool TextureCache::store(const TextureCacheRequest& request, std::span<const uint8_t> source, std::span<const uint8_t> output) noexcept {
    try {
        const uint64_t bytes = 40 + request.metadata.size() + source.size() + output.size();
        if (!owned_name(request.name) || request.metadata.size() > 2048 || source.empty() || source.size() > kMaxPart || output.size() != request.output_bytes ||
            !request.output_bytes || output.size() > kMaxPart || bytes > budget || !max_files) return false;
        std::lock_guard writing(write_lock);
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
        { std::lock_guard lock(index_lock); known_bytes -= index[request.name]; index[request.name] = bytes; known_bytes += bytes; invalid.erase(request.name);++generation; }
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
    fs::directory_iterator it(root,error),end;
    for (; !error && it != end && visited++ < 8192; it.increment(error)) {
        if (!owned_name(it->path().filename().string()) || it->is_symlink(error) || !it->is_regular_file(error)) continue;
        const auto bytes = it->file_size(error); if (error) break;
        const auto written = it->last_write_time(error); if (error) break;
        entries.push_back({it->path(), bytes, written}); total += bytes;
    }
    // Never reconcile an incomplete/failed scan: unvisited entries are unknown.
    if(error || it!=end) return;
    std::unordered_map<std::string,uint64_t> refreshed;
    refreshed.reserve(entries.size());
    for(const auto& entry:entries) refreshed.emplace(entry.path.filename().string(),entry.bytes);
    {
        std::lock_guard lock(index_lock);
        index.swap(refreshed);known_bytes=total;++generation;
        std::erase_if(invalid,[&](const auto& name) { return !index.contains(name); });
    }
    size_t remaining = entries.size();
    if (total <= budget && remaining <= max_files) return;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.written < b.written; });
    for (const auto& entry : entries) {
        if (total <= budget && remaining <= max_files) break;
        if (fs::remove(entry.path, error)) {
            total -= entry.bytes;
            --remaining;
            std::lock_guard lock(index_lock);
            const auto name=entry.path.filename().string();
            if(auto found=index.find(name);found!=index.end()) { known_bytes-=found->second;index.erase(found); }
            invalid.erase(name);++generation;
        }
    }
}
}
