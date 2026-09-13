#include "backends/embedding_batch.h"

#include <catch2/catch_test_macros.hpp>

using apogee::backends::batch_ranges;

TEST_CASE("inputs are split at the provider's maximum and nowhere else",
          "[backends][embed][batch]") {
    // The batch-splitting boundary, in one table. Batch-first is the Core
    // constraint; per-input calls were Ommi's recorded cost trap.
    CHECK(batch_ranges(0, 100).empty());
    CHECK(batch_ranges(1, 100) == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});
    CHECK(batch_ranges(100, 100) == std::vector<std::pair<std::size_t, std::size_t>>{{0, 100}});
    CHECK(batch_ranges(101, 100) ==
          std::vector<std::pair<std::size_t, std::size_t>>{{0, 100}, {100, 101}});
    CHECK(batch_ranges(250, 100) ==
          std::vector<std::pair<std::size_t, std::size_t>>{{0, 100}, {100, 200}, {200, 250}});
}

TEST_CASE("a zero maximum still makes progress", "[backends][embed][batch]") {
    // A maximum of 0 would be an infinite loop in the obvious implementation.
    // It is treated as 1, so a misconfigured caller is slow rather than hung.
    CHECK(batch_ranges(3, 0) ==
          std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}, {1, 2}, {2, 3}});
}
