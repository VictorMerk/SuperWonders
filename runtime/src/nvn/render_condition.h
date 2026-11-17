#pragma once
#include <cstdint>

namespace nx::nvn {
// The reports are {counter, timestamp}. Timestamp-only reports must not leave a
// nonzero counter behind: engines compare it with the next report's counter to
// avoid clearing the same capture again.
constexpr uint64_t report_counter_value(uint32_t type, uint64_t serial) {
    return type == 0 || type == 16 ? 0 : type == 1 ? 1 : serial;
}
constexpr bool conditional_render(uint32_t mode, uint64_t a, uint64_t b) {
    return mode == 0 ? a == b : mode == 1 ? a != b : true;
}
}
