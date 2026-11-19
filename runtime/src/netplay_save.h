#pragma once
#include "netplay_wire.h"
#include <filesystem>
#include <functional>

namespace nx::lan {
constexpr size_t kMaxSaveBytes = 64u<<20;
Fingerprint digest(std::span<const uint8_t> bytes);
// Only the save subtree is captured. No game data, settings, credentials or cache.
std::vector<uint8_t> capture_save(const std::filesystem::path& save, const std::function<void()>& checkpoint = {});
// Destination must not exist. Complete validation precedes filesystem writes.
void restore_save(std::span<const uint8_t> bytes, const std::filesystem::path& destination,
                  const std::function<void()>& checkpoint = {});
}
