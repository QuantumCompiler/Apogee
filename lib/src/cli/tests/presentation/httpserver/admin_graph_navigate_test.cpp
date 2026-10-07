#include "httpserver/admin_graph_navigate.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "agentloop/graph_context.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// The navigation reads served (27l): `{name}` graphs-first as `--graph`
/// resolves it; a resolution failure in the error envelope with the CLI's
/// message -- `404` for nothing by that name or a graph not built, `400` for
/// an ambiguous name (its `candidates` beside the message), a cap out of
/// range, or a parameter that is missing or not a number. The `200` bodies
/// are pinned to the CLI's and the tools' in `graph_navigate_test.cpp`.
namespace {

using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

struct Fixture {
    apogee::testing::TempDir home{"admin-graph-navigate-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    Fixture() {
        std::filesystem::create_directories(config_path.parent_path());
        std::ofstream{config_path, std::ios::binary}
            << "graphs:\n  work:\n    collections: [notes]\n  later:\n    collections: [notes]\n";
        apogee::embedstore::Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        apogee::testing::build_navigation_graph(work, notes, "notes");
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = config_path};
    }
};

[[nodiscard]] HttpRequest get(std::map<std::string, std::string> query) {
    HttpRequest request;
    request.method = "GET";
    request.query = std::move(query);
    return request;
}

[[nodiscard]] nlohmann::json error_of(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    REQUIRE(body.contains("error"));
    return body["error"];
}

}  // namespace

TEST_CASE("served navigation reads answer 200 with the document", "[httpserver][admin][graph]") {
    const Fixture fixture;
    const HttpResponse path = apogee::httpserver::admin_graph_path(
        fixture.context(), "work", get({{"from", "main"}, {"to", "pkg.lib.helper"}}));
    CHECK(path.status == 200);
    CHECK(path.content_type == "application/json");
    const nlohmann::json body = nlohmann::json::parse(path.body);
    CHECK(body["object"] == "graph.path");
    CHECK(body["hops"] == 2);
    CHECK(body["matched"]["from"] == "unqualified");

    const HttpResponse explain = apogee::httpserver::admin_graph_explain(fixture.context(), "work",
                                                                         get({{"node", "Vault"}}));
    CHECK(explain.status == 200);
    CHECK(nlohmann::json::parse(explain.body)["decisions"][0]["name"] == "kr-0001");
}

TEST_CASE("served navigation failures carry the CLI's message and the right status",
          "[httpserver][admin][graph]") {
    const Fixture fixture;
    const AdminConfigContext context = fixture.context();

    const HttpResponse ambiguous =
        apogee::httpserver::admin_graph_explain(context, "work", get({{"node", "helper"}}));
    CHECK(ambiguous.status == 400);
    const nlohmann::json ambiguity = error_of(ambiguous);
    CHECK(ambiguity["message"].get<std::string>().starts_with(
        "'helper' names 2 nodes in graph 'work' -- name one: "));
    REQUIRE(ambiguity["candidates"].size() == 2);
    CHECK(ambiguity["candidates"][0]["type"] == "function");

    const HttpResponse missing =
        apogee::httpserver::admin_graph_explain(context, "work", get({{"node", "the vault"}}));
    CHECK(missing.status == 404);
    CHECK(error_of(missing)["type"] == "not_found_error");
    CHECK(error_of(missing)["candidates"][0]["name"] == "Vault");

    CHECK(apogee::httpserver::admin_graph_explain(context, "later", get({{"node", "Vault"}}))
              .status == 404);
    CHECK(
        apogee::httpserver::admin_graph_explain(context, "nope", get({{"node", "Vault"}})).status ==
        404);
    CHECK(apogee::httpserver::admin_graph_explain(context, "work", get({})).status == 400);
    CHECK(apogee::httpserver::admin_graph_path(context, "work", get({{"from", "main"}})).status ==
          400);

    struct Row {
        std::string verb;
        std::map<std::string, std::string> query;
        std::string part;
    };

    const std::vector<Row> rows{
        {"path",
         {{"from", "main"}, {"to", "pkg.app.run"}, {"max_hops", "33"}},
         "the hop cap must be between 1 and 32 (got 33)"},
        {"path",
         {{"from", "main"}, {"to", "pkg.app.run"}, {"max_hops", "x"}},
         "the max_hops query parameter must be an integer"},
        {"path",
         {{"from", "main"}, {"to", "pkg.app.run"}, {"directed", "maybe"}},
         "the directed query parameter must be true or false"},
        {"explain", {{"node", "Vault"}, {"max_neighbors", "101"}}, "the neighbour cap"},
        {"neighbors", {{"node", "Vault"}, {"direction", "up"}}, "must be both, out or in"},
        {"query", {{"q", "vault"}, {"hops", "3"}}, "a query walks 1 or 2 hops (got 3)"},
        {"query", {{"q", "vault"}, {"max_entities", "1e3"}}, "must be an integer"},
        {"query", {}, "the q query parameter is required"},
    };
    for (const Row& row : rows) {
        INFO(row.verb << " " << row.part);
        HttpResponse response;
        if (row.verb == "path") {
            response = apogee::httpserver::admin_graph_path(context, "work", get(row.query));
        } else if (row.verb == "explain") {
            response = apogee::httpserver::admin_graph_explain(context, "work", get(row.query));
        } else if (row.verb == "neighbors") {
            response = apogee::httpserver::admin_graph_neighbors(context, "work", get(row.query));
        } else {
            response = apogee::httpserver::admin_graph_query(context, "work", get(row.query));
        }
        CHECK(response.status == 400);
        CHECK(error_of(response)["message"].get<std::string>().find(row.part) != std::string::npos);
    }
}
