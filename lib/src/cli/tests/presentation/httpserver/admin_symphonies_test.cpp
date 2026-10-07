#include "httpserver/admin_symphonies.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "httpserver/http_types.h"
#include "support/env_guard.h"

/// The symphonies slice of the control plane (27q), and its parity proof: a
/// symphony written over HTTP and one written by the CLI leave byte-identical
/// config entries -- create, edit's twin, delete -- and the reads serve the
/// CLI's `--output-format json` documents byte for byte.
namespace {

using apogee::httpserver::admin_create_symphony;
using apogee::httpserver::admin_delete_symphony;
using apogee::httpserver::admin_get_symphony;
using apogee::httpserver::admin_list_symphonies;
using apogee::httpserver::admin_put_symphony;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

constexpr std::string_view kBase =
    "# keep me\nbackends:\n  l3b:\n    type: mock\nmodels:\n  default: l3b\n";

struct Fixture {
    apogee::testing::TempDir home{"admin-symphonies-" + std::to_string(std::random_device{}())};
    std::filesystem::path cli_config = home.path() / "cli" / "config" / "config.yaml";
    std::filesystem::path http_config = home.path() / "http" / "config" / "config.yaml";
    apogee::harness::Config startup;

    Fixture() {
        for (const std::filesystem::path& path : {cli_config, http_config}) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream{path, std::ios::binary} << kBase;
        }
        startup = apogee::harness::load_config(http_config);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = http_config, .startup = &startup};
    }

    /// The CLI against `config`, its stdout returned; it must succeed.
    static std::string cli(const std::filesystem::path& config,
                           const std::vector<std::string>& args) {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            throw;
        }
        std::cout.rdbuf(old_out);
        REQUIRE(code == 0);
        return captured.str();
    }

    [[nodiscard]] static std::string bytes(const std::filesystem::path& path) {
        const std::ifstream in{path, std::ios::binary};
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

nlohmann::json pair_body() {
    return nlohmann::json::parse(R"({
    "name": "pair",
    "description": "Two steps.",
    "stages": [
        {"name": "first", "role": "utility", "prompt": "Do: {{input}}"},
        {"name": "second", "role": "chat", "prompt": "Check {{first}} against {{input}}"}
    ]
})");
}

}  // namespace

TEST_CASE("an HTTP-written symphony is byte-identical to a CLI-written one",
          "[httpserver][admin][symphonies][parity]") {
    const Fixture fixture;
    (void)Fixture::cli(fixture.cli_config,
                       {"symphonies", "create", "pair", "--description", "Two steps.", "--stage",
                        "first:utility:Do: {{input}}", "--stage",
                        "second:chat:Check {{first}} against {{input}}"});
    const HttpResponse created =
        admin_create_symphony(fixture.context(), with_body("POST", pair_body()));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json body = nlohmann::json::parse(created.body);
    CHECK(body["object"] == "symphony");
    CHECK(body["source"] == "config");

    // A starter copied: the CLI's --from, and the starter's view sent back.
    const nlohmann::json starter =
        nlohmann::json::parse(admin_get_symphony(fixture.context(), "summarize-verify").body);
    (void)Fixture::cli(fixture.cli_config,
                       {"symphonies", "create", "copy", "--from", "summarize-verify"});
    nlohmann::json copy = starter;
    copy["name"] = "copy";
    REQUIRE(admin_create_symphony(fixture.context(), with_body("POST", copy)).status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    // The edit twin: PUT is create with force, as `edit` writes back.
    nlohmann::json changed = pair_body();
    changed["description"] = "Changed.";
    (void)Fixture::cli(fixture.cli_config,
                       {"symphonies", "create", "pair", "--force", "--description", "Changed.",
                        "--stage", "first:utility:Do: {{input}}", "--stage",
                        "second:chat:Check {{first}} against {{input}}"});
    REQUIRE(admin_put_symphony(fixture.context(), "pair", with_body("PUT", changed)).status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    // Delete, both ways.
    (void)Fixture::cli(fixture.cli_config, {"symphonies", "delete", "copy"});
    REQUIRE(admin_delete_symphony(fixture.context(), "copy").status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
}

TEST_CASE("the reads serve the CLI's JSON documents byte for byte",
          "[httpserver][admin][symphonies][parity]") {
    const Fixture fixture;
    REQUIRE(admin_create_symphony(fixture.context(), with_body("POST", pair_body())).status == 201);
    const HttpResponse listed = admin_list_symphonies(fixture.context());
    REQUIRE(listed.status == 200);
    CHECK(listed.body + "\n" ==
          Fixture::cli(fixture.http_config, {"symphonies", "list", "--output-format", "json"}));
    for (const std::string name : {"pair", "extract-facts"}) {
        const HttpResponse got = admin_get_symphony(fixture.context(), name);
        REQUIRE(got.status == 200);
        CHECK(got.body + "\n" == Fixture::cli(fixture.http_config, {"symphonies", "show", name,
                                                                    "--output-format", "json"}));
    }
}

TEST_CASE("what the plane refuses, and the status it says it with",
          "[httpserver][admin][symphonies]") {
    const Fixture fixture;
    const std::string before = Fixture::bytes(fixture.http_config);
    nlohmann::json backend = pair_body();
    backend["stages"][0]["role"] = "l3b";
    const HttpResponse refused =
        admin_create_symphony(fixture.context(), with_body("POST", backend));
    CHECK(refused.status == 400);
    CHECK_THAT(refused.body, Catch::Matchers::ContainsSubstring("'l3b' is a backend"));
    nlohmann::json keyed = pair_body();
    keyed["stages"][0]["backend"] = "l3b";
    CHECK(admin_create_symphony(fixture.context(), with_body("POST", keyed)).status == 400);
    nlohmann::json unnamed = pair_body();
    unnamed.erase("name");
    CHECK(admin_create_symphony(fixture.context(), with_body("POST", unnamed)).status == 400);
    CHECK(admin_create_symphony(fixture.context(), with_body("POST", nlohmann::json{{"name", "x"}}))
              .status == 400);
    nlohmann::json typed = pair_body();
    typed["stages"][0]["answer_tokens"] = "lots";
    CHECK(admin_create_symphony(fixture.context(), with_body("POST", typed)).status == 400);
    CHECK(Fixture::bytes(fixture.http_config) == before);

    REQUIRE(admin_create_symphony(fixture.context(), with_body("POST", pair_body())).status == 201);
    CHECK(admin_create_symphony(fixture.context(), with_body("POST", pair_body())).status == 409);
    nlohmann::json forced = pair_body();
    forced["force"] = true;
    CHECK(admin_create_symphony(fixture.context(), with_body("POST", forced)).status == 200);

    CHECK(admin_get_symphony(fixture.context(), "nope").status == 404);
    CHECK(admin_get_symphony(fixture.context(), "../../etc/passwd").status == 404);
    const HttpResponse starter = admin_delete_symphony(fixture.context(), "describe-answer");
    CHECK(starter.status == 404);
    CHECK_THAT(starter.body, Catch::Matchers::ContainsSubstring("is a shipped starter"));
    CHECK(admin_delete_symphony(fixture.context(), "nope").status == 404);
}

TEST_CASE("a chain written over HTTP is byte-identical to one the CLI writes (27r)",
          "[httpserver][admin][symphonies][parity]") {
    const Fixture fixture;
    (void)Fixture::cli(
        fixture.cli_config,
        {"symphonies", "create", "digest", "--play", "summary:summarize-verify", "--stage",
         "note:chat:Note {{summary}}", "--play", "facts:extract-facts:{{note}}"});
    const nlohmann::json body = nlohmann::json::parse(R"({
    "name": "digest",
    "stages": [
        {"name": "summary", "play": "summarize-verify"},
        {"name": "note", "role": "chat", "prompt": "Note {{summary}}"},
        {"name": "facts", "play": "extract-facts", "input": "{{note}}"}
    ]
})");
    const HttpResponse created = admin_create_symphony(fixture.context(), with_body("POST", body));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    // The view read back is the shape a client sends: PUT it unchanged and
    // nothing moves.
    const nlohmann::json view =
        nlohmann::json::parse(admin_get_symphony(fixture.context(), "digest").body);
    CHECK(view["stages"][0] == nlohmann::json::parse(R"({"name": "summary", "play":
                                                     "summarize-verify", "image": false})"));
    CHECK(view["problems"].empty());
    const std::string written = Fixture::bytes(fixture.http_config);
    REQUIRE(admin_put_symphony(fixture.context(), "digest", with_body("PUT", view)).status == 200);
    CHECK(Fixture::bytes(fixture.http_config) == written);

    // A loop is refused over HTTP as on the command line, nothing written.
    const nlohmann::json loop = nlohmann::json::parse(R"({
    "name": "summarize-verify",
    "stages": [{"name": "back", "play": "digest"}]
})");
    const HttpResponse refused = admin_create_symphony(fixture.context(), with_body("POST", loop));
    CHECK(refused.status == 400);
    CHECK_THAT(refused.body, Catch::Matchers::ContainsSubstring(
                                 "a loop, summarize-verify → digest → summarize-verify"));
    // A stage that is both kinds is refused by the parser, never half-read.
    nlohmann::json both = body;
    both["name"] = "both";
    both["stages"][0]["role"] = "chat";
    const HttpResponse mixed = admin_create_symphony(fixture.context(), with_body("POST", both));
    CHECK(mixed.status == 400);
    CHECK_THAT(mixed.body, Catch::Matchers::ContainsSubstring("never both"));
    CHECK(Fixture::bytes(fixture.http_config) == written);
}
