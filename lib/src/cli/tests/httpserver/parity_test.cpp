#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "events/bus.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "support/env_guard.h"

/// The CLI↔HTTP parity table -- and its completeness.
///
/// "Parity is the product" means every mutating CLI action reaches a remote
/// client through the control plane. Ommi kept a hand-maintained list of such
/// actions; the list was correct on the day each row was written. This test
/// walks the CLI's whole subcommand tree and requires EVERY subcommand to be
/// classified below -- as a twin (with its admin route), a backfill (with the
/// area that owns it), a carve-out (with the reason), or read-only. Adding a
/// subcommand without placing it here fails the build, which is the only way a
/// parity table stays true.
namespace {

using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::httpserver::Mux;
using apogee::httpserver::RouteSpec;

enum class Kind { Twin, Backfill, CarveOut, ReadOnly };

struct Classification {
    Kind kind;
    /// Twin: the admin route. Backfill: the owning area. Carve-out: the reason.
    std::string method;
    std::string path;
    std::string note;
};

Classification twin(std::string method, std::string path) {
    return Classification{Kind::Twin, std::move(method), std::move(path), {}};
}

Classification backfill(std::string area) {
    return Classification{Kind::Backfill, {}, {}, std::move(area)};
}

Classification carve_out(std::string reason) {
    return Classification{Kind::CarveOut, {}, {}, std::move(reason)};
}

Classification read_only() {
    return Classification{Kind::ReadOnly, {}, {}, {}};
}

/// THE table. Every leaf subcommand of `apogee`, by its full name.
const std::map<std::string, Classification>& table() {
    static const std::map<std::string, Classification> rows{
        // --- twins: the first CRUD slices -----------------------------------
        {"config add-backend", twin("POST", "/v1/admin/backends")},
        {"config delete-backend", twin("DELETE", "/v1/admin/backends/{id}")},
        {"config set-default", twin("POST", "/v1/admin/backends/default")},
        {"config set-default-embedding", twin("POST", "/v1/admin/backends/default-embedding")},
        {"config set-default-extraction", twin("POST", "/v1/admin/backends/default-extraction")},
        {"config format", twin("POST", "/v1/admin/config/format")},
        {"config set-permission", twin("PUT", "/v1/admin/permissions/{id}")},
        {"config delete-mcp-server", twin("DELETE", "/v1/admin/mcp-servers/{id}")},
        {"mcp create", twin("POST", "/v1/admin/mcp-servers")},
        {"mcp enable", twin("PUT", "/v1/admin/mcp-servers/{id}")},
        {"mcp disable", twin("PUT", "/v1/admin/mcp-servers/{id}")},
        {"auth add", twin("PUT", "/v1/admin/auth/{id}")},
        {"auth clear", twin("DELETE", "/v1/admin/auth/{id}")},
        {"agents create", twin("POST", "/v1/admin/agents")},
        {"agents edit", twin("PUT", "/v1/admin/agents/{id}")},
        {"agents delete", twin("DELETE", "/v1/admin/agents/{id}")},
        {"knowledge capture", twin("POST", "/v1/admin/knowledge/capture")},
        {"knowledge link", twin("PATCH", "/v1/admin/knowledge/{id}")},
        {"knowledge status", twin("PATCH", "/v1/admin/knowledge/{id}")},
        {"knowledge delete", twin("DELETE", "/v1/admin/knowledge/{id}")},
        {"knowledge reindex", twin("POST", "/v1/admin/knowledge/reindex")},
        {"graph build", twin("POST", "/v1/admin/graph/{id}/build")},
        {"graph delete", twin("DELETE", "/v1/admin/graph/{id}")},
        {"graph communities", twin("POST", "/v1/admin/graph/{id}/communities")},
        {"graph dedupe", twin("POST", "/v1/admin/graph/{id}/dedupe")},
        {"config add-graph", twin("POST", "/v1/admin/graphs")},
        {"config delete-graph", twin("DELETE", "/v1/admin/graphs/{id}")},
        // --- backfills: owned by the area that owns the CLI action ----------
        {"embed ingest", backfill("embedstore: the embeddings data plane, an async job")},
        {"embed delete", backfill("embedstore: the embeddings data plane")},
        {"models pull", backfill("models: the models plane, an async job")},
        {"models delete", backfill("models: the models plane")},
        {"models repair", backfill("models: the models plane")},
        {"models quantize", backfill("models: the models plane, an async job")},
        {"chats title", backfill("sessions: transcripts on disk")},
        {"chats delete", backfill("sessions: transcripts on disk")},
        // --- carve-outs: host-local by nature ---------------------------------
        {"config init", carve_out("the server cannot exist without a config to start from")},
        {"check", carve_out("--fix repairs the local install; host-local by nature")},
        {"uninstall", carve_out("removes the binary and the data directory; host-local")},
        {"serve", carve_out("it is the server")},
        {"__mcp-tools",
         carve_out("an MCP server on this process's own stdio, spawned by another client")},
        // --- read-only / interactive ------------------------------------------
        {"chat", read_only()},
        {"complete", read_only()},
        {"analyze", read_only()},
        {"agents list", read_only()},
        {"graph stats", read_only()},
        {"graph show", read_only()},
        {"version", read_only()},
        {"__complete", read_only()},
        {"config get", read_only()},
        {"config path", read_only()},
        {"mcp list", read_only()},
        {"mcp test", read_only()},
        {"auth list", read_only()},
        {"embed query", read_only()},
        {"embed list", read_only()},
        {"embed info", read_only()},
        {"models list", read_only()},
        {"models info", read_only()},
        {"models status", read_only()},
        {"chats list", read_only()},
        {"chats info", read_only()},
        {"knowledge query", read_only()},
        {"knowledge list", read_only()},
        {"knowledge info", read_only()},
        {"knowledge export", read_only()},
    };
    return rows;
}

/// Every leaf subcommand under `app`, as "parent child" names.
void collect_leaves(const CLI::App& app, const std::string& prefix, std::vector<std::string>& out) {
    const std::vector<const CLI::App*> children = app.get_subcommands({});
    if (children.empty()) {
        out.push_back(prefix);
        return;
    }
    for (const CLI::App* child : children) {
        collect_leaves(*child,
                       prefix.empty() ? child->get_name() : prefix + " " + child->get_name(), out);
    }
}

}  // namespace

TEST_CASE("every CLI subcommand is classified, and every twin's route is registered and gated",
          "[httpserver][parity]") {
    apogee::commands::RootCommand root{apogee::commands::default_registry()};
    std::vector<std::string> leaves;
    for (const CLI::App* command : root.app().get_subcommands({})) {
        collect_leaves(*command, command->get_name(), leaves);
    }
    REQUIRE_FALSE(leaves.empty());

    // Completeness in both directions: an unclassified subcommand, or a row
    // naming a subcommand that no longer exists, is a stale table.
    for (const std::string& leaf : leaves) {
        INFO("unclassified subcommand: " << leaf);
        CHECK(table().contains(leaf));
    }
    for (const auto& [name, classification] : table()) {
        INFO("the table names a subcommand that does not exist: " << name);
        CHECK(std::find(leaves.begin(), leaves.end(), name) != leaves.end());
    }

    // Every twin's route exists in the table AND sits behind the gate: an
    // unauthenticated request gets 401, never the 404 a missing route gives.
    const apogee::testing::TempDir home{"parity-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const apogee::harness::Config config =
        apogee::harness::parse_config("backends:\n  mock:\n    type: mock\n", "<parity>");
    apogee::harness::Harness harness{config};
    harness.register_provider("mock", std::make_shared<apogee::backends::MockProvider>(
                                          apogee::backends::MockProvider::Options{}));
    harness.use_default_router();
    apogee::httpserver::HandlerOptions options;
    options.served = {"mock"};
    options.default_backend = "mock";
    apogee::httpserver::Handler handler{harness, options, nullptr};
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminOptions admin_options;
    admin_options.config_path = home.path() / "config.yaml";
    admin_options.startup = config;
    apogee::httpserver::AdminHandler admin{admin_options, jobs, bus};
    const Mux mux{handler, admin, "parity-token"};
    const std::vector<RouteSpec> routes = Mux::routes();

    for (const auto& [name, classification] : table()) {
        if (classification.kind != Kind::Twin) {
            continue;
        }
        INFO("twin of " << name << ": " << classification.method << " " << classification.path);
        bool registered = false;
        for (const RouteSpec& route : routes) {
            if (route.method == classification.method && route.pattern == classification.path) {
                registered = true;
                CHECK(route.admin);
            }
        }
        CHECK(registered);

        HttpRequest probe;
        probe.method = classification.method;
        std::string path = classification.path;
        if (const std::size_t id = path.find("{id}"); id != std::string::npos) {
            path.replace(id, 4, "probe");
        }
        probe.path = path;
        CHECK(mux.dispatch(probe).status == 401);
    }
}

TEST_CASE("the admin prefix is gated before routing, and mounted only when given",
          "[httpserver][parity][auth]") {
    const apogee::harness::Config config;
    apogee::harness::Harness harness{config};
    apogee::httpserver::Handler handler{harness, {}, nullptr};

    // No plane: the prefix is a plain 404, so a public-only server does not
    // pretend to have a control plane.
    const Mux bare{handler};
    CHECK_FALSE(bare.admin_mounted());
    HttpRequest request;
    request.method = "GET";
    request.path = "/v1/admin/backends";
    CHECK(bare.dispatch(request).status == 404);

    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminHandler admin{{}, jobs, bus};
    const Mux mux{handler, admin, "secret"};
    CHECK(mux.admin_mounted());

    // Unauthenticated: 401 for a real route AND for one that does not exist.
    CHECK(mux.dispatch(request).status == 401);
    HttpRequest unknown = request;
    unknown.path = "/v1/admin/does-not-exist";
    CHECK(mux.dispatch(unknown).status == 401);
    HttpRequest bare_prefix = request;
    bare_prefix.path = "/v1/admin";
    CHECK(mux.dispatch(bare_prefix).status == 401);

    // Authenticated: the unknown path is now an honest 404, and a known one
    // reaches its handler.
    unknown.headers["authorization"] = "Bearer secret";
    CHECK(mux.dispatch(unknown).status == 404);
    HttpRequest jobs_list = request;
    jobs_list.path = "/v1/admin/jobs";
    jobs_list.headers["authorization"] = "Bearer secret";
    const HttpResponse listed = mux.dispatch(jobs_list);
    CHECK(listed.status == 200);
    CHECK(nlohmann::json::parse(listed.body)["object"] == "list");

    // The public plane is untouched by the gate.
    HttpRequest health;
    health.method = "GET";
    health.path = "/health";
    CHECK(mux.dispatch(health).status == 200);
}
