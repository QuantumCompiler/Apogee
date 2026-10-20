#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "contracts/assets.h"
#include "operations/mcp_presets.h"
#include "support/cli_home.h"
#include "training/python_env.h"

/// `mcp presets` and `mcp create --from` (47c) through the real command
/// tree: the listing and its document, the composition pin -- `--from
/// code-exec` writes the config byte for byte as the hand-typed
/// registration -- the refusals with the file untouched, the empty state,
/// and the preset names completed.
namespace {

using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kConfig = "backends:\n  mock:\n    type: mock\n";

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The server seeded and a training environment's interpreter faked, so the
/// preset resolves the same on every host.
struct Ready {
    apogee::testing::CliHome home{std::string{kConfig}};
    std::filesystem::path script =
        home.home() / apogee::harness::bundled_code_exec_server_relative_path();
    std::filesystem::path python =
        apogee::training::PythonEnv{home.home() / "training" / "venv"}.interpreter();

    Ready() {
        std::filesystem::create_directories(script.parent_path());
        std::ofstream{script, std::ios::binary} << apogee::harness::bundled_mcp_scripts()[0].text;
        std::filesystem::create_directories(python.parent_path());
        std::ofstream{python, std::ios::binary} << "#!fake";
    }
};

}  // namespace

TEST_CASE("mcp presets lists what the box ships, with its document", "[cli][mcp][presets]") {
    const Ready ready;
    std::string out;
    std::string err;
    REQUIRE(ready.home.run({"mcp", "presets"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("code-exec  Runs Python for any model"));
    CHECK_THAT(out, ContainsSubstring("register: apogee mcp create code --from code-exec"));
    CHECK_THAT(
        out, ContainsSubstring("runs:     " + ready.python.string() + " " + ready.script.string()));
    CHECK_THAT(out, ContainsSubstring("self       Apogee's own read-only tools"));
    CHECK_THAT(out, ContainsSubstring("for another MCP host: \"apogee\": "));
    CHECK_THAT(out, ContainsSubstring("not registered into Apogee"));

    REQUIRE(ready.home.run({"mcp", "presets", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    REQUIRE(document["data"].size() == 2);
    CHECK(document["data"][0]["command"] == ready.python.string());
    CHECK(document["data"][0]["args"] == nlohmann::json::array({ready.script.string()}));
    CHECK(document["data"][1]["args"] == nlohmann::json::array({"__mcp-tools"}));
}

TEST_CASE("create --from writes the config byte for byte as the hand-typed registration",
          "[cli][mcp][presets]") {
    // The composition pin: one create core, so the preset and its typed
    // twin cannot differ by a byte.
    const Ready ready;
    const std::string before = slurp(ready.home.config_path());
    std::string out;
    std::string err;
    REQUIRE(ready.home.run({"mcp", "create", "code", "--from", "code-exec"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("registered MCP server 'code'"));
    CHECK_THAT(out, ContainsSubstring(R"(Test it:  apogee mcp test code run_python '{"code": )"));
    const std::string from = slurp(ready.home.config_path());
    CHECK(from != before);

    std::ofstream{ready.home.config_path(), std::ios::binary | std::ios::trunc} << before;
    REQUIRE(ready.home.run({"mcp", "create", "code", "--command", ready.python.string(), "--args",
                            ready.script.string()},
                           &out, &err) == 0);
    CHECK(slurp(ready.home.config_path()) == from);
}

TEST_CASE("create --from refuses self, an unknown name and a typed command, the file untouched",
          "[cli][mcp][presets]") {
    const Ready ready;
    const std::string before = slurp(ready.home.config_path());
    std::string out;
    std::string err;
    CHECK(ready.home.run({"mcp", "create", "me", "--from", "self"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("it would serve Apogee the tools it already has natively"));
    CHECK(ready.home.run({"mcp", "create", "x", "--from", "nope"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("no preset named 'nope' (the presets: code-exec, self"));
    CHECK(ready.home.run({"mcp", "create", "x", "--from", "code-exec", "--command", "/bin/true"},
                         &out, &err) != 0);
    CHECK(slurp(ready.home.config_path()) == before);

    // Not seeded: refused with the repair named.
    std::filesystem::remove(ready.script);
    CHECK(ready.home.run({"mcp", "create", "code", "--from", "code-exec"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("the 'code-exec' preset cannot run here"));
    CHECK_THAT(err, ContainsSubstring("apogee check --fix"));
    CHECK(slurp(ready.home.config_path()) == before);
}

TEST_CASE("mcp list's empty state names the presets, and --from completes their names",
          "[cli][mcp][presets][completion]") {
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"mcp", "list"}, &out, &err) == 0);
    CHECK(out == std::string{apogee::operations::kNoMcpServers} + "\n");
    REQUIRE(home.run({"__complete", "mcp", "create", "code", "--from", ""}, &out, &err) == 0);
    CHECK(out == "code-exec\nself\n");
    REQUIRE(home.run({"__complete", "mcp", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("presets"));
}
