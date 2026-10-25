#include "platform/file_lock.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "support/env_guard.h"

/// The advisory lock every config edit holds (55f): exclusive across threads
/// as across processes, released by its destructor or by `release`, its file
/// left in place; a waiter that outlasts its bound is refused and told so,
/// and one whose holder lets go takes it at once.
namespace {

using apogee::platform::FileLock;
using Catch::Matchers::ContainsSubstring;
using std::chrono::milliseconds;

struct Fixture {
    apogee::testing::TempDir dir{"file-lock-" + std::to_string(std::random_device{}())};
    std::filesystem::path lock = dir.path() / "config.json.lock";
};

[[nodiscard]] milliseconds since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - start);
}

}  // namespace

TEST_CASE("a file lock is exclusive until released, and its file stays where it was",
          "[platform][file_lock]") {
    const Fixture fixture;
    FileLock::Refusal refusal;
    std::optional<FileLock> first = FileLock::acquire(fixture.lock, milliseconds{0}, refusal);
    REQUIRE(first.has_value());
    CHECK(refusal.error.empty());
    CHECK(first->path() == fixture.lock);
    CHECK(std::filesystem::exists(fixture.lock));

    // Held: another taker waits out its bound, then is refused and told why.
    const auto start = std::chrono::steady_clock::now();
    const std::optional<FileLock> second =
        FileLock::acquire(fixture.lock, milliseconds{120}, refusal);
    CHECK_FALSE(second.has_value());
    CHECK(refusal.timed_out);
    CHECK_THAT(refusal.error, ContainsSubstring(fixture.lock.string()));
    CHECK_THAT(refusal.error, ContainsSubstring("120 ms"));
    CHECK(since(start) >= milliseconds{100});
    CHECK(since(start) < milliseconds{5000});

    // A move keeps the lock held; the moved-from one holds nothing.
    FileLock moved = std::move(*first);
    first.reset();
    CHECK_FALSE(FileLock::acquire(fixture.lock, milliseconds{0}, refusal).has_value());
    moved.release();
    moved.release();  // twice is safe

    // Released: taken at once, and the file is still there.
    CHECK(std::filesystem::exists(fixture.lock));
    {
        const std::optional<FileLock> third =
            FileLock::acquire(fixture.lock, milliseconds{0}, refusal);
        CHECK(third.has_value());
        CHECK_FALSE(refusal.timed_out);
    }
    // The destructor let it go.
    CHECK(FileLock::acquire(fixture.lock, milliseconds{0}, refusal).has_value());
    CHECK(std::filesystem::exists(fixture.lock));
}

TEST_CASE("a waiter takes the file lock as soon as its holder lets go", "[platform][file_lock]") {
    const Fixture fixture;
    FileLock::Refusal refusal;
    std::optional<FileLock> held = FileLock::acquire(fixture.lock, milliseconds{0}, refusal);
    REQUIRE(held.has_value());
    std::thread holder{[lock = std::move(*held)]() mutable {
        std::this_thread::sleep_for(milliseconds{150});
        lock.release();
    }};
    const auto start = std::chrono::steady_clock::now();
    const std::optional<FileLock> waited =
        FileLock::acquire(fixture.lock, milliseconds{10000}, refusal);
    const milliseconds took = since(start);
    holder.join();
    CHECK(waited.has_value());
    CHECK(refusal.error.empty());
    CHECK(took >= milliseconds{100});
    CHECK(took < milliseconds{5000});  // the holder's release, not the bound
}

TEST_CASE("a file lock that cannot be opened says why, and is no timeout",
          "[platform][file_lock]") {
    const Fixture fixture;
    FileLock::Refusal refusal;
    const std::filesystem::path nowhere = fixture.dir.path() / "no-such-dir" / "x.lock";
    CHECK_FALSE(FileLock::acquire(nowhere, milliseconds{1000}, refusal).has_value());
    CHECK_FALSE(refusal.timed_out);
    CHECK_THAT(refusal.error, ContainsSubstring("cannot open the lock"));
    CHECK_THAT(refusal.error, ContainsSubstring(nowhere.string()));
}

TEST_CASE("threads that read and rewrite one file under the file lock lose no write",
          "[platform][file_lock][concurrent]") {
    // The lost update the config editor had (55f), at the seam: each taker
    // reads a counter, sleeps a moment, and writes it back one higher.
    // Without the lock the increments overwrite each other.
    const Fixture fixture;
    const std::filesystem::path counter = fixture.dir.path() / "counter";
    std::ofstream{counter, std::ios::binary} << "0";
    constexpr int kThreads = 8;
    constexpr int kRounds = 12;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    std::vector<std::string> failures(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int round = 0; round < kRounds; ++round) {
                FileLock::Refusal refusal;
                const std::optional<FileLock> lock =
                    FileLock::acquire(fixture.lock, milliseconds{20000}, refusal);
                if (!lock.has_value()) {
                    failures[static_cast<std::size_t>(t)] = refusal.error;
                    return;
                }
                std::ifstream in{counter, std::ios::binary};
                int value = 0;
                in >> value;
                in.close();
                std::this_thread::sleep_for(milliseconds{1});
                std::ofstream{counter, std::ios::binary | std::ios::trunc} << (value + 1);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const std::string& failure : failures) {
        CHECK(failure.empty());
    }
    std::ifstream in{counter, std::ios::binary};
    int value = 0;
    in >> value;
    CHECK(value == kThreads * kRounds);
}
