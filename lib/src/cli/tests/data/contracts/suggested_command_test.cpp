#include "contracts/suggested_command.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contracts/paths.h"
#include "platform/child_process.h"
#include "support/channel_guard.h"
#include "support/env_guard.h"

/// CLI-M48: one renderer of a command a person copies. Each word is read
/// back by the shells a person pastes into -- sh, bash, zsh and dash -- and
/// must come back exactly; a command orders its options and `--` as
/// CLI11 2.4.2 reads them, and a plain name prints today's bytes.
namespace {

using apogee::harness::CommandShell;
using apogee::harness::operand;
using apogee::harness::option;
using apogee::harness::option_placeholder;
using apogee::harness::placeholder;
using apogee::harness::render;
using apogee::harness::render_all;
using apogee::harness::shell_word;
using apogee::harness::SuggestedCommand;

/// A word and its POSIX rendering, the measured table (`c:rt`).
struct Word {
    std::string word{};
    std::string posix{};
};

const std::vector<Word> kWords{
    {"plain", "plain"},
    {"-x", "-x"},
    {"-h", "-h"},
    {"--graph", "--graph"},
    {"--", "--"},
    {"my notes", "\"my notes\""},
    {"a;b", "\"a;b\""},
    {"it's", "\"it's\""},
    {"$HOME", "'$HOME'"},
    {"a\"b", "'a\"b'"},
    {"x`id`", "'x`id`'"},
    {"~x", "\"~x\""},
    {"=ls", "\"=ls\""},
    {"=x", "\"=x\""},
    {"", "\"\""},
    {R"(C:\Users\A B\m.gguf)", R"("C:\Users\A B\m.gguf")"},
    {R"(\\server\share\m.gguf)", R"('\\server\share\m.gguf')"},
    {R"(C:\dir\)", R"('C:\dir\')"},
    {"it's!", R"('it'\''s!')"},
    {"a!b", "'a!b'"},
    {"n\nl", "'n\nl'"},
    {"a\tb", "'a\tb'"},
    {"a*b", "\"a*b\""},
    {"#x", "\"#x\""},
    {"{a,b}", "\"{a,b}\""},
    {"a&b", "\"a&b\""},
    {"a|b", "\"a|b\""},
    {"a>b", "\"a>b\""},
    {"x(y)", "\"x(y)\""},
    {"O'Brien$x", R"('O'\''Brien$x')"},
    {"a b'c\"d", R"('a b'\''c"d')"},
};

/// The words `printf '%s\0' <words>` printed, split on its NULs.
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

/// `line` run by `shell` (and its leading arguments), the environment
/// cleared and a temporary folder its working directory, as
/// `printf '%s\0' <line>`: the words it printed, or nothing when it failed.
[[nodiscard]] std::optional<std::vector<std::string>> replay(const std::string& env,
                                                             const std::vector<std::string>& shell,
                                                             const std::string& line,
                                                             const std::filesystem::path& folder,
                                                             std::string& why) {
    apogee::platform::ChildCommand command;
    command.program = env;
    command.arguments = {"-i", "PATH=/usr/bin:/bin"};
    command.arguments.insert(command.arguments.end(), shell.begin(), shell.end());
    command.arguments.push_back("-c");
    command.arguments.push_back("printf '%s\\0' " + line);
    command.working_directory = folder.string();
    const apogee::platform::CapturedRun ran = apogee::platform::run_captured(
        command, apogee::platform::CapturedLimits{.timeout = std::chrono::seconds{30}});
    if (ran.status != 0) {
        why = ran.error + " " + ran.tail;
        return std::nullopt;
    }
    return nul_words(ran.out);
}

/// The shells this host has, by name, each as the argv that starts it --
/// zsh with `-f`, so no rc file of the developer's is read.
[[nodiscard]] std::map<std::string, std::vector<std::string>> posix_shells() {
    std::map<std::string, std::vector<std::string>> shells;
    if (std::filesystem::exists("/bin/sh")) {
        shells["sh"] = {"/bin/sh"};
    }
    for (const auto& [name, extra] : std::vector<std::pair<std::string, std::vector<std::string>>>{
             {"bash", {}}, {"zsh", {"-f"}}, {"dash", {}}}) {
        if (const std::string found = apogee::platform::find_on_path(name); !found.empty()) {
            std::vector<std::string> argv{found};
            argv.insert(argv.end(), extra.begin(), extra.end());
            shells[name] = argv;
        }
    }
    return shells;
}

}  // namespace

TEST_CASE("a word renders bare, double-quoted or single-quoted by the rule",
          "[contracts][suggested-command][M48]") {
    for (const Word& word : kWords) {
        INFO("the word [" << word.word << "]");
        CHECK(shell_word(word.word, CommandShell::Posix) == word.posix);
    }
    // A `!` never sits in double quotes: an interactive bash or zsh expands it.
    for (const std::string_view bang : {"it's!", "a!b"}) {
        CHECK(shell_word(bang, CommandShell::Posix).starts_with("'"));
    }
    // A newline or a tab is single-quoted, so the word stays one word.
    CHECK(shell_word("n\nl", CommandShell::Posix).starts_with("'"));
    CHECK(shell_word("a\tb", CommandShell::Posix).starts_with("'"));
}

TEST_CASE("every word comes back exactly through sh, bash, zsh and dash",
          "[contracts][suggested-command][M48][shells]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform: no shell can be started");
    }
    // env(1) where POSIX puts it, never a PATH lookup a developer's own
    // `env` file could shadow.
    const std::string env = "/usr/bin/env";
    if (!std::filesystem::exists(env)) {
        SKIP("no /usr/bin/env: the shells cannot be started with a cleared environment");
    }
    const std::map<std::string, std::vector<std::string>> shells = posix_shells();
    for (const char* wanted : {"sh", "bash", "zsh", "dash"}) {
        if (!shells.contains(wanted)) {
            WARN("no " << wanted << " on this host: its replay is skipped");
        }
    }
    REQUIRE_FALSE(shells.empty());
    const apogee::testing::TempDir folder{"suggested-words-" +
                                          std::to_string(std::random_device{}())};
    // A file a glob would find, so an unquoted `a*b` would read differently.
    std::ofstream{folder.path() / "aXb"} << "";
    std::string line;
    std::vector<std::string> expected;
    for (const Word& word : kWords) {
        line += " " + shell_word(word.word, CommandShell::Posix);
        expected.push_back(word.word);
    }
    for (const auto& [name, argv] : shells) {
        INFO("read back by " << name << ": " << line);
        std::string why;
        const std::optional<std::vector<std::string>> words =
            replay(env, argv, line, folder.path(), why);
        INFO(why);
        REQUIRE(words.has_value());
        CHECK(*words == expected);
    }
}

TEST_CASE("a command keeps today's order, and moves a dash-led operand past --",
          "[contracts][suggested-command][M48]") {
    const auto build = [](std::vector<apogee::harness::CommandPart> parts) {
        return render(SuggestedCommand{.verbs = {"graph", "build"}, .parts = std::move(parts)},
                      CommandShell::Posix);
    };
    // The worked examples.
    CHECK(build({operand("-x")}) == "apogee graph build -- -x");
    CHECK(build({operand("--")}) == "apogee graph build -- --");
    CHECK(build({operand("a;b")}) == "apogee graph build \"a;b\"");
    CHECK(build({operand("=ls")}) == "apogee graph build \"=ls\"");
    CHECK(build({operand("my notes")}) == "apogee graph build \"my notes\"");
    CHECK(build({operand("it's")}) == "apogee graph build \"it's\"");
    CHECK(build({operand("$HOME")}) == "apogee graph build '$HOME'");
    CHECK(build({operand("it's!")}) == R"(apogee graph build 'it'\''s!')");
    const auto set_backend = [](std::string_view name) {
        return render(
            SuggestedCommand{.verbs = {"config", "set-backend"},
                             .parts = {operand(name),
                                       option_placeholder("--model-path", "<an existing .gguf>")}},
            CommandShell::Posix);
    };
    CHECK(set_backend("-gone") ==
          "apogee config set-backend --model-path <an existing .gguf> -- -gone");
    CHECK(set_backend("flat") == "apogee config set-backend flat --model-path <an existing .gguf>");
    // A greedy option eats the `--` that follows it: one more is the operands'.
    CHECK(build({option_placeholder("--source", "<dir>", true), operand("-x")}) ==
          "apogee graph build --source <dir> -- -- -x");
    CHECK(build({option_placeholder("--source", "<dir>", true), operand("g7")}) ==
          "apogee graph build --source <dir> -- g7");
    // An option's value parses whole, so it never needs `--`.
    CHECK(build({option_placeholder("--source", "<dir>", true), option("--graph", "-x")}) ==
          "apogee graph build --source <dir> --graph -x");
    CHECK(render(
              SuggestedCommand{
                  .program = "chmod", .verbs = {"700"}, .parts = {operand("/h/it's home/config")}},
              CommandShell::Posix) == "chmod 700 \"/h/it's home/config\"");
    // Placeholders print verbatim; the empty word is `""`.
    CHECK(render(SuggestedCommand{.verbs = {"symphonies", "play"},
                                  .parts = {operand("s1"), option_placeholder("--input", "\"…\"")}},
                 CommandShell::Posix) == "apogee symphonies play s1 --input \"…\"");
    CHECK(render(SuggestedCommand{.verbs = {"symphonies", "play"},
                                  .parts = {operand("-q"), option_placeholder("--input", "\"…\"")}},
                 CommandShell::Posix) == "apogee symphonies play --input \"…\" -- -q");
    CHECK(render(SuggestedCommand{.verbs = {"embed", "ingest"},
                                  .parts = {operand("notes"), placeholder("<path>")}},
                 CommandShell::Posix) == "apogee embed ingest notes <path>");
    CHECK(render(SuggestedCommand{.verbs = {"config", "set-backend"},
                                  .parts = {operand("m"), option("--adapters", "")}},
                 CommandShell::Posix) == "apogee config set-backend m --adapters \"\"");
    CHECK(render(SuggestedCommand{.verbs = {"graph", "communities"},
                                  .parts = {operand("notes"), option("--list")}},
                 CommandShell::Posix) == "apogee graph communities notes --list");
    CHECK(render(SuggestedCommand{.verbs = {"graph", "communities"},
                                  .parts = {operand("-x"), option("--list")}},
                 CommandShell::Posix) == "apogee graph communities --list -- -x");
    // Two commands, joined as a site joins them.
    CHECK(render_all({SuggestedCommand{.verbs = {"train", "eval"}, .parts = {operand("r1")}},
                      SuggestedCommand{.verbs = {"train", "promote"}, .parts = {operand("r1")}}},
                     " && ") == "apogee train eval r1 && apogee train promote r1");
}

TEST_CASE("a command names the build that printed it, and the root flag in force",
          "[contracts][suggested-command][M49]") {
    using apogee::harness::Channel;
    using apogee::harness::RootFlag;
    using apogee::harness::RootFlagScope;
    const auto build = [] {
        return render(SuggestedCommand{.verbs = {"graph", "build"}, .parts = {operand("-x")}},
                      CommandShell::Posix);
    };
    const auto chmod = [] {
        return render(
            SuggestedCommand{.program = "chmod", .verbs = {"700"}, .parts = {operand("/h")}},
            CommandShell::Posix);
    };
    // A release build with no flag: today's bytes.
    CHECK(build() == "apogee graph build -- -x");
    {
        const apogee::testing::BakedChannelGuard dev{Channel::Dev};
        CHECK(build() == "apogee-dev graph build -- -x");
        CHECK(chmod() == "chmod 700 /h");
    }
    {
        const apogee::testing::BakedChannelGuard test{Channel::Test};
        CHECK(build() == "apogee-test graph build -- -x");
        CHECK(chmod() == "chmod 700 /h");
    }
    {
        const RootFlagScope flag{RootFlag{.channel = Channel::Dev, .custom_config = {}}};
        CHECK(build() == "apogee --dev graph build -- -x");
        CHECK(chmod() == "chmod 700 /h");
        // A literal command starts with the same words.
        CHECK(apogee::harness::printed_program(CommandShell::Posix) == "apogee --dev");
    }
    {
        // `--custom`'s file is one word, quoted by the rule like any other.
        const apogee::testing::TempDir folder{"suggested-custom-" +
                                              std::to_string(std::random_device{}())};
        const std::filesystem::path file = folder.path() / "a b" / "config" / "config.json";
        const RootFlagScope flag{RootFlag{.channel = std::nullopt, .custom_config = file}};
        CHECK(build() ==
              "apogee --custom \"" + file.lexically_normal().string() + "\" graph build -- -x");
        CHECK(chmod() == "chmod 700 /h");
    }
    // After each case, the channel and the flag are the ones before it.
    CHECK(apogee::harness::baked_channel() == Channel::Release);
    CHECK_FALSE(apogee::harness::root_flag().has_value());
    CHECK(build() == "apogee graph build -- -x");
    CHECK(apogee::harness::printed_program(CommandShell::Posix) == "apogee");
}

TEST_CASE("PowerShell's rendering quotes a comma and a leading @, and doubles a single quote",
          "[contracts][suggested-command][M48]") {
    CHECK(shell_word("a,b", CommandShell::PowerShell) == "\"a,b\"");
    CHECK(shell_word("@x", CommandShell::PowerShell) == "\"@x\"");
    CHECK(shell_word("x@y", CommandShell::PowerShell) == "x@y");
    CHECK(shell_word("it's$x", CommandShell::PowerShell) == "'it''s$x'");
    CHECK(shell_word("$HOME", CommandShell::PowerShell) == "'$HOME'");
    CHECK(shell_word("it's", CommandShell::PowerShell) == "\"it's\"");
    CHECK(shell_word("", CommandShell::PowerShell) == "\"\"");
    CHECK(shell_word("plain", CommandShell::PowerShell) == "plain");
    // PowerShell reads the typographic quotes as its own, and `--%` bare as
    // its stop-parsing token; the POSIX shells read neither.
    CHECK(shell_word("a\u201Cb", CommandShell::PowerShell) == "'a\u201Cb'");
    CHECK(shell_word("it\u2019s", CommandShell::PowerShell) == "'it\u2019\u2019s'");
    CHECK(shell_word("--%", CommandShell::PowerShell) == "\"--%\"");
    CHECK(shell_word("a\u201Cb", CommandShell::Posix) == "\"a\u201Cb\"");
    CHECK(shell_word("--%", CommandShell::Posix) == "--%");
    // POSIX leaves both bare: neither shell reads them specially.
    CHECK(shell_word("a,b", CommandShell::Posix) == "a,b");
    CHECK(shell_word("@x", CommandShell::Posix) == "@x");
    // A known gap, named: `%` is bare in both, and `cmd /C` -- what an
    // agent's run_command evals through on Windows -- expands `%VAR%` inside
    // quotes and out alike, so no quoting this rule can choose stops it.
    CHECK(shell_word("%PATH%", CommandShell::PowerShell) == "%PATH%");
    CHECK(shell_word("%PATH%", CommandShell::Posix) == "%PATH%");
    // The build's own shell is the one its people paste into.
#if defined(_WIN32)
    CHECK(apogee::harness::native_command_shell() == CommandShell::PowerShell);
#else
    CHECK(apogee::harness::native_command_shell() == CommandShell::Posix);
#endif
    CHECK(shell_word("a,b") == shell_word("a,b", apogee::harness::native_command_shell()));
}

namespace {

/// `text` as a PowerShell single-quoted string.
[[nodiscard]] std::string powershell_literal(std::string_view text) {
    std::string quoted = "'";
    for (const char c : text) {
        quoted += c;
        if (c == '\'') {
            quoted += '\'';
        }
    }
    return quoted + "'";
}

}  // namespace

TEST_CASE("PowerShell's renderings reach a native command as the words they name",
          "[contracts][suggested-command][M48][powershell]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const std::string pwsh = apogee::platform::find_on_path("pwsh");
    if (pwsh.empty()) {
        SKIP("no pwsh on PATH: the PowerShell renderings cannot be replayed here");
    }
    // A fake `apogee` that says each word it was sent, in brackets.
    const apogee::testing::TempDir work{"suggested-pwsh-" + std::to_string(std::random_device{}())};
    const std::filesystem::path bin = work.path() / "bin";
    std::filesystem::create_directories(bin);
    std::ofstream{bin / "apogee", std::ios::binary} << R"(#!/bin/sh
line=
for word in "$@"; do line="$line[$word]"; done
printf '%s\n' "$line"
)";
    std::filesystem::permissions(bin / "apogee", std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
    // Each command as PowerShell renders it, and the argv it must reach.
    const std::vector<std::pair<SuggestedCommand, std::string>> commands{
        {{.verbs = {"graph", "build"}, .parts = {operand("-x")}}, "[graph][build][--][-x]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("--")}}, "[graph][build][--][--]"},
        {{.verbs = {"config", "set-backend"}, .parts = {operand("m"), option("--adapters", "")}},
         "[config][set-backend][m][--adapters][]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("my notes")}}, "[graph][build][my notes]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("it's")}}, "[graph][build][it's]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("$HOME")}}, "[graph][build][$HOME]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("it's$x")}}, "[graph][build][it's$x]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("a,b")}}, "[graph][build][a,b]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("@x")}}, "[graph][build][@x]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("~x")}}, "[graph][build][~x]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("a*b")}}, "[graph][build][a*b]"},
        {{.verbs = {"graph", "build"}, .parts = {operand(R"(C:\Users\A B\m.gguf)")}},
         R"([graph][build][C:\Users\A B\m.gguf])"},
        {{.verbs = {"graph", "build"}, .parts = {operand("a\u201Cb")}}, "[graph][build][a\u201Cb]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("it\u2019s")}},
         "[graph][build][it\u2019s]"},
        {{.verbs = {"graph", "build"}, .parts = {operand("--%")}}, "[graph][build][--][--%]"},
    };
    std::string driver = "$env:PATH = " + powershell_literal(bin.string()) +
                         " + [System.IO.Path]::PathSeparator + $env:PATH\n";
    for (const std::string_view mode : {"Standard", "Legacy"}) {
        driver += "$global:PSNativeCommandArgumentPassing = '" + std::string{mode} + "'\n";
        for (const auto& [command, argv] : commands) {
            driver += render(command, CommandShell::PowerShell) + "\n";
        }
    }
    const std::filesystem::path script = work.path() / "driver.ps1";
    std::ofstream{script, std::ios::binary} << driver;
    apogee::platform::ChildCommand command;
    command.program = pwsh;
    command.arguments = {"-NoProfile", "-NonInteractive", "-File", script.string()};
    command.working_directory = work.path().string();
    const std::filesystem::path home_dir = work.path() / "home";
    std::filesystem::create_directories(home_dir);
    const std::string home = home_dir.string();
    command.unset_environment = {"HOME",
                                 "XDG_CACHE_HOME",
                                 "XDG_CONFIG_HOME",
                                 "XDG_DATA_HOME",
                                 "DOTNET_EnableDiagnostics",
                                 "POWERSHELL_TELEMETRY_OPTOUT",
                                 "POWERSHELL_UPDATECHECK"};
    command.extra_environment = {{"HOME", home},
                                 {"XDG_CACHE_HOME", home + "/.cache"},
                                 {"XDG_CONFIG_HOME", home + "/.config"},
                                 {"XDG_DATA_HOME", home + "/.local/share"},
                                 {"DOTNET_EnableDiagnostics", "0"},
                                 {"POWERSHELL_TELEMETRY_OPTOUT", "1"},
                                 {"POWERSHELL_UPDATECHECK", "Off"}};
    const apogee::platform::CapturedRun ran = apogee::platform::run_captured(
        command, apogee::platform::CapturedLimits{.timeout = std::chrono::minutes{2}});
    INFO("pwsh's run: " << ran.error);
    INFO("pwsh's stderr: " << ran.tail);
    REQUIRE(ran.status == 0);
    std::vector<std::string> lines;
    std::istringstream read{ran.out};
    for (std::string line; std::getline(read, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    REQUIRE(lines.size() == 2 * commands.size());
    for (std::size_t index = 0; index < commands.size(); ++index) {
        const std::string rendered = render(commands[index].first, CommandShell::PowerShell);
        // Standard passing -- pwsh 7.3 and later, and Windows' default for an
        // .exe -- is the rendering's contract: every word, `--` and the empty
        // word among them, reaches the program as named.
        INFO("Standard passing of `" << rendered << "`");
        CHECK(lines[index] == commands[index].second);
        // Legacy passing -- Windows PowerShell 5.1's rules -- drops an empty
        // word; that and anything else it changes is said, never asserted.
        if (const std::string& legacy = lines[commands.size() + index];
            legacy != commands[index].second) {
            WARN("Legacy passing reads `" << rendered << "` as " << legacy << ", not "
                                          << commands[index].second);
        }
    }
}
