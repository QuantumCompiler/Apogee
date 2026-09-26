#include "httpserver/admin_config.h"

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
#include "harness/config_edit.h"
#include "httpserver/http_types.h"
#include "support/env_guard.h"

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
    REQUIRE(data.size() == 5);
    bool saw_write = false;
    for (const nlohmann::json& row : data) {
        if (row["tool"] == "write_file") {
            saw_write = true;
            CHECK(row["level"] == "ask");
        }
    }
    CHECK(saw_write);

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
