#include "scaffold/mcp_server.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "support/env_guard.h"
#include "support/text_edits.h"

/// The scaffold core: files, the entry, register-only, and every refusal.
namespace {

using apogee::scaffold::create_mcp_server;
using apogee::scaffold::McpServerSpec;
using apogee::scaffold::sanitize_server_name;

struct Tree {
    apogee::testing::TempDir home{"scaffold-mcp-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = home.path() / "config" / "config.yaml";

    Tree() {
        std::filesystem::create_directories(config.parent_path());
        std::ofstream{config} << apogee::harness::config_template();
    }

    [[nodiscard]] std::string bytes() const {
        std::ifstream in{config, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
};

/// Every file under the home but the config itself, sorted (CLI-M52).
std::vector<std::filesystem::path> files_besides_config(const Tree& tree) {
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator{tree.home.path(), code}) {
        if (entry.path() != tree.config && !entry.is_directory()) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

/// The words `create_mcp_server` refuses `spec` with; fails the case when
/// it creates instead.
std::string refusal(const Tree& tree, const McpServerSpec& spec) {
    try {
        (void)create_mcp_server(tree.config, spec);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    FAIL("'" + spec.name + "' was created, not refused");
    return {};
}

}  // namespace

TEST_CASE("create writes a runnable Python server and registers it beside the config",
          "[scaffold][mcp]") {
    const Tree tree;
    const std::string before = tree.bytes();
    McpServerSpec spec;
    spec.name = "weather.v2";
    const apogee::scaffold::McpServerResult result = create_mcp_server(tree.config, spec);
    CHECK(result.name == "weather-v2");
    CHECK(result.directory == tree.home.path() / "mcp" / "weather-v2");
    REQUIRE(std::filesystem::exists(result.directory / "server.py"));
    CHECK(std::filesystem::exists(result.directory / "test_server.py"));
    CHECK(std::filesystem::exists(result.directory / "README.md"));
    if (apogee::harness::supports_private_modes()) {
        const std::filesystem::perms mode =
            std::filesystem::status(result.directory / "server.py").permissions();
        CHECK((mode & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
    }
    // The server template is complete: it names itself and the client's version.
    const std::string server = apogee::scaffold::python_server_template("weather-v2");
    CHECK(server.find("weather-v2") != std::string::npos);
    CHECK(server.find("__NAME__") == std::string::npos);
    CHECK(server.find("2025-03-26") != std::string::npos);
    CHECK(server.find("readOnlyHint") != std::string::npos);

    // The config: the shipped template plus exactly one entry.
    const apogee::harness::Config config = apogee::harness::load_config(tree.config);
    const apogee::harness::McpServerConfig* entry = config.find_mcp_server("weather-v2");
    REQUIRE(entry != nullptr);
    CHECK(entry->command == (result.directory / "server.py").string());
    CHECK(entry->enabled);
    // Everything the template said, kept in place around the one entry.
    CHECK(apogee::testing::inserted(before, tree.bytes()).has_value());

    // A second create is refused without force, and replaces with it.
    CHECK_THROWS_AS(create_mcp_server(tree.config, spec), std::runtime_error);
    spec.force = true;
    CHECK_NOTHROW(create_mcp_server(tree.config, spec));
}

TEST_CASE("register-only writes no files and keeps the arguments", "[scaffold][mcp]") {
    const Tree tree;
    McpServerSpec spec;
    spec.name = "ext";
    spec.command = "/usr/local/bin/some-server";
    spec.args = {"--port", "0"};
    const apogee::scaffold::McpServerResult result = create_mcp_server(tree.config, spec);
    CHECK(result.directory.empty());
    CHECK_FALSE(std::filesystem::exists(tree.home.path() / "mcp"));
    const apogee::harness::Config config = apogee::harness::load_config(tree.config);
    const apogee::harness::McpServerConfig* entry = config.find_mcp_server("ext");
    REQUIRE(entry != nullptr);
    CHECK(entry->command == "/usr/local/bin/some-server");
    CHECK(entry->args == std::vector<std::string>{"--port", "0"});
}

TEST_CASE("names are sanitised or refused", "[scaffold][mcp]") {
    CHECK(sanitize_server_name("a.b:c") == "a-b-c");
    CHECK(sanitize_server_name("Ok_name-1") == "Ok_name-1");
    CHECK_THROWS_AS(sanitize_server_name(""), std::runtime_error);
    CHECK_THROWS_AS(sanitize_server_name("has space"), std::runtime_error);
    CHECK_THROWS_AS(sanitize_server_name("../up"), std::runtime_error);
    CHECK_THROWS_AS(sanitize_server_name("a__b"), std::runtime_error);  // the namespace delimiter
}

TEST_CASE("mcp/ is declared once, in the layout, so every install path seeds it",
          "[scaffold][mcp][layout]") {
    bool declared = false;
    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        declared = declared || entry.relative_path == "mcp";
    }
    CHECK(declared);
    CHECK(apogee::harness::mcp_servers_dir().filename() == "mcp");
}

TEST_CASE("a name the config cannot hold makes no scaffold folder, run after run",
          "[scaffold][mcp][M52]") {
    // CLI-M52: `mcp create -- -m` wrote mcp/-m/ and its three files, then
    // the config refused the name; a rerun said the directory existed, and
    // `--force` wrote it again. The add runs on the config's text first.
    const Tree tree;
    const std::string bytes = tree.bytes();
    for (const auto& [typed, said] : std::vector<std::pair<std::string, std::string>>{
             {"-m", "MCP server name '-m' is not one"},
             {".z", "MCP server name '.z' (as '-z') is not one"},
             {"null", "MCP server name 'null' is not one"}}) {
        for (const bool force : {false, false, true}) {
            INFO(typed << (force ? " --force" : ""));
            McpServerSpec spec;
            spec.name = typed;
            spec.force = force;
            const std::string words = refusal(tree, spec);
            CHECK(words.starts_with(said));
            CHECK(words.find("already exists") == std::string::npos);
            CHECK(files_besides_config(tree).empty());
            CHECK_FALSE(std::filesystem::exists(tree.home.path() / "mcp"));
            CHECK(tree.bytes() == bytes);
        }
    }
}

TEST_CASE("a missing or broken config, a collision and the cap make no scaffold folder",
          "[scaffold][mcp][M52]") {
    const Tree tree;
    McpServerSpec spec;
    spec.name = "weather";
    (void)create_mcp_server(tree.config, spec);
    std::filesystem::remove_all(tree.home.path() / "mcp");
    std::filesystem::remove(tree.config.string() + ".lock");
    const std::string bytes = tree.bytes();

    // The entry is there and its folder is gone: refused as existing.
    CHECK(refusal(tree, spec).find("MCP server 'weather' already exists; pass --force") !=
          std::string::npos);
    spec.name = "WEATHER";
    CHECK(refusal(tree, spec).find("collides with existing 'weather'") != std::string::npos);
    spec.name = std::string(201, 'm');
    CHECK(refusal(tree, spec).find("is 201 bytes -- a new name is at most 200") !=
          std::string::npos);
    CHECK(files_besides_config(tree).empty());
    CHECK(tree.bytes() == bytes);
    spec.name = std::string(200, 'm');
    CHECK(create_mcp_server(tree.config, spec).name == spec.name);
    std::filesystem::remove_all(tree.home.path() / "mcp");
    std::filesystem::remove(tree.config.string() + ".lock");
    std::ofstream{tree.config, std::ios::binary} << bytes;

    spec.name = "plain";
    std::ofstream{tree.config, std::ios::binary} << "backends: [not, a, mapping]\n";
    CHECK(refusal(tree, spec).find("written") == std::string::npos);
    std::filesystem::remove(tree.config);
    CHECK(refusal(tree, spec).find("config init") != std::string::npos);
    CHECK(files_besides_config(tree).empty());
}

TEST_CASE("a lock held past the wait keeps the scaffold, and the words name every file",
          "[scaffold][mcp][M52]") {
    const Tree tree;
    const std::string bytes = tree.bytes();
    const apogee::platform::FileLock held = apogee::harness::lock_config_file(tree.config);
    McpServerSpec spec;
    spec.name = "late";
    const std::filesystem::path directory = tree.home.path() / "mcp" / "late";
    try {
        (void)create_mcp_server(tree.config, spec, std::chrono::milliseconds{150});
        FAIL("the edit went through a held lock");
    } catch (const std::runtime_error& e) {
        const std::string words = e.what();
        CHECK(words.starts_with("updating config: another edit of "));
        CHECK(words.find("(the scaffold was written to " + directory.string() +
                         ": server.py, test_server.py and README.md; run it again with --force "
                         "to replace it)") != std::string::npos);
    }
    CHECK(std::filesystem::exists(directory / "server.py"));
    CHECK(std::filesystem::exists(directory / "README.md"));
    CHECK(tree.bytes() == bytes);
}

TEST_CASE("register-only takes no dry run: its refusals keep their words", "[scaffold][mcp][M52]") {
    // `mcp create --command` and `--from` write no files, so they keep
    // today's words byte for byte -- the Mac app's Xcode setting holds them.
    const Tree tree;
    McpServerSpec spec;
    spec.name = "xcode";
    spec.command = "/usr/bin/xcrun";
    (void)create_mcp_server(tree.config, spec);
    CHECK(refusal(tree, spec) ==
          "updating config: MCP server 'xcode' already exists; pass --force to replace it");
    CHECK_FALSE(std::filesystem::exists(tree.home.path() / "mcp"));
}

TEST_CASE("a hand-written entry past 200 bytes is not new: force replaces it",
          "[scaffold][mcp][M52]") {
    // The cap is for new names only (ADR 0006): an existing entry keeps
    // working, and `--force` rewrites its scaffold.
    const Tree tree;
    const std::string name(210, 'h');
    apogee::harness::McpServerConfig hand;
    hand.command = "/opt/srv";
    std::ofstream{tree.config, std::ios::binary}
        << apogee::harness::append_mcp_server(tree.bytes(), name, hand, false);
    REQUIRE(apogee::harness::load_config(tree.config).find_mcp_server(name) != nullptr);
    McpServerSpec spec;
    spec.name = name;
    spec.force = true;
    CHECK(create_mcp_server(tree.config, spec).name == name);
    CHECK(std::filesystem::exists(tree.home.path() / "mcp" / name / "server.py"));
}
