#pragma once
#include <cstdint>
#include <iterator>
#include <utility>

namespace nx::nvn {

// Regions are disjoint, keyed by their first guest byte. Return the overlapping
// half-open map range without allocating a temporary list. Touching ranges do
// not overlap. Iterators stay valid until the caller erases the range.
template<class Regions>
auto overlapping_regions(Regions& regions, const uint8_t* p, uint64_t size) {
    auto last = regions.upper_bound(p);
    auto first = last;
    if (first != regions.begin() && std::prev(first)->first + std::prev(first)->second.size > p) --first;
    while (last != regions.end() && last->first < p + size) ++last;
    return std::pair{first, last};
}

}  // namespace nx::nvn
