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

using apogee::commands::BootstrapHost;
using apogee::commands::BootstrapResult;
using apogee::commands::BootstrapSeams;
using apogee::commands::BootstrapStep;
using Catch::Matchers::ContainsSubstring;
using Words = std::vector<std::string>;

/// A scripted runner: every command line it was handed -- the binary's and
/// another program's alike -- and what each says and how it ends.
struct Script {
    std::vector<Words> ran;
    std::vector<Words> programs;
    std::vector<std::string> said;
    /// A step's exit code and its lines, by its command line.
    std::map<std::string, std::pair<int, std::vector<std::string>>> answers;

    [[nodiscard]] int answer(const Words& words, const std::function<void(std::string)>& line) {
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
    }

    [[nodiscard]] BootstrapSeams seams(const BootstrapHost& host) {
        BootstrapSeams seams;
        seams.host = host;
        seams.run = [this](const Words& words, const std::function<void(std::string)>& line) {
            ran.push_back(words);
            return answer(words, line);
        };
        seams.run_program = [this](const Words& argv,
                                   const std::function<void(std::string)>& line) {
            programs.push_back(argv);
            return answer(argv, line);
        };
        seams.say = [this](const std::string& line) { said.push_back(line); };
        return seams;
    }

    [[nodiscard]] BootstrapSeams seams(bool apple_silicon, bool nvidia_smi) {
        return seams(BootstrapHost{.apple_silicon = apple_silicon, .nvidia_smi = nvidia_smi});
    }
};

/// A Mac with Homebrew whose ffmpeg cannot burn captions: the user's.
const BootstrapHost kUsersMac{
    .apple_silicon = true, .macos = true, .brew = true, .ffmpeg_subtitles = false};

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("the bootstrap's plan is train setup's own table, the host deciding each set",
          "[cli][bootstrap]") {
    const std::vector<BootstrapStep> apple =
        apogee::commands::bootstrap_plan(BootstrapHost{.apple_silicon = true});
    // Every set of the table, in its order, then the code-execution server,
    // the two registrations and ffmpeg with captions.
    const auto names = apogee::training::requirement_set_names();
    REQUIRE(apple.size() == names.size() + 4);
    for (std::size_t index = 0; index < names.size(); ++index) {
        CHECK(apple[index].name == "the '" + std::string{names[index]} + "' set");
    }
    CHECK(apple[names.size()].words == Words{"mcp", "create", "code", "--from", "code-exec"});
    CHECK(apple[names.size() + 1].words == Words{"providers", "scan", "--register"});
    CHECK(apple[names.size() + 2].words == Words{"config", "scan", "--register"});
    CHECK(apple.back().name == "ffmpeg with captions");
    const auto step = [](const std::vector<BootstrapStep>& plan, const std::string& set) {
        return *std::ranges::find(plan, "the '" + set + "' set", &BootstrapStep::name);
    };
    // Apple silicon: its stacks, never the CUDA trainer's.
    CHECK(step(apple, "mlx").words == Words{"train", "setup", "--with", "mlx"});
    CHECK(step(apple, "mlx-vlm").words == Words{"train", "setup", "--with", "mlx-vlm"});
    CHECK(step(apple, "video").words == Words{"train", "setup", "--with", "video"});
    CHECK(step(apple, "voice").words == Words{"train", "setup", "--with", "voice"});
    // 53h: the embedding route's set, Apple silicon's too.
    CHECK(step(apple, "mlx-embeddings").words ==
          Words{"train", "setup", "--with", "mlx-embeddings"});
    CHECK(step(apple, "peft").words.empty());
    CHECK(step(apple, "peft").status == "skipped");
    CHECK(step(apple, "peft").reason == "for an NVIDIA GPU, and nvidia-smi is not on PATH here");
    // An NVIDIA box: the other way round.
    const std::vector<BootstrapStep> cuda =
        apogee::commands::bootstrap_plan(BootstrapHost{.nvidia_smi = true});
    CHECK(step(cuda, "peft").words == Words{"train", "setup", "--with", "peft"});
    CHECK(step(cuda, "mlx").reason == "for Apple silicon, which this host is not");
    CHECK(step(cuda, "convert").words == Words{"train", "setup", "--with", "convert"});
    CHECK(step(cuda, "mlx-embeddings").reason == "for Apple silicon, which this host is not");
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
                                           {"train", "setup", "--with", "mlx-embeddings"},
                                           {"mcp", "create", "code", "--from", "code-exec"},
                                           {"providers", "scan", "--register"},
                                           {"config", "scan", "--register"}});
    CHECK(script.programs.empty());  // not macOS: no other program runs
    const auto said = [&script](const std::string& line) {
        return std::ranges::find(script.said, line) != script.said.end();
    };
    CHECK(script.said.front() ==
          "bootstrap: the runtimes this machine can run, then its "
          "providers and its model store, registered");
    CHECK(said("[1/12] the 'prepare' set -- apogee train setup --with prepare"));
    CHECK(
        said("[3/12] the 'peft' set -- skipped: for an NVIDIA GPU, and nvidia-smi is not on "
             "PATH here"));
    CHECK(said("  environment already exists at /home/.apogee/training/venv"));
    CHECK(
        said("  skipped -- exit 1: ERROR: Could not find a version that satisfies the "
             "requirement torch>=2.6"));
    CHECK(said("[9/12] the code-execution server -- apogee mcp create code --from code-exec"));
    CHECK(said("[10/12] the providers -- apogee providers scan --register"));
    CHECK(said("  claude  credentials found  backend: claude"));
    CHECK(
        said("[12/12] ffmpeg with captions -- skipped: Homebrew's ffmpeg-full is macOS's -- "
             "'apogee check' names this host's way"));
    // The voice set still ran after video's failed; the last line is the door.
    CHECK(script.said.back() ==
          "done: 9 of 12 steps, 3 skipped -- 'apogee bootstrap' runs it again any time");
    const auto& video = *std::ranges::find(result.steps, "the 'video' set", &BootstrapStep::name);
    CHECK(video.status == "skipped");
    const auto& store = *std::ranges::find(result.steps, "the model store", &BootstrapStep::name);
    CHECK(store.status == "done");

    // The JSON summary: every step, its command, how it ended.
    const nlohmann::json document = apogee::commands::bootstrap_document(result);
    CHECK(document["object"] == "apogee.bootstrap");
    CHECK(document["minimal"] == false);
    REQUIRE(document["steps"].size() == 12);
    CHECK(document["steps"][2] == nlohmann::json{{"name", "the 'peft' set"},
                                                 {"command", nullptr},
                                                 {"status", "skipped"},
                                                 {"reason",
                                                  "for an NVIDIA GPU, and nvidia-smi is "
                                                  "not on PATH here"}});
    CHECK(document["steps"][5]["command"] == "apogee train setup --with video");
    CHECK(document["steps"][10] == nlohmann::json{{"name", "the model store"},
                                                  {"command", "apogee config scan --register"},
                                                  {"status", "done"},
                                                  {"reason", nullptr}});
    CHECK(document["steps"][11]["command"] == nullptr);
}

TEST_CASE("a step that cannot start is skipped, saying why", "[cli][bootstrap]") {
    Script script;
    script.answers["config scan --register"] = {
        -1, {"could not start apogee: no child processes here"}};
    const BootstrapResult result = apogee::commands::run_bootstrap(script.seams(false, false));
    const auto& store = *std::ranges::find(result.steps, "the model store", &BootstrapStep::name);
    CHECK(store.status == "skipped");
    CHECK(store.reason == "could not start apogee: no child processes here");
    // Neither Apple silicon nor NVIDIA: the any-host sets, the code-execution
    // server and the registrations.
    CHECK(script.ran.size() == 7);
}

TEST_CASE("the code-execution server is registered once, never over another server's name",
          "[cli][bootstrap]") {
    const auto code_step = [](const BootstrapHost& host) {
        const std::vector<BootstrapStep> plan = apogee::commands::bootstrap_plan(host);
        return *std::ranges::find(plan, "the code-execution server", &BootstrapStep::name);
    };
    CHECK(code_step({}).words == Words{"mcp", "create", "code", "--from", "code-exec"});
    // A second bootstrap is quiet about it.
    const BootstrapStep again = code_step({.code_exec_registered_as = "code"});
    CHECK(again.words.empty());
    CHECK(again.status == "skipped");
    CHECK(again.reason == "registered already, as 'code'");
    CHECK(code_step({.code_exec_registered_as = "python"}).reason ==
          "registered already, as 'python'");
    // The name held by something else: never written over, the way named.
    const BootstrapStep taken =
        code_step({.code_exec_registered_as = "code", .code_exec_name_taken = true});
    CHECK(taken.words.empty());
    CHECK(taken.reason ==
          "an MCP server named 'code' runs something else -- 'apogee mcp create <name> --from "
          "code-exec' registers it under another");
}

TEST_CASE("ffmpeg with captions: check's remedy run on a Mac with Homebrew, in its order",
          "[cli][bootstrap][ffmpeg]") {
    // The one declaration: check types it, the bootstrap runs it.
    CHECK(apogee::commands::remedy_line(apogee::commands::macos_captions_remedy()) ==
          "brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full");
    const auto ffmpeg_step = [](const BootstrapHost& host) {
        return apogee::commands::bootstrap_plan(host).back();
    };
    // Where it is not the remedy, or not needed: skipped, saying why.
    CHECK(ffmpeg_step({.apple_silicon = true}).reason ==
          "Homebrew's ffmpeg-full is macOS's -- 'apogee check' names this host's way");
    CHECK(ffmpeg_step({.macos = true, .ffmpeg_subtitles = false}).reason ==
          "Homebrew is not on PATH -- 'apogee check' names the commands");
    CHECK(ffmpeg_step({.macos = true, .brew = true}).reason ==
          "no ffmpeg answered on PATH -- 'apogee check' names the one to install");
    CHECK(ffmpeg_step({.macos = true, .brew = true, .ffmpeg_subtitles = true}).reason ==
          "this ffmpeg burns captions already (its subtitles filter is there)");

    // The user's Mac: every command, in order, through the program seam.
    {
        Script script;
        script.answers["brew unlink ffmpeg"] = {1, {"Error: No such keg: ffmpeg"}};
        const BootstrapResult result = apogee::commands::run_bootstrap(script.seams(kUsersMac));
        CHECK(script.programs == std::vector<Words>{{"brew", "install", "ffmpeg-full"},
                                                    {"brew", "unlink", "ffmpeg"},
                                                    {"brew", "link", "--force", "ffmpeg-full"}});
        // The unlink may fail -- no Homebrew ffmpeg to unlink -- and the
        // link still runs; the step is done.
        CHECK(result.steps.back().status == "done");
        CHECK(std::ranges::find(script.said,
                                "[12/12] ffmpeg with captions -- brew install "
                                "ffmpeg-full && brew unlink ffmpeg; brew link "
                                "--force ffmpeg-full") != script.said.end());
        CHECK(apogee::commands::bootstrap_document(result)["steps"][11]["command"] ==
              "brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full");
    }
    // An install that fails ends the step there, never fatal, saying which.
    {
        Script script;
        script.answers["brew install ffmpeg-full"] = {1, {"Error: no network"}};
        const BootstrapResult result = apogee::commands::run_bootstrap(script.seams(kUsersMac));
        CHECK(script.programs == std::vector<Words>{{"brew", "install", "ffmpeg-full"}});
        CHECK(result.steps.back().status == "skipped");
        CHECK(result.steps.back().reason == "brew install ffmpeg-full: exit 1: Error: no network");
        CHECK(script.said.back().starts_with("done: "));
    }
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
