#include "scaffold/mcp_server.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "harness/config.h"
#include "harness/layout.h"
#include "support/env_guard.h"

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
    CHECK(tree.bytes().starts_with(before));

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
