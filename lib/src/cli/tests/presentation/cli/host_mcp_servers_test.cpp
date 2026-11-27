#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "acp/host_servers.h"
#include "agent/tool.h"
#include "cli/acp_cmd.h"
#include "cli/helpers.h"
#include "contracts/config.h"
#include "mcp/registry.h"
#include "support/env_guard.h"
#include "support/fake_mcp_server.h"

/// The MCP servers an ACP host hands a session (56b), on the session's side:
/// what the handover becomes, how a host's server joins the config's fleet --
/// its name made a server name, a config entry it displaces set aside, one
/// dial for both -- and that its tools pass the gate as any MCP tool's. Over
/// the scripted fleet: no process, no network. The whole, on the real binary
/// and a scripted ACP host, is `cli.acp_conformance`'s.
namespace {

using apogee::mcp::ServerSpec;
using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kSentinel = "s3nt1nel-v4lue";

/// A `mcp_spawn` that hands out the scripted fleet and keeps every spec it is
/// handed -- the fleet itself keeps only the name.
struct Recorder {
    std::vector<ServerSpec> handed;
    std::vector<std::shared_ptr<apogee::testing::FakeMcpTransport>> made;

    [[nodiscard]] auto spawn() {
        return [this, fleet = apogee::testing::fake_fleet(&made)](
                   const ServerSpec& spec, apogee::mcp::StderrTail::Sink sink, std::string& error) {
            handed.push_back(spec);
            return fleet(spec, std::move(sink), error);
        };
    }
};

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line + "\n";
    }
    return out;
}

[[nodiscard]] std::vector<std::string> names_of(const std::vector<ServerSpec>& specs) {
    std::vector<std::string> names;
    for (const ServerSpec& spec : specs) {
        names.push_back(spec.name);
    }
    return names;
}

[[nodiscard]] ServerSpec spec(std::string name, std::string command,
                              std::vector<std::string> args = {},
                              std::vector<std::string> env = {}) {
    return ServerSpec{.name = std::move(name),
                      .command = std::move(command),
                      .args = std::move(args),
                      .env = std::move(env),
                      .enabled = true};
}

/// `find_on_path`, as a machine with `xcrun` in /usr/bin answers it.
[[nodiscard]] std::string fake_path(std::string_view program) {
    return program == "xcrun" ? std::string{"/usr/bin/xcrun"} : std::string{};
}

[[nodiscard]] bool sent_a_call(const apogee::testing::FakeMcpTransport& fake) {
    for (const nlohmann::json& frame : fake.sent()) {
        if (frame.value("method", std::string{}) == "tools/call") {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("a host's server names are made server names; a refused or repeated one is skipped",
          "[commands][mcp][host-servers]") {
    std::vector<std::string> said;
    const std::vector<ServerSpec> specs = apogee::commands::session_mcp_specs(
        {},
        {spec("xcode.tools", "a"), spec("bad name!", "b"), spec("a__b", "c"),
         spec("Xcode-Tools", "d"), spec("", "e"), spec("plain:one", "f")},
        fake_path, said);
    // `.` and `:` become `-`, as `mcp create` makes them; a second server of
    // one name, compared as the config compares names, is skipped.
    CHECK(names_of(specs) == std::vector<std::string>{"xcode-tools", "plain-one"});
    REQUIRE(said.size() == 4);
    const std::string text = joined(said);
    CHECK_THAT(text, ContainsSubstring("[mcp] warning: the host's server 'bad name!' was "
                                       "skipped: 'bad name!' is not a server name"));
    CHECK_THAT(text, ContainsSubstring("'a__b' was skipped: a server name must be non-empty and "
                                       "must not contain '__'"));
    CHECK_THAT(text, ContainsSubstring("'Xcode-Tools' was skipped: the host named a server "
                                       "'Xcode-Tools' already"));
    CHECK_THAT(text, ContainsSubstring("the host's server '' was skipped"));
}

TEST_CASE("a host server sets aside the config entry of its name, or of its command and arguments",
          "[commands][mcp][host-servers]") {
    std::vector<std::string> said;
    const std::vector<ServerSpec> specs = apogee::commands::session_mcp_specs(
        {spec("SELF", "/bin/apogee", {"mcp", "serve"}, {"FROM=config"}),
         spec("xcode", "xcrun", {"mcpbridge"}), spec("other", "xcrun", {"something-else"}),
         spec("keep", "/bin/keep")},
        {spec("self", "/bin/apogee-elsewhere", {"mcp", "serve"}, {"FROM=host"}),
         // A bare command is found on PATH before it is compared.
         spec("xcode-tools", "/usr/bin/xcrun", {"mcpbridge"},
              {"MCP_XCODE_SESSION_ID=" + std::string{kSentinel}})},
        fake_path, said);
    // The config's first, as they were, then the host's.
    CHECK(names_of(specs) == std::vector<std::string>{"other", "keep", "self", "xcode-tools"});
    CHECK(specs.at(2).env == std::vector<std::string>{"FROM=host"});
    REQUIRE(said.size() == 2);
    CHECK(said[0] ==
          "[mcp] warning: SELF: the config's server is set aside for this session -- the "
          "host's 'self' takes its place");
    CHECK(said[1] ==
          "[mcp] warning: xcode: the config's server is set aside for this session -- the "
          "host's 'xcode-tools' takes its place");
    CHECK_THAT(joined(said), !ContainsSubstring(std::string{kSentinel}));
}

TEST_CASE("a command found on PATH and one written as a path are compared in one normal form",
          "[commands][mcp][host-servers]") {
    // A PATH entry written `/usr//bin` or `/usr/bin/.` finds `xcrun` there,
    // spelled so; the host's `/usr/bin/xcrun` is the same program, and on
    // Windows `lexically_normal` would spell it `\usr\bin\xcrun`. Both sides
    // are normalized and compared with the generic separator.
    for (const std::string_view found : {"/usr//bin/xcrun", "/usr/bin/./xcrun"}) {
        CAPTURE(found);
        std::vector<std::string> said;
        const std::vector<ServerSpec> specs = apogee::commands::session_mcp_specs(
            {spec("xcode", "xcrun", {"mcpbridge"})},
            {spec("xcode-tools", "/usr/bin/xcrun", {"mcpbridge"})},
            [&found](std::string_view program) {
                return program == "xcrun" ? std::string{found} : std::string{};
            },
            said);
        CHECK(names_of(specs) == std::vector<std::string>{"xcode-tools"});
        CHECK(said == std::vector<std::string>{
                          "[mcp] warning: xcode: the config's server is set aside for this "
                          "session -- the host's 'xcode-tools' takes its place"});
    }
}

TEST_CASE("the host's servers join the one fleet: after the config's, even with none configured",
          "[commands][mcp][host-servers]") {
    // An empty config: the host's server connects all the same.
    {
        Recorder recorder;
        const auto mcp = std::make_shared<apogee::mcp::Registry>();
        const apogee::harness::Config config;
        const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools(
            apogee::commands::BuiltInToolOptions{.config = &config,
                                                 .mcp = mcp,
                                                 .mcp_spawn = recorder.spawn(),
                                                 .session_mcp_servers = {spec("host", "well")}});
        REQUIRE(registry.find("mcp__host__echo") != nullptr);
        CHECK_FALSE(registry.find("mcp__host__echo")->writes);  // its server said read-only
        CHECK(registry.find("mcp__host__write")->writes);       // no annotation: gated
        CHECK(registry.find("read_file") != nullptr);
        CHECK(mcp->connected_count() == 1);
    }
    // With the config's: one dial, the config's first; a read-only policy
    // still applies last, over the host's tools too.
    Recorder recorder;
    std::vector<std::string> lines;
    apogee::harness::Config config;
    apogee::harness::McpServerConfig srv;
    srv.command = "well";
    config.mcp_servers.emplace("srv", srv);
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry registry =
        apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
            .config = &config,
            .policy = apogee::harness::AgentToolPolicy::ReadOnly,
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = recorder.spawn(),
            // Its own arguments: the same command and arguments would be the
            // config's server again, set aside (below).
            .session_mcp_servers = {spec("host", "well", {"--as-host"})}});
    CHECK(names_of(recorder.handed) == std::vector<std::string>{"srv", "host"});
    CHECK(registry.find("mcp__srv__echo") != nullptr);
    CHECK(registry.find("mcp__host__echo") != nullptr);
    CHECK(registry.find("mcp__host__write") == nullptr);
    CHECK(mcp->connected_count() == 2);
    CHECK_THAT(joined(lines), ContainsSubstring("[mcp] connecting: host..."));
}

TEST_CASE(
    "an http server and a stdio one from a host: only the stdio one is spawned, its env as given",
    "[commands][mcp][host-servers][acp]") {
    const apogee::testing::TempDir root{"host-mcp-" + std::to_string(std::random_device{}())};
    const nlohmann::json servers{
        {{"type", "http"},
         {"name", "remote"},
         {"url", "http://127.0.0.1:9/mcp"},
         {"headers", {{{"name", "Authorization"}, {"value", std::string{kSentinel}}}}}},
        {{"name", "self"},
         {"command", "well"},
         {"args", nlohmann::json::array()},
         {"env", {{{"name", "TOKEN"}, {"value", std::string{kSentinel}}}}}}};
    // The adapter's half, the handover, then the child's half.
    std::string error;
    const std::filesystem::path handover =
        apogee::acp::write_handover(apogee::acp::host_servers(servers), root.path(), error);
    REQUIRE(error.empty());
    std::vector<std::string> notes;
    const std::vector<ServerSpec> hosted = apogee::commands::host_mcp_servers(handover, notes);
    CHECK_FALSE(std::filesystem::exists(handover));
    REQUIRE(notes.size() == 1);
    CHECK(notes[0] == "Apogee connects stdio MCP servers only; skipped: remote");

    Recorder recorder;
    std::vector<std::string> lines;
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry registry =
        apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = recorder.spawn(),
            .session_mcp_servers = hosted});
    REQUIRE(recorder.handed.size() == 1);
    CHECK(recorder.handed[0].name == "self");
    CHECK(recorder.handed[0].env == std::vector<std::string>{"TOKEN=" + std::string{kSentinel}});
    CHECK(registry.find("mcp__self__echo") != nullptr);
    CHECK(registry.find("mcp__remote__echo") == nullptr);
    CHECK_THAT(joined(lines), !ContainsSubstring(std::string{kSentinel}));

    // A handover that cannot be read is said, and connects nothing.
    std::vector<std::string> unread;
    CHECK(apogee::commands::host_mcp_servers(root.path() / "gone.json", unread).empty());
    REQUIRE(unread.size() == 1);
    CHECK_THAT(unread[0], ContainsSubstring("the host's MCP servers could not be read"));
}

TEST_CASE(
    "a slow host server is skipped at the bound, a dying one says its last line, neither fatal",
    "[commands][mcp][host-servers]") {
    Recorder recorder;
    std::vector<std::string> lines;
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const auto began = std::chrono::steady_clock::now();
    const apogee::agent::ToolRegistry registry =
        apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = recorder.spawn(),
            .session_mcp_servers = {spec("slow", "slow"), spec("dying", "dying"),
                                    spec("well", "well")},
            .mcp_connect_timeout = std::chrono::milliseconds{300}});
    const auto took = std::chrono::steady_clock::now() - began;
    // Inside the bound, give or take the dying one's at-once failure.
    CHECK(took < std::chrono::seconds{3});
    CHECK(mcp->connected_count() == 1);
    CHECK(registry.find("mcp__well__echo") != nullptr);
    const std::string text = joined(lines);
    CHECK_THAT(text, ContainsSubstring("[mcp] warning: slow: connect timed out"));
    CHECK_THAT(text, ContainsSubstring("[mcp] warning: dying: connect failed (skipped)"));
    const auto said = std::ranges::count_if(lines, [](const std::string& line) {
        return line.find("fatal: could not reach upstream") != std::string::npos;
    });
    CHECK(said == 1);
}

TEST_CASE("a host's tool passes the gate as any MCP tool's: a refusal never reaches its server",
          "[commands][mcp][host-servers][permissions]") {
    Recorder recorder;
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.mcp = mcp,
                                             .mcp_spawn = recorder.spawn(),
                                             .session_mcp_servers = {spec("host", "well")}});
    REQUIRE(recorder.made.size() == 1);
    const apogee::testing::FakeMcpTransport& server = *recorder.made.front();

    std::vector<std::string> asked;
    const apogee::agent::DispatchContext refusing{
        .permission =
            [](const apogee::agent::GateRequest&) { return apogee::agent::Permission::Ask; },
        .confirm =
            [&asked](const apogee::agent::GateRequest& request) {
                asked.emplace_back(request.tool);
                return false;
            }};
    // The unannotated tool asks, and a no never reaches the server.
    const apogee::agent::ToolOutcome refused = apogee::agent::dispatch(
        registry,
        apogee::harness::ToolCall{
            .id = "c1", .name = "mcp__host__write", .arguments = R"({"path":"x"})"},
        refusing);
    CHECK(refused.is_error);
    CHECK(asked == std::vector<std::string>{"mcp__host__write"});
    CHECK_FALSE(sent_a_call(server));
    // The read-only one never asks, and runs.
    const apogee::agent::ToolOutcome echoed = apogee::agent::dispatch(
        registry,
        apogee::harness::ToolCall{
            .id = "c2", .name = "mcp__host__echo", .arguments = R"({"text":"hi"})"},
        refusing);
    CHECK_FALSE(echoed.is_error);
    CHECK(asked.size() == 1);
    CHECK(sent_a_call(server));
}

TEST_CASE("a config entry and a host server of one name: one connection, the host's, no throw",
          "[commands][mcp][host-servers]") {
    Recorder recorder;
    std::vector<std::string> lines;
    apogee::harness::Config config;
    apogee::harness::McpServerConfig self;
    self.command = "well";
    self.env = {"FROM=config"};
    config.mcp_servers.emplace("self", self);
    const auto mcp = std::make_shared<apogee::mcp::Registry>();
    apogee::agent::ToolRegistry registry;
    REQUIRE_NOTHROW(
        registry = apogee::commands::make_built_in_tools(apogee::commands::BuiltInToolOptions{
            .config = &config,
            .mcp = mcp,
            .mcp_status = [&lines](std::string_view line) { lines.emplace_back(line); },
            .mcp_spawn = recorder.spawn(),
            .session_mcp_servers = {spec("self", "well", {}, {"FROM=host"})}}));
    REQUIRE(recorder.handed.size() == 1);
    CHECK(recorder.handed[0].env == std::vector<std::string>{"FROM=host"});
    CHECK(mcp->connected_count() == 1);
    CHECK(registry.find("mcp__self__echo") != nullptr);
    CHECK_THAT(joined(lines), ContainsSubstring("self: the config's server is set aside for this "
                                                "session -- the host's 'self' takes its place"));
}
