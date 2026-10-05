#include "platform/pid_lock.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>

#include "platform/platform.h"
#include "support/env_guard.h"

/// The PID lock the cycle and the task runner share: exclusive, naming its
/// holder and what it holds the lock for, released by its destructor; a
/// holder whose process is gone refused by default and taken over only when
/// asked; a file naming no process never taken over.
namespace {

using apogee::platform::PidLock;

struct Fixture {
    apogee::testing::TempDir dir{"pid-lock-" + std::to_string(std::random_device{}())};
    std::filesystem::path lock = dir.path() / "nested" / "x.lock";
};

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

/// A process id nothing runs under.
constexpr long kGone = 999999999;

}  // namespace

TEST_CASE("a PID lock is exclusive, names its holder and is released by its destructor",
          "[platform][pid_lock]") {
    const Fixture fixture;
    {
        PidLock::Refusal refusal;
        const std::optional<PidLock> first =
            PidLock::acquire(fixture.lock, "task-1", /*take_stale=*/false, refusal);
        REQUIRE(first.has_value());
        CHECK(refusal.error.empty());
        CHECK(read(fixture.lock) ==
              std::to_string(apogee::platform::current_process_id()) + "\ntask-1\n");

        const std::optional<PidLock::Holder> holder = PidLock::holder(fixture.lock);
        REQUIRE(holder.has_value());
        CHECK(holder->pid == apogee::platform::current_process_id());
        CHECK(holder->label == "task-1");
        CHECK(holder->running);

        // A live holder is refused even when stale locks may be taken.
        const std::optional<PidLock> second =
            PidLock::acquire(fixture.lock, "task-2", /*take_stale=*/true, refusal);
        CHECK_FALSE(second.has_value());
        REQUIRE(refusal.holder.has_value());
        CHECK(refusal.holder->label == "task-1");
        CHECK(refusal.holder->running);
    }
    CHECK_FALSE(std::filesystem::exists(fixture.lock));
    CHECK_FALSE(PidLock::holder(fixture.lock).has_value());
}

TEST_CASE("a PID lock without a label holds the process id alone, as the cycle's did",
          "[platform][pid_lock]") {
    const Fixture fixture;
    PidLock::Refusal refusal;
    std::optional<PidLock> lock = PidLock::acquire(fixture.lock, {}, false, refusal);
    REQUIRE(lock.has_value());
    CHECK(read(fixture.lock) == std::to_string(apogee::platform::current_process_id()) + "\n");
    lock->release();
    lock->release();  // twice is safe
    CHECK_FALSE(std::filesystem::exists(fixture.lock));
}

TEST_CASE("a lock its process left is refused by default and taken over only when asked",
          "[platform][pid_lock]") {
    const Fixture fixture;
    write(fixture.lock, std::to_string(kGone) + "\ntask-dead\n");

    PidLock::Refusal refusal;
    CHECK_FALSE(PidLock::acquire(fixture.lock, "task-new", /*take_stale=*/false, refusal));
    REQUIRE(refusal.holder.has_value());
    CHECK(refusal.holder->pid == kGone);
    CHECK_FALSE(refusal.holder->running);
    CHECK(read(fixture.lock) == std::to_string(kGone) + "\ntask-dead\n");

    const std::optional<PidLock> taken =
        PidLock::acquire(fixture.lock, "task-new", /*take_stale=*/true, refusal);
    REQUIRE(taken.has_value());
    CHECK_FALSE(refusal.holder.has_value());
    CHECK(PidLock::holder(fixture.lock)->label == "task-new");
}

TEST_CASE("a lock file naming no process is never taken over", "[platform][pid_lock]") {
    const Fixture fixture;
    write(fixture.lock, "not a number\n");
    PidLock::Refusal refusal;
    CHECK_FALSE(PidLock::acquire(fixture.lock, "task-new", /*take_stale=*/true, refusal));
    REQUIRE(refusal.holder.has_value());
    CHECK(refusal.holder->pid == 0);
    CHECK_FALSE(refusal.holder->running);
    CHECK(read(fixture.lock) == "not a number\n");
}

TEST_CASE("a PID lock that cannot be made says why", "[platform][pid_lock]") {
    const Fixture fixture;
    // A file where the lock's directory would go.
    write(fixture.dir.path() / "blocked", "x");
    PidLock::Refusal refusal;
    CHECK_FALSE(PidLock::acquire(fixture.dir.path() / "blocked" / "x.lock", "t", true, refusal));
    CHECK_FALSE(refusal.holder.has_value());
    CHECK_FALSE(refusal.error.empty());
}

TEST_CASE("utc_time lays a moment out on the UTC calendar", "[platform][time]") {
    // 2026-10-04T12:34:56Z.
    const auto when = std::chrono::system_clock::time_point{std::chrono::seconds{1791117296}};
    CHECK(apogee::platform::utc_time(when, "%Y-%m-%dT%H:%M:%SZ") == "2026-10-04T12:34:56Z");
    CHECK(apogee::platform::utc_time(when, "%Y%m%d-%H%M%S") == "20261004-123456");
}
