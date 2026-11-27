#include "acp/host_servers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"

/// The host's MCP servers (56b): what `session/new`'s `mcpServers` becomes,
/// and the handover that carries it to the session's child -- the values
/// in a file only its owner reads, gone once read. The session's side --
/// the names, the displacement, the dial -- is `cli/host_mcp_servers_test`'s;
/// the whole, on the real binary, `cli.acp_conformance`'s.
namespace {

using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kSecret = "s3cr3t-value";

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line + "\n";
    }
    return out;
}

[[nodiscard]] std::string temp_label() {
    return "acp-host-servers-" + std::to_string(std::random_device{}());
}

}  // namespace

TEST_CASE("a host's stdio servers are taken as named; http, sse and others set aside, named once",
          "[acp][host-servers]") {
    const nlohmann::json servers = nlohmann::json::parse(R"([
        {"name": "xcode-tools", "command": "/usr/bin/xcrun", "args": ["mcpbridge"],
         "env": [{"name": "MCP_XCODE_SESSION_ID", "value": "s3cr3t-value"},
                 {"name": "HOME_HINT", "value": "${HOME}/~"}]},
        {"type": "stdio", "name": "typed", "command": "srv"},
        {"type": "http", "name": "remote", "url": "http://127.0.0.1:9/mcp",
         "headers": [{"name": "Authorization", "value": "Bearer s3cr3t-value"}]},
        {"type": "sse", "name": "events", "url": "http://127.0.0.1:9/sse", "headers": []},
        {"type": "websocket", "name": "later", "url": "ws://x"}
    ])");
    const apogee::acp::HostServers taken = apogee::acp::host_servers(servers);
    REQUIRE(taken.servers.size() == 2);
    CHECK(taken.servers[0].name == "xcode-tools");
    CHECK(taken.servers[0].command == "/usr/bin/xcrun");
    CHECK(taken.servers[0].args == std::vector<std::string>{"mcpbridge"});
    // name=value, the values as given: a host's ${VAR} is the host's.
    CHECK(taken.servers[0].env ==
          std::vector<std::string>{"MCP_XCODE_SESSION_ID=s3cr3t-value", "HOME_HINT=${HOME}/~"});
    CHECK(taken.servers[1].name == "typed");
    CHECK(taken.servers[1].args.empty());
    REQUIRE(taken.notes.size() == 1);
    CHECK(taken.notes[0] ==
          "Apogee connects stdio MCP servers only; skipped: remote, events, later");
    CHECK_THAT(joined(taken.notes), !ContainsSubstring(std::string{kSecret}));

    // What acp says on stderr: names, commands and env names -- no value,
    // no argument.
    const std::string described = apogee::acp::describe(taken);
    CHECK(described ==
          "xcode-tools (/usr/bin/xcrun; env MCP_XCODE_SESSION_ID, HOME_HINT), typed (srv)");

    // Nothing named, or not a list: nothing taken, nothing said.
    CHECK(apogee::acp::host_servers(nlohmann::json::array()).empty());
    CHECK(apogee::acp::host_servers(nlohmann::json{}).empty());
    CHECK(apogee::acp::host_servers(nlohmann::json{{"name", "x"}}).empty());
}

TEST_CASE(
    "a host server whose env, args or command cannot be taken is skipped, said without values",
    "[acp][host-servers]") {
    const nlohmann::json servers = nlohmann::json::parse(R"([
        {"name": "empty-key", "command": "srv", "env": [{"name": "", "value": "s3cr3t-value"}]},
        {"name": "equals-key", "command": "srv",
         "env": [{"name": "K=s3cr3t-value", "value": "s3cr3t-value"}]},
        {"name": "object-env", "command": "srv", "env": {"K": "s3cr3t-value"}},
        {"name": "number-value", "command": "srv", "env": [{"name": "K", "value": 7}]},
        {"name": "no-command", "args": []},
        {"name": "bad-args", "command": "srv", "args": ["ok", 3]},
        "not an object",
        {"name": "fine", "command": "srv", "env": []}
    ])");
    const apogee::acp::HostServers taken = apogee::acp::host_servers(servers);
    REQUIRE(taken.servers.size() == 1);
    CHECK(taken.servers[0].name == "fine");
    const std::string notes = joined(taken.notes);
    CHECK(taken.notes.size() == 7);
    CHECK_THAT(notes, ContainsSubstring("'empty-key' was skipped: an env variable's name is "
                                        "empty or holds '='"));
    CHECK_THAT(notes, ContainsSubstring("'equals-key' was skipped: an env variable's name is "
                                        "empty or holds '='"));
    CHECK_THAT(notes, ContainsSubstring("'object-env' was skipped: its env is not a list"));
    CHECK_THAT(notes, ContainsSubstring("'number-value' was skipped: its env is not a list"));
    CHECK_THAT(notes, ContainsSubstring("'no-command' was skipped: it names no command"));
    CHECK_THAT(notes, ContainsSubstring("'bad-args' was skipped: its args are not a list"));
    CHECK_THAT(notes, ContainsSubstring("not an object was skipped"));
    // A refused name may hold a value: neither is said.
    CHECK_THAT(notes, !ContainsSubstring(std::string{kSecret}));
}

TEST_CASE("the handover: owner-only, read once and gone, the values never anywhere else",
          "[acp][host-servers][handover]") {
    const apogee::testing::TempDir root{temp_label()};
    apogee::acp::HostServers servers;
    servers.servers.push_back(apogee::acp::HostServer{.name = "xcode-tools",
                                                      .command = "/usr/bin/xcrun",
                                                      .args = {"mcpbridge", "--flag"},
                                                      .env = {"K=" + std::string{kSecret}}});
    servers.notes.emplace_back("Apogee connects stdio MCP servers only; skipped: remote");

    std::string error;
    const std::filesystem::path file = apogee::acp::write_handover(servers, root.path(), error);
    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(file));
    CHECK(file.parent_path().parent_path() == root.path());
    CHECK(file.parent_path().filename().string().starts_with("apogee-acp-"));
#ifndef _WIN32
    using std::filesystem::perms;
    CHECK((std::filesystem::status(file).permissions() & perms::all) ==
          (perms::owner_read | perms::owner_write));
    CHECK((std::filesystem::status(file.parent_path()).permissions() & perms::all) ==
          perms::owner_all);
#endif
    // A second is a fresh folder of its own.
    const std::filesystem::path other = apogee::acp::write_handover(servers, root.path(), error);
    CHECK(other.parent_path() != file.parent_path());

    const apogee::acp::HostServers read = apogee::acp::read_handover(file, error);
    CHECK(error.empty());
    REQUIRE(read.servers.size() == 1);
    CHECK(read.servers[0].name == "xcode-tools");
    CHECK(read.servers[0].command == "/usr/bin/xcrun");
    CHECK(read.servers[0].args == std::vector<std::string>{"mcpbridge", "--flag"});
    CHECK(read.servers[0].env == std::vector<std::string>{"K=" + std::string{kSecret}});
    CHECK(read.notes == servers.notes);
    // Read once: the file and its folder are gone.
    CHECK_FALSE(std::filesystem::exists(file));
    CHECK_FALSE(std::filesystem::exists(file.parent_path()));
    (void)apogee::acp::read_handover(file, error);
    CHECK_THAT(error, ContainsSubstring("cannot read"));

    // One the child never read is discarded by the session.
    apogee::acp::discard_handover(other);
    CHECK_FALSE(std::filesystem::exists(other.parent_path()));
}

TEST_CASE("a handover acp did not make is read and left; a broken one is refused",
          "[acp][host-servers][handover]") {
    const apogee::testing::TempDir root{temp_label()};
    // A file a person named: read, never removed -- only acp's own go.
    const std::filesystem::path theirs = root.path() / "servers.json";
    {
        std::ofstream out{theirs};
        out << R"({"servers":[{"name":"a","command":"srv","args":[],"env":[]}],"notes":[]})";
    }
    std::string error;
    const apogee::acp::HostServers read = apogee::acp::read_handover(theirs, error);
    CHECK(error.empty());
    CHECK(read.servers.size() == 1);
    CHECK(std::filesystem::exists(theirs));
    apogee::acp::discard_handover(theirs);
    CHECK(std::filesystem::exists(theirs));

    // Not a handover: refused, and still removed when it is acp's.
    const std::filesystem::path folder = root.path() / "apogee-acp-0123456789abcdef";
    std::filesystem::create_directory(folder);
    {
        std::ofstream out{folder / "servers.json"};
        out << "not json";
    }
    (void)apogee::acp::read_handover(folder / "servers.json", error);
    CHECK_THAT(error, ContainsSubstring("is not a handover of MCP servers"));
    CHECK_FALSE(std::filesystem::exists(folder));
}
