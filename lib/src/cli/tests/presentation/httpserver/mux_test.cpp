#include "httpserver/mux.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "backends/mock.h"
#include "contracts/config.h"
#include "events/bus.h"
#include "harness/harness.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/jobs.h"
#include "httpserver/serve.h"
#include "support/env_guard.h"

/// The request gate in front of every route (57n), as a table over
/// `HttpRequest`s with no socket: who may reach a `serve` at all. A page the
/// user opens can aim `fetch` at `127.0.0.1`, and a rebinding page can even
/// read the reply, so a loopback-bound server answers only requests addressed
/// to it by a loopback name, and no server answers a page from another site.
namespace {

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::httpserver::BindOptions;
using apogee::httpserver::Handler;
using apogee::httpserver::HandlerOptions;
using apogee::httpserver::host_check_applies;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::httpserver::Mux;
using apogee::httpserver::request_refusal;
using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kConfig = R"(
models:
  default: mock
backends:
  mock:
    type: mock
    model: mock-1
)";

struct Fixture {
    // First, so a session file lands in a throwaway home.
    apogee::testing::TempDir home{"serve-mux-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};

    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<apogee::harness::Harness> harness;
    std::unique_ptr<Handler> handler;
    std::unique_ptr<Mux> mux;

    Fixture() {
        MockProvider::Options options;
        options.backend_name = "mock";
        options.turns = {MockTurn{"mock response", {}, apogee::harness::FinishReason::Stop, {}}};
        provider = std::make_shared<MockProvider>(std::move(options));
        harness = std::make_unique<apogee::harness::Harness>(
            apogee::harness::parse_config(kConfig, "<test>"));
        harness->register_provider("mock", provider);
        harness->use_default_router();
        HandlerOptions served;
        served.served = {"mock"};
        served.default_backend = "mock";
        handler = std::make_unique<Handler>(*harness, std::move(served), nullptr);
        mux = std::make_unique<Mux>(*handler);
    }
};

HttpRequest request(std::string method, std::string path, std::string host = "127.0.0.1:8080") {
    HttpRequest out;
    out.method = std::move(method);
    out.path = std::move(path);
    out.remote_address = "127.0.0.1";
    if (!host.empty()) {
        out.headers["host"] = std::move(host);
    }
    return out;
}

HttpRequest chat(std::string host, std::string session = {}) {
    HttpRequest out = request("POST", "/v1/chat/completions", std::move(host));
    nlohmann::json body{
        {"messages", nlohmann::json::array({nlohmann::json{{"role", "user"}, {"content", "hi"}}})}};
    if (!session.empty()) {
        body["session_id"] = std::move(session);
    }
    out.body = body.dump();
    out.headers["content-type"] = "application/json";
    return out;
}

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

void require_refused(const HttpResponse& response, std::string_view says) {
    CHECK(response.status == 403);
    const nlohmann::json body = parsed(response);
    CHECK(body["error"]["type"] == "forbidden");
    CHECK_THAT(body["error"]["message"].get<std::string>(),
               ContainsSubstring("request refused") && ContainsSubstring(std::string{says}));
}

}  // namespace

TEST_CASE("a loopback-bound server refuses a foreign Host on every route, health included",
          "[httpserver][mux][gate]") {
    Fixture fixture;
    // A real session, made on loopback, for the session routes to aim at.
    const HttpResponse made = fixture.mux->dispatch(chat("127.0.0.1:8080", "new"));
    REQUIRE(made.status == 200);
    const std::string id = made.headers.at("X-Apogee-Session-Id");
    const std::size_t turns = fixture.provider->turn_count();

    const std::string foreign = "attacker.example:8080";
    for (HttpRequest probe :
         {chat(foreign), chat(foreign, id), request("GET", "/v1/sessions", foreign),
          request("GET", "/v1/sessions/" + id, foreign),
          request("DELETE", "/v1/sessions/" + id, foreign), request("GET", "/health", foreign),
          request("GET", "/v1/models", foreign), request("GET", "/nope", foreign)}) {
        CAPTURE(probe.method, probe.path);
        require_refused(fixture.mux->dispatch(probe), "Host attacker.example:8080");
    }
    // No turn ran, and the session the DELETE aimed at is still there.
    CHECK(fixture.provider->turn_count() == turns);
    CHECK(fixture.mux->dispatch(request("GET", "/v1/sessions/" + id)).status == 200);
}

TEST_CASE("loopback names pass in every spelling and at any port, and lookalikes do not",
          "[httpserver][mux][gate]") {
    Fixture fixture;
    for (const std::string host :
         {"127.0.0.1:8080", "127.0.0.1", "localhost:8080", "LOCALHOST:3000", "localhost",
          "[::1]:8080", "[::1]", "127.9.9.9:1"}) {
        CAPTURE(host);
        CHECK(fixture.mux->dispatch(request("GET", "/health", host)).status == 200);
        CHECK(request_refusal(request("GET", "/health", host), true).empty());
    }
    // A request with no Host at all is no browser's: an HTTP/1.0 client, or
    // a test. A page cannot send one.
    CHECK(fixture.mux->dispatch(request("GET", "/health", "")).status == 200);

    // Names an attacker can point at 127.0.0.1 are names an attacker owns.
    for (const std::string host :
         {"127.attacker.example:8080", "127.0.0.1.nip.io", "localhost.attacker.example",
          "attacker.example", "0.0.0.0:8080", "192.168.1.20:8080", "[::ffff:7f00:1]:8080",
          "127.0.0.256", "1270.0.0.1", "127.0.0.1:8080:9", "[::1]x", "[::1]x80", ":8080",
          "127.0.0.1:", "127.0.0.1."}) {
        CAPTURE(host);
        require_refused(fixture.mux->dispatch(request("GET", "/health", host)), "Host " + host);
    }
}

TEST_CASE("a page on another site is refused before the handler runs, whatever its body's type",
          "[httpserver][mux][gate]") {
    Fixture fixture;
    // The sweep's forgery: a cross-site `fetch` with `mode: 'no-cors'` and a
    // CORS-safelisted text/plain body, addressed to the loopback server.
    HttpRequest forged = chat("127.0.0.1:8080", "new");
    forged.headers["content-type"] = "text/plain;charset=UTF-8";
    forged.headers["origin"] = "http://attacker.example";
    require_refused(fixture.mux->dispatch(forged), "Origin http://attacker.example");
    CHECK(fixture.provider->turn_count() == 0);
    CHECK(fixture.handler->sessions().size() == 0);

    // The same forgery after a rebinding: both its Host and its Origin are
    // the attacker's. The Host is what a loopback server refuses first.
    forged.headers["host"] = "attacker.example:8080";
    require_refused(fixture.mux->dispatch(forged), "Host attacker.example:8080");
    CHECK(fixture.provider->turn_count() == 0);

    // `null` is what a sandboxed frame or a file sends: no site to trust.
    HttpRequest sandboxed = chat("127.0.0.1:8080");
    sandboxed.headers["origin"] = "null";
    require_refused(fixture.mux->dispatch(sandboxed), "Origin null");

    // A lookalike of loopback in an Origin is the attacker's name too.
    HttpRequest lookalike = chat("127.0.0.1:8080");
    lookalike.headers["origin"] = "http://127.attacker.example:8080";
    require_refused(fixture.mux->dispatch(lookalike), "Origin http://127.attacker.example:8080");
    CHECK(fixture.provider->turn_count() == 0);

    // A page served from this machine passes, at any port; so does a client
    // that sends no Origin -- curl, the OpenAI SDKs.
    for (const std::string origin :
         {"http://localhost:3000", "http://127.0.0.1:5173", "https://[::1]:8443", ""}) {
        CAPTURE(origin);
        HttpRequest local = chat("127.0.0.1:8080");
        if (!origin.empty()) {
            local.headers["origin"] = origin;
        }
        CHECK(fixture.mux->dispatch(local).status == 200);
    }
}

TEST_CASE("a remote-bound server takes any Host, and an Origin only from loopback or itself",
          "[httpserver][mux][gate]") {
    Fixture fixture;
    fixture.mux->set_host_check(false);
    CHECK_FALSE(fixture.mux->host_check());

    // The operator's proxy and access control own the name it is reached by.
    CHECK(fixture.mux->dispatch(request("GET", "/health", "api.example.com")).status == 200);
    CHECK(fixture.mux->dispatch(request("GET", "/health", "10.0.0.5:8080")).status == 200);
    CHECK(fixture.mux->dispatch(chat("api.example.com")).status == 200);

    // A page served by the same site passes; a port is no other site.
    for (const std::string origin :
         {"https://api.example.com", "http://API.example.com:8080", "http://localhost:3000"}) {
        CAPTURE(origin);
        HttpRequest same = chat("api.example.com");
        same.headers["origin"] = origin;
        CHECK(fixture.mux->dispatch(same).status == 200);
    }
    const std::size_t turns = fixture.provider->turn_count();

    HttpRequest foreign = chat("api.example.com");
    foreign.headers["origin"] = "http://attacker.example";
    foreign.headers["content-type"] = "text/plain";
    require_refused(fixture.mux->dispatch(foreign), "Origin http://attacker.example");
    // An Origin that only ends like the Host is another site.
    foreign.headers["origin"] = "https://evil-api.example.com";
    require_refused(fixture.mux->dispatch(foreign), "Origin https://evil-api.example.com");
    CHECK(fixture.provider->turn_count() == turns);
}

TEST_CASE("the bind decides the Host check: --allow-remote hands the name to the proxy",
          "[httpserver][mux][gate][bind]") {
    // What `run_server` tells the mux, for each bind the policy permits: a
    // loopback bind checks Host unless --allow-remote says a reverse proxy
    // in front owns the name -- one on this host forwards the client's
    // (Caddy, Traefik and HAProxy by default, nginx with `Host $host`).
    struct Policy {
        std::string host;
        bool allow_remote;
        bool checks;
    };

    for (const Policy& policy : {Policy{"127.0.0.1", false, true}, Policy{"localhost", false, true},
                                 Policy{"[::1]", false, true}, Policy{"127.0.0.1", true, false},
                                 Policy{"localhost", true, false}, Policy{"0.0.0.0", true, false},
                                 Policy{"192.168.1.20", true, false}}) {
        CAPTURE(policy.host, policy.allow_remote);
        BindOptions bind;
        bind.host = policy.host;
        bind.allow_remote = policy.allow_remote;
        CHECK(host_check_applies(bind) == policy.checks);

        Fixture fixture;
        fixture.mux->set_host_check(host_check_applies(bind));
        // The proxy's forwarded Host, and a same-site page behind it.
        HttpRequest proxied = request("GET", "/health", "api.example.com");
        const HttpResponse answered = fixture.mux->dispatch(proxied);
        if (policy.checks) {
            require_refused(answered, "Host api.example.com");
        } else {
            CHECK(answered.status == 200);
        }
        proxied.headers["origin"] = "https://api.example.com";
        CHECK((fixture.mux->dispatch(proxied).status == 200) == !policy.checks);
        // A page from another site is refused under every policy.
        proxied.headers["origin"] = "http://attacker.example";
        require_refused(fixture.mux->dispatch(proxied),
                        policy.checks ? "Host api.example.com" : "Origin http://attacker.example");
        // Loopback names pass under every policy.
        CHECK(fixture.mux->dispatch(request("GET", "/health", "127.0.0.1:8080")).status == 200);
    }
}

TEST_CASE("the gate runs ahead of the admin gate, which is otherwise unchanged",
          "[httpserver][mux][gate][admin]") {
    Fixture fixture;
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminOptions options;
    options.config_path = fixture.home.path() / "config" / "config.yaml";
    options.startup = fixture.harness->config();
    apogee::httpserver::AdminHandler admin{options, jobs, bus};
    const Mux mux{*fixture.handler, admin, "token"};
    CHECK(mux.host_check());  // the stricter answer until told otherwise

    HttpRequest authorized = request("GET", "/v1/admin/nope", "attacker.example:8080");
    authorized.headers["authorization"] = "Bearer token";
    require_refused(mux.dispatch(authorized), "Host attacker.example:8080");

    // On loopback the bearer gate answers as it always has: 401 for a path
    // that exists and one that does not alike, 404 only once authenticated.
    CHECK(mux.dispatch(request("GET", "/v1/admin/backends")).status == 401);
    CHECK(mux.dispatch(request("GET", "/v1/admin/nope")).status == 401);
    HttpRequest missing = request("GET", "/v1/admin/nope");
    missing.headers["authorization"] = "Bearer token";
    CHECK(mux.dispatch(missing).status == 404);
}

TEST_CASE("a refusal echoes the value a client set, mended, and nothing else",
          "[httpserver][mux][gate][utf8]") {
    Fixture fixture;
    HttpRequest odd = request("GET", "/health", std::string{"caf\xe9.example"});
    odd.headers["authorization"] = "Bearer sk-not-echoed";
    odd.headers["cookie"] = "session=not-echoed";
    const HttpResponse response = fixture.mux->dispatch(odd);
    CHECK(response.status == 403);
    const nlohmann::json body = parsed(response);  // valid JSON: the byte was mended
    const std::string message = body["error"]["message"].get<std::string>();
    CHECK_THAT(message, ContainsSubstring("Host caf\xef\xbf\xbd.example"));
    CHECK(response.body.find("not-echoed") == std::string::npos);
    CHECK(response.headers.empty());
}
