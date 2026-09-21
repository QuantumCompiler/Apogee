#include "httpserver/admin_agents.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
#include "harness/config.h"
#include "httpserver/http_types.h"
#include "support/env_guard.h"

/// The agents slice of the control plane, and its parity proof: an agent
/// made over HTTP and one made by the CLI leave byte-identical files and a
/// byte-identical config entry.
namespace {

using apogee::httpserver::admin_create_agent;
using apogee::httpserver::admin_delete_agent;
using apogee::httpserver::admin_get_agent;
using apogee::httpserver::admin_list_agents;
using apogee::httpserver::admin_put_agent;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

struct Fixture {
    apogee::testing::TempDir home{"admin-agents-" + std::to_string(std::random_device{}())};
    std::filesystem::path cli_config = home.path() / "cli" / "config" / "config.yaml";
    std::filesystem::path http_config = home.path() / "http" / "config" / "config.yaml";
    apogee::harness::Config startup;

    Fixture() {
        for (const std::filesystem::path& path : {cli_config, http_config}) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream{path, std::ios::binary} << apogee::harness::config_template();
        }
        startup = apogee::harness::load_config(http_config);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = http_config, .startup = &startup};
    }

    void cli(const std::vector<std::string>& args) const {
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", cli_config.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        REQUIRE(root.run(static_cast<int>(argv.size()), argv.data()) == 0);
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

TEST_CASE("an HTTP-created agent is byte-identical to a CLI-created one: files and config",
          "[httpserver][admin][agents][parity]") {
    const Fixture fixture;
    fixture.cli({"agents", "create", "reviewer", "--description", "Reviews code", "--tools",
                 "read-only", "--collection", "adrs"});
    const HttpResponse created = admin_create_agent(
        fixture.context(), with_body("POST", nlohmann::json{{"name", "reviewer"},
                                                            {"description", "Reviews code"},
                                                            {"tools", "read-only"},
                                                            {"collection", "adrs"}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    CHECK(Fixture::bytes(fixture.home.path() / "cli" / "prompts" / "reviewer.txt") ==
          Fixture::bytes(fixture.home.path() / "http" / "prompts" / "reviewer.txt"));
    CHECK(Fixture::bytes(fixture.home.path() / "cli" / "schemas" / "reviewer-output.json") ==
          Fixture::bytes(fixture.home.path() / "http" / "schemas" / "reviewer-output.json"));
    const nlohmann::json body = parsed(created);
    CHECK(body["name"] == "reviewer");
    CHECK(body["tools"] == "read-only");
    CHECK(body["collection"] == "adrs");
    CHECK(body["save_subdir"] == "reviewer");
    CHECK(body["bundled"] == false);
    CHECK(body["prompt_path"].get<std::string>().ends_with("/http/prompts/reviewer.txt"));

    // A second create collides; force replaces; a bad policy is a 400.
    CHECK(admin_create_agent(fixture.context(),
                             with_body("POST", nlohmann::json{{"name", "reviewer"}}))
              .status == 409);
    CHECK(
        admin_create_agent(fixture.context(),
                           with_body("POST", nlohmann::json{{"name", "reviewer"}, {"force", true}}))
            .status == 201);
    CHECK(admin_create_agent(fixture.context(),
                             with_body("POST", nlohmann::json{{"name", "x"}, {"tools", "maybe"}}))
              .status == 400);
    CHECK(admin_create_agent(fixture.context(), with_body("POST", nlohmann::json{{"tools", "all"}}))
              .status == 400);
    CHECK(admin_create_agent(fixture.context(),
                             with_body("POST", nlohmann::json{{"name", "x"}, {"mcp", "notalist"}}))
              .status == 400);
}

TEST_CASE("list shows the bundled three and the entries; get inlines the bodies",
          "[httpserver][admin][agents]") {
    const Fixture fixture;
    const HttpResponse listed = admin_list_agents(fixture.context());
    REQUIRE(listed.status == 200);
    const nlohmann::json data = parsed(listed)["data"];
    REQUIRE(data.size() == 3);
    CHECK(data[0]["name"] == "security-review");
    CHECK(data[0]["bundled"] == true);
    CHECK(data[0]["tools"] == "read-only");

    // A bundled agent's bodies come from the compiled-in text until seeded.
    const HttpResponse bundled = admin_get_agent(fixture.context(), "release-notes");
    REQUIRE(bundled.status == 200);
    const nlohmann::json view = parsed(bundled);
    REQUIRE(view["prompt_bodies"].size() == 1);
    CHECK(view["prompt_bodies"][0]["present"] == false);
    CHECK(view["prompt_bodies"][0]["body"].get<std::string>().find("release notes") !=
          std::string::npos);
    CHECK(view["schema_bodies"][0]["body"].get<std::string>().find("human_summary") !=
          std::string::npos);

    // A created agent's bodies are its files.
    (void)admin_create_agent(fixture.context(),
                             with_body("POST", nlohmann::json{{"name", "mine"},
                                                              {"prompt_body", "MY PROMPT\n"},
                                                              {"no_schema", true}}));
    const HttpResponse mine = admin_get_agent(fixture.context(), "mine");
    REQUIRE(mine.status == 200);
    CHECK(parsed(mine)["prompt_bodies"][0]["body"] == "MY PROMPT\n");
    CHECK(parsed(mine)["prompt_bodies"][0]["present"] == true);
    CHECK(parsed(mine)["schema_bodies"].empty());
    CHECK(parsed(admin_list_agents(fixture.context()))["data"].size() == 4);
    CHECK(admin_get_agent(fixture.context(), "nope").status == 404);

    // An entry overriding a bundled agent is listed in its slot.
    (void)admin_create_agent(
        fixture.context(),
        with_body("POST", nlohmann::json{{"name", "security-review"}, {"model", "local"}}));
    const nlohmann::json after = parsed(admin_list_agents(fixture.context()))["data"];
    REQUIRE(after.size() == 4);
    CHECK(after[0]["name"] == "security-review");
    CHECK(after[0]["overrides_bundled"] == true);
    CHECK(after[0]["model"] == "local");
}

TEST_CASE("put edits in place with force implied; delete removes the entry and, asked, the files",
          "[httpserver][admin][agents]") {
    const Fixture fixture;
    (void)admin_create_agent(fixture.context(), with_body("POST", nlohmann::json{{"name", "ed"}}));
    const HttpResponse put = admin_put_agent(
        fixture.context(), "ed",
        with_body("PUT", nlohmann::json{{"prompt_body", "EDITED\n"}, {"tools", "all"}}));
    REQUIRE(put.status == 200);
    CHECK(parsed(put)["tools"] == "all");
    CHECK(Fixture::bytes(fixture.home.path() / "http" / "prompts" / "ed.txt") == "EDITED\n");
    // The name from the path, whatever the body says.
    const HttpResponse renamed = admin_put_agent(
        fixture.context(), "ed", with_body("PUT", nlohmann::json{{"name", "other"}}));
    CHECK(renamed.status == 200);
    CHECK(parsed(renamed)["name"] == "ed");

    HttpRequest keep;
    keep.method = "DELETE";
    const HttpResponse deleted = admin_delete_agent(fixture.context(), "ed", keep);
    REQUIRE(deleted.status == 200);
    CHECK(parsed(deleted)["deleted"] == "ed");
    CHECK(parsed(deleted)["files_removed"].empty());
    CHECK(std::filesystem::exists(fixture.home.path() / "http" / "prompts" / "ed.txt"));
    CHECK(admin_delete_agent(fixture.context(), "ed", keep).status == 404);

    (void)admin_create_agent(fixture.context(),
                             with_body("POST", nlohmann::json{{"name", "gone"}}));
    HttpRequest purge;
    purge.method = "DELETE";
    purge.query["purge"] = "true";
    const HttpResponse purged = admin_delete_agent(fixture.context(), "gone", purge);
    REQUIRE(purged.status == 200);
    CHECK(parsed(purged)["files_removed"].size() == 2);
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "http" / "prompts" / "gone.txt"));
    CHECK_FALSE(
        std::filesystem::exists(fixture.home.path() / "http" / "schemas" / "gone-output.json"));
    // A bundled agent with no entry has nothing to delete.
    CHECK(admin_delete_agent(fixture.context(), "merge-request", keep).status == 404);
    CHECK(apogee::harness::load_config(fixture.http_config).agents.empty());
}
