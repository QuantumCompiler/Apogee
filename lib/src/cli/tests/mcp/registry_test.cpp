#include "mcp/registry.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "events/bus.h"
#include "support/fake_mcp_server.h"

/// The registry over the fleet: skipped never fatal, progress before the
/// dial, the tail on one line, and the tools in the loop's own registry.
namespace {

using apogee::mcp::Registry;
using apogee::mcp::RegistryOptions;
using apogee::mcp::ServerSpec;
using apogee::mcp::ServerStatus;

struct Run {
    std::vector<std::string> lines;
    std::vector<std::shared_ptr<apogee::testing::FakeMcpTransport>> made;
    Registry registry;

    explicit Run(std::vector<ServerSpec> servers,
                 std::chrono::milliseconds connect_timeout = std::chrono::seconds{5}) {
        RegistryOptions options;
        options.status = [this](std::string_view line) { lines.emplace_back(line); };
        options.connect_timeout = connect_timeout;
        options.call_timeout = std::chrono::seconds{2};
        options.spawn = apogee::testing::fake_fleet(&made);
        registry.connect_all(std::move(servers), options);
    }

    [[nodiscard]] const ServerStatus* status_of(std::string_view name) const {
        static ServerStatus none;
        for (const ServerStatus& status : registry.status()) {
            if (status.name == name) {
                none = status;
                return &none;
            }
        }
        return nullptr;
    }
};

ServerSpec server(std::string name, std::string command, bool enabled = true) {
    ServerSpec spec;
    spec.name = std::move(name);
    spec.command = std::move(command);
    spec.enabled = enabled;
    return spec;
}

}  // namespace

TEST_CASE("every personality is handled: connected, skipped, timed out -- never fatal",
          "[mcp][registry][fleet]") {
    const auto started = std::chrono::steady_clock::now();
    const Run run{{server("well", "well"), server("chatty", "chatty"), server("dying", "dying"),
                   server("slow", "slow"), server("junky", "malformed"),
                   server("off", "well", false), server("gone", "missing"), server("blank", "")},
                  std::chrono::milliseconds{400}};
    // The whole fleet, slow one included, well under what a timeout per
    // server would cost if a dead one waited it out.
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{5});
    CHECK(run.registry.connected_count() == 3);  // well, chatty, junky

    REQUIRE(run.status_of("well") != nullptr);
    CHECK(run.status_of("well")->connected);
    CHECK(run.status_of("well")->protocol_version == "2025-03-26");
    CHECK(run.status_of("well")->tools == std::vector<std::string>{"echo", "write"});
    CHECK(run.status_of("chatty")->connected);
    CHECK_FALSE(run.status_of("dying")->connected);
    CHECK(run.status_of("dying")->error.find("fatal: could not reach upstream") !=
          std::string::npos);
    CHECK_FALSE(run.status_of("slow")->connected);
    CHECK(run.status_of("slow")->error.find("timed out") != std::string::npos);
    CHECK(run.status_of("junky")->connected);
    CHECK_FALSE(run.status_of("off")->enabled);
    CHECK_FALSE(run.status_of("off")->connected);
    CHECK(run.status_of("off")->error.empty());  // listed, never dialled
    CHECK_FALSE(run.status_of("gone")->connected);
    CHECK(run.status_of("gone")->error.find("no such file") != std::string::npos);
    CHECK(run.status_of("blank")->error == "no command set");

    // The disabled, blank and missing servers were never even made.
    CHECK(run.made.size() == 5);  // well, chatty, dying, slow, junky
}

TEST_CASE(
    "progress precedes every dial, results follow, and a failure carries its tail on one line",
    "[mcp][registry][status]") {
    const Run run{{server("dying", "dying"), server("well", "well")}};
    REQUIRE(run.lines.size() >= 4);
    CHECK(run.lines[0] == "[mcp] connecting: dying...");
    // The read loop's own note may land first; the warning follows, on one
    // line, with the tail folded in.
    std::size_t warning_at = run.lines.size();
    for (std::size_t i = 0; i < run.lines.size(); ++i) {
        if (run.lines[i].starts_with("[mcp] warning: dying: connect failed (skipped):")) {
            warning_at = i;
        }
    }
    REQUIRE(warning_at < run.lines.size());
    CHECK(warning_at > 0);
    CHECK(run.lines[warning_at].find("stderr:") != std::string::npos);
    CHECK(run.lines[warning_at].find("fatal: could not reach upstream") != std::string::npos);
    CHECK(run.lines[warning_at].find('\n') == std::string::npos);
    // Diagnostics from the client's read loop ride the same writer, and
    // every line is Apogee's own.
    for (const std::string& line : run.lines) {
        INFO(line);
        CHECK(line.starts_with("[mcp]"));
    }
    std::size_t connecting_well = 0;
    std::size_t connected_well = 0;
    for (std::size_t i = 0; i < run.lines.size(); ++i) {
        if (run.lines[i] == "[mcp] connecting: well...") {
            connecting_well = i;
        }
        if (run.lines[i] == "[mcp] connected: well (2 tools)") {
            connected_well = i;
        }
    }
    CHECK(connecting_well < connected_well);
}

TEST_CASE("tools register namespaced, gated unless read-only, and dispatch to their server",
          "[mcp][registry][permission]") {
    apogee::events::Subscription subscription =
        apogee::events::subscribe(apogee::events::default_bus());
    const Run run{{server("srv", "well"), server("other", "erroring")}};
    apogee::agent::ToolRegistry registry;
    run.registry.register_into(registry);
    REQUIRE(registry.find("mcp__srv__echo") != nullptr);
    REQUIRE(registry.find("mcp__srv__write") != nullptr);
    REQUIRE(registry.find("mcp__other__echo") != nullptr);
    CHECK_FALSE(registry.find("mcp__srv__echo")->writes);  // readOnlyHint: true
    CHECK(registry.find("mcp__srv__write")->writes);       // no annotation: destructive
    CHECK(registry.find("mcp__srv__echo")->parameters_schema.find("\"text\"") != std::string::npos);

    const apogee::agent::ToolOutcome echoed =
        registry.find("mcp__srv__echo")->run(R"({"text":"hello"})");
    CHECK_FALSE(echoed.is_error);
    CHECK(echoed.content == "echo: hello");
    const apogee::agent::ToolOutcome declined = registry.find("mcp__other__echo")->run("{}");
    CHECK(declined.is_error);

    // The direct path `mcp test` takes.
    std::string error;
    CHECK(run.registry.call("mcp__srv__echo", R"({"text":"x"})", error).text == "echo: x");
    CHECK(error.empty());
    (void)run.registry.call("mcp__nobody__echo", "{}", error);
    CHECK(error.find("no connected server") != std::string::npos);
    (void)run.registry.call("read_file", "{}", error);
    CHECK(error.find("not an MCP tool name") != std::string::npos);

    // Lifecycle events, both ways.
    bool connected = false;
    while (const auto event = subscription.subscriber().wait_for(std::chrono::milliseconds{50})) {
        connected = connected || event->type == "mcp.server.connected";
    }
    CHECK(connected);
}

TEST_CASE("built-ins dispatch identically with zero, one failed, or one connected server",
          "[mcp][registry]") {
    apogee::agent::Tool native;
    native.name = "read_file";
    native.description = "native";
    native.run = [](std::string_view) { return apogee::agent::ToolOutcome{"native ran", false}; };
    for (const std::vector<ServerSpec> servers :
         {std::vector<ServerSpec>{}, std::vector<ServerSpec>{server("dying", "dying")},
          std::vector<ServerSpec>{server("well", "well")}}) {
        const Run run{servers};
        apogee::agent::ToolRegistry registry;
        registry.add(native);
        run.registry.register_into(registry);
        apogee::harness::ToolCall call;
        call.id = "c";
        call.name = "read_file";
        call.arguments = "{}";
        CHECK(apogee::agent::dispatch(registry, call, {}).content == "native ran");
        // A namespaced tool of a server that is down is an error result the
        // model reads -- never a fall-through, never an exception.
        call.name = "mcp__dying__echo";
        CHECK(apogee::agent::dispatch(registry, call, {}).is_error);
    }
}
