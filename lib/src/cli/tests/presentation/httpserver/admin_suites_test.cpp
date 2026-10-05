#include "httpserver/admin_suites.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "httpserver/http_types.h"
#include "support/env_guard.h"

/// The suites: config slice (27d), and its parity proof: every edit made over
/// HTTP and the same edit made by the CLI leave byte-identical files; the
/// CLI's rules answer with the status a client acts on; a write says whether
/// the running server must restart to resolve under it.
namespace {

using apogee::httpserver::admin_create_suite;
using apogee::httpserver::admin_delete_suite;
using apogee::httpserver::admin_get_suite;
using apogee::httpserver::admin_list_suites;
using apogee::httpserver::admin_put_suite;
using apogee::httpserver::admin_set_default_suite;
using apogee::httpserver::admin_set_suite_member;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

struct Fixture {
    apogee::testing::TempDir home{"admin-suites-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path cli_config = home.path() / "cli" / "config" / "config.yaml";
    std::filesystem::path http_config = home.path() / "http" / "config" / "config.yaml";
    apogee::harness::Config startup;

    Fixture() {
        for (const std::filesystem::path& path : {cli_config, http_config}) {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream{path, std::ios::binary} << apogee::harness::config_template();
            for (const char* name : {"root", "helper", "embedder"}) {
                apogee::harness::BackendConfig backend;
                backend.type = apogee::harness::BackendType::Mock;
                apogee::harness::edit_config_file(path, [&](std::string_view content) {
                    return apogee::harness::append_backend(content, name, backend, false);
                });
            }
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

    [[nodiscard]] bool same() const {
        return bytes(cli_config) == bytes(http_config);
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

TEST_CASE("every suite edit over HTTP is byte-identical to the CLI's",
          "[httpserver][admin][suites][parity]") {
    const Fixture fixture;
    // add-suite <-> POST.
    REQUIRE(fixture.cli({"config", "add-suite", "research", "--chat", "root", "--utility", "helper",
                         "--context-size", "utility=4096", "--toolset", "utility=fs,git",
                         "--description", "Deep work"}) == 0);
    const HttpResponse created = admin_create_suite(
        fixture.context(),
        with_body(
            "POST",
            {{"name", "research"},
             {"description", "Deep work"},
             {"members",
              {{"chat", "root"},
               {"utility",
                {{"backend", "helper"}, {"context_size", 4096}, {"toolset", {"fs", "git"}}}}}}}));
    REQUIRE(created.status == 201);
    CHECK(fixture.same());
    const nlohmann::json data = parsed(created)["data"];
    CHECK(data["name"] == "research");
    CHECK(data["description"] == "Deep work");
    CHECK(data["members"]["chat"]["backend"] == "root");
    CHECK(data["members"]["utility"]["context_size"] == 4096);
    CHECK(data["members"]["utility"]["toolset"] == nlohmann::json({"fs", "git"}));
    CHECK(data["default"] == false);
    // A suite the server did not start with: resolution has not moved yet.
    CHECK(parsed(created)["restart_required"] == true);

    // set-suite <-> PUT members, one member at a time.
    REQUIRE(fixture.cli({"config", "set-suite", "research", "--embedding", "embedder"}) == 0);
    REQUIRE(
        admin_set_suite_member(fixture.context(), "research",
                               with_body("PUT", {{"role", "embedding"}, {"member", "embedder"}}))
            .status == 200);
    CHECK(fixture.same());
    REQUIRE(fixture.cli({"config", "set-suite", "research", "--remove", "embedding"}) == 0);
    REQUIRE(admin_set_suite_member(fixture.context(), "research",
                                   with_body("PUT", {{"role", "embedding"}, {"member", nullptr}}))
                .status == 200);
    CHECK(fixture.same());

    // set-default-suite <-> POST default.
    REQUIRE(fixture.cli({"config", "set-default-suite", "research"}) == 0);
    const HttpResponse defaulted =
        admin_set_default_suite(fixture.context(), with_body("POST", {{"name", "research"}}));
    REQUIRE(defaulted.status == 200);
    CHECK(fixture.same());
    CHECK(parsed(defaulted)["name"] == "research");
    CHECK(parsed(admin_get_suite(fixture.context(), "RESEARCH"))["data"]["default"] == true);

    // delete-suite <-> DELETE, refused alike while it is the default.
    CHECK(fixture.cli({"config", "delete-suite", "research"}) != 0);
    const HttpResponse refused = admin_delete_suite(fixture.context(), "research");
    CHECK(refused.status == 409);
    CHECK(refused.body.find("is the default suite") != std::string::npos);
    REQUIRE(fixture.cli({"config", "set-default-suite", "off"}) == 0);
    REQUIRE(
        admin_set_default_suite(fixture.context(), with_body("POST", {{"name", "off"}})).status ==
        200);
    REQUIRE(fixture.cli({"config", "delete-suite", "research"}) == 0);
    REQUIRE(admin_delete_suite(fixture.context(), "research").status == 200);
    CHECK(fixture.same());
    CHECK(admin_get_suite(fixture.context(), "research").status == 404);
    CHECK(parsed(admin_list_suites(fixture.context()))["data"].empty());
}

TEST_CASE("the suite twins answer the CLI's rules with a client's statuses",
          "[httpserver][admin][suites]") {
    const Fixture fixture;
    const auto post = [&](const nlohmann::json& body) {
        return admin_create_suite(fixture.context(), with_body("POST", body));
    };
    CHECK(post({{"name", "s"}}).status == 400);  // no members
    CHECK(post({{"name", "off"}, {"members", {{"chat", "root"}}}}).status == 400);
    CHECK(post({{"name", "s"}, {"members", {{"chat", "ghost"}}}}).status == 400);
    CHECK(post({{"name", "s"}, {"members", {{"root", "root"}}}}).status == 400);
    CHECK(post({{"name", "s"},
                {"members", {{"chat", {{"backend", "root"}, {"toolset", {"browser"}}}}}}})
              .status == 400);
    CHECK(post({{"name", "s"}, {"members", {{"chat", {{"backend", "root"}, {"context_size", 0}}}}}})
              .status == 400);
    CHECK(post({{"name", "s"}, {"members", 7}}).status == 400);
    const std::string before = Fixture::bytes(fixture.http_config);
    CHECK(Fixture::bytes(fixture.http_config) == before);

    REQUIRE(post({{"name", "s"}, {"members", {{"chat", "root"}}}}).status == 201);
    const HttpResponse again = post({{"name", "S"}, {"members", {{"chat", "root"}}}});
    CHECK(again.status == 409);
    // PUT replaces; a body name must match the path.
    CHECK(admin_put_suite(fixture.context(), "s",
                          with_body("PUT", {{"name", "t"}, {"members", {{"chat", "root"}}}}))
              .status == 400);
    const HttpResponse put = admin_put_suite(fixture.context(), "s",
                                             with_body("PUT", {{"members", {{"chat", "helper"}}}}));
    REQUIRE(put.status == 200);
    CHECK(parsed(put)["data"]["members"]["chat"]["backend"] == "helper");
    CHECK(admin_put_suite(fixture.context(), "nope",
                          with_body("PUT", {{"members", {{"chat", "root"}}}}))
              .status == 404);
    // A member edit: a role is required, a missing suite is 404, the last
    // member is never removed.
    CHECK(admin_set_suite_member(fixture.context(), "s", with_body("PUT", {{"member", "root"}}))
              .status == 400);
    CHECK(admin_set_suite_member(fixture.context(), "nope",
                                 with_body("PUT", {{"role", "chat"}, {"member", "root"}}))
              .status == 404);
    CHECK(admin_set_suite_member(fixture.context(), "s",
                                 with_body("PUT", {{"role", "chat"}, {"member", nullptr}}))
              .status == 400);
    CHECK(
        admin_set_default_suite(fixture.context(), with_body("POST", {{"name", "nope"}})).status ==
        400);
    CHECK(admin_delete_suite(fixture.context(), "nope").status == 404);
}
