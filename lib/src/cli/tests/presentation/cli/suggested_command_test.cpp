#include "contracts/suggested_command.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cli/line_tokens.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "embedstore/store.h"
#include "platform/child_process.h"
#include "support/channel_guard.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "support/printed_command.h"

/// CLI-M48, the bug as a fixture: every command Apogee prints for a name it
/// made -- a collection `-x` or `a;b`, a symphony `-q`, a path under a home
/// whose folder holds a space and a quote -- and for a backend a person
/// wrote by hand, pasted into the shells a person uses, reaches the verb on
/// the named thing: never exit 106, 109 or 114, never help, never a second
/// command, never another target.
namespace {

namespace fs = std::filesystem;
using apogee::testing::CliHome;

/// The exits CLI11 gives a command line it cannot read: a required argument
/// missing (106), an extra one (109), a value of the wrong kind (114).
[[nodiscard]] bool refused_by_the_parser(int code) {
    return code == 106 || code == 109 || code == 114;
}

/// `text` as a YAML single-quoted scalar, so a path holding a quote, a
/// space or a backslash is read as written.
[[nodiscard]] std::string yaml_quoted(const std::string& text) {
    std::string out = "'";
    for (const char c : text) {
        out += c;
        if (c == '\'') {
            out += '\'';
        }
    }
    return out + "'";
}

[[nodiscard]] std::string after(const std::string& text, std::string_view marker) {
    const std::size_t at = text.find(marker);
    REQUIRE(at != std::string::npos);
    std::string out = text.substr(at + marker.size());
    while (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

/// The words `printf '%s\0' <command>` printed, split on its NULs.
[[nodiscard]] std::vector<std::string> nul_words(const std::string& out) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : out) {
        if (c == '\0') {
            words.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    return words;
}

/// The shells this host has, each as the argv that starts it -- zsh with
/// `-f`, so no rc file of the developer's is read.
[[nodiscard]] std::map<std::string, std::vector<std::string>> shells() {
    std::map<std::string, std::vector<std::string>> out;
    if (fs::exists("/bin/sh")) {
        out["sh"] = {"/bin/sh"};
    }
    for (const auto& [name, extra] : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"bash", {}}, {"zsh", {"-f"}}}) {
        if (const std::string found = apogee::platform::find_on_path(name); !found.empty()) {
            std::vector<std::string> argv{found};
            argv.insert(argv.end(), extra.begin(), extra.end());
            out[name] = argv;
        }
    }
    return out;
}

/// `script` run by `shell` with the environment cleared, in `folder`.
[[nodiscard]] apogee::platform::CapturedRun run_in(const std::vector<std::string>& shell,
                                                   const std::string& script,
                                                   const fs::path& folder) {
    apogee::platform::ChildCommand command;
    command.program = "/usr/bin/env";
    command.arguments = {"-i", "PATH=/usr/bin:/bin"};
    command.arguments.insert(command.arguments.end(), shell.begin(), shell.end());
    command.arguments.push_back("-c");
    command.arguments.push_back(script);
    command.working_directory = folder.string();
    return apogee::platform::run_captured(
        command, apogee::platform::CapturedLimits{.timeout = std::chrono::seconds{30}});
}

/// `command` as `shell` reads it -- the argv it would run -- through
/// `printf '%s\0' <command>`: a pasted line's words, nothing executed.
[[nodiscard]] std::vector<std::string> argv_of(const std::vector<std::string>& shell,
                                               const std::string& command, const fs::path& folder) {
    const apogee::platform::CapturedRun ran = run_in(shell, "printf '%s\\0' " + command, folder);
    INFO("read by " << shell.front() << ": " << command << " -- " << ran.error << ran.tail);
    REQUIRE(ran.status == 0);
    return nul_words(ran.out);
}

/// What a printed command does once pasted: read by every shell here --
/// each must read the same words -- and then run, its program word
/// dropped, against `home`.
struct Pasted {
    std::vector<std::string> words{};
    int code = -1;
    std::string out{};
    std::string err{};
};

[[nodiscard]] Pasted paste(const CliHome& home, const std::string& command,
                           const fs::path& folder) {
    Pasted pasted;
    for (const auto& [name, shell] : shells()) {
        INFO("pasted into " << name);
        const std::vector<std::string> words = argv_of(shell, command, folder);
        if (pasted.words.empty()) {
            pasted.words = words;
        }
        CHECK(words == pasted.words);
    }
    REQUIRE_FALSE(pasted.words.empty());
    REQUIRE(pasted.words.front() == "apogee");
    const std::vector<std::string> args(pasted.words.begin() + 1, pasted.words.end());
    pasted.code = home.run(args, &pasted.out, &pasted.err);
    INFO(pasted.out << pasted.err);
    CHECK_FALSE(refused_by_the_parser(pasted.code));
    CHECK(pasted.out.find("Usage:") == std::string::npos);
    return pasted;
}

/// The collections the CLI made before CLI-M46's create rule, or that a
/// store on disk still holds, and the command each remedy must print.
const std::vector<std::pair<std::string, std::string>> kCollections{
    {"-x", "apogee graph build -- -x"},
    {"-h", "apogee graph build -- -h"},
    {"--graph", "apogee graph build -- --graph"},
    {"--", "apogee graph build -- --"},
    {"=ls", "apogee graph build \"=ls\""},
    {"a;b", "apogee graph build \"a;b\""},
    {"my notes", "apogee graph build \"my notes\""},
    {"it's", "apogee graph build \"it's\""},
    {"$HOME", "apogee graph build '$HOME'"},
    {"x`id`", "apogee graph build 'x`id`'"},
};

/// A home under a folder whose name holds a space and a quote, with a mock
/// extractor as its default backend, hand-written backends `-gone` (its
/// model gone), `a;b` and `a`, and a plain collection `seed` copied to every
/// odd name above and to `a` -- stores on disk, as an older CLI made them.
struct OddHome {
    CliHome home{"", "config.yaml", "it's home"};
    apogee::testing::TempDir scratch{"suggested-cli-" + std::to_string(std::random_device{}())};

    OddHome() {
        const fs::path extractor = home.home() / "extractor.json";
        std::ofstream{extractor, std::ios::binary}
            << R"({"turns": [{"text": "{\"entities\": [{\"name\": \"Fox\", \"type\": )"
               R"(\"system\", \"description\": \"a fox\"}], \"relations\": []}"}]})";
        std::ofstream{home.config_path(), std::ios::binary}
            << "models:\n  default: extractor\nbackends:\n  extractor:\n    type: mock\n"
               "    model_path: " +
                   yaml_quoted(extractor.string()) +
                   "\n  '-gone':\n    type: llamacpp\n    model_path: " +
                   yaml_quoted((home.home() / "gone" / "m.gguf").string()) +
                   "\n  'a;b':\n    type: mock\n  a:\n    type: mock\n";
        const fs::path docs = home.home() / "docs";
        fs::create_directories(docs);
        std::ofstream{docs / "a.txt", std::ios::binary} << "Fox runs in the forest.\n";
        std::string out;
        std::string err;
        INFO(err);
        REQUIRE(home.run({"embed", "ingest", "-q", "--retriever", "lexical", "seed", docs.string()},
                         &out, &err) == 0);
        for (const auto& [name, command] : kCollections) {
            fs::copy_file(store("seed"), store(name));
        }
        fs::copy_file(store("seed"), store("a"));
    }

    [[nodiscard]] fs::path store(const std::string& name) const {
        return home.home() / "embeddings" / (name + ".db");
    }

    [[nodiscard]] bool built(const std::string& name) const {
        return apogee::embedstore::Store{store(name)}.graph_stats().built();
    }
};

}  // namespace

TEST_CASE("every graph remedy for a collection the CLI made runs as printed in sh, bash and zsh",
          "[commands][suggested-command][M48]") {
    if (!apogee::platform::supports_child_processes() || !fs::exists("/usr/bin/env")) {
        SKIP("no child processes or no /usr/bin/env here: no shell can read a pasted line");
    }
    if (apogee::platform::find_on_path("zsh").empty()) {
        WARN("no zsh on this host: the zsh reading of each line is skipped");
    }
    const OddHome odd;
    std::string out;
    std::string err;

    // graph stats on a collection with no graph: the hint on stdout.
    REQUIRE(odd.home.run({"graph", "stats", "--", "-h"}, &out, &err) == 0);
    CHECK(out.find("No graph built for \"-h\". Run: apogee graph build -- -h\n") !=
          std::string::npos);

    for (const auto& [name, expected] : kCollections) {
        INFO("collection [" << name << "]");
        REQUIRE(odd.home.run({"graph", "show", "--", name, "Fox"}, &out, &err) == 1);
        // The house voice: one stderr line, `apogee graph: `, as before.
        CHECK(err.starts_with("apogee graph: no graph built for '" + name + "' -- run: "));
        const std::string remedy = after(err, " -- run: ");
        CHECK(remedy == expected);
        const Pasted pasted = paste(odd.home, remedy, odd.scratch.path());
        CHECK(pasted.code == 0);
        CHECK(odd.built(name));
        if (name == "-x") {
            // A success's own hint names it the same way.
            CHECK(pasted.out.find("Run `apogee graph stats -- -x` for the graph's shape.") !=
                  std::string::npos);
        }
    }
    // `a;b` built itself and ran nothing after it: collection `a` is untouched.
    CHECK_FALSE(odd.built("a"));

    // graph communities' refusal on a graph with no relations.
    REQUIRE(odd.home.run({"graph", "communities", "--", "-x"}, &out, &err) == 1);
    CHECK(err.starts_with("apogee graph: "));
    const Pasted again = paste(odd.home, after(err, "build it first: "), odd.scratch.path());
    CHECK(again.words == std::vector<std::string>{"apogee", "graph", "build", "--", "-x"});
    CHECK(again.code == 0);
}

TEST_CASE("a collection named across a line break is one single-quoted word in its remedy",
          "[commands][suggested-command][M48]") {
    if (!apogee::platform::supports_child_processes() || !fs::exists("/usr/bin/env")) {
        SKIP("no child processes or no /usr/bin/env here: no shell can read a pasted line");
    }
    const OddHome odd;
    fs::copy_file(odd.store("seed"), odd.store("n\nl"));
    std::string out;
    std::string err;
    REQUIRE(odd.home.run({"graph", "show", "--", "n\nl", "Fox"}, &out, &err) == 1);
    const std::string remedy = after(err, " -- run: ");
    CHECK(remedy == "apogee graph build 'n\nl'");
    for (const auto& [name, shell] : shells()) {
        INFO("read by " << name);
        CHECK(argv_of(shell, remedy, odd.scratch.path()) ==
              std::vector<std::string>{"apogee", "graph", "build", "n\nl"});
    }
}

TEST_CASE("a backend door for a hand-written name reaches the verb, never another backend",
          "[commands][suggested-command][M48]") {
    if (!apogee::platform::supports_child_processes() || !fs::exists("/usr/bin/env")) {
        SKIP("no child processes or no /usr/bin/env here: no shell can read a pasted line");
    }
    const OddHome odd;
    std::string out;
    std::string err;

    // check's row for `-gone`: the options first, then `--`, in the text and
    // the JSON `remedy`, and each door, its placeholder filled, reaches the verb.
    const std::string doors =
        "apogee config set-backend --model-path <an existing .gguf> -- -gone to repoint it, or "
        "apogee config delete-backend -- -gone to remove it";
    (void)odd.home.run({"check", "--no-color"}, &out, &err);
    CHECK(out.find(doors) != std::string::npos);
    (void)odd.home.run({"check", "--output-format", "json"}, &out, &err);
    const nlohmann::json document = nlohmann::json::parse(out);
    std::string remedy;
    for (const nlohmann::json& row : document.at("rows")) {
        if (row.at("name") == "backend: -gone") {
            remedy = row.value("remedy", "");
        }
    }
    CHECK(remedy == doors);
    const fs::path gguf = odd.home.home() / "models dir" / "m.gguf";
    fs::create_directories(gguf.parent_path());
    std::ofstream{gguf, std::ios::binary} << apogee::testing::minimal_gguf("llama");
    for (std::string door : apogee::testing::remedy_doors(remedy)) {
        const std::string mark = "<an existing .gguf>";
        if (const std::size_t at = door.find(mark); at != std::string::npos) {
            door.replace(at, mark.size(), apogee::harness::shell_word(gguf.string()));
        }
        INFO("door: " << door);
        const Pasted pasted = paste(odd.home, door, odd.scratch.path());
        CHECK(pasted.words.at(pasted.words.size() - 2) == "--");
        CHECK(pasted.words.back() == "-gone");
    }

    // models delete's remedy for `a;b`: pasted, it never touches backend `a`.
    REQUIRE(odd.home.run({"models", "delete", "--yes", "--", "a;b"}, &out, &err) != 0);
    CHECK(err.starts_with("apogee models: "));
    CHECK(err.find("remove the backend itself with 'apogee config delete-backend \"a;b\"'") !=
          std::string::npos);
    const std::size_t start = err.find("with '") + 6;
    const std::string command = err.substr(start, err.rfind('\'') - start);
    const Pasted pasted = paste(odd.home, command, odd.scratch.path());
    CHECK(pasted.words == std::vector<std::string>{"apogee", "config", "delete-backend", "a;b"});
    CHECK(apogee::harness::load_config(odd.home.config_path()).find_backend("a") != nullptr);
}

TEST_CASE("a path under a home whose folder holds a space and a quote is one word",
          "[commands][suggested-command][M48]") {
    if (!apogee::platform::supports_child_processes() || !fs::exists("/usr/bin/env")) {
        SKIP("no child processes or no /usr/bin/env here: no shell can read a pasted line");
    }
    const OddHome odd;
    std::string out;
    std::string err;

    // check's Filesystem row: the chmod it names changes the mode.
    if (apogee::harness::supports_private_modes()) {
        const fs::path config = odd.home.home() / "config";
        fs::permissions(config, fs::perms::owner_all | fs::perms::group_read |
                                    fs::perms::group_exec | fs::perms::others_read |
                                    fs::perms::others_exec);
        (void)odd.home.run({"check", "--output-format", "json"}, &out, &err);
        std::string remedy;
        const nlohmann::json rows = nlohmann::json::parse(out).at("rows");
        for (const nlohmann::json& row : rows) {
            if (row.at("section") == "Filesystem" && row.value("remedy", "").starts_with("chmod")) {
                remedy = row.value("remedy", "");
            }
        }
        CHECK(remedy == "chmod 700 \"" + config.string() + "\"   (or: apogee check --fix)");
        const std::string chmod = remedy.substr(0, remedy.find("   (or:"));
        const apogee::platform::CapturedRun ran = run_in({"/bin/sh"}, chmod, odd.scratch.path());
        INFO(ran.error << ran.tail);
        CHECK(ran.status == 0);
        CHECK((fs::status(config).permissions() & fs::perms::mask) == fs::perms::owner_all);
    }

    // symphonies edit's rename door names its scratch file under the home.
    REQUIRE(odd.home.run({"symphonies", "create", "s1"}, &out, &err) == 0);
    const fs::path renamer = odd.scratch.path() / "rename.sh";
    std::ofstream{renamer, std::ios::binary}
        << "sed 's/^name: s1$/name: solo2/' \"$1\" > \"$1.new\" && mv \"$1.new\" \"$1\"\n";
    {
        const apogee::testing::EnvGuard editor{"EDITOR", "/bin/sh " + renamer.string()};
        REQUIRE(odd.home.run({"symphonies", "edit", "s1"}, &out, &err) == 1);
    }
    CHECK(err.starts_with("apogee symphonies: 'edit' does not rename"));
    const fs::path kept = odd.home.home() / "cache" / "symphony-s1.yaml";
    const std::string door = "apogee symphonies create solo2 --from \"" + kept.string() + "\"";
    CHECK(err.find("with '" + door + "' (the edit is kept there)") != std::string::npos);
    const Pasted pasted = paste(odd.home, door, odd.scratch.path());
    CHECK(pasted.code == 0);
    CHECK(pasted.words.back() == kept.string());
}

TEST_CASE("a symphony named with a leading dash is played and edited as created says",
          "[commands][suggested-command][M48]") {
    if (!apogee::platform::supports_child_processes() || !fs::exists("/usr/bin/env")) {
        SKIP("no child processes or no /usr/bin/env here: no shell can read a pasted line");
    }
    const OddHome odd;
    std::string out;
    std::string err;
    REQUIRE(odd.home.run({"symphonies", "create", "s1"}, &out, &err) == 0);
    CHECK(out.find("Play it:  apogee symphonies play s1 --input \"…\"\n") != std::string::npos);
    INFO(err);
    REQUIRE(odd.home.run({"symphonies", "create", "--", "-q"}, &out, &err) == 0);
    const std::string play = "apogee symphonies play --input \"…\" -- -q";
    const std::string edit = "apogee symphonies edit -- -q";
    CHECK(out.find("Play it:  " + play + "\n") != std::string::npos);
    CHECK(out.find("Edit it:  " + edit + "\n") != std::string::npos);
    // Pasted, each reaches its verb on `-q`; the play's input filled first.
    std::string filled = play;
    filled.replace(filled.find("\"…\""), 5, "hi");
    const Pasted played = paste(odd.home, filled, odd.scratch.path());
    CHECK(played.words.back() == "-q");
    const std::string truth = apogee::platform::find_on_path("true");
    REQUIRE_FALSE(truth.empty());
    const apogee::testing::EnvGuard editor{"EDITOR", truth};
    const Pasted edited = paste(odd.home, edit, odd.scratch.path());
    CHECK(edited.code == 0);
    CHECK(edited.out.find("no change to symphony '-q'") != std::string::npos);
}

TEST_CASE("every word comes back exactly through line_tokens, the Console's table too",
          "[commands][suggested-command][M48]") {
    // line_tokens is the TUI's exec line and the Mac app's Console's table:
    // one rendering serves both and the POSIX shells.
    for (const std::string& word : {std::string{"plain"},
                                    std::string{"-x"},
                                    std::string{"--"},
                                    std::string{"my notes"},
                                    std::string{"a;b"},
                                    std::string{"it's"},
                                    std::string{"$HOME"},
                                    std::string{"a\"b"},
                                    std::string{"x`id`"},
                                    std::string{"~x"},
                                    std::string{"=ls"},
                                    std::string{},
                                    std::string{R"(C:\Users\A B\m.gguf)"},
                                    std::string{R"(\\server\share\m.gguf)"},
                                    std::string{R"(C:\dir\)"},
                                    std::string{"it's!"},
                                    std::string{"a!b"},
                                    std::string{"n\nl"},
                                    std::string{"a\tb"},
                                    std::string{"a*b"},
                                    std::string{"#x"},
                                    std::string{"{a,b}"},
                                    std::string{"a&b"},
                                    std::string{"a|b"},
                                    std::string{"a>b"},
                                    std::string{"x(y)"},
                                    std::string{"O'Brien$x"},
                                    std::string{"a b'c\"d"}}) {
        const std::string rendered =
            apogee::harness::shell_word(word, apogee::harness::CommandShell::Posix);
        INFO("[" << word << "] rendered " << rendered);
        const apogee::commands::LineTokens tokens = apogee::commands::line_tokens(rendered);
        CHECK(tokens.error.empty());
        CHECK(tokens.words == std::vector<std::string>{word});
    }
    // PowerShell's rare spelling -- a quote together with `$`, a backtick,
    // `"`, `!` or a doubled or trailing backslash -- is `''` inside single
    // quotes, which line_tokens reads as nothing: on Windows that word
    // round-trips through PowerShell, not the exec line.
    const std::string rare =
        apogee::harness::shell_word("it's$x", apogee::harness::CommandShell::PowerShell);
    CHECK(rare == "'it''s$x'");
    CHECK(apogee::commands::line_tokens(rare).words == std::vector<std::string>{"its$x"});
}

// --- CLI-M49: the binary and the root that printed it ----------------------
//
// A printed command begins with the build's name and the root flag that
// chose the run's root, so pasted it acts on the install that printed it --
// never a release binary over another root.

namespace {

using apogee::harness::Channel;

/// The extractor every home here builds its graphs with: one entity, Fox.
constexpr std::string_view kExtractorTurns =
    R"({"turns": [{"text": "{\"entities\": [{\"name\": \"Fox\", \"type\": )"
    R"(\"system\", \"description\": \"a fox\"}], \"relations\": []}"}]})";

/// A throwaway home directory with `APOGEE_HOME` and `APOGEE_CONFIG` unset,
/// so the root flags decide -- CliHome's `APOGEE_HOME` would refuse `--dev`
/// -- and collection `plain` ingested into the release root and the dev
/// root alike, each with a mock extractor as its default backend.
struct FlaggedHome {
    apogee::testing::TempDir home{"m49-home-" + std::to_string(std::random_device{}())};
    apogee::testing::TempDir scratch{"m49-scratch-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvUnsetGuard no_override{"APOGEE_HOME"};
    apogee::testing::EnvUnsetGuard no_config{"APOGEE_CONFIG"};
    apogee::testing::EnvGuard user_home{"HOME", home.path().string()};
    apogee::testing::EnvGuard profile{"USERPROFILE", home.path().string()};

    FlaggedHome() {
        std::ofstream{extractor(), std::ios::binary} << kExtractorTurns;
        const fs::path docs = scratch.path() / "docs";
        fs::create_directories(docs);
        std::ofstream{docs / "a.txt", std::ios::binary} << "Fox runs in the forest.\n";
        for (const std::vector<std::string>& flag :
             {std::vector<std::string>{}, std::vector<std::string>{"--dev"}}) {
            write_config(root(flag.empty() ? ".apogee" : ".apogee-dev"));
            std::vector<std::string> ingest = flag;
            ingest.insert(ingest.end(), {"embed", "ingest", "-q", "--retriever", "lexical", "plain",
                                         docs.string()});
            std::string out;
            std::string err;
            INFO(err);
            REQUIRE(run(ingest, &out, &err) == 0);
        }
    }

    [[nodiscard]] fs::path extractor() const {
        return scratch.path() / "extractor.json";
    }

    [[nodiscard]] fs::path root(std::string_view directory) const {
        return home.path() / directory;
    }

    /// A config at `<root>/config/config.json` naming the extractor.
    void write_config(const fs::path& at) const {
        fs::create_directories(at / "config");
        const nlohmann::json config{
            {"models", {{"default", "extractor"}}},
            {"backends",
             {{"extractor", {{"type", "mock"}, {"model_path", extractor().string()}}}}}};
        std::ofstream{at / "config" / "config.json", std::ios::binary} << config.dump(2) << "\n";
    }

    /// The real command tree over `args`, with no `--config`: the run finds
    /// its root by the chain, the flag first.
    int run(const std::vector<std::string>& args, std::string* out, std::string* err) const {
        const std::ostringstream captured;
        const std::ostringstream errors;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(errors.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand command{apogee::commands::default_registry()};
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : args) {
                argv.push_back(arg.c_str());
            }
            code = command.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str();
        *err = errors.str();
        return code;
    }

    /// `command`, read by /bin/sh, its program word dropped, run here.
    int paste(const std::string& command, std::vector<std::string>* words, std::string* out,
              std::string* err) const {
        *words = argv_of({"/bin/sh"}, command, scratch.path());
        REQUIRE_FALSE(words->empty());
        return run(std::vector<std::string>(words->begin() + 1, words->end()), out, err);
    }
};

[[nodiscard]] bool built_in(const fs::path& root, const std::string& name) {
    return apogee::embedstore::Store{root / "embeddings" / (name + ".db")}.graph_stats().built();
}

/// Every line of `text` begins `prefix`.
[[nodiscard]] bool every_line_begins(const std::string& text, std::string_view prefix) {
    std::istringstream lines{text};
    for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with(prefix)) {
            return false;
        }
    }
    return true;
}

/// The doors `check` still joins by hand, whose program word comes with
/// them onto the renderer in CLI-M61 (its sites, not this item's): the set
/// remedy `setup_remedy` builds from the set's name. CLI-M61 empties this.
[[nodiscard]] bool moved_by_m61(const std::string& door) {
    return door.starts_with("apogee train setup --with ") ||
           door.starts_with("apogee train setup --rebuild --with ");
}

[[nodiscard]] bool child_shell_here() {
    return apogee::platform::supports_child_processes() && fs::exists("/bin/sh") &&
           fs::exists("/usr/bin/env");
}

}  // namespace

TEST_CASE("a remedy printed under --dev names --dev, and pasted it builds the dev root's graph",
          "[commands][suggested-command][M49]") {
    if (!child_shell_here()) {
        SKIP("no child processes or no /bin/sh here: no shell can read a pasted line");
    }
    const FlaggedHome flagged;
    std::string out;
    std::string err;

    // The hint on stdout, then the refusal on stderr, each naming --dev.
    REQUIRE(flagged.run({"--dev", "graph", "stats", "plain"}, &out, &err) == 0);
    CHECK(out.find("No graph built for \"plain\". Run: apogee --dev graph build plain\n") !=
          std::string::npos);
    REQUIRE(flagged.run({"--dev", "graph", "show", "plain", "Fox"}, &out, &err) == 1);
    // The house voice: one stderr line, `apogee graph: `, as before.
    CHECK(every_line_begins(err, "apogee graph: "));
    CHECK(err ==
          "apogee graph: no graph built for 'plain' -- run: apogee --dev graph build plain\n");

    // Pasted: the dev root's graph is built, the release root's left alone.
    std::vector<std::string> words;
    INFO(err);
    REQUIRE(flagged.paste(after(err, " -- run: "), &words, &out, &err) == 0);
    CHECK(words == std::vector<std::string>{"apogee", "--dev", "graph", "build", "plain"});
    CHECK(out.find("Run `apogee --dev graph stats plain` for the graph's shape.") !=
          std::string::npos);
    CHECK(built_in(flagged.root(".apogee-dev"), "plain"));
    CHECK_FALSE(built_in(flagged.root(".apogee"), "plain"));
    REQUIRE(flagged.run({"--dev", "graph", "stats", "plain"}, &out, &err) == 0);
    CHECK(out.find("Nodes:") != std::string::npos);
    REQUIRE(flagged.run({"graph", "stats", "plain"}, &out, &err) == 0);
    CHECK(out.find("No graph built for \"plain\". Run: apogee graph build plain\n") !=
          std::string::npos);

    // `version`'s first word stays the product's.
    REQUIRE(flagged.run({"--dev", "version"}, &out, &err) == 0);
    CHECK(out.starts_with("apogee "));
}

TEST_CASE("a remedy printed under --custom names its file, absolute and quoted, and runs there",
          "[commands][suggested-command][M49]") {
    if (!child_shell_here()) {
        SKIP("no child processes or no /bin/sh here: no shell can read a pasted line");
    }
    const FlaggedHome flagged;
    const fs::path custom = flagged.scratch.path() / "a b";
    flagged.write_config(custom);
    const std::string file = (custom / "config" / "config.json").lexically_normal().string();
    std::string out;
    std::string err;
    INFO(err);
    REQUIRE(flagged.run({"--custom", file, "embed", "ingest", "-q", "--retriever", "lexical",
                         "plain", (flagged.scratch.path() / "docs").string()},
                        &out, &err) == 0);

    REQUIRE(flagged.run({"--custom", file, "graph", "show", "plain", "Fox"}, &out, &err) == 1);
    const std::string remedy = after(err, " -- run: ");
    CHECK(remedy == "apogee --custom \"" + file + "\" graph build plain");
    std::vector<std::string> words;
    REQUIRE(flagged.paste(remedy, &words, &out, &err) == 0);
    CHECK(words == std::vector<std::string>{"apogee", "--custom", file, "graph", "build", "plain"});
    CHECK(built_in(custom, "plain"));
    CHECK_FALSE(built_in(flagged.root(".apogee"), "plain"));
    CHECK_FALSE(built_in(flagged.root(".apogee-dev"), "plain"));
}

TEST_CASE("a dev or test build names itself in every command it prints, and the voice stays",
          "[commands][suggested-command][M49]") {
    for (const Channel channel : apogee::harness::kChannels) {
        const std::string program{apogee::harness::channel_program(channel)};
        INFO(program << " build");
        const apogee::testing::BakedChannelGuard baked{channel};
        // A YAML config: check's Config row names `config migrate`.
        const CliHome home{""};
        const fs::path docs = home.home() / "docs";
        fs::create_directories(docs);
        std::ofstream{docs / "a.txt", std::ios::binary} << "Fox runs in the forest.\n";
        std::string out;
        std::string err;
        REQUIRE(
            home.run({"embed", "ingest", "-q", "--retriever", "lexical", "plain", docs.string()},
                     &out, &err) == 0);

        // graph show's remedy, after the house voice.
        REQUIRE(home.run({"graph", "show", "plain", "Fox"}, &out, &err) == 1);
        CHECK(err == "apogee graph: no graph built for 'plain' -- run: " + program +
                         " graph build plain\n");

        // A preset's refusal ends with its door, named the same way.
        REQUIRE(home.run({"mcp", "create", "mine", "--from", "self"}, &out, &err) != 0);
        CHECK(err.find("(" + program + " mcp presets)") != std::string::npos);

        // check's text and its JSON remedies: an Apogee command begins with
        // this build's name, and the chmod rows keep `chmod`.
        const bool modes = apogee::harness::supports_private_modes();
        if (modes) {
            fs::permissions(home.home() / "config",
                            fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                fs::perms::others_read | fs::perms::others_exec);
        }
        (void)home.run({"check", "--no-color"}, &out, &err);
        CHECK(out.find(program + " config migrate") != std::string::npos);
        (void)home.run({"check", "--output-format", "json"}, &out, &err);
        const nlohmann::json rows = nlohmann::json::parse(out).at("rows");
        bool migrate = false;
        bool chmod = false;
        for (const nlohmann::json& row : rows) {
            const std::string remedy = row.value("remedy", "");
            if (remedy.empty()) {
                continue;
            }
            INFO("row " << row.at("name") << ": " << remedy);
            for (const std::string& door : apogee::testing::remedy_doors(remedy)) {
                if (door.starts_with("apogee") && !moved_by_m61(door)) {
                    CHECK(door.starts_with(program + " "));
                }
            }
            migrate = migrate || remedy == program + " config migrate";
            if (remedy.starts_with("chmod")) {
                chmod = true;
                CHECK(remedy.ends_with("   (or: " + program + " check --fix)"));
            }
        }
        CHECK(migrate);
        CHECK(chmod == modes);

        // `version`'s first word stays the product's.
        REQUIRE(home.run({"version"}, &out, &err) == 0);
        CHECK(out.starts_with("apogee "));
    }
}
