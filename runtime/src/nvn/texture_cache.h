#pragma once
#include "nvn_tiling.h"
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace nx::nvn {
// Low-VRAM BC3 conversions only. Versioned by both the directory and file magic.
// Fingerprints select a file; exact source bytes and metadata still decide a hit.
struct TextureCacheRequest {
    std::string name;
    std::vector<uint8_t> metadata;
    std::vector<LinearLevel> output_levels;
    uint64_t output_bytes = 0;
};
TextureCacheRequest texture_cache_request(uint32_t format, uint32_t layers,
    std::span<const uint8_t> source, std::span<const LinearLevel> levels);

class TextureCache {
    std::filesystem::path root;
    uint64_t budget;
    size_t max_files;
    std::mutex index_lock;
    std::unordered_map<std::string,uint64_t> index;
    uint64_t known_bytes = 0;
    void trim();
public:
    explicit TextureCache(std::filesystem::path directory, uint64_t bytes = 256ull << 20, size_t files = 4096);
    bool load(const TextureCacheRequest&, std::span<const uint8_t> source, std::vector<uint8_t>& output) noexcept;
    // Call from the bounded background writer, never while presenting.
    bool store(const TextureCacheRequest&, std::span<const uint8_t> source, std::span<const uint8_t> output) noexcept;
};
}
