#pragma once
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace nx::streaming {
bool enabled();
void set_enabled(bool);
bool eligible_music(const std::string& guest_path);
struct CacheStats { uint64_t reserved_bytes = 0, entries = 0, hits = 0, completed = 0, failed = 0, dropped = 0; };

// Only immutable content BGM/ambience streams are offered by the filesystem adapter.
// Reads never wait for prefetch: until a complete verified-size snapshot is ready,
// the caller retains the normal file path. No disk cache or changed audio format.
class MusicCache {
public:
    struct Entry;
    using Token = std::shared_ptr<Entry>;
    explicit MusicCache(uint64_t total_bytes = 64ull << 20, uint64_t per_file = 8ull << 20,
                        size_t entries = 16, size_t pending = 4);
    ~MusicCache();
    MusicCache(const MusicCache&) = delete;
    MusicCache& operator=(const MusicCache&) = delete;
    Token open(const std::filesystem::path&);
    bool read(const Token&, int64_t offset, void* output, uint64_t bytes, uint64_t& read_bytes) const;
    CacheStats stats() const;
    void stop();
private:
    struct State;
    std::unique_ptr<State> state;
};
}
