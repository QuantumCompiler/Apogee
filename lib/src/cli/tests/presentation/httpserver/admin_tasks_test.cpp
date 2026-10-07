#include "httpserver/admin_tasks.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>

#include "contracts/config.h"
#include "contracts/layout.h"
#include "events/bus.h"
#include "harness/harness.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "support/env_guard.h"
#include "tasks/ledger.h"
#include "tasks/task.h"
#include "tasks/view.h"

/// The task reads on the control plane (27j): the list newest first, the
/// newest fifty unless `?all=true`, `data` never null; one task's view with
/// an honest 404, a 400 for an id that is not one and a 500 that never names
/// the ledger's path; whether a process runs it, from the lock; and every
/// route behind the gate, with no task control reachable by any method.
namespace {

using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
namespace t = apogee::tasks;

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

struct Fixture {
    apogee::testing::TempDir home{"admin-tasks-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path root = home.path() / "tasks";

    /// A task's ledger, written as the CLI writes it.
    [[nodiscard]] t::Task save(const std::string& id, std::string_view status,
                               const std::string& goal = "Find the answer") const {
        t::Task task;
        task.id = id;
        task.goal = goal;
        task.checks = {{t::CheckKind::Require, "42"}};
        task.session_id = "20261004-120000-abcd";
        task.working_directory = "/work";
        task.status = std::string{status};
        task.created_at = "2026-10-04T12:00:00Z";
        t::record_transition(task, t::kCreatedEvent, task.created_at, 0, goal);
        REQUIRE(t::save_task(root, task).empty());
        return task;
    }

    static HttpRequest get(const std::string& path) {
        HttpRequest request;
        request.method = "GET";
        request.path = path;
        return request;
    }
};

}  // namespace

TEST_CASE("the task list is newest first, fifty unless all, and never null",
          "[httpserver][admin][tasks]") {
    Fixture fixture;
    HttpResponse response = apogee::httpserver::admin_list_tasks(Fixture::get("/v1/admin/tasks"));
    REQUIRE(response.status == 200);
    CHECK(parsed(response) ==
          nlohmann::json{{"object", "list"}, {"data", nlohmann::json::array()}, {"total", 0}});

    for (int index = 0; index < 52; ++index) {
        (void)fixture.save("task-20261004-1200" + std::to_string(10 + index), t::kDone,
                           "goal " + std::to_string(index));
    }
    response = apogee::httpserver::admin_list_tasks(Fixture::get("/v1/admin/tasks"));
    REQUIRE(response.status == 200);
    nlohmann::json body = parsed(response);
    CHECK(body["total"] == 52);
    REQUIRE(body["data"].size() == 50);
    CHECK(body["data"][0]["id"] == "task-20261004-120061");
    CHECK(body["data"][0] == nlohmann::json::parse(R"({"id": "task-20261004-120061",
        "status": "done", "rounds_used": 0, "rounds_budget": 8, "goal": "goal 51"})"));
    // The body is the listing's document -- the one `task list
    // --output-format json` prints.
    CHECK(response.body ==
          t::to_json(t::make_task_list(t::list_tasks(fixture.root), false)).dump());

    HttpRequest all = Fixture::get("/v1/admin/tasks");
    all.query["all"] = "true";
    CHECK(parsed(apogee::httpserver::admin_list_tasks(all))["data"].size() == 52);
    all.query["all"] = "yes";
    CHECK(apogee::httpserver::admin_list_tasks(all).status == 400);
}

TEST_CASE("one task's view, an honest 404, and refusals that never name the ledger's path",
          "[httpserver][admin][tasks]") {
    Fixture fixture;
    const t::Task task = fixture.save("task-20261004-120000", t::kHalted);
    HttpResponse response = apogee::httpserver::admin_get_task(task.id);
    REQUIRE(response.status == 200);
    CHECK(response.body == t::to_json(t::make_task_view(task, std::nullopt)).dump());
    CHECK(parsed(response)["status"] == "halted");

    CHECK(apogee::httpserver::admin_get_task("task-20991231-000000").status == 404);
    CHECK(apogee::httpserver::admin_get_task("..").status == 400);
    CHECK(apogee::httpserver::admin_get_task("a/b").status == 400);

    // An unreadable ledger is a 500 that says so -- and never where it is.
    const std::filesystem::path broken = t::ledger_path(fixture.root, "task-20261004-130000");
    std::filesystem::create_directories(broken.parent_path());
    std::ofstream{broken} << "{ not json";
    response = apogee::httpserver::admin_get_task("task-20261004-130000");
    CHECK(response.status == 500);
    CHECK(response.body.find(fixture.root.string()) == std::string::npos);
    CHECK(response.body.find("task.json") == std::string::npos);
    // And the list leaves it out, as `task list` does.
    CHECK(parsed(apogee::httpserver::admin_list_tasks(Fixture::get("/v1/admin/tasks")))["total"] ==
          1);
}

TEST_CASE("a running task's view names the process that holds its lock",
          "[httpserver][admin][tasks]") {
    Fixture fixture;
    const t::Task task = fixture.save("task-20261004-120000", t::kRunning);
    // Nothing runs it: interrupted, and resumable.
    nlohmann::json body = parsed(apogee::httpserver::admin_get_task(task.id));
    CHECK(body["process"].is_null());
    CHECK(body["interrupted"] == true);
    std::string error;
    std::optional<t::TaskLock> lock = t::TaskLock::acquire(fixture.root, task.id, error);
    REQUIRE(lock.has_value());
    body = parsed(apogee::httpserver::admin_get_task(task.id));
    CHECK(body["process"].is_number_integer());
    CHECK(body["interrupted"] == false);
    lock->release();
}

TEST_CASE("the task routes are gated, served through the mux, and no task control has a route",
          "[httpserver][admin][tasks][mux]") {
    Fixture fixture;
    const t::Task task = fixture.save("task-20261004-120000", t::kHalted);
    const apogee::harness::Config config;
    apogee::harness::Harness harness{config};
    apogee::httpserver::Handler handler{harness, {}, nullptr};
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminHandler admin{{}, jobs, bus};
    const apogee::httpserver::Mux mux{handler, admin, "secret"};

    for (const std::string& path : {std::string{"/v1/admin/tasks"}, "/v1/admin/tasks/" + task.id}) {
        HttpRequest probe = Fixture::get(path);
        INFO(path);
        CHECK(mux.dispatch(probe).status == 401);
        probe.headers["authorization"] = "Bearer secret";
        CHECK(mux.dispatch(probe).status == 200);
    }
    // Run, resume, halt, cancel -- by any method, at any path a client might
    // guess -- reach nothing; the ledger is untouched.
    for (const char* method : {"POST", "PUT", "PATCH", "DELETE"}) {
        for (const std::string& path :
             {std::string{"/v1/admin/tasks"}, "/v1/admin/tasks/" + task.id,
              "/v1/admin/tasks/" + task.id + "/run", "/v1/admin/tasks/" + task.id + "/resume",
              "/v1/admin/tasks/" + task.id + "/halt", "/v1/admin/tasks/" + task.id + "/cancel",
              std::string{"/v1/admin/tasks/run"}}) {
            HttpRequest control = Fixture::get(path);
            control.method = method;
            control.headers["authorization"] = "Bearer secret";
            INFO(method << " " << path);
            const int status = mux.dispatch(control).status;
            CHECK(status != 200);
            CHECK(status != 202);
        }
    }
    std::string error;
    const std::optional<t::Task> after = t::load_task(fixture.root, task.id, error);
    REQUIRE(after.has_value());
    CHECK(after->status == t::kHalted);
    CHECK(t::read_request(fixture.root, task.id) == t::Request::None);
}
