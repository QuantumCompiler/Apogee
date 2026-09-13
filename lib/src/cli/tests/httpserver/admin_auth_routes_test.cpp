#include "httpserver/admin_auth_routes.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "harness/config.h"
#include "httpserver/http_types.h"
#include "secrets/store.h"
#include "support/env_guard.h"

/// The credential routes: loopback-only intake, metadata-only output.
namespace {

using apogee::httpserver::admin_clear_credential;
using apogee::httpserver::admin_list_credentials;
using apogee::httpserver::admin_put_credential;
using apogee::httpserver::AdminAuthContext;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;

constexpr std::string_view kSecret = "sk-LEAKPROBE-a1b2c3d4e5";

struct Fixture {
    apogee::testing::TempDir home{"admin-auth-routes-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = home.path() / "config" / "config.yaml";
    apogee::secrets::EnvSnapshot env;  // empty: the developer's shell stays out

    Fixture() {
        std::filesystem::create_directories(config.parent_path());
        std::ofstream{config} << "backends:\n  gpt:\n    type: openai\n  local:\n    type: mock\n";
    }

    [[nodiscard]] AdminAuthContext context() const {
        return AdminAuthContext{.config_path = config, .env = &env};
    }
};

HttpRequest put(std::string key, std::string remote, std::string forwarded = {}) {
    HttpRequest request;
    request.method = "PUT";
    request.body = nlohmann::json{{"key", std::move(key)}}.dump();
    request.remote_address = std::move(remote);
    if (!forwarded.empty()) {
        request.headers["x-forwarded-for"] = std::move(forwarded);
    }
    return request;
}

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

}  // namespace

TEST_CASE("a key is accepted from a loopback peer only, whatever a header says",
          "[httpserver][auth][secrets]") {
    const Fixture fixture;

    // A remote peer is refused before the body is looked at, and a spoofed
    // forwarded header does not change that.
    const HttpResponse remote =
        admin_put_credential(fixture.context(), "openai", put(std::string{kSecret}, "203.0.113.9"));
    CHECK(remote.status == 403);
    CHECK(parsed(remote)["error"]["type"] == "forbidden");
    const HttpResponse spoofed = admin_put_credential(
        fixture.context(), "openai", put(std::string{kSecret}, "203.0.113.9", "127.0.0.1"));
    CHECK(spoofed.status == 403);
    CHECK_FALSE(std::filesystem::exists(apogee::secrets::credentials_path(fixture.config)));

    // A loopback peer -- in either spelling -- is accepted, and nothing echoes.
    for (const std::string peer : {"127.0.0.1", "::1"}) {
        const HttpResponse local =
            admin_put_credential(fixture.context(), "openai", put(std::string{kSecret}, peer));
        REQUIRE(local.status == 200);
        CHECK(parsed(local)["provider"] == "openai");
        CHECK(parsed(local).contains("stored_at"));
        CHECK(local.body.find(kSecret) == std::string::npos);
    }
    const apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(fixture.config)};
    CHECK(*store.key_for("openai") == kSecret);
}

TEST_CASE("the listing is metadata plus each backend's source, never a key",
          "[httpserver][auth][secrets]") {
    const Fixture fixture;
    REQUIRE(admin_put_credential(fixture.context(), "openai", put(std::string{kSecret}, "::1"))
                .status == 200);

    const HttpResponse listed = admin_list_credentials(fixture.context());
    REQUIRE(listed.status == 200);
    const nlohmann::json body = parsed(listed);
    REQUIRE(body["data"].size() == 1);
    CHECK(body["data"][0]["provider"] == "openai");
    CHECK(body["data"][0].contains("stored_at"));
    CHECK_FALSE(body["data"][0].contains("key"));
    // The configured openai entry resolves from the store; the mock takes
    // no key and is not listed.
    REQUIRE(body["backends"].size() == 1);
    CHECK(body["backends"][0]["name"] == "gpt");
    CHECK(body["backends"][0]["source"] == "store");
    CHECK(listed.body.find(kSecret) == std::string::npos);
}

TEST_CASE("providers are validated, and a vendor CLI is refused with the principle",
          "[httpserver][auth][secrets]") {
    const Fixture fixture;
    const HttpResponse cli =
        admin_put_credential(fixture.context(), "claude-cli", put("x", "127.0.0.1"));
    CHECK(cli.status == 400);
    CHECK(parsed(cli)["error"]["message"].get<std::string>().find("vendor CLI") !=
          std::string::npos);
    const HttpResponse unknown =
        admin_put_credential(fixture.context(), "carrier-pigeon", put("x", "127.0.0.1"));
    CHECK(unknown.status == 400);
    CHECK(parsed(unknown)["error"]["message"].get<std::string>().find(
              "anthropic, openai, google") != std::string::npos);

    // The key must be in the body, and not empty.
    HttpRequest bare;
    bare.method = "PUT";
    bare.remote_address = "127.0.0.1";
    bare.body = "{}";
    CHECK(admin_put_credential(fixture.context(), "openai", bare).status == 400);
    bare.body = R"({"key": ""})";
    CHECK(admin_put_credential(fixture.context(), "openai", bare).status == 400);
    bare.body = "not json";
    CHECK(admin_put_credential(fixture.context(), "openai", bare).status == 400);
}

TEST_CASE("clear removes the slot and is a 404 when there is none", "[httpserver][auth][secrets]") {
    const Fixture fixture;
    CHECK(admin_clear_credential(fixture.context(), "openai").status == 404);
    REQUIRE(admin_put_credential(fixture.context(), "openai", put("k", "127.0.0.1")).status == 200);
    const HttpResponse cleared = admin_clear_credential(fixture.context(), "openai");
    CHECK(cleared.status == 200);
    CHECK(parsed(cleared)["cleared"] == "openai");
    CHECK(admin_clear_credential(fixture.context(), "openai").status == 404);
    CHECK(admin_clear_credential(fixture.context(), "gemini-cli").status == 400);
}
