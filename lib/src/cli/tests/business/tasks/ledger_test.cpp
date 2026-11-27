#include "tasks/ledger.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "platform/platform.h"
#include "support/env_guard.h"

/// The ledger on disk: written whole through a temp file, read back, listed
/// newest first with an unreadable one named and skipped; ids that cannot
/// leave `tasks/`; the halt and cancel requests another process leaves; and
/// the one-task lock -- a live holder refused by name, one whose process is
/// gone taken over, as a killed task's resume needs.
namespace {

namespace t = apogee::tasks;

struct Fixture {
    apogee::testing::TempDir dir{"task-ledger-" + std::to_string(std::random_device{}())};
    std::filesystem::path root = dir.path() / "tasks";
};

t::Task make(const std::string& id, const std::string& created) {
    t::Task task;
    task.id = id;
    task.goal = "goal of " + id;
    task.created_at = created;
    return task;
}

}  // namespace

TEST_CASE("a ledger is written whole and read back", "[tasks][ledger]") {
    const Fixture fixture;
    t::Task task = make("task-20261004-120000", "2026-10-04T12:00:00Z");
    task.checks.push_back({t::CheckKind::Require, "42"});
    REQUIRE(t::save_task(fixture.root, task).empty());
    CHECK(std::filesystem::is_regular_file(fixture.root / task.id / "task.json"));
    // Nothing left beside it but the ledger: the temp file was renamed.
    int entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(fixture.root / task.id)) {
        (void)entry;
        ++entries;
    }
    CHECK(entries == 1);

    std::string error;
    const std::optional<t::Task> back = t::load_task(fixture.root, task.id, error);
    REQUIRE(back.has_value());
    CHECK(error.empty());
    CHECK(back->goal == task.goal);
    CHECK(back->checks.size() == 1);
}

TEST_CASE("a missing, unreadable or misnamed ledger says so", "[tasks][ledger]") {
    const Fixture fixture;
    std::string error;
    CHECK_FALSE(t::load_task(fixture.root, "task-nope", error).has_value());
    CHECK(error == "no task 'task-nope'");
    CHECK_FALSE(t::load_task(fixture.root, "../escape", error).has_value());
    CHECK(error == "not a task id: '../escape'");
    t::Task bad = make("../escape", "");
    CHECK_FALSE(t::save_task(fixture.root, bad).empty());

    std::filesystem::create_directories(fixture.root / "task-broken");
    std::ofstream{fixture.root / "task-broken" / "task.json"} << "{not json";
    CHECK_FALSE(t::load_task(fixture.root, "task-broken", error).has_value());
    CHECK(error.find("is not JSON") != std::string::npos);
}

TEST_CASE("tasks list newest first, an unreadable one named and skipped", "[tasks][ledger]") {
    const Fixture fixture;
    REQUIRE(
        t::save_task(fixture.root, make("task-20261004-100000", "2026-10-04T10:00:00Z")).empty());
    REQUIRE(
        t::save_task(fixture.root, make("task-20261004-120000", "2026-10-04T12:00:00Z")).empty());
    REQUIRE(
        t::save_task(fixture.root, make("task-20261004-120000-2", "2026-10-04T12:00:00Z")).empty());
    std::filesystem::create_directories(fixture.root / "task-broken");
    std::ofstream{fixture.root / "task-broken" / "task.json"} << "[]";
    std::filesystem::create_directories(fixture.root / "not-a-task");

    std::vector<std::string> problems;
    const std::vector<t::Task> all = t::list_tasks(fixture.root, &problems);
    REQUIRE(all.size() == 3);
    CHECK(all[0].id == "task-20261004-120000-2");
    CHECK(all[1].id == "task-20261004-120000");
    CHECK(all[2].id == "task-20261004-100000");
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("task-broken") != std::string::npos);
    CHECK(t::list_tasks(fixture.dir.path() / "absent").empty());
}

TEST_CASE("inside one second, the id's own time and then its suffix order the list",
          "[tasks][ledger]") {
    // A ledger whose `created_at` was read after its id was minted can carry
    // an earlier second's suffixed id beside a later second's: the later
    // second is the newer task, whatever the ids' lengths (found at 27t's
    // boundary: the task e2e's newest-task lookup returned the older one).
    const Fixture fixture;
    const std::string second = "2026-10-04T12:00:00Z";
    for (const char* id : {"task-20261004-115959-2", "task-20261004-120000",
                           "task-20261004-120000-9", "task-20261004-120000-10"}) {
        REQUIRE(t::save_task(fixture.root, make(id, second)).empty());
    }
    const std::vector<t::Task> all = t::list_tasks(fixture.root);
    REQUIRE(all.size() == 4);
    CHECK(all[0].id == "task-20261004-120000-10");
    CHECK(all[1].id == "task-20261004-120000-9");
    CHECK(all[2].id == "task-20261004-120000");
    CHECK(all[3].id == "task-20261004-115959-2");
}

TEST_CASE("a new task's id and created_at come from one reading of the clock", "[tasks][ledger]") {
    const Fixture fixture;
    const auto at = std::chrono::sys_days{std::chrono::year{2026} / 10 / 4} +
                    std::chrono::hours{11} + std::chrono::minutes{59} + std::chrono::seconds{59} +
                    std::chrono::milliseconds{999};
    CHECK(t::new_task_id(fixture.root, at) == "task-20261004-115959");
    CHECK(t::timestamp(at) == "2026-10-04T11:59:59Z");
    CHECK(t::timestamp(std::chrono::system_clock::now()).size() ==
          std::string{"YYYY-MM-DDTHH:MM:SSZ"}.size());
}

TEST_CASE("a new task's id is its start in UTC, suffixed past one that exists", "[tasks][ledger]") {
    const Fixture fixture;
    const auto when = std::chrono::system_clock::time_point{std::chrono::seconds{1791117296}};
    CHECK(t::new_task_id(fixture.root, when) == "task-20261004-123456");
    std::filesystem::create_directories(fixture.root / "task-20261004-123456");
    CHECK(t::new_task_id(fixture.root, when) == "task-20261004-123456-2");
    std::filesystem::create_directories(fixture.root / "task-20261004-123456-2");
    CHECK(t::new_task_id(fixture.root, when) == "task-20261004-123456-3");
    CHECK(t::valid_task_id("task-20261004-123456-3"));
    CHECK_FALSE(t::valid_task_id(""));
    CHECK_FALSE(t::valid_task_id("a/b"));
    CHECK_FALSE(t::valid_task_id(".."));
    CHECK_FALSE(t::valid_task_id(std::string(65, 'a')));
}

TEST_CASE("a halt or cancel is left for the running task and read back", "[tasks][ledger]") {
    const Fixture fixture;
    const std::string id = "task-20261004-120000";
    CHECK(t::read_request(fixture.root, id) == t::Request::None);
    REQUIRE(t::write_request(fixture.root, id, t::Request::Halt).empty());
    CHECK(t::read_request(fixture.root, id) == t::Request::Halt);
    REQUIRE(t::write_request(fixture.root, id, t::Request::Cancel).empty());
    CHECK(t::read_request(fixture.root, id) == t::Request::Cancel);
    t::clear_request(fixture.root, id);
    CHECK(t::read_request(fixture.root, id) == t::Request::None);
    CHECK_FALSE(t::write_request(fixture.root, "../x", t::Request::Halt).empty());
}

TEST_CASE("a request standing when the lock is taken is cleared; one written after it stays",
          "[tasks][ledger][lock][57u]") {
    // 27h's rule, its point moved to the lock changing hands: a request left
    // for a process that ended is stale once a new one holds the lock, and
    // one written after that is addressed to the holder.
    const Fixture fixture;
    const std::string id = "task-20261004-120000";
    REQUIRE(t::write_request(fixture.root, id, t::Request::Cancel).empty());
    std::string error;
    std::optional<t::TaskLock> lock = t::TaskLock::acquire(fixture.root, id, error);
    REQUIRE(lock.has_value());
    CHECK(t::read_request(fixture.root, id) == t::Request::None);

    REQUIRE(t::write_request(fixture.root, id, t::Request::Halt).empty());
    CHECK(t::read_request(fixture.root, id) == t::Request::Halt);
    // A second acquire, refused by the live holder, clears nothing.
    CHECK_FALSE(t::TaskLock::acquire(fixture.root, id, error).has_value());
    CHECK(t::read_request(fixture.root, id) == t::Request::Halt);
    lock->release();

    // Another task's lock never touches this task's request.
    const std::optional<t::TaskLock> other =
        t::TaskLock::acquire(fixture.root, "task-20261004-130000", error);
    REQUIRE(other.has_value());
    CHECK(t::read_request(fixture.root, id) == t::Request::Halt);
}

TEST_CASE("a run still starting is no task yet, and what its refusal leaves is removed",
          "[tasks][ledger][lock][57u]") {
    const Fixture fixture;
    const std::string id = "task-20261004-120000";
    std::string error;
    std::optional<t::TaskLock> lock = t::TaskLock::acquire(fixture.root, id, error);
    REQUIRE(lock.has_value());
    CHECK_FALSE(t::ledger_written(fixture.root, id));
    // A stop sent while the run starts makes its directory before its ledger.
    REQUIRE(t::write_request(fixture.root, id, t::Request::Cancel).empty());
    std::vector<std::string> problems;
    CHECK(t::list_tasks(fixture.root, &problems).empty());
    CHECK(problems.empty());  // not a task yet, and not a problem either

    // Refused before it ran: the request and the then-empty directory go.
    t::discard_unread_request(fixture.root, id);
    CHECK(t::read_request(fixture.root, id) == t::Request::None);
    CHECK_FALSE(std::filesystem::exists(t::task_dir(fixture.root, id)));
    lock->release();

    // The run ended before it read the stop -- killed during its setup -- so
    // the stop is all its directory holds: no task, and no problem either.
    REQUIRE(t::write_request(fixture.root, id, t::Request::Cancel).empty());
    problems.clear();
    CHECK(t::list_tasks(fixture.root, &problems).empty());
    CHECK(problems.empty());
    // A directory with no ledger and anything else in it is named, as
    // before -- and so is an empty one.
    std::ofstream{t::task_dir(fixture.root, id) / "stray"} << "x\n";
    const std::string empty = "task-20261004-110000";
    std::filesystem::create_directories(t::task_dir(fixture.root, empty));
    problems.clear();
    CHECK(t::list_tasks(fixture.root, &problems).empty());
    CHECK(problems.size() == 2);
    CHECK(std::ranges::count(problems, "no task '" + id + "'") == 1);
    CHECK(std::ranges::count(problems, "no task '" + empty + "'") == 1);
    std::filesystem::remove(t::task_dir(fixture.root, id) / "stray");
    std::filesystem::remove(t::task_dir(fixture.root, empty));

    // A ledger keeps its directory: only the request goes.
    REQUIRE(t::save_task(fixture.root, make(id, "2026-10-04T12:00:00Z")).empty());
    CHECK(t::ledger_written(fixture.root, id));
    t::discard_unread_request(fixture.root, id);
    CHECK(t::read_request(fixture.root, id) == t::Request::None);
    CHECK(t::ledger_written(fixture.root, id));
    // Never a path out of `tasks/`.
    t::discard_unread_request(fixture.root, "..");
    CHECK(std::filesystem::exists(fixture.root));
    CHECK_FALSE(t::ledger_written(fixture.root, "../x"));
}

TEST_CASE("a run killed in its setup leaves its unread stop: no task, no problem, its id kept",
          "[tasks][ledger][lock][57u]") {
    // A new run held the lock, a stop sent while it started made its
    // directory, and the run was killed there -- before its signal handlers
    // were up, so nothing of its own cleaned up.
    const Fixture fixture;
    const auto when = std::chrono::system_clock::time_point{std::chrono::seconds{1791117296}};
    const std::string killed = t::new_task_id(fixture.root, when);
    REQUIRE(t::write_request(fixture.root, killed, t::Request::Cancel).empty());
    std::ofstream{fixture.root / "task.lock"} << "999999999\n" << killed << "\n";
    std::vector<std::string> problems;
    CHECK(t::list_tasks(fixture.root, &problems).empty());
    CHECK(problems.empty());

    // The next run takes the dead holder's lock over and leaves another
    // task's directory be: still no problem, and the id never minted again
    // -- the next run of that second is `-2`, the one after `-3`, listed in
    // the order they began.
    const std::string second = t::new_task_id(fixture.root, when);
    CHECK(second == killed + "-2");
    std::string error;
    std::optional<t::TaskLock> lock = t::TaskLock::acquire(fixture.root, second, error);
    REQUIRE(lock.has_value());
    CHECK(t::read_request(fixture.root, killed) == t::Request::Cancel);
    REQUIRE(t::save_task(fixture.root, make(second, "2026-10-04T12:34:56Z")).empty());
    lock->release();
    const std::string third = t::new_task_id(fixture.root, when);
    CHECK(third == killed + "-3");
    REQUIRE(t::save_task(fixture.root, make(third, "2026-10-04T12:34:56Z")).empty());
    problems.clear();
    const std::vector<t::Task> listed = t::list_tasks(fixture.root, &problems);
    CHECK(problems.empty());
    REQUIRE(listed.size() == 2);
    CHECK(listed.at(0).id == third);
    CHECK(listed.at(1).id == second);
}

TEST_CASE("one task runs at a time: the lock refuses a second, naming the first",
          "[tasks][ledger][lock]") {
    const Fixture fixture;
    std::string error;
    {
        std::optional<t::TaskLock> first =
            t::TaskLock::acquire(fixture.root, "task-20261004-120000", error);
        REQUIRE(first.has_value());
        CHECK(t::running_task(fixture.root) == "task-20261004-120000");
        const std::optional<t::LockHolder> holder = t::lock_holder(fixture.root);
        REQUIRE(holder.has_value());
        CHECK(holder->pid == apogee::platform::current_process_id());

        const std::optional<t::TaskLock> second =
            t::TaskLock::acquire(fixture.root, "task-20261004-130000", error);
        CHECK_FALSE(second.has_value());
        CHECK(error.find("task task-20261004-120000 is running (process ") == 0);
        CHECK(error.find("one task runs at a time") != std::string::npos);
        CHECK(error.find("'apogee task cancel'") != std::string::npos);
    }
    CHECK_FALSE(t::running_task(fixture.root).has_value());
    CHECK_FALSE(t::lock_holder(fixture.root).has_value());
}

TEST_CASE("a lock a killed task left is taken over; one naming nothing is refused",
          "[tasks][ledger][lock]") {
    const Fixture fixture;
    std::filesystem::create_directories(fixture.root);
    std::ofstream{fixture.root / "task.lock"} << "999999999\ntask-dead\n";
    CHECK_FALSE(t::running_task(fixture.root).has_value());
    REQUIRE(t::lock_holder(fixture.root).has_value());
    CHECK_FALSE(t::lock_holder(fixture.root)->running);

    std::string error;
    std::optional<t::TaskLock> taken = t::TaskLock::acquire(fixture.root, "task-dead", error);
    REQUIRE(taken.has_value());
    CHECK(t::running_task(fixture.root) == "task-dead");
    taken->release();

    std::ofstream{fixture.root / "task.lock"} << "garbage\n";
    CHECK_FALSE(t::TaskLock::acquire(fixture.root, "task-new", error).has_value());
    CHECK(error.find("another task holds the lock") == 0);
}
