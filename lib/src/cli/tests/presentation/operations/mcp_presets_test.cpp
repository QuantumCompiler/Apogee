#include "operations/mcp_presets.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "contracts/assets.h"
#include "support/env_guard.h"
#include "training/python_env.h"

/// The presets (47c) as data: the table, each preset resolved on a faked
/// machine -- seeded or not, an environment's interpreter, PATH's, none --
/// the refusals `--from` speaks, and the document both surfaces read.
namespace {

using apogee::operations::McpPresetHost;
using apogee::operations::McpPresetResolution;
using Catch::Matchers::ContainsSubstring;

/// A data directory, PATH faked: `python3` is found or not as the test says.
struct Machine {
    apogee::testing::TempDir dir{"mcp-presets-" + std::to_string(std::random_device{}())};
    std::string on_path;

    [[nodiscard]] McpPresetHost host() const {
        McpPresetHost host;
        host.home = dir.path();
        host.executable = "/opt/apogee/bin/apogee";
        host.find_on_path = [this](std::string_view) { return on_path; };
        return host;
    }

    [[nodiscard]] std::filesystem::path script() const {
        return dir.path() / apogee::harness::bundled_code_exec_server_relative_path();
    }

    void seed() const {
        std::filesystem::create_directories(script().parent_path());
        std::ofstream{script(), std::ios::binary} << apogee::harness::bundled_mcp_scripts()[0].text;
    }

    [[nodiscard]] std::filesystem::path venv() const {
        return apogee::training::PythonEnv{dir.path() / "training" / "venv"}.interpreter();
    }

    void make_venv() const {
        std::filesystem::create_directories(venv().parent_path());
        std::ofstream{venv(), std::ios::binary} << "#!fake";
    }
};

[[nodiscard]] const apogee::operations::McpPreset& preset(std::string_view name) {
    const apogee::operations::McpPreset* found = apogee::operations::find_mcp_preset(name);
    REQUIRE(found != nullptr);
    return *found;
}

[[nodiscard]] std::string refusal_of(std::string_view name, const McpPresetHost& host) {
    try {
        (void)apogee::operations::resolve_preset_for_create(name, host);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "resolved";
}

}  // namespace

TEST_CASE("the presets are two, code-exec registrable and self for other hosts",
          "[operations][mcp][presets]") {
    CHECK(apogee::operations::mcp_preset_names() ==
          std::vector<std::string_view>{"code-exec", "self"});
    CHECK(preset("code-exec").refusal.empty());
    CHECK(preset("code-exec").server_name == "code");
    CHECK(preset("code-exec").provides == "run_python, write_input");
    CHECK_FALSE(preset("self").refusal.empty());
    CHECK(apogee::operations::find_mcp_preset("nope") == nullptr);
    // Every preset says how to try it, as JSON `mcp test` takes.
    for (const apogee::operations::McpPreset& each : apogee::operations::mcp_presets()) {
        INFO(each.name);
        CHECK_FALSE(each.description.empty());
        CHECK(nlohmann::json::parse(each.try_arguments).is_object());
    }
}

TEST_CASE("code-exec resolves to the seeded script under the interpreter it would run under",
          "[operations][mcp][presets]") {
    Machine machine;
    // Not seeded: what is missing, and the doctor's repair.
    McpPresetResolution resolution =
        apogee::operations::resolve_mcp_preset(preset("code-exec"), machine.host());
    CHECK_FALSE(resolution.runnable());
    CHECK_THAT(resolution.missing, ContainsSubstring("code_exec_server.py is not seeded"));
    CHECK(resolution.remedy == "apogee check --fix");

    // Seeded, no environment, nothing on PATH: no Python, said, never guessed.
    machine.seed();
    resolution = apogee::operations::resolve_mcp_preset(preset("code-exec"), machine.host());
    CHECK_FALSE(resolution.runnable());
    CHECK_THAT(resolution.missing, ContainsSubstring("no Python to run it"));
    CHECK(resolution.remedy == "apogee train setup");
    CHECK_THAT(refusal_of("code-exec", machine.host()),
               ContainsSubstring("the 'code-exec' preset cannot run here: no Python to run it"));

    // python3 on PATH.
    machine.on_path = "/usr/bin/python3";
    resolution = apogee::operations::resolve_mcp_preset(preset("code-exec"), machine.host());
    CHECK(resolution.runnable());
    CHECK(resolution.command == "/usr/bin/python3");
    CHECK(resolution.args == std::vector<std::string>{machine.script().string()});

    // The training environment's interpreter wins over PATH.
    machine.make_venv();
    resolution = apogee::operations::resolve_preset_for_create("code-exec", machine.host());
    CHECK(resolution.command == machine.venv().string());
    CHECK(resolution.args == std::vector<std::string>{machine.script().string()});
}

TEST_CASE("self resolves to this binary's mcp serve and is refused into Apogee",
          "[operations][mcp][presets]") {
    Machine machine;
    const McpPresetResolution resolution =
        apogee::operations::resolve_mcp_preset(preset("self"), machine.host());
    CHECK(resolution.runnable());
    CHECK(resolution.command == "/opt/apogee/bin/apogee");
    // The public spelling (54a); the hidden `__mcp-tools` stays its alias.
    CHECK(resolution.args == std::vector<std::string>{"mcp", "serve"});
    CHECK(apogee::operations::mcp_host_entry(resolution) ==
          nlohmann::json{{"command", "/opt/apogee/bin/apogee"}, {"args", {"mcp", "serve"}}});
    CHECK_THAT(
        std::string{preset("self").provides},
        ContainsSubstring("models_list, models_status, train_status, train_versions, check"));
    // The circularity, said.
    CHECK_THAT(refusal_of("self", machine.host()),
               ContainsSubstring("registered into Apogee it would serve Apogee the tools it "
                                 "already has natively"));
    // An unknown name names the presets.
    CHECK_THAT(refusal_of("searxng", machine.host()),
               ContainsSubstring("no preset named 'searxng' (the presets: code-exec, self"));
}

TEST_CASE("the presets document says each preset's facts and where it stands here",
          "[operations][mcp][presets][documents]") {
    Machine machine;
    const nlohmann::json unseeded = apogee::operations::mcp_presets_document(machine.host());
    REQUIRE(unseeded["object"] == "list");
    REQUIRE(unseeded["data"].size() == 2);
    const nlohmann::json& code = unseeded["data"][0];
    CHECK(code["name"] == "code-exec");
    CHECK(code["registers_into_apogee"] == true);
    CHECK(code["register"] == "apogee mcp create code --from code-exec");
    CHECK(code["runnable"] == false);
    CHECK(code["remedy"] == "apogee check --fix");
    CHECK_FALSE(code.contains("refusal"));
    const nlohmann::json& self = unseeded["data"][1];
    CHECK(self["name"] == "self");
    CHECK(self["registers_into_apogee"] == false);
    CHECK_FALSE(self.contains("register"));
    CHECK(self.contains("refusal"));
    CHECK(self["host_entry"] ==
          nlohmann::json{
              {"name", "apogee"},
              {"server", {{"command", "/opt/apogee/bin/apogee"}, {"args", {"mcp", "serve"}}}}});

    machine.seed();
    machine.make_venv();
    const nlohmann::json ready = apogee::operations::mcp_presets_document(machine.host());
    CHECK(ready["data"][0]["runnable"] == true);
    CHECK_FALSE(ready["data"][0].contains("missing"));
    CHECK(ready["data"][0]["command"] == machine.venv().string());
}
