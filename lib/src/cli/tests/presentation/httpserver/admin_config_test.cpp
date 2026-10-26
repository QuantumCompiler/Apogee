#include "httpserver/admin_config.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "events/bus.h"
#include "harness/harness.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/http_types.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "modelstore/sidecar.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "training/python_env.h"

/// The config slice of the control plane, and THE parity proof: an HTTP edit
/// and a CLI edit on the same starting file leave byte-identical files.
namespace {

using apogee::httpserver::admin_create_backend;
using apogee::httpserver::admin_create_mcp_server;
using apogee::httpserver::admin_delete_backend;
using apogee::httpserver::admin_delete_mcp_server;
using apogee::httpserver::admin_get_backend;
using apogee::httpserver::admin_get_mcp_server;
using apogee::httpserver::admin_list_backends;
using apogee::httpserver::admin_list_mcp_servers;
using apogee::httpserver::admin_list_permissions;
using apogee::httpserver::admin_put_permission;
using apogee::httpserver::admin_rename_backend;
using apogee::httpserver::admin_set_backend_fields;
using apogee::httpserver::admin_set_mcp_server_enabled;
using apogee::httpserver::admin_set_role;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

struct Fixture {
    apogee::testing::TempDir home{"admin-config-" + std::to_string(std::random_device{}())};
    std::filesystem::path cli_config = home.path() / "cli" / "config.yaml";
    std::filesystem::path http_config = home.path() / "http" / "config.yaml";
    apogee::harness::Config startup;

    Fixture() {
        // Both start from the shipped template -- the file a real install has.
        for (const std::filesystem::path& path : {cli_config, http_config}) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream{path, std::ios::binary} << apogee::harness::config_template();
        }
        startup = apogee::harness::load_config(http_config);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = http_config, .startup = &startup};
    }

    /// Runs the real CLI in-process against the CLI copy.
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

HttpRequest post(const nlohmann::json& body, std::string remote = "127.0.0.1") {
    HttpRequest request;
    request.method = "POST";
    request.body = body.dump();
    request.remote_address = std::move(remote);
    return request;
}

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

}  // namespace

TEST_CASE("the helper role pointers set over HTTP are byte-identical to the CLI's",
          "[httpserver][admin][parity][helpers]") {
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "helper", "--type", "mock"});
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "helper"}, {"type", "mock"}}))
                .status == 201);
    for (const auto& [verb, field] :
         {std::pair{"set-default-vision", "default_vision"},
          std::pair{"set-default-transcription", "default_transcription"},
          std::pair{"set-default-utility", "default_utility"}}) {
        CAPTURE(field);
        fixture.cli({"config", verb, "helper"});
        const HttpResponse set =
            admin_set_role(fixture.context(), field, post(nlohmann::json{{"name", "helper"}}));
        REQUIRE(set.status == 200);
        CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
        CHECK(Fixture::bytes(fixture.http_config)
                  .find(std::string{"    \""} + field + "\": \"helper\"") != std::string::npos);
    }
    // `config get` reads each one back -- vision moved to a backend of its
    // own, so no pointer can be read in place of another.
    fixture.cli({"config", "add-backend", "seer", "--type", "mock"});
    fixture.cli({"config", "set-default-vision", "seer"});
    for (const auto& [key, expected] : {std::pair{"models.default_vision", "seer\n"},
                                        std::pair{"models.default_transcription", "helper\n"},
                                        std::pair{"models.default_utility", "helper\n"}}) {
        CAPTURE(key);
        std::ostringstream out;
        std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
        fixture.cli({"config", "get", key});
        std::cout.rdbuf(old_out);
        CHECK(out.str() == expected);
    }
    // And the listing names each role and the rung it resolved on.
    const nlohmann::json listed =
        parsed(apogee::httpserver::admin_list_backends(fixture.context()));
    CHECK(listed["roles"]["default_utility"]["backend"] == "helper");
    CHECK(listed["roles"]["default_utility"]["from"] == "role_pointer");
    CHECK(listed["roles"]["default_vision"]["from"] == "role_pointer");
    CHECK(listed["roles"]["default_transcription"]["backend"] == "helper");
}

TEST_CASE("each helper role route sets its own pointer, through the mux",
          "[httpserver][admin][mux][helpers]") {
    const Fixture fixture;
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "helper"}, {"type", "mock"}}))
                .status == 201);
    apogee::harness::Harness harness{fixture.startup};
    harness.use_default_router();
    apogee::httpserver::Handler handler{harness, apogee::httpserver::HandlerOptions{}, nullptr};
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminOptions options;
    options.config_path = fixture.http_config;
    options.startup = fixture.startup;
    apogee::httpserver::AdminHandler admin{options, jobs, bus};
    const apogee::httpserver::Mux mux{handler, admin, "token"};
    for (const auto& [route, field] : {std::pair{"default-vision", "default_vision"},
                                       std::pair{"default-transcription", "default_transcription"},
                                       std::pair{"default-utility", "default_utility"}}) {
        CAPTURE(route);
        HttpRequest request = post(nlohmann::json{{"name", "helper"}});
        request.path = std::string{"/v1/admin/backends/"} + route;
        request.headers["authorization"] = "Bearer token";
        REQUIRE(mux.dispatch(request).status == 200);
        CHECK(Fixture::bytes(fixture.http_config)
                  .find(std::string{"    \""} + field + "\": \"helper\"") != std::string::npos);
    }
}

TEST_CASE("a cache type added over HTTP is byte-identical to the CLI's, and a bad one refused",
          "[httpserver][admin][parity][cache]") {
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "local", "--type", "llamacpp", "--model-path",
                 "/m/a.gguf", "--context-size", "65536", "--cache-type", "q4_0"});
    const HttpResponse created =
        admin_create_backend(fixture.context(), post(nlohmann::json{{"name", "local"},
                                                                    {"type", "llamacpp"},
                                                                    {"model_path", "/m/a.gguf"},
                                                                    {"context_size", 65536},
                                                                    {"cache_type", "q4_0"}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    CHECK(Fixture::bytes(fixture.http_config).find("      \"cache_type\": \"q4_0\"") !=
          std::string::npos);
    CHECK(parsed(created)["cache_type"] == "q4_0");

    const HttpResponse refused =
        admin_create_backend(fixture.context(), post(nlohmann::json{{"name", "other"},
                                                                    {"type", "llamacpp"},
                                                                    {"model_path", "/m/a.gguf"},
                                                                    {"cache_type", "q8"}}));
    CHECK(refused.status == 400);
    CHECK(refused.body.find("q8") != std::string::npos);
    CHECK(Fixture::bytes(fixture.http_config).find("other:") == std::string::npos);
}

TEST_CASE("a backend's sampling added over HTTP is byte-identical to the CLI's, and read back",
          "[httpserver][admin][parity][sampling]") {
    // 26h: the same knobs on both surfaces, written by the one editor.
    const Fixture fixture;
    fixture.cli({"config",    "add-backend",
                 "local",     "--type",
                 "llamacpp",  "--model-path",
                 "/m/a.gguf", "--temperature",
                 "0.7",       "--top-p",
                 "0.8",       "--top-k",
                 "20",        "--min-p",
                 "0.05",      "--repeat-penalty",
                 "1.1",       "--presence-penalty",
                 "1.5",       "--seed",
                 "42"});
    const HttpResponse created =
        admin_create_backend(fixture.context(), post(nlohmann::json{{"name", "local"},
                                                                    {"type", "llamacpp"},
                                                                    {"model_path", "/m/a.gguf"},
                                                                    {"temperature", 0.7},
                                                                    {"top_p", 0.8},
                                                                    {"top_k", 20},
                                                                    {"min_p", 0.05},
                                                                    {"repeat_penalty", 1.1},
                                                                    {"presence_penalty", 1.5},
                                                                    {"seed", 42}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json view = parsed(created);
    CHECK(view["top_p"] == 0.8);
    CHECK(view["top_k"] == 20);
    CHECK(view["min_p"] == 0.05);
    CHECK(view["repeat_penalty"] == 1.1);
    CHECK(view["presence_penalty"] == 1.5);
    CHECK(view["seed"] == 42);

    // A value the config's rules refuse is refused here too, and nothing lands.
    const HttpResponse refused = admin_create_backend(
        fixture.context(),
        post(nlohmann::json{
            {"name", "other"}, {"type", "llamacpp"}, {"model_path", "/m/a.gguf"}, {"top_p", 1.5}}));
    CHECK(refused.status >= 400);
    CHECK(refused.body.find("top_p") != std::string::npos);
    const HttpResponse typed = admin_create_backend(
        fixture.context(),
        post(nlohmann::json{
            {"name", "other"}, {"type", "llamacpp"}, {"model_path", "/m/a.gguf"}, {"top_k", 2.5}}));
    CHECK(typed.status == 400);
    CHECK(Fixture::bytes(fixture.http_config).find("other:") == std::string::npos);
}

TEST_CASE("a backend's thinking added over HTTP is byte-identical to the CLI's, and read back",
          "[httpserver][admin][parity][thinking]") {
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "local", "--type", "llamacpp", "--model-path",
                 "/m/a.gguf", "--thinking", "off", "--thinking-budget", "512"});
    const HttpResponse created =
        admin_create_backend(fixture.context(), post(nlohmann::json{{"name", "local"},
                                                                    {"type", "llamacpp"},
                                                                    {"model_path", "/m/a.gguf"},
                                                                    {"thinking", "off"},
                                                                    {"thinking_budget", 512}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json view = parsed(created);
    CHECK(view["thinking"] == "off");
    CHECK(view["thinking_budget"] == 512);

    // A mode that is not one is refused, and nothing lands.
    const HttpResponse refused =
        admin_create_backend(fixture.context(), post(nlohmann::json{{"name", "other"},
                                                                    {"type", "llamacpp"},
                                                                    {"model_path", "/m/a.gguf"},
                                                                    {"thinking", "sometimes"}}));
    CHECK(refused.status == 400);
    CHECK(refused.body.find("on, off, auto") != std::string::npos);
    const HttpResponse negative =
        admin_create_backend(fixture.context(), post(nlohmann::json{{"name", "other"},
                                                                    {"type", "llamacpp"},
                                                                    {"model_path", "/m/a.gguf"},
                                                                    {"thinking_budget", -1}}));
    CHECK(negative.status >= 400);
    CHECK(Fixture::bytes(fixture.http_config).find("other:") == std::string::npos);
}

TEST_CASE("an HTTP add-backend is byte-identical to the CLI's on the same file",
          "[httpserver][admin][parity]") {
    // The parity invariant's first end-to-end proof: not "the same fields",
    // the same BYTES -- comments, key order, separator lines and all.
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "work", "--type", "mock", "--model", "mock-1",
                 "--context-size", "4096"});
    const HttpResponse created = admin_create_backend(
        fixture.context(),
        post(nlohmann::json{
            {"name", "work"}, {"type", "mock"}, {"model", "mock-1"}, {"context_size", 4096}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json body = parsed(created);
    CHECK(body["name"] == "work");
    CHECK(body["type"] == "mock");
    CHECK(body["context_size"] == 4096);
    CHECK(body["api_key_set"] == false);

    // And the role pointer, through the same helper.
    fixture.cli({"config", "set-default", "work"});
    const HttpResponse pointed =
        admin_set_role(fixture.context(), "default", post(nlohmann::json{{"name", "work"}}));
    REQUIRE(pointed.status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    // And format, which touches whitespace only.
    fixture.cli({"config", "format"});
    REQUIRE(apogee::httpserver::admin_format_config(fixture.context()).status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    // And the delete, which is add's exact inverse on both surfaces.
    fixture.cli({"config", "delete-backend", "work"});
    const HttpResponse deleted = admin_delete_backend(fixture.context(), "work");
    REQUIRE(deleted.status == 200);
    CHECK(parsed(deleted)["deleted"] == "work");
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
}

TEST_CASE("the view never carries the key, and a literal key needs a loopback peer",
          "[httpserver][admin][secrets]") {
    const Fixture fixture;
    // A ${ENV} reference is not a secret: accepted from anywhere.
    const HttpResponse reference = admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "cloud"},
                                               {"type", "anthropic"},
                                               {"api_key", "${ANTHROPIC_API_KEY}"}},
                                "203.0.113.9"));
    REQUIRE(reference.status == 201);
    CHECK(parsed(reference)["api_key_set"] == true);
    CHECK(reference.body.find("ANTHROPIC_API_KEY") == std::string::npos);

    // A literal key from a remote peer is refused before anything is written.
    const std::string before = Fixture::bytes(fixture.http_config);
    const HttpResponse remote = admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "leaky"},
                                               {"type", "openai"},
                                               {"api_key", "sk-live-secret-1234"}},
                                "203.0.113.9"));
    CHECK(remote.status == 403);
    CHECK(Fixture::bytes(fixture.http_config) == before);
    // From loopback it is accepted -- and still never echoed.
    const HttpResponse local = admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "leaky"},
                                               {"type", "openai"},
                                               {"api_key", "sk-live-secret-1234"}},
                                "::1"));
    REQUIRE(local.status == 201);
    CHECK(local.body.find("sk-live") == std::string::npos);
    CHECK(admin_list_backends(fixture.context()).body.find("sk-live") == std::string::npos);
    CHECK(admin_get_backend(fixture.context(), "leaky").body.find("sk-live") == std::string::npos);
    CHECK(parsed(admin_get_backend(fixture.context(), "leaky"))["api_key_set"] == true);
}

TEST_CASE("the config slice refuses what the CLI refuses, in the error envelope",
          "[httpserver][admin]") {
    const Fixture fixture;
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "one"}, {"type", "mock"}}))
                .status == 201);

    // Duplicate name: 409, and --force's twin replaces.
    const HttpResponse duplicate = admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "one"}, {"type", "mock"}}));
    CHECK(duplicate.status == 409);
    CHECK(parsed(duplicate)["error"]["type"] == "conflict");
    CHECK(admin_create_backend(
              fixture.context(),
              post(nlohmann::json{{"name", "one"}, {"type", "mock"}, {"force", true}}))
              .status == 201);

    // Unknown type, missing name, not an object.
    const HttpResponse bad_type = admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "two"}, {"type", "carrier-pigeon"}}));
    CHECK(bad_type.status == 400);
    CHECK(parsed(bad_type)["error"]["message"].get<std::string>().find("accepted") !=
          std::string::npos);
    CHECK(admin_create_backend(fixture.context(), post(nlohmann::json{{"type", "mock"}})).status ==
          400);
    HttpRequest garbage;
    garbage.body = "[]";
    CHECK(admin_create_backend(fixture.context(), garbage).status == 400);

    // A role pointer at nothing is refused, naming what exists.
    const HttpResponse dangling =
        admin_set_role(fixture.context(), "default", post(nlohmann::json{{"name", "ghost"}}));
    CHECK(dangling.status == 400);
    CHECK(parsed(dangling)["error"]["message"].get<std::string>().find("one") != std::string::npos);

    // Unknown backend on GET and DELETE.
    CHECK(admin_get_backend(fixture.context(), "ghost").status == 404);
    CHECK(admin_delete_backend(fixture.context(), "ghost").status == 404);

    // The list names roles by what RESOLVES, never by reading a pointer.
    const nlohmann::json listed = parsed(admin_list_backends(fixture.context()));
    CHECK(listed["object"] == "list");
    CHECK(listed["data"].size() == 1);
    CHECK(listed["roles"]["default"].contains("backend"));
    CHECK(listed["roles"]["default"].contains("from"));
}

TEST_CASE("restart_required says when the file no longer matches what the server started with",
          "[httpserver][admin]") {
    const Fixture fixture;
    // The startup snapshot has no backends. Adding one is drift.
    const nlohmann::json added = parsed(admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "one"}, {"type", "mock"}})));
    CHECK(added["restart_required"] == true);
    CHECK(parsed(admin_list_backends(fixture.context()))["restart_required"] == true);
    // Deleting it again restores the membership the server started with.
    CHECK(parsed(admin_delete_backend(fixture.context(), "one"))["restart_required"] == false);

    // The pure predicate, on its own.
    const apogee::harness::Config a = apogee::harness::parse_config(
        "models:\n  default: x\nbackends:\n  x:\n    type: mock\n", "<a>");
    const apogee::harness::Config same = apogee::harness::parse_config(
        "models:\n  default: x\nbackends:\n  x:\n    type: mock\n    model: changed\n", "<b>");
    const apogee::harness::Config role = apogee::harness::parse_config(
        "models:\n  default: x\n  default_embedding: x\nbackends:\n  x:\n    type: mock\n", "<c>");
    CHECK_FALSE(apogee::httpserver::config_drifted(a, same));  // a field, not membership
    CHECK(apogee::httpserver::config_drifted(a, role));
}

TEST_CASE("is_literal_api_key tells a secret from a reference", "[httpserver][admin][secrets]") {
    CHECK(apogee::httpserver::is_literal_api_key("sk-abc"));
    CHECK_FALSE(apogee::httpserver::is_literal_api_key("${OPENAI_API_KEY}"));
    CHECK_FALSE(apogee::httpserver::is_literal_api_key(""));
    CHECK(apogee::httpserver::is_literal_api_key("${unterminated"));
}

TEST_CASE("the permissions twin edits the same line the CLI edits, byte for byte",
          "[httpserver][admin][permissions]") {
    const Fixture fixture;
    // The listing: every destructive tool at its shipped level.
    const HttpResponse listed = admin_list_permissions(fixture.context());
    REQUIRE(listed.status == 200);
    const nlohmann::json data = nlohmann::json::parse(listed.body)["data"];
    REQUIRE(data.size() == 6);
    bool saw_write = false;
    bool saw_edit = false;
    for (const nlohmann::json& row : data) {
        if (row["tool"] == "write_file") {
            saw_write = true;
            CHECK(row["level"] == "ask");
        }
        if (row["tool"] == "edit_file") {
            saw_edit = true;
            CHECK(row["level"] == "ask");
        }
    }
    CHECK(saw_write);
    CHECK(saw_edit);

    HttpRequest put;
    put.method = "PUT";
    put.body = R"({"level":"allow"})";
    const HttpResponse changed = admin_put_permission(fixture.context(), "write_file", put);
    REQUIRE(changed.status == 200);
    const nlohmann::json body = nlohmann::json::parse(changed.body);
    CHECK(body["tool"] == "write_file");
    CHECK(body["level"] == "allow");
    CHECK(body["restart_required"] == true);  // the server read `ask` at startup

    fixture.cli({"config", "set-permission", "write_file", "allow"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));

    // The refusals the CLI makes, in the envelope.
    put.body = R"({"level":"sometimes"})";
    CHECK(admin_put_permission(fixture.context(), "write_file", put).status == 400);
    put.body = R"({"level":"allow"})";
    CHECK(admin_put_permission(fixture.context(), "write file", put).status == 400);
    put.body = "nope";
    CHECK(admin_put_permission(fixture.context(), "write_file", put).status == 400);
    // Setting the same level back reports no restart.
    put.body = R"({"level":"ask"})";
    CHECK(nlohmann::json::parse(admin_put_permission(fixture.context(), "write_file", put)
                                    .body)["restart_required"] == false);
}

TEST_CASE("the allowed-hosts twins leave the same bytes the CLI and the [a]lways answer leave",
          "[httpserver][admin][hosts]") {
    const Fixture fixture;
    const std::string shipped = Fixture::bytes(fixture.http_config);
    const HttpResponse empty = apogee::httpserver::admin_list_allowed_hosts(fixture.context());
    REQUIRE(empty.status == 200);
    CHECK(nlohmann::json::parse(empty.body)["data"].empty());

    const HttpResponse added =
        apogee::httpserver::admin_put_allowed_host(fixture.context(), "Docs.Python.org");
    REQUIRE(added.status == 200);
    const nlohmann::json body = nlohmann::json::parse(added.body);
    CHECK(body["host"] == "docs.python.org");
    CHECK(body["restart_required"] == true);  // the server read none at startup
    fixture.cli({"config", "add-allowed-host", "Docs.Python.org"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));
    // The same bytes the prompt's [a]lways writes: one transform behind all three.
    CHECK(Fixture::bytes(fixture.http_config) ==
          apogee::harness::add_allowed_host(shipped, "docs.python.org"));

    const nlohmann::json listed = nlohmann::json::parse(
        apogee::httpserver::admin_list_allowed_hosts(fixture.context()).body)["data"];
    REQUIRE(listed.size() == 1);
    CHECK(listed[0]["host"] == "docs.python.org");
    CHECK(listed[0]["valid"] == true);
    // Idempotent: a second PUT changes nothing.
    CHECK(
        apogee::httpserver::admin_put_allowed_host(fixture.context(), "docs.python.org.").status ==
        200);
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));

    // Refusals, in the envelope; nothing written.
    CHECK(
        apogee::httpserver::admin_put_allowed_host(fixture.context(), "https://x.example").status ==
        400);
    CHECK(apogee::httpserver::admin_delete_allowed_host(fixture.context(), "pypi.org").status ==
          404);

    const HttpResponse removed =
        apogee::httpserver::admin_delete_allowed_host(fixture.context(), "docs.python.org");
    REQUIRE(removed.status == 200);
    CHECK(nlohmann::json::parse(removed.body)["deleted"] == "docs.python.org");
    fixture.cli({"config", "delete-allowed-host", "docs.python.org"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));
    CHECK(Fixture::bytes(fixture.http_config) == shipped);
}

TEST_CASE("the MCP server twins leave the same bytes the CLI leaves, and never list env values",
          "[httpserver][admin][mcp]") {
    const Fixture fixture;
    // Register-only over HTTP and on the CLI, with args.
    HttpRequest post;
    post.method = "POST";
    post.body = R"({"name":"ext","command":"/opt/srv","args":["--x","1"]})";
    const HttpResponse created = admin_create_mcp_server(fixture.context(), post);
    REQUIRE(created.status == 201);
    const nlohmann::json view = nlohmann::json::parse(created.body);
    CHECK(view["name"] == "ext");
    CHECK(view["enabled"] == true);
    CHECK(view["env_set"] == false);
    CHECK(view["restart_required"] == true);
    CHECK(view["directory"] == "");
    fixture.cli({"mcp", "create", "ext", "--command", "/opt/srv", "--args", "--x", "1"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));

    // A duplicate is a 409; force replaces.
    CHECK(admin_create_mcp_server(fixture.context(), post).status == 409);
    post.body = R"({"name":"ext","command":"/opt/srv2","force":true})";
    CHECK(admin_create_mcp_server(fixture.context(), post).status == 201);
    post.body = R"({"command":"/opt/srv"})";
    CHECK(admin_create_mcp_server(fixture.context(), post).status == 400);
    post.body = R"({"name":"bad name","command":"/opt/srv"})";
    CHECK(admin_create_mcp_server(fixture.context(), post).status == 400);

    // The view: command, args, enabled, env presence -- and no env values.
    fixture.cli({"mcp", "create", "ext", "--command", "/opt/srv2", "--force"});
    const HttpResponse listed = admin_list_mcp_servers(fixture.context());
    REQUIRE(listed.status == 200);
    CHECK(nlohmann::json::parse(listed.body)["data"].size() == 1);
    const HttpResponse one = admin_get_mcp_server(fixture.context(), "ext");
    REQUIRE(one.status == 200);
    CHECK(nlohmann::json::parse(one.body)["command"] == "/opt/srv2");
    CHECK_FALSE(nlohmann::json::parse(one.body).contains("env"));
    CHECK(admin_get_mcp_server(fixture.context(), "nope").status == 404);

    // enable/disable: one line, byte-identical to the CLI.
    HttpRequest put;
    put.method = "PUT";
    put.body = R"({"enabled":false})";
    const HttpResponse disabled = admin_set_mcp_server_enabled(fixture.context(), "ext", put);
    REQUIRE(disabled.status == 200);
    CHECK(nlohmann::json::parse(disabled.body)["enabled"] == false);
    fixture.cli({"mcp", "disable", "ext"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));
    put.body = R"({"enabled":"yes"})";
    CHECK(admin_set_mcp_server_enabled(fixture.context(), "ext", put).status == 400);
    put.body = R"({"enabled":true})";
    CHECK(admin_set_mcp_server_enabled(fixture.context(), "nope", put).status == 404);

    // Delete, both ways.
    CHECK(admin_delete_mcp_server(fixture.context(), "ext").status == 200);
    fixture.cli({"config", "delete-mcp-server", "ext"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));
    CHECK(admin_delete_mcp_server(fixture.context(), "ext").status == 404);
}

TEST_CASE("a preset registered over HTTP is byte-identical to the CLI's --from and typed twin",
          "[httpserver][admin][mcp][presets]") {
    // 47c: `from` resolves the preset through the same core `mcp create
    // --from` reaches. Both configs share one data directory, so the preset
    // resolves to the same script and interpreter on either surface.
    const Fixture fixture;
    const std::filesystem::path script =
        fixture.home.path() / apogee::harness::bundled_code_exec_server_relative_path();
    std::filesystem::create_directories(script.parent_path());
    std::ofstream{script, std::ios::binary} << apogee::harness::bundled_mcp_scripts()[0].text;
    const std::filesystem::path python =
        apogee::training::PythonEnv{fixture.home.path() / "training" / "venv"}.interpreter();
    std::filesystem::create_directories(python.parent_path());
    std::ofstream{python, std::ios::binary} << "#!fake";

    const HttpResponse created = admin_create_mcp_server(
        fixture.context(), post(nlohmann::json{{"name", "code"}, {"from", "code-exec"}}));
    REQUIRE(created.status == 201);
    CHECK(parsed(created)["command"] == python.string());
    fixture.cli({"mcp", "create", "code", "--command", python.string(), "--args", script.string()});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));

    // self is refused with its reason; a preset takes no command; an
    // unknown one names the presets.
    const HttpResponse self = admin_create_mcp_server(
        fixture.context(), post(nlohmann::json{{"name", "me"}, {"from", "self"}}));
    CHECK(self.status == 400);
    CHECK(self.body.find("already has natively") != std::string::npos);
    CHECK(admin_create_mcp_server(
              fixture.context(),
              post(nlohmann::json{{"name", "x"}, {"from", "code-exec"}, {"command", "/bin/true"}}))
              .status == 400);
    const HttpResponse unknown = admin_create_mcp_server(
        fixture.context(), post(nlohmann::json{{"name", "x"}, {"from", "nope"}}));
    CHECK(unknown.status == 400);
    CHECK(unknown.body.find("the presets: code-exec, self") != std::string::npos);
}

TEST_CASE("a backend renamed over HTTP is byte-identical to the CLI's, every reference with it",
          "[httpserver][admin][parity][rename]") {
    // 46a: the twin calls the CLI's one core -- the same edit, byte for byte.
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "helper", "--type", "mock"});
    fixture.cli({"config", "set-default-utility", "helper"});
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "helper"}, {"type", "mock"}}))
                .status == 201);
    REQUIRE(admin_set_role(fixture.context(), "default_utility",
                           post(nlohmann::json{{"name", "helper"}}))
                .status == 200);
    REQUIRE(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    fixture.cli({"config", "rename-backend", "helper", "assistant"});
    const HttpResponse renamed = admin_rename_backend(fixture.context(), "helper",
                                                      post(nlohmann::json{{"name", "assistant"}}));
    REQUIRE(renamed.status == 200);
    const nlohmann::json body = parsed(renamed);
    CHECK(body.at("renamed") == "helper");
    CHECK(body.at("to") == "assistant");
    CHECK(body.at("said").at(0).get<std::string>().starts_with(
        "renamed backend 'helper' to 'assistant'"));
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    CHECK(Fixture::bytes(fixture.http_config).find("\"default_utility\": \"assistant\"") !=
          std::string::npos);

    // Unknown is 404; a name that cannot be is 400, said, and nothing moved.
    CHECK(admin_rename_backend(fixture.context(), "nope", post(nlohmann::json{{"name", "x"}}))
              .status == 404);
    const std::string before = Fixture::bytes(fixture.http_config);
    const HttpResponse refused = admin_rename_backend(fixture.context(), "assistant",
                                                      post(nlohmann::json{{"name", "two words"}}));
    CHECK(refused.status == 400);
    CHECK(refused.body.find("cannot name a backend") != std::string::npos);
    CHECK(Fixture::bytes(fixture.http_config) == before);
    CHECK(admin_rename_backend(fixture.context(), "assistant", post(nlohmann::json::object()))
              .status == 400);
}

TEST_CASE("a backend edited over HTTP is byte-identical to the CLI's, and never takes a key",
          "[httpserver][admin][parity][set-backend]") {
    // 46b: the twin calls the CLI's one core.
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "helper", "--type", "mock", "--model", "m1"});
    REQUIRE(admin_create_backend(
                fixture.context(),
                post(nlohmann::json{{"name", "helper"}, {"type", "mock"}, {"model", "m1"}}))
                .status == 201);
    REQUIRE(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    fixture.cli({"config", "set-backend", "helper", "--model", "m2", "--context-size", "8192"});
    HttpRequest patch = post(nlohmann::json{{"model", "m2"}, {"context_size", 8192}});
    patch.method = "PATCH";
    const HttpResponse edited = admin_set_backend_fields(fixture.context(), "helper", patch);
    REQUIRE(edited.status == 200);
    CHECK(parsed(edited).at("edited") == "helper");
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    // A key is refused, and the file is untouched; so is a field no backend has.
    const std::string before = Fixture::bytes(fixture.http_config);
    const HttpResponse keyed = admin_set_backend_fields(
        fixture.context(), "helper", post(nlohmann::json{{"api_key", "sk-planted"}}));
    CHECK(keyed.status == 400);
    CHECK(keyed.body.find("a key is never taken on the command line") != std::string::npos);
    CHECK(keyed.body.find("sk-planted") == std::string::npos);
    CHECK(
        admin_set_backend_fields(fixture.context(), "helper", post(nlohmann::json{{"colour", "x"}}))
            .status == 400);
    CHECK(Fixture::bytes(fixture.http_config) == before);
    CHECK(admin_set_backend_fields(fixture.context(), "nope", post(nlohmann::json{{"model", "x"}}))
              .status == 404);
}

TEST_CASE("adapters pinned over HTTP are byte-identical to the CLI's, and the view shows them",
          "[httpserver][admin][parity][set-backend][adapters]") {
    // 53a: the twin pins through the CLI's one core, the base read where it
    // lies.
    const Fixture fixture;
    const std::filesystem::path base = fixture.home.path() / "base.gguf";
    const std::filesystem::path lora = fixture.home.path() / "style.gguf";
    std::ofstream{base, std::ios::binary} << "base weights";
    std::ofstream{lora, std::ios::binary} << "style delta";
    fixture.cli(
        {"config", "add-backend", "tuned", "--type", "llamacpp", "--model-path", base.string()});
    REQUIRE(admin_create_backend(
                fixture.context(),
                post(nlohmann::json{
                    {"name", "tuned"}, {"type", "llamacpp"}, {"model_path", base.string()}}))
                .status == 201);
    fixture.cli({"config", "set-backend", "tuned", "--adapters", lora.string() + ":0.5"});
    HttpRequest patch = post(nlohmann::json{{"adapters", lora.string() + ":0.5"}});
    patch.method = "PATCH";
    REQUIRE(admin_set_backend_fields(fixture.context(), "tuned", patch).status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json view = parsed(admin_get_backend(fixture.context(), "tuned"));
    CHECK(view.dump().find(apogee::models::file_sha256(base)) != std::string::npos);
    CHECK(view.dump().find(R"("scale":0.5)") != std::string::npos);

    // 55e: moving the base under them is refused the CLI's way over HTTP,
    // the file untouched -- and a move to the same weights is byte-identical.
    const std::filesystem::path other = fixture.home.path() / "other.gguf";
    std::ofstream{other, std::ios::binary} << "other weights";
    const std::string before = Fixture::bytes(fixture.http_config);
    HttpRequest move = post(nlohmann::json{{"model_path", other.string()}});
    move.method = "PATCH";
    const HttpResponse refused = admin_set_backend_fields(fixture.context(), "tuned", move);
    CHECK(refused.status == 400);
    CHECK(refused.body.find("'tuned' applies adapters pinned to other weights than") !=
          std::string::npos);
    CHECK(Fixture::bytes(fixture.http_config) == before);
    const std::filesystem::path copy = fixture.home.path() / "copy.gguf";
    std::ofstream{copy, std::ios::binary} << "base weights";
    fixture.cli({"config", "set-backend", "tuned", "--model-path", copy.string()});
    HttpRequest same = post(nlohmann::json{{"model_path", copy.string()}});
    same.method = "PATCH";
    REQUIRE(admin_set_backend_fields(fixture.context(), "tuned", same).status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
}

TEST_CASE("config set's twin moves the same scalars the CLI moves, byte for byte",
          "[httpserver][admin][config-set]") {
    // 52e: the allow-listed scalars through the one `set_scalar`, from either
    // door; a value as a JSON boolean or number is the CLI's text.
    const Fixture fixture;
    HttpRequest put;
    put.method = "PUT";
    put.body = R"({"value":false})";
    const HttpResponse recall = admin_put_config_value(fixture.context(), "memory.recall", put);
    REQUIRE(recall.status == 200);
    CHECK(nlohmann::json::parse(recall.body) ==
          nlohmann::json{{"key", "memory.recall"}, {"value", "false"}, {"restart_required", true}});
    put.body = R"({"value":"http://127.0.0.1:8888"})";
    REQUIRE(admin_put_config_value(fixture.context(), "tools.search.url", put).status == 200);
    put.body = R"({"value":7})";
    REQUIRE(admin_put_config_value(fixture.context(), "tools.search.results", put).status == 200);

    fixture.cli({"config", "set", "memory.recall", "false"});
    fixture.cli({"config", "set", "tools.search.url", "http://127.0.0.1:8888"});
    fixture.cli({"config", "set", "tools.search.results", "7"});
    CHECK(Fixture::bytes(fixture.http_config) == Fixture::bytes(fixture.cli_config));

    // The refusals the CLI makes, in the envelope, the file untouched.
    const std::string before = Fixture::bytes(fixture.http_config);
    put.body = R"({"value":"sk-123"})";
    const HttpResponse secret =
        admin_put_config_value(fixture.context(), "backends.x.api_key", put);
    CHECK(secret.status == 400);
    CHECK(secret.body.find("apogee auth add") != std::string::npos);
    put.body = R"({"value":"maybe"})";
    CHECK(admin_put_config_value(fixture.context(), "memory.recall", put).status == 400);
    put.body = R"({"value":[1]})";
    CHECK(admin_put_config_value(fixture.context(), "memory.recall", put).status == 400);
    put.body = R"({})";
    CHECK(admin_put_config_value(fixture.context(), "memory.recall", put).status == 400);
    CHECK(Fixture::bytes(fixture.http_config) == before);
}

TEST_CASE("a set whose kind no mlx entry runs is refused over HTTP in the CLI's own words",
          "[httpserver][admin][kind]") {
    // 55d: the kind decides the role at every door, the admin twins too --
    // the one predicate, one wording, the file untouched either way.
    const Fixture fixture;
    const std::filesystem::path voice = fixture.home.path() / "kokoro" / "mlx" / "ffffffffffff";
    apogee::testing::write_mlx_model(voice, {.kind = "voice"});
    const auto cli = [&fixture](const std::vector<std::string>& args, std::string* err) {
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", fixture.cli_config.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        std::ostringstream captured;
        std::ostringstream said;
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        std::streambuf* old_out = std::cout.rdbuf(said.rdbuf());
        const int code = root.run(static_cast<int>(argv.size()), argv.data());
        std::cerr.rdbuf(old_err);
        std::cout.rdbuf(old_out);
        *err = captured.str();
        return code;
    };
    const std::string cli_before = Fixture::bytes(fixture.cli_config);
    const std::string http_before = Fixture::bytes(fixture.http_config);

    const HttpResponse added = admin_create_backend(
        fixture.context(),
        post(nlohmann::json{{"name", "vox"}, {"type", "mlx"}, {"model_path", voice.string()}}));
    CHECK(added.status == 400);
    const std::string message = parsed(added)["error"]["message"].get<std::string>();
    CHECK(message.find(voice.string() + " is kind voice (recorded) -- a voice model: 'apogee "
                                        "voice say' speaks with it, never a chat backend") !=
          std::string::npos);
    std::string err;
    CHECK(cli({"config", "add-backend", "vox", "--type", "mlx", "--model-path", voice.string()},
              &err) == 1);
    CHECK(err == "apogee config: " + message + "\n");
    CHECK(Fixture::bytes(fixture.cli_config) == cli_before);
    CHECK(Fixture::bytes(fixture.http_config) == http_before);

    // An entry over it written past the doors -- by hand, as the one editor
    // writes one -- takes no role on either surface.
    for (const std::filesystem::path& path : {fixture.cli_config, fixture.http_config}) {
        apogee::harness::BackendConfig backend;
        backend.type = apogee::harness::BackendType::Mlx;
        backend.model_path = voice.string();
        apogee::harness::edit_config_file(path, [&backend](std::string_view content) {
            return apogee::harness::append_backend(content, "vox", backend, false);
        });
    }
    const std::string cli_entry = Fixture::bytes(fixture.cli_config);
    for (const std::string field : {"default", "default_embedding"}) {
        INFO(field);
        const HttpResponse pointed =
            admin_set_role(fixture.context(), field, post(nlohmann::json{{"name", "vox"}}));
        CHECK(pointed.status == 400);
        const std::string refused = parsed(pointed)["error"]["message"].get<std::string>();
        CHECK(refused.starts_with("backend 'vox' cannot take a role: " + voice.string() +
                                  " is kind voice (recorded)"));
        CHECK(cli({"config", field == "default" ? "set-default" : "set-default-embedding", "vox"},
                  &err) == 1);
        CHECK(err == "apogee config: " + refused + "\n");
    }
    CHECK(Fixture::bytes(fixture.cli_config) == cli_entry);
    CHECK(apogee::harness::load_config(fixture.http_config).models.default_backend != "vox");
    // And no edit moves an entry over it: set-backend's core, both surfaces' one.
    CHECK(cli({"config", "set-backend", "vox", "--model-path", voice.string()}, &err) == 1);
    CHECK(err.find(" is kind voice (recorded)") != std::string::npos);
}

TEST_CASE("a role over HTTP takes its own kind, refused in the CLI's own words",
          "[httpserver][admin][kind][roles]") {
    // M33: the role gate knows the role at every door. An entry over an
    // embedding set takes the embedding role and no other; one over a chat
    // set every role but that one -- one predicate, one wording.
    const Fixture fixture;
    const std::filesystem::path embedding =
        fixture.home.path() / "gemma-embedding" / "mlx" / "eeeeeeeeeeee";
    apogee::testing::write_mlx_model(embedding, {.kind = "embedding"});
    const std::filesystem::path chat = fixture.home.path() / "gemma" / "mlx" / "cccccccccccc";
    apogee::testing::write_mlx_model(chat, {.kind = "chat"});
    for (const std::filesystem::path& path : {fixture.cli_config, fixture.http_config}) {
        for (const std::pair<std::string, std::filesystem::path>& entry :
             {std::pair<std::string, std::filesystem::path>{"Emb-4bit", embedding},
              std::pair<std::string, std::filesystem::path>{"Chat-4bit", chat}}) {
            apogee::harness::BackendConfig backend;
            backend.type = apogee::harness::BackendType::Mlx;
            backend.model_path = entry.second.string();
            apogee::harness::edit_config_file(path, [&](std::string_view content) {
                return apogee::harness::append_backend(content, entry.first, backend, false);
            });
        }
    }
    const auto cli = [&fixture](const std::vector<std::string>& args, std::string* err) {
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", fixture.cli_config.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        std::ostringstream captured;
        std::ostringstream said;
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        std::streambuf* old_out = std::cout.rdbuf(said.rdbuf());
        const int code = root.run(static_cast<int>(argv.size()), argv.data());
        std::cerr.rdbuf(old_err);
        std::cout.rdbuf(old_out);
        *err = captured.str();
        return code;
    };
    for (const std::string_view field : apogee::harness::models_role_fields()) {
        INFO(field);
        const bool embeds = field == "default_embedding";
        const std::string refused_name = embeds ? "Chat-4bit" : "Emb-4bit";
        std::string verb = "set-" + std::string{field};
        std::ranges::replace(verb, '_', '-');
        const std::string cli_before = Fixture::bytes(fixture.cli_config);
        const std::string http_before = Fixture::bytes(fixture.http_config);
        const HttpResponse pointed =
            admin_set_role(fixture.context(), field, post(nlohmann::json{{"name", refused_name}}));
        CHECK(pointed.status == 400);
        const std::string refused = parsed(pointed)["error"]["message"].get<std::string>();
        CHECK(refused.starts_with("backend '" + refused_name +
                                  "' cannot take a role: " + (embeds ? chat : embedding).string() +
                                  " is kind " + (embeds ? "chat" : "embedding") + " (recorded)"));
        std::string err;
        CHECK(cli({"config", verb, refused_name}, &err) == 1);
        CHECK(err == "apogee config: " + refused + "\n");
        CHECK(Fixture::bytes(fixture.cli_config) == cli_before);
        CHECK(Fixture::bytes(fixture.http_config) == http_before);
        // The entry of the role's own kind, taken on both, the same bytes.
        const std::string taken = embeds ? "Emb-4bit" : "Chat-4bit";
        REQUIRE(admin_set_role(fixture.context(), field, post(nlohmann::json{{"name", taken}}))
                    .status == 200);
        CHECK(cli({"config", verb, taken}, &err) == 0);
        CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    }
}

TEST_CASE("an HTTP add-backend sets the default when none is set, as the CLI does, and says so",
          "[httpserver][admin][parity][default]") {
    // M33: the admin twin of `config add-backend` makes an entry that chats
    // the default when the config sets none -- the CLI's bytes, said in its
    // words -- and never moves one already set.
    const Fixture fixture;
    REQUIRE(apogee::harness::load_config(fixture.http_config).models.default_backend.empty());
    fixture.cli({"config", "add-backend", "work", "--type", "mock", "--model", "mock-1"});
    const HttpResponse created = admin_create_backend(
        fixture.context(),
        post(nlohmann::json{{"name", "work"}, {"type", "mock"}, {"model", "mock-1"}}));
    REQUIRE(created.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    CHECK(apogee::harness::load_config(fixture.http_config).models.default_backend == "work");
    CHECK(parsed(created)["said"] == "models.default set to 'work' (none was set)");

    fixture.cli({"config", "add-backend", "spare", "--type", "mock"});
    const HttpResponse second = admin_create_backend(
        fixture.context(), post(nlohmann::json{{"name", "spare"}, {"type", "mock"}}));
    REQUIRE(second.status == 201);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    CHECK(apogee::harness::load_config(fixture.http_config).models.default_backend == "work");
    CHECK(parsed(second)["said"] == "");

    // An entry over an embedding set is no chat backend: never made the
    // default on anyone's behalf.
    const Fixture fresh;
    const std::filesystem::path embedding =
        fresh.home.path() / "gemma-embedding" / "mlx" / "eeeeeeeeeeee";
    apogee::testing::write_mlx_model(embedding, {.kind = "embedding"});
    const HttpResponse embedder = admin_create_backend(
        fresh.context(),
        post(nlohmann::json{
            {"name", "Emb-4bit"}, {"type", "mlx"}, {"model_path", embedding.string()}}));
    REQUIRE(embedder.status == 201);
    CHECK(parsed(embedder)["said"] == "");
    CHECK(apogee::harness::load_config(fresh.http_config).models.default_backend.empty());
}

TEST_CASE("an HTTP delete-backend clears the roles that named the entry, as the CLI does",
          "[httpserver][admin][parity][default]") {
    // M33: the twin of `config delete-backend` leaves no role pointer naming
    // what it removed -- the CLI's bytes, its lines as `said`.
    const Fixture fixture;
    fixture.cli({"config", "add-backend", "work", "--type", "mock"});
    fixture.cli({"config", "set-default-utility", "work"});
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "work"}, {"type", "mock"}}))
                .status == 201);
    REQUIRE(
        admin_set_role(fixture.context(), "default_utility", post(nlohmann::json{{"name", "work"}}))
            .status == 200);
    REQUIRE(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));

    fixture.cli({"config", "delete-backend", "work"});
    const HttpResponse deleted = admin_delete_backend(fixture.context(), "work");
    REQUIRE(deleted.status == 200);
    CHECK(Fixture::bytes(fixture.cli_config) == Fixture::bytes(fixture.http_config));
    const nlohmann::json body = parsed(deleted);
    CHECK(body["deleted"] == "work");
    CHECK(body["said"] ==
          nlohmann::json::array({"models.default cleared: it named 'work'",
                                 "models.default_utility cleared: it named 'work'"}));
    const apogee::harness::Config after = apogee::harness::load_config(fixture.http_config);
    CHECK(after.models.default_backend.empty());
    CHECK(after.models.default_utility.empty());
    // Nothing named: nothing said.
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "other"}, {"type", "mock"}}))
                .status == 201);
    REQUIRE(admin_set_role(fixture.context(), "default", post(nlohmann::json{{"name", "other"}}))
                .status == 200);
    REQUIRE(admin_create_backend(fixture.context(),
                                 post(nlohmann::json{{"name", "spare"}, {"type", "mock"}}))
                .status == 201);
    CHECK(parsed(admin_delete_backend(fixture.context(), "spare"))["said"] ==
          nlohmann::json::array());
}
