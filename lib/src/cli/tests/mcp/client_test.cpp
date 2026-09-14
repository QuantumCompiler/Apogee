#include "mcp/client.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>

#include "harness/cancellation.h"
#include "support/fake_mcp_server.h"

/// The client against the scripted fleet: the handshake, the cache, the
/// call path, and -- above all -- a dead server releasing its waiters now.
namespace {

using apogee::mcp::Client;
using apogee::mcp::ClientOptions;
using apogee::testing::FakeMcpTransport;
using Personality = FakeMcpTransport::Personality;

struct Forwarder final : apogee::mcp::Transport {
    std::shared_ptr<FakeMcpTransport> inner;

    explicit Forwarder(std::shared_ptr<FakeMcpTransport> fake) : inner{std::move(fake)} {}

    bool send(const nlohmann::json& m) override {
        return inner->send(m);
    }

    std::optional<std::string> recv(std::chrono::milliseconds t) override {
        return inner->recv(t);
    }

    std::string stderr_tail() const override {
        return inner->stderr_tail();
    }

    void close() override {
        inner->close();
    }
};

std::unique_ptr<Client> connect(const std::shared_ptr<FakeMcpTransport>& fake, std::string& error,
                                std::chrono::milliseconds connect_timeout = std::chrono::seconds{5},
                                std::vector<std::string>* log = nullptr) {
    ClientOptions options;
    options.connect_timeout = connect_timeout;
    options.call_timeout = std::chrono::seconds{2};
    options.client_version = "test";
    if (log != nullptr) {
        options.log = [log](std::string_view line) { log->emplace_back(line); };
    }
    return Client::connect("fake", std::make_unique<Forwarder>(fake), options, error);
}

}  // namespace

TEST_CASE("a well-behaved server: handshake, cached tools, a call, and the notification",
          "[mcp][client]") {
    auto fake = std::make_shared<FakeMcpTransport>(Personality::Well, "srv");
    std::string error;
    const std::unique_ptr<Client> client = connect(fake, error);
    REQUIRE(client != nullptr);
    CHECK(error.empty());
    CHECK(client->protocol_version() == "2025-03-26");
    CHECK(client->server_name() == "srv");
    REQUIRE(client->tools().size() == 2);
    CHECK(client->tools()[0].name == "echo");
    CHECK(client->tools()[0].read_only_hint == true);
    CHECK_FALSE(client->tools()[1].read_only_hint.has_value());

    // What went over the wire: initialize (with our version), the initialized
    // notification (no id), tools/list -- in that order.
    const std::vector<nlohmann::json> sent = fake->sent();
    REQUIRE(sent.size() == 3);
    CHECK(sent[0]["method"] == "initialize");
    CHECK(sent[0]["params"]["protocolVersion"] == "2025-03-26");
    CHECK(sent[0]["params"]["clientInfo"]["name"] == "apogee");
    CHECK(sent[1]["method"] == "notifications/initialized");
    CHECK_FALSE(sent[1].contains("id"));
    CHECK(sent[2]["method"] == "tools/list");

    const apogee::mcp::ToolCallResult result =
        client->call_tool("echo", R"({"text":"hi"})", apogee::harness::CancellationToken{});
    CHECK_FALSE(result.is_error);
    CHECK(result.text == "echo: hi");  // the image block was skipped
    const apogee::mcp::ToolCallResult unknown =
        client->call_tool("nope", "{}", apogee::harness::CancellationToken{});
    CHECK(unknown.is_error);
    CHECK(unknown.text.find("unknown tool") != std::string::npos);
    // Bad arguments are an empty object, not a crash.
    CHECK_FALSE(
        client->call_tool("echo", "not json", apogee::harness::CancellationToken{}).is_error);
}

TEST_CASE("a server that dies mid-handshake fails at once, with its stderr tail",
          "[mcp][client][dying]") {
    auto fake = std::make_shared<FakeMcpTransport>(Personality::Dying, "dead");
    std::string error;
    const auto started = std::chrono::steady_clock::now();
    // A long connect timeout: the point is that it is NOT waited out.
    const std::unique_ptr<Client> client = connect(fake, error, std::chrono::seconds{20});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(client == nullptr);
    CHECK(elapsed < std::chrono::seconds{2});
    CHECK(error.find("initialize") != std::string::npos);
    CHECK(error.find("connection closed") != std::string::npos);
    CHECK(error.find("stderr:") != std::string::npos);
    CHECK(error.find("fatal: could not reach upstream") != std::string::npos);
    CHECK(error.find('\n') == std::string::npos);  // one line: it must fit a status render
}

TEST_CASE("a server that never answers times out at the bound and is closed",
          "[mcp][client][slow]") {
    auto fake = std::make_shared<FakeMcpTransport>(Personality::Slow, "slow");
    std::string error;
    const auto started = std::chrono::steady_clock::now();
    const std::unique_ptr<Client> client = connect(fake, error, std::chrono::milliseconds{300});
    CHECK(client == nullptr);
    CHECK(error.find("timed out") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{5});
    CHECK(fake->closed());
}

TEST_CASE("junk frames, unexpected ids and notifications are logged and skipped",
          "[mcp][client][malformed]") {
    auto fake = std::make_shared<FakeMcpTransport>(Personality::Malformed, "junky");
    std::string error;
    std::vector<std::string> log;
    const std::unique_ptr<Client> client = connect(fake, error, std::chrono::seconds{5}, &log);
    REQUIRE(client != nullptr);
    CHECK(client->tools().size() == 2);
    bool saw_bad = false;
    bool saw_unexpected = false;
    bool saw_notification = false;
    for (const std::string& line : log) {
        saw_bad = saw_bad || line.find("bad frame") != std::string::npos;
        saw_unexpected = saw_unexpected || line.find("unexpected response id") != std::string::npos;
        saw_notification = saw_notification || line.find("notification") != std::string::npos;
    }
    CHECK(saw_bad);
    CHECK(saw_unexpected);
    CHECK(saw_notification);
}

TEST_CASE("an isError result is an error the model reads, and a closed connection fails a call",
          "[mcp][client][erroring]") {
    auto fake = std::make_shared<FakeMcpTransport>(Personality::Erroring, "err");
    std::string error;
    const std::unique_ptr<Client> client = connect(fake, error);
    REQUIRE(client != nullptr);
    const apogee::mcp::ToolCallResult result =
        client->call_tool("echo", "{}", apogee::harness::CancellationToken{});
    CHECK(result.is_error);
    CHECK(result.text.starts_with("Error"));
    CHECK(result.text.find("the server declined") != std::string::npos);

    // Close, then call: no hang, an error result.
    client->close();
    CHECK(client->done());
    const apogee::mcp::ToolCallResult after =
        client->call_tool("echo", "{}", apogee::harness::CancellationToken{});
    CHECK(after.is_error);
    CHECK(after.text.find("connection closed") != std::string::npos);
    client->close();  // idempotent, no race
}

TEST_CASE("a cancelled call returns promptly, and an unanswered one times out at its bound",
          "[mcp][client]") {
    auto fake = std::make_shared<FakeMcpTransport>(Personality::SlowCalls, "srv");
    std::string error;
    ClientOptions options;
    options.connect_timeout = std::chrono::seconds{5};
    options.call_timeout = std::chrono::milliseconds{400};
    const std::unique_ptr<Client> client =
        Client::connect("srv", std::make_unique<Forwarder>(fake), options, error);
    REQUIRE(client != nullptr);

    // Cancelled before the call: back at once, as a result.
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    auto started = std::chrono::steady_clock::now();
    const apogee::mcp::ToolCallResult cancelled = client->call_tool("echo", "{}", token);
    CHECK(cancelled.is_error);
    CHECK(cancelled.text.find("cancelled") != std::string::npos);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds{300});

    // Never answered: the call bound, not forever.
    started = std::chrono::steady_clock::now();
    const apogee::mcp::ToolCallResult late =
        client->call_tool("echo", "{}", apogee::harness::CancellationToken::create());
    CHECK(late.is_error);
    CHECK(late.text.find("timed out") != std::string::npos);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed >= std::chrono::milliseconds{350});
    CHECK(elapsed < std::chrono::seconds{3});
}
