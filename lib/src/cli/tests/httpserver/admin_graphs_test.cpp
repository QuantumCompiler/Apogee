#include "httpserver/admin_graphs.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "agentloop/graph_context.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "httpserver/http_types.h"
#include "support/env_guard.h"

/// The graphs: config slice, and its parity proof: an entry made over HTTP
/// and one made by the CLI are byte-identical; the CLI's rules answer with
/// the status a client acts on; `built` reports the database; a delete
/// leaves the database alone.
namespace {

using apogee::httpserver::admin_create_graph;
using apogee::httpserver::admin_delete_graph_config;
using apogee::httpserver::admin_get_graph;
using apogee::httpserver::admin_list_graphs;
using apogee::httpserver::admin_put_graph;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

struct Fixture {
    apogee::testing::TempDir home{"admin-graphs-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path cli_config = home.path() / "cli" / "config" / "config.yaml";
    std::filesystem::path http_config = home.path() / "http" / "config" / "config.yaml";
    apogee::harness::Config startup;

    Fixture() {
        for (const std::filesystem::path& path : {cli_config, http_config}) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream{path, std::ios::binary} << apogee::harness::config_template();
            apogee::harness::BackendConfig local;
            local.type = apogee::harness::BackendType::Mock;
            apogee::harness::edit_config_file(path, [&](std::string_view content) {
                return apogee::harness::append_backend(content, "local", local, false);
            });
        }
        startup = apogee::harness::load_config(http_config);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = http_config, .startup = &startup};
    }

    int cli(const std::vector<std::string>& args) const {
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", cli_config.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        return root.run(static_cast<int>(argv.size()), argv.data());
    }

    [[nodiscard]] static std::string bytes(const std::filesystem::path& path) {
        std::ifstream in{path, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
};

HttpRequest with_body(std::string method, const nlohmann::json& body) {
    HttpRequest request;
    request.method = std::move(method);
    request.body = body.dump();
    request.remote_address = "127.0.0.1";
    return request;
}

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

}  // namespace

TEST_CASE("an HTTP-created graphs: entry is byte-identical to a CLI-created one",
          "[httpserver][admin][graphs][parity]") {
    const Fixture fixture;
    REQUIRE(fixture.cli({"config", "add-graph", "work", "--collections", "docs,meetings",
                         "--extract-backend", "local", "--hops", "2"}) == 0);
    const HttpResponse created = admin_create_graph(
        fixture.context(), with_body("POST", nlohmann::json{{"name", "work"},
                                                            {"collections", {"docs", "meetings"}},
                                                            {"extract_backend", "local"},
                                                            {"hops", 2}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json data = parsed(created)["data"];
    CHECK(data["name"] == "work");
    CHECK(data["collections"] == nlohmann::json({"docs", "meetings"}));
    CHECK(data["extract_backend"] == "local");
    CHECK(data["hops"] == 2);
    CHECK(data["max_entities"] == 8);
    CHECK(data["built"] == false);
    // Unknown members are warnings, never a refusal: ingest registers them.
    REQUIRE(parsed(created).contains("warnings"));
    CHECK(parsed(created)["warnings"].size() == 2);

    const HttpResponse listed = admin_list_graphs(fixture.context());
    REQUIRE(listed.status == 200);
    CHECK(parsed(listed)["data"].size() == 1);
    CHECK(parsed(listed)["data"][0]["name"] == "work");
    CHECK(parsed(admin_get_graph(fixture.context(), "WORK"))["data"]["name"] == "work");
    CHECK(admin_get_graph(fixture.context(), "nope").status == 404);

    // Built follows the database's existence.
    {
        apogee::embedstore::Store built{apogee::agentloop::graph_db_path("work")};
    }
    CHECK(parsed(admin_get_graph(fixture.context(), "work"))["data"]["built"] == true);
}

TEST_CASE("POST refuses what the CLI refuses with a 400, and an existing name with a 409",
          "[httpserver][admin][graphs][refusals]") {
    const Fixture fixture;
    const auto post = [&](const nlohmann::json& body) {
        return admin_create_graph(fixture.context(), with_body("POST", body));
    };
    CHECK(post(nlohmann::json{{"collections", {"docs"}}}).status == 400);  // no name
    CHECK(post(nlohmann::json{{"name", "work"}}).status == 400);           // no members
    CHECK(post(nlohmann::json{{"name", "work"}, {"collections", "docs"}}).status == 400);
    CHECK(post(nlohmann::json{{"name", "work"}, {"collections", {"docs"}}, {"hops", 3}}).status ==
          400);
    CHECK(post(nlohmann::json{
                   {"name", "work"}, {"collections", {"docs"}}, {"extract_backend", "ghost"}})
              .status == 400);
    CHECK(post(nlohmann::json{{"name", "../x"}, {"collections", {"docs"}}}).status == 400);
    // The collision ban: a collection's name, whether registered or on disk.
    apogee::harness::edit_config_file(fixture.http_config, [](std::string_view content) {
        return apogee::harness::append_embedding(content, "docs",
                                                 apogee::harness::EmbeddingConfig{}, false);
    });
    const HttpResponse collides = post(nlohmann::json{{"name", "docs"}, {"collections", {"docs"}}});
    CHECK(collides.status == 400);
    CHECK(parsed(collides)["error"]["message"].get<std::string>().find("collection name") !=
          std::string::npos);
    {
        apogee::embedstore::Store on_disk{fixture.home.path() / "embeddings" / "tickets.db"};
    }
    CHECK(post(nlohmann::json{{"name", "tickets"}, {"collections", {"docs"}}}).status == 400);

    REQUIRE(post(nlohmann::json{{"name", "work"}, {"collections", {"docs"}}}).status == 201);
    const HttpResponse duplicate =
        post(nlohmann::json{{"name", "work"}, {"collections", {"docs"}}});
    CHECK(duplicate.status == 409);
    CHECK(parsed(duplicate)["error"]["type"] == "conflict");
    CHECK(post(nlohmann::json{{"name", "Work"}, {"collections", {"docs"}}}).status == 409);
}

TEST_CASE("PUT replaces the entry in place and DELETE removes it, leaving the database",
          "[httpserver][admin][graphs][put][delete]") {
    const Fixture fixture;
    REQUIRE(admin_create_graph(
                fixture.context(),
                with_body("POST", nlohmann::json{{"name", "work"}, {"collections", {"docs"}}}))
                .status == 201);
    const std::string before = Fixture::bytes(fixture.http_config);
    CHECK(admin_put_graph(fixture.context(), "nope",
                          with_body("PUT", nlohmann::json{{"collections", {"docs"}}}))
              .status == 404);
    CHECK(admin_put_graph(
              fixture.context(), "work",
              with_body("PUT", nlohmann::json{{"name", "other"}, {"collections", {"docs"}}}))
              .status == 400);
    const HttpResponse replaced = admin_put_graph(
        fixture.context(), "work",
        with_body("PUT",
                  nlohmann::json{{"collections", {"docs", "meetings"}}, {"max_entities", 4}}));
    REQUIRE(replaced.status == 200);
    CHECK(parsed(replaced)["data"]["max_entities"] == 4);
    const apogee::harness::Config loaded = apogee::harness::load_config(fixture.http_config);
    REQUIRE(loaded.find_graph("work") != nullptr);
    CHECK(loaded.find_graph("work")->collections == std::vector<std::string>{"docs", "meetings"});
    CHECK(loaded.graphs.size() == 1);
    CHECK(Fixture::bytes(fixture.http_config) != before);

    {
        apogee::embedstore::Store built{apogee::agentloop::graph_db_path("work")};
    }
    const HttpResponse deleted = admin_delete_graph_config(fixture.context(), "work");
    REQUIRE(deleted.status == 200);
    CHECK(parsed(deleted)["deleted"] == "work");
    CHECK(apogee::harness::load_config(fixture.http_config).graphs.empty());
    CHECK(std::filesystem::exists(apogee::agentloop::graph_db_path("work")));  // data stays
    CHECK(admin_delete_graph_config(fixture.context(), "work").status == 404);
}
