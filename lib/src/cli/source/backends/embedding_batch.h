#pragma once

#include <cstddef>
#include <utility>
#include <vector>

/// The one piece of embedding plumbing every client shares: splitting a list of
/// inputs into request-sized batches.
///
/// Batch-first is a Core constraint of the embedding items, and it was earned:
/// Ommi's graph item recorded per-chunk embedding calls as its cost trap -- a
/// corpus of ten thousand chunks is ten thousand round trips at one per call,
/// and a few dozen at a provider's documented maximum. Every client here takes
/// a whole list and asks this function where the request boundaries fall.
namespace apogee::backends {

/// Half-open `[begin, end)` index ranges covering `count` inputs, each at most
/// `max_per_batch` long, in order. `count == 0` yields no ranges; a
/// `max_per_batch` of 0 is treated as 1 so the split always makes progress.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> batch_ranges(
    std::size_t count, std::size_t max_per_batch) noexcept;

}  // namespace apogee::backends
