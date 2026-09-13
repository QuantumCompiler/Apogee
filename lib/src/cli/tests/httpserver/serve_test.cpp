#include "httpserver/serve.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "httpserver/mux.h"

/// The bind policy and the route table -- the parts of `serve.cpp`'s
/// neighbourhood that need no socket to assert.
namespace {

using apogee::httpserver::bind_refusal;
using apogee::httpserver::BindOptions;
using apogee::httpserver::is_loopback_host;
using apogee::httpserver::match_route;
using apogee::httpserver::Mux;
using apogee::httpserver::RouteSpec;

}  // namespace

TEST_CASE("loopback is recognised in every spelling, and nothing else is", "[httpserver][bind]") {
    CHECK(is_loopback_host("127.0.0.1"));
    CHECK(is_loopback_host("127.9.9.9"));
    CHECK(is_loopback_host("localhost"));
    CHECK(is_loopback_host("LOCALHOST"));
    CHECK(is_loopback_host("::1"));
    CHECK(is_loopback_host("[::1]"));

    CHECK_FALSE(is_loopback_host("0.0.0.0"));
    CHECK_FALSE(is_loopback_host("::"));
    CHECK_FALSE(is_loopback_host("192.168.1.20"));
    CHECK_FALSE(is_loopback_host("example.test"));
    CHECK_FALSE(is_loopback_host("1270.0.0.1"));
    CHECK_FALSE(is_loopback_host(""));
}

TEST_CASE("a non-loopback bind fails closed unless remote was allowed", "[httpserver][bind]") {
    BindOptions bind;
    CHECK(bind_refusal(bind).empty());  // the default: 127.0.0.1

    bind.host = "0.0.0.0";
    const std::string refusal = bind_refusal(bind);
    CHECK_FALSE(refusal.empty());
    CHECK(refusal.find("--allow-remote") != std::string::npos);
    CHECK(refusal.find("0.0.0.0") != std::string::npos);

    bind.allow_remote = true;
    CHECK(bind_refusal(bind).empty());

    // An unrecognisable host is not loopback: the policy errs closed.
    BindOptions odd;
    odd.host = "my-server";
    CHECK_FALSE(bind_refusal(odd).empty());

    BindOptions empty;
    empty.host.clear();
    CHECK_FALSE(bind_refusal(empty).empty());

    BindOptions port;
    port.port = 70000;
    CHECK(bind_refusal(port).find("--port") != std::string::npos);
}

TEST_CASE("route patterns match whole paths and capture one id segment", "[httpserver][mux]") {
    CHECK(match_route("/health", "/health") == std::string{});
    CHECK_FALSE(match_route("/health", "/health/").has_value());
    CHECK_FALSE(match_route("/health", "/healthy").has_value());
    CHECK(match_route("/v1/sessions/{id}", "/v1/sessions/abc-123") == std::string{"abc-123"});
    CHECK_FALSE(match_route("/v1/sessions/{id}", "/v1/sessions/").has_value());
    CHECK_FALSE(match_route("/v1/sessions/{id}", "/v1/sessions").has_value());
    CHECK_FALSE(match_route("/v1/sessions/{id}", "/v1/sessions/a/b").has_value());
}

TEST_CASE("the route table is the documented one", "[httpserver][mux]") {
    const std::vector<RouteSpec> routes = Mux::routes();
    const auto has = [&routes](std::string_view method, std::string_view pattern) {
        for (const RouteSpec& route : routes) {
            if (route.method == method && route.pattern == pattern) {
                return true;
            }
        }
        return false;
    };
    CHECK(routes.size() == 8);
    CHECK(has("POST", "/v1/chat/completions"));
    CHECK(has("POST", "/v1/completions"));
    CHECK(has("GET", "/v1/models"));
    CHECK(has("GET", "/v1/model/status"));
    CHECK(has("GET", "/health"));
    CHECK(has("GET", "/v1/sessions"));
    CHECK(has("GET", "/v1/sessions/{id}"));
    CHECK(has("DELETE", "/v1/sessions/{id}"));
}
