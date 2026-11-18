#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace nx::nvn {
// Machine-local cache of the player's translated shaders. Keep it with private game data.
bool build_shader_bundle(const std::filesystem::path& data, std::string& error,
                         const std::function<void(uint64_t,uint64_t)>& progress = {});
class ShaderBundle {
public:
    explicit ShaderBundle(const std::filesystem::path& data, bool enabled = true);
    ~ShaderBundle();
    std::vector<uint8_t> load(const std::string& hash);
    void flush();
    std::span<const uint8_t> find(const std::string& hash) const;
    size_t size() const;
    size_t memory_bytes() const;
private:
    struct Entry { uint64_t offset, size, timestamp, checksum; };
    std::filesystem::path directory;
    std::vector<uint8_t> bytes;
    std::unordered_map<uint64_t,Entry> entries;
    struct Journal;
    std::unique_ptr<Journal> journal;
};
// Runtime singleton, defined alongside shader_for; standalone cache tests do not need a platform.
ShaderBundle& shader_bundle();
void flush_shader_bundle();  // no allocation if no game shader was loaded
}
