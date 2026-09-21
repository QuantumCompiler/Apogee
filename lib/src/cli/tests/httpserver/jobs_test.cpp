#include "httpserver/jobs.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "events/bus.h"

/// The async-job substrate, driven by a scripted job: the registry, the
/// events it emits, and the rule that cancel wins.
namespace {

using apogee::events::Bus;
using apogee::events::Event;
using apogee::httpserver::JobRecord;
using apogee::httpserver::JobRegistry;
using apogee::httpserver::JobStatus;
using apogee::httpserver::JobWriter;

std::vector<std::string> drain_types(apogee::events::Subscriber& subscriber) {
    std::vector<std::string> types;
    while (const auto event = subscriber.wait_for(std::chrono::milliseconds{5})) {
        types.push_back(event->type);
    }
    return types;
}

}  // namespace

TEST_CASE("a job runs to completion and every step is an event", "[httpserver][jobs]") {
    Bus bus;
    apogee::events::Subscription subscription = apogee::events::subscribe(bus);
    JobRegistry registry{bus};

    const JobRegistry::Started started = registry.start("scripted", nlohmann::json{{"input", "x"}});
    CHECK(started.id.rfind("job_", 0) == 0);
    CHECK(started.id.size() == 16);
    CHECK_FALSE(started.cancellation.stop_requested());

    registry.progress(started.id, "half way", nlohmann::json{{"done", 1}, {"total", 2}});
    REQUIRE(registry.get(started.id).has_value());
    CHECK(registry.get(started.id)->status == JobStatus::Running);
    CHECK(registry.get(started.id)->message == "half way");

    registry.finish(started.id, nlohmann::json{{"output", "y"}});
    const JobRecord done = *registry.get(started.id);
    CHECK(done.status == JobStatus::Succeeded);
    CHECK(done.result["output"] == "y");
    CHECK(done.message.empty());
    CHECK_FALSE(done.finished.empty());

    CHECK(
        drain_types(subscription.subscriber()) ==
        std::vector<std::string>{"admin.job.started", "admin.job.progress", "admin.job.completed"});

    const nlohmann::json json = apogee::httpserver::job_json(done);
    CHECK(json["status"] == "succeeded");
    CHECK(json["kind"] == "scripted");
    CHECK(json["result"]["output"] == "y");
    CHECK_FALSE(json.contains("error"));
}

TEST_CASE("cancel fires the token, and nothing overwrites a cancelled job", "[httpserver][jobs]") {
    Bus bus;
    apogee::events::Subscription subscription = apogee::events::subscribe(bus);
    JobRegistry registry{bus};
    const JobRegistry::Started started = registry.start("scripted", nlohmann::json::object());

    // The worker: runs under the token, dies when it is cancelled, and -- like
    // a real worker -- reports its death as a failure.
    std::thread worker{[&registry, started] {
        while (!started.cancellation.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        registry.fail(started.id, "killed");
        registry.finish(started.id, nlohmann::json{{"should", "not land"}});
    }};

    const auto cancelled = registry.cancel(started.id);
    REQUIRE(cancelled.has_value());
    CHECK(cancelled->status == JobStatus::Cancelled);
    worker.join();

    // THE rule: cancelled stays cancelled. The worker's fail and finish were
    // no-ops, and neither emitted anything.
    const JobRecord final = *registry.get(started.id);
    CHECK(final.status == JobStatus::Cancelled);
    CHECK(final.error.empty());
    CHECK(final.result.is_null());
    CHECK(drain_types(subscription.subscriber()) ==
          std::vector<std::string>{"admin.job.started", "admin.job.cancelled"});

    // Idempotent, and an unknown id is nothing.
    CHECK(registry.cancel(started.id)->status == JobStatus::Cancelled);
    CHECK_FALSE(registry.cancel("job_nope").has_value());
    CHECK_FALSE(registry.get("job_nope").has_value());
}

TEST_CASE("a failed job keeps its error and a finished one ignores a late fail",
          "[httpserver][jobs]") {
    Bus bus;
    JobRegistry registry{bus};
    const JobRegistry::Started failing = registry.start("scripted", nlohmann::json::object());
    registry.fail(failing.id, "disk full");
    registry.finish(failing.id, nlohmann::json{{"late", true}});
    CHECK(registry.get(failing.id)->status == JobStatus::Failed);
    CHECK(registry.get(failing.id)->error == "disk full");
    CHECK(apogee::httpserver::job_json(*registry.get(failing.id))["error"] == "disk full");

    const JobRegistry::Started fine = registry.start("scripted", nlohmann::json::object());
    registry.finish(fine.id, nlohmann::json::object());
    registry.fail(fine.id, "too late");
    CHECK(registry.get(fine.id)->status == JobStatus::Succeeded);
    // Cancel is idempotent on any final state: a succeeded job stays
    // succeeded, and cancelling it emits nothing.
    apogee::events::Subscription late = apogee::events::subscribe(bus);
    REQUIRE(registry.cancel(fine.id).has_value());
    CHECK(registry.cancel(fine.id)->status == JobStatus::Succeeded);
    CHECK(registry.get(fine.id)->status == JobStatus::Succeeded);
    CHECK(drain_types(late.subscriber()).empty());

    // Newest first.
    const std::vector<JobRecord> listed = registry.list();
    REQUIRE(listed.size() == 2);
    CHECK((listed[0].id == fine.id || listed[0].started >= listed[1].started));
}

TEST_CASE("a job writer turns lines into progress events", "[httpserver][jobs]") {
    Bus bus;
    apogee::events::Subscription subscription = apogee::events::subscribe(bus);
    JobRegistry registry{bus};
    const JobRegistry::Started started = registry.start("scripted", nlohmann::json::object());
    (void)drain_types(subscription.subscriber());

    JobWriter writer{registry, started.id};
    writer.write("first li");
    writer.write("ne\n\nsecond line\r\nthird");
    CHECK(registry.get(started.id)->message == "second line");
    writer.flush();
    CHECK(registry.get(started.id)->message == "third");
    CHECK(drain_types(subscription.subscriber()) == std::vector<std::string>{"admin.job.progress",
                                                                             "admin.job.progress",
                                                                             "admin.job.progress"});
}
