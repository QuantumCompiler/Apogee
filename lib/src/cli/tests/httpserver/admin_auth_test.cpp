#include "httpserver/admin_auth.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "harness/layout.h"
#include "httpserver/http_types.h"
#include "support/env_guard.h"

/// The bearer gate, exhaustively over header shapes, and the token file.
namespace {

using apogee::httpserver::admin_token_path;
using apogee::httpserver::bearer_of;
using apogee::httpserver::bearer_valid;
using apogee::httpserver::constant_time_equal;
using apogee::httpserver::generate_admin_token;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::load_or_create_admin_token;
using apogee::httpserver::unauthorized_response;

HttpRequest with_authorization(std::string value) {
    HttpRequest request;
    request.method = "GET";
    request.path = "/v1/admin/backends";
    if (!value.empty()) {
        request.headers["authorization"] = std::move(value);
    }
    return request;
}

}  // namespace

TEST_CASE("the bearer is read from the header only, never the query", "[httpserver][auth]") {
    const std::string token = "0123456789abcdef";
    CHECK(bearer_valid(with_authorization("Bearer " + token), token));
    CHECK(
        bearer_valid(with_authorization("bearer " + token), token));  // scheme is case-insensitive
    CHECK(bearer_valid(with_authorization("Bearer   " + token + "  "), token));

    CHECK_FALSE(bearer_valid(with_authorization({}), token));
    CHECK_FALSE(bearer_valid(with_authorization("Basic " + token), token));
    CHECK_FALSE(bearer_valid(with_authorization(token), token));  // no scheme
    CHECK_FALSE(bearer_valid(with_authorization("Bearer "), token));
    CHECK_FALSE(bearer_valid(with_authorization("Bearer " + token + "x"), token));
    // One byte off, same length: the shape a timing attack probes.
    std::string almost = token;
    almost.back() = '0';
    CHECK_FALSE(bearer_valid(with_authorization("Bearer " + almost), token));

    // The right token in the query string is REFUSED: a query lands in
    // request logs, and a token in a log is not a secret.
    HttpRequest query = with_authorization({});
    query.query["token"] = token;
    query.query["access_token"] = token;
    CHECK_FALSE(bearer_valid(query, token));
    CHECK(bearer_of(query).empty());

    // An empty token opens nothing.
    CHECK_FALSE(bearer_valid(with_authorization("Bearer "), ""));
}

TEST_CASE("constant_time_equal compares whole strings", "[httpserver][auth]") {
    CHECK(constant_time_equal("abc", "abc"));
    CHECK_FALSE(constant_time_equal("abc", "abd"));
    CHECK_FALSE(constant_time_equal("abc", "ab"));
    CHECK_FALSE(constant_time_equal("", "a"));
    CHECK(constant_time_equal("", ""));
}

TEST_CASE("the 401 names the scheme and the way to find the token", "[httpserver][auth]") {
    const auto response = unauthorized_response();
    CHECK(response.status == 401);
    CHECK(response.headers.at("WWW-Authenticate").rfind("Bearer", 0) == 0);
    const nlohmann::json body = nlohmann::json::parse(response.body);
    CHECK(body["error"]["type"] == "authentication_error");
    CHECK(body["error"]["message"].get<std::string>().find("--print-admin-token") !=
          std::string::npos);
}

TEST_CASE("the token is generated once, beside the config, and private", "[httpserver][auth]") {
    const apogee::testing::TempDir home{"admin-token-" + std::to_string(std::random_device{}())};
    const std::filesystem::path config = home.path() / "config" / "config.yaml";
    CHECK(admin_token_path(config) == home.path() / "config" / "admin-token");

    const std::string first = load_or_create_admin_token(config);
    CHECK(first.size() == 64);
    for (const char c : first) {
        CHECK(std::isxdigit(static_cast<unsigned char>(c)) != 0);
    }
    // Stable across reads: the file is the token.
    CHECK(load_or_create_admin_token(config) == first);
    CHECK(std::filesystem::exists(admin_token_path(config)));

    if (apogee::harness::supports_private_modes()) {
        const std::filesystem::perms mode =
            std::filesystem::status(admin_token_path(config)).permissions() &
            std::filesystem::perms::mask;
        CHECK(mode == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    }

    // Two fresh tokens never collide.
    CHECK(generate_admin_token() != generate_admin_token());

    // An empty file is regenerated rather than served as a plane nobody can
    // enter.
    std::ofstream{admin_token_path(config), std::ios::trunc} << "\n";
    const std::string regenerated = load_or_create_admin_token(config);
    CHECK(regenerated.size() == 64);
    CHECK(regenerated != first);
}
