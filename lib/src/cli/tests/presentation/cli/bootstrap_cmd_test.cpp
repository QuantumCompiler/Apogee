#include "cli/bootstrap_cmd.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "support/cli_home.h"
#include "support/env_guard.h"
#include "training/python_env.h"

/// `apogee bootstrap` (51), over a scripted runner -- never a real install,
/// never the network: the plan read from `train setup`'s one table, each
/// step the shipped command run once and in order, a failing step skipped
/// and said while the rest run, `--minimal` and `APOGEE_MINIMAL_INSTALL`
/// skipping it whole with the re-run door named, the JSON summary, the
/// one-declaration scan, and the real command tree under the minimal switch.
namespace {

using apogee::commands::BootstrapResult;
using apogee::commands::BootstrapSeams;
using apogee::commands::BootstrapStep;
using Catch::Matchers::ContainsSubstring;
using Words = std::vector<std::string>;

/// A scripted runner: every command line it was handed, and what each says
/// and how it ends.
struct Script {
    std::vector<Words> ran;
    std::vector<std::string> said;
    /// A step's exit code and its lines, by its command line.
    std::map<std::string, std::pair<int, std::vector<std::string>>> answers;

    [[nodiscard]] BootstrapSeams seams(bool apple_silicon, bool nvidia_smi) {
        BootstrapSeams seams;
        seams.apple_silicon = apple_silicon;
        seams.nvidia_smi = nvidia_smi;
        seams.run = [this](const Words& words, const std::function<void(std::string)>& line) {
            ran.push_back(words);
            std::string typed;
            for (const std::string& word : words) {
                typed += (typed.empty() ? "" : " ") + word;
            }
            const auto found = answers.find(typed);
            if (found == answers.end()) {
                line("ok");
                return 0;
            }
            for (const std::string& text : found->second.second) {
                line(text);
            }
            return found->second.first;
        };
        seams.say = [this](const std::string& line) { said.push_back(line); };
        return seams;
    }
};

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("the bootstrap's plan is train setup's own table, the host deciding each set",
          "[cli][bootstrap]") {
    const std::vector<BootstrapStep> apple = apogee::commands::bootstrap_plan(true, false);
    // Every set of the table, in its order, then the two registrations.
    const auto names = apogee::training::requirement_set_names();
    REQUIRE(apple.size() == names.size() + 2);
    for (std::size_t index = 0; index < names.size(); ++index) {
        CHECK(apple[index].name == "the '" + std::string{names[index]} + "' set");
    }
    CHECK(apple[names.size()].words == Words{"providers", "scan", "--register"});
    CHECK(apple.back().words == Words{"config", "scan", "--register"});
    const auto step = [](const std::vector<BootstrapStep>& plan, const std::string& set) {
        return *std::ranges::find(plan, "the '" + set + "' set", &BootstrapStep::name);
    };
    // Apple silicon: its stacks, never the CUDA trainer's.
    CHECK(step(apple, "mlx").words == Words{"train", "setup", "--with", "mlx"});
    CHECK(step(apple, "mlx-vlm").words == Words{"train", "setup", "--with", "mlx-vlm"});
    CHECK(step(apple, "video").words == Words{"train", "setup", "--with", "video"});
    CHECK(step(apple, "voice").words == Words{"train", "setup", "--with", "voice"});
    CHECK(step(apple, "peft").words.empty());
    CHECK(step(apple, "peft").status == "skipped");
    CHECK(step(apple, "peft").reason == "for an NVIDIA GPU, and nvidia-smi is not on PATH here");
    // An NVIDIA box: the other way round.
    const std::vector<BootstrapStep> cuda = apogee::commands::bootstrap_plan(false, true);
    CHECK(step(cuda, "peft").words == Words{"train", "setup", "--with", "peft"});
    CHECK(step(cuda, "mlx").reason == "for Apple silicon, which this host is not");
    CHECK(step(cuda, "convert").words == Words{"train", "setup", "--with", "convert"});
}

TEST_CASE("each step runs once, in order, said as it goes; one that fails is skipped",
          "[cli][bootstrap]") {
    Script script;
    script.answers["train setup --with video"] = {
        1,
        {"environment already exists at /home/.apogee/training/venv",
         "installing the 'video' set: torch>=2.6 ...",
         "ERROR: Could not find a version that satisfies the requirement "
         "torch>=2.6"}};
    script.answers["providers scan --register"] = {0,
                                                   {"claude  credentials found  backend: claude"}};
    const BootstrapResult result = apogee::commands::run_bootstrap(script.seams(true, false));
    CHECK_FALSE(result.minimal);
    // Every runnable step once, in the table's order, the registrations last.
    CHECK(script.ran == std::vector<Words>{{"train", "setup", "--with", "prepare"},
                                           {"train", "setup", "--with", "mlx"},
                                           {"train", "setup", "--with", "convert"},
                                           {"train", "setup", "--with", "mlx-vlm"},
                                           {"train", "setup", "--with", "video"},
                                           {"train", "setup", "--with", "voice"},
                                           {"providers", "scan", "--register"},
                                           {"config", "scan", "--register"}});
    const auto said = [&script](const std::string& line) {
        return std::ranges::find(script.said, line) != script.said.end();
    };
    CHECK(script.said.front() ==
          "bootstrap: the runtimes this machine can run, then its "
          "providers and its model store, registered");
    CHECK(said("[1/9] the 'prepare' set -- apogee train setup --with prepare"));
    CHECK(
        said("[3/9] the 'peft' set -- skipped: for an NVIDIA GPU, and nvidia-smi is not on "
             "PATH here"));
    CHECK(said("  environment already exists at /home/.apogee/training/venv"));
    CHECK(
        said("  skipped -- exit 1: ERROR: Could not find a version that satisfies the "
             "requirement torch>=2.6"));
    CHECK(said("[8/9] the providers -- apogee providers scan --register"));
    CHECK(said("  claude  credentials found  backend: claude"));
    // The voice set still ran after video's failed; the last line is the door.
    CHECK(script.said.back() ==
          "done: 7 of 9 steps, 2 skipped -- 'apogee bootstrap' runs it again any time");
    const auto& video = *std::ranges::find(result.steps, "the 'video' set", &BootstrapStep::name);
    CHECK(video.status == "skipped");
    CHECK(result.steps.back().status == "done");

    // The JSON summary: every step, its command, how it ended.
    const nlohmann::json document = apogee::commands::bootstrap_document(result);
    CHECK(document["object"] == "apogee.bootstrap");
    CHECK(document["minimal"] == false);
    REQUIRE(document["steps"].size() == 9);
    CHECK(document["steps"][2] == nlohmann::json{{"name", "the 'peft' set"},
                                                 {"command", nullptr},
                                                 {"status", "skipped"},
                                                 {"reason",
                                                  "for an NVIDIA GPU, and nvidia-smi is "
                                                  "not on PATH here"}});
    CHECK(document["steps"][5]["command"] == "apogee train setup --with video");
    CHECK(document["steps"][8] == nlohmann::json{{"name", "the model store"},
                                                 {"command", "apogee config scan --register"},
                                                 {"status", "done"},
                                                 {"reason", nullptr}});
}

TEST_CASE("a step that cannot start is skipped, saying why", "[cli][bootstrap]") {
    Script script;
    script.answers["config scan --register"] = {
        -1, {"could not start apogee: no child processes here"}};
    const BootstrapResult result = apogee::commands::run_bootstrap(script.seams(false, false));
    CHECK(result.steps.back().status == "skipped");
    CHECK(result.steps.back().reason == "could not start apogee: no child processes here");
    // Neither Apple silicon nor NVIDIA: the any-host sets and the registrations.
    CHECK(script.ran.size() == 6);
}

TEST_CASE("a minimal install skips the bootstrap whole, naming the door",
          "[cli][bootstrap][minimal]") {
    Script script;
    const BootstrapResult result =
        apogee::commands::run_bootstrap(script.seams(true, false), "APOGEE_MINIMAL_INSTALL");
    CHECK(script.ran.empty());
    CHECK(result.minimal);
    CHECK(script.said == std::vector<std::string>{
                             "bootstrap skipped: a minimal install (APOGEE_MINIMAL_INSTALL) -- "
                             "'apogee bootstrap' installs the runtimes and registers what this "
                             "machine has, any time"});
    CHECK(apogee::commands::bootstrap_document(result) ==
          nlohmann::json{{"object", "apogee.bootstrap"},
                         {"minimal", true},
                         {"minimal_by", "APOGEE_MINIMAL_INSTALL"},
                         {"steps", nlohmann::json::array()}});

    using apogee::commands::minimal_install_env;
    {
        const apogee::testing::EnvUnsetGuard unset{"APOGEE_MINIMAL_INSTALL"};
        CHECK_FALSE(minimal_install_env());
    }
    for (const auto& [value, minimal] : std::vector<std::pair<std::string, bool>>{
             {"1", true}, {"yes", true}, {"0", false}, {"false", false}, {"", false}}) {
        const apogee::testing::EnvGuard set{"APOGEE_MINIMAL_INSTALL", value};
        INFO(value);
        CHECK(minimal_install_env() == minimal);
    }
}

TEST_CASE("the set list is declared once: no other source names the sets in a list",
          "[cli][bootstrap][declaration]") {
    // A second list of the sets -- three or more of their names as literals
    // on one line -- anywhere but the table is the drift this guards.
    const std::filesystem::path source{APOGEE_SOURCE_DIR};
    std::vector<std::string> names;
    for (const std::string_view name : apogee::training::requirement_set_names()) {
        names.push_back("\"" + std::string{name} + "\"");
    }
    std::vector<std::string> found;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        const std::filesystem::path& path = entry.path();
        if ((path.extension() != ".cpp" && path.extension() != ".h") ||
            path.filename() == "python_env.cpp") {
            continue;
        }
        std::istringstream lines{slurp(path)};
        for (std::string line; std::getline(lines, line);) {
            std::set<std::string> hits;
            for (const std::string& name : names) {
                if (line.find(name) != std::string::npos) {
                    hits.insert(name);
                }
            }
            if (hits.size() >= 3) {
                found.push_back(path.filename().string() + ": " + line);
            }
        }
    }
    CHECK(found.empty());
}

TEST_CASE("apogee bootstrap on the real tree, minimal: said, green, and touching nothing",
          "[cli][bootstrap][minimal]") {
    // Never a real bootstrap in a test: the switch is always on here.
    const apogee::testing::CliHome home{"backends:\n  m:\n    type: mock\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"bootstrap", "--minimal"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("bootstrap skipped: a minimal install (--minimal)"));
    CHECK_THAT(out, ContainsSubstring("'apogee bootstrap' installs the runtimes"));
    {
        const apogee::testing::EnvGuard minimal{"APOGEE_MINIMAL_INSTALL", "1"};
        REQUIRE(home.run({"bootstrap", "--output-format", "json"}, &out, &err) == 0);
        const nlohmann::json document = nlohmann::json::parse(out);
        CHECK(document["minimal_by"] == "APOGEE_MINIMAL_INSTALL");
        CHECK(document["steps"].empty());
    }
    CHECK_FALSE(std::filesystem::exists(home.home() / "training" / "venv"));
}
