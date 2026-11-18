#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace nx::nvn {

// Mapped upload memory can be uncached. Build the reference in CPU RAM first,
// then upload those exact bytes without reading back the GPU mapping. The
// reference remains independent of later guest writes and GPU output.
inline std::shared_ptr<std::vector<uint8_t>> snapshot_upload(
    uint8_t* mapped, const uint8_t* guest, size_t size, size_t padded) {
    auto before = std::make_shared<std::vector<uint8_t>>(guest, guest + size);
    if (size) std::memcpy(mapped, before->data(), size);
    if (padded > size) std::memset(mapped + size, 0, padded - size);
    return before;
}

}  // namespace nx::nvn
