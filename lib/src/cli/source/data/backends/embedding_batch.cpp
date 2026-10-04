#include "backends/embedding_batch.h"

#include <algorithm>

namespace apogee::backends {

std::vector<std::pair<std::size_t, std::size_t>> batch_ranges(std::size_t count,
                                                              std::size_t max_per_batch) noexcept {
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    const std::size_t step = std::max<std::size_t>(max_per_batch, 1);
    for (std::size_t begin = 0; begin < count; begin += step) {
        ranges.emplace_back(begin, std::min(begin + step, count));
    }
    return ranges;
}

}  // namespace apogee::backends
