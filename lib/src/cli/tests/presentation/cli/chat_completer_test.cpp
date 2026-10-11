#include "cli/chat_completer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/retriever.h"
#include "ansi/text_width.h"
#include "backends/model_roster.h"
#include "cli/chat.h"
#include "cli/chat_attachments.h"
#include "cli/suite_residency.h"
#include "contracts/config.h"
#include "knowledge/record.h"
#include "support/env_guard.h"

using apogee::commands::chat_commands;
using apogee::commands::chat_help_lines;
using apogee::commands::ChatCommandSpec;
using apogee::commands::ChatCompletionSources;
using apogee::commands::ChatVerb;
using apogee::commands::DirectoryEntry;
using apogee::commands::find_chat_command;
using apogee::commands::suggest_chat_input;
using apogee::commands::Suggestions;

namespace {

/// A filesystem as a map from directory to entries, so no test touches disk.
ChatCompletionSources sources_with(std::map<std::string, std::vector<DirectoryEntry>> tree) {
    ChatCompletionSources sources;
    sources.backends = {{"claude", "anthropic · claude-sonnet-5"},
                        {"local", "llamacpp · qwen.gguf"},
                        {"mock", "mock"}};
    sources.working_directory = "/work";
    sources.list = [tree = std::move(tree)](const std::filesystem::path& directory) {
        const auto found = tree.find(directory.lexically_normal().generic_string());
        return found == tree.end() ? std::vector<DirectoryEntry>{} : found->second;
    };
    return sources;
}

ChatCompletionSources project() {
    return sources_with({
        {"/work",
         {{"src", true},
          {"README.md", false},
          {".git", true},
          {"lib", true},
          {"notes.txt", false},
          {"library", true},
          {"my file.pdf", false},
          {"my folder", true}}},
        {"/work/src/", {{"main.cpp", false}, {"commands", true}, {"common", true}}},
        {"/work/my folder/", {{"inside.txt", false}}},
    });
}

std::vector<std::string> texts(const Suggestions& suggestions) {
    std::vector<std::string> out;
    for (const auto& candidate : suggestions.candidates) {
        out.push_back(candidate.text);
    }
    return out;
}

std::vector<std::string> labels(const Suggestions& suggestions) {
    std::vector<std::string> out;
    for (const auto& candidate : suggestions.candidates) {
        out.push_back(candidate.label.empty() ? candidate.text : candidate.label);
    }
    return out;
}

}  // namespace

// --- the one table -----------------------------------------------------------

TEST_CASE("the table is what /help, completion and dispatch all read", "[chat][completer]") {
    // The drift this exists to prevent had already happened once: /retriever
    // and /rerank were dispatched but sat in neither the completion list nor
    // /help.
    const Suggestions bare = suggest_chat_input("/", project());
    const std::vector<std::string> help = chat_help_lines();
    REQUIRE(bare.candidates.size() == chat_commands().size());
    REQUIRE(help.size() == chat_commands().size());

    std::size_t row = 0;
    for (const ChatCommandSpec& spec : chat_commands()) {
        const std::string name = "/" + std::string{spec.verb};
        INFO(name);

        // Dispatch: the REPL parses the line, then looks the word up here.
        const auto parsed = apogee::commands::parse_slash(name);
        REQUIRE(parsed.has_value());
        CHECK(find_chat_command(parsed->name) == &spec);

        // Completion offers it, described, in table order.
        CHECK(labels(bare)[row] == name);
        CHECK(bare.candidates[row].description == spec.description);

        // /help shows it with the same description.
        CHECK(help[row].find(name) != std::string::npos);
        CHECK(help[row].find(std::string{spec.description}) != std::string::npos);
        ++row;
    }
    CHECK(find_chat_command("retriever") != nullptr);
    CHECK(find_chat_command("rerank") != nullptr);
    CHECK(find_chat_command("nonsense") == nullptr);
}

TEST_CASE("every handler the REPL has is reachable from the table", "[chat][completer]") {
    // The other direction: a verb with a handler but no row would be
    // unreachable, since the REPL only dispatches what the table names.
    for (int id = 0; id <= static_cast<int>(ChatVerb::Exit); ++id) {
        bool found = false;
        for (const ChatCommandSpec& spec : chat_commands()) {
            found = found || static_cast<int>(spec.id) == id;
        }
        INFO("ChatVerb " << id);
        CHECK(found);
    }
}

TEST_CASE("/help lines up its descriptions", "[chat][completer]") {
    const std::vector<std::string> help = chat_help_lines();
    const std::size_t column = help.front().find("List these commands");
    REQUIRE(column != std::string::npos);
    for (std::size_t i = 0; i < help.size(); ++i) {
        const ChatCommandSpec& spec = chat_commands()[i];
        CHECK(help[i].find(std::string{spec.description}) == column);
    }
    CHECK(help[1] == "  /model [backend|model]" + std::string(column - 24, ' ') +
                         "Show the backend answering, or switch: a backend, a roster model, or "
                         "backend:model");
}

// --- commands ----------------------------------------------------------------

TEST_CASE("a partial command narrows, and a command taking an argument gains a space",
          "[chat][completer]") {
    const Suggestions mo = suggest_chat_input("/mo", project());
    CHECK(mo.from == 0);
    CHECK(texts(mo) == std::vector<std::string>{"/model ", "/models"});
    CHECK(labels(mo) == std::vector<std::string>{"/model", "/models"});

    CHECK(texts(suggest_chat_input("/re", project())) ==
          std::vector<std::string>{"/retriever ", "/rerank ", "/recall ", "/revoke "});
    CHECK(suggest_chat_input("/zzz", project()).candidates.empty());
}

TEST_CASE("a path at the start of a line is not a command", "[chat][completer]") {
    // parse_slash's rule: `/usr/bin/env is a path` is a question.
    CHECK(suggest_chat_input("/usr/bin", project()).candidates.empty());
}

TEST_CASE("a command's own values follow it", "[chat][completer]") {
    const Suggestions model = suggest_chat_input("/model ", project());
    CHECK(model.from == 7);
    CHECK(texts(model) == std::vector<std::string>{"claude", "local", "mock"});
    CHECK(model.candidates[0].description == "anthropic · claude-sonnet-5");

    const Suggestions narrowed = suggest_chat_input("/model  cl", project());
    CHECK(narrowed.from == 8);
    CHECK(texts(narrowed) == std::vector<std::string>{"claude"});

    std::vector<std::string> retrievers;
    for (const std::string_view name : apogee::agentloop::retriever_names()) {
        retrievers.emplace_back(name);
    }
    const Suggestions retriever = suggest_chat_input("/retriever ", project());
    CHECK(texts(retriever) == retrievers);
    for (const auto& candidate : retriever.candidates) {
        CHECK_FALSE(candidate.description.empty());
    }

    CHECK(texts(suggest_chat_input("/rerank ", project())) ==
          std::vector<std::string>{"off", "on", "auto", "claude", "local", "mock"});
    CHECK(texts(suggest_chat_input("/think ", project())) ==
          std::vector<std::string>{"on", "off", "auto"});
    CHECK(texts(suggest_chat_input("/think o", project())) ==
          std::vector<std::string>{"on", "off"});
    CHECK(texts(suggest_chat_input("/recall ", project())) ==
          std::vector<std::string>{"on", "off"});
    // The gated tools for /allow and /deny, the session's answers for /revoke
    // (26o), from the live chat.
    apogee::commands::ChatCompletionSources gated = project();
    gated.gated_tools = [] { return std::vector<std::string>{"run_command", "write_file"}; };
    gated.session_permissions = [] { return std::vector<std::string>{"write_file"}; };
    CHECK(texts(suggest_chat_input("/allow ", gated)) ==
          std::vector<std::string>{"run_command", "write_file"});
    CHECK(texts(suggest_chat_input("/deny w", gated)) == std::vector<std::string>{"write_file"});
    CHECK(texts(suggest_chat_input("/revoke ", gated)) == std::vector<std::string>{"write_file"});

    std::vector<std::string> statuses;
    for (const std::string_view status : apogee::knowledge::valid_statuses()) {
        statuses.emplace_back(status);
    }
    CHECK(texts(suggest_chat_input("/capture ", project())) == statuses);
}

TEST_CASE("an argument with no values, or past its one word, offers nothing", "[chat][completer]") {
    CHECK(suggest_chat_input("/system ", project()).candidates.empty());
    CHECK(suggest_chat_input("/model claude extra", project()).candidates.empty());
    CHECK(suggest_chat_input("/nonsense ", project()).candidates.empty());
}

TEST_CASE("backend names no longer complete in the middle of a message", "[chat][completer]") {
    CHECK(suggest_chat_input("tell me about cl", project()).candidates.empty());
    CHECK(suggest_chat_input("mo", project()).candidates.empty());
    CHECK(suggest_chat_input("", project()).candidates.empty());
    // `/` suggests only at column 0, where a command is a command.
    CHECK(suggest_chat_input("see /mo", project()).candidates.empty());
}

// --- @ paths -----------------------------------------------------------------

TEST_CASE("@ lists the working directory, folders marked and hidden ones left out",
          "[chat][completer]") {
    const Suggestions at = suggest_chat_input("@", project());
    CHECK(at.from == 0);
    CHECK(texts(at) == std::vector<std::string>{"@lib/", "@library/", "@\"my file.pdf\"",
                                                "@\"my folder/", "@notes.txt", "@README.md",
                                                "@src/"});
}

TEST_CASE("a leading dot shows hidden entries", "[chat][completer]") {
    CHECK(texts(suggest_chat_input("@.", project())) == std::vector<std::string>{"@.git/"});
}

TEST_CASE("@ descends into a folder", "[chat][completer]") {
    CHECK(texts(suggest_chat_input("@src/", project())) ==
          std::vector<std::string>{"@src/commands/", "@src/common/", "@src/main.cpp"});
    CHECK(texts(suggest_chat_input("@src/comma", project())) ==
          std::vector<std::string>{"@src/commands/"});
}

TEST_CASE("an @ mid-line completes from where it starts", "[chat][completer]") {
    const Suggestions mid = suggest_chat_input("summarize @li", project());
    CHECK(mid.from == 10);
    CHECK(texts(mid) == std::vector<std::string>{"@lib/", "@library/"});

    // Inside a command's argument too.
    CHECK(suggest_chat_input("/system read @no", project()).from == 13);
}

TEST_CASE("an @ that is not starting a word, or a lone one before a space, offers nothing",
          "[chat][completer]") {
    CHECK(suggest_chat_input("mail me@li", project()).candidates.empty());
    CHECK(suggest_chat_input("look @ ", project()).candidates.empty());
    CHECK(suggest_chat_input("@lib/ and ", project()).candidates.empty());
}

TEST_CASE("names match ignoring case unless the prefix has a capital", "[chat][completer]") {
    CHECK(texts(suggest_chat_input("@read", project())) == std::vector<std::string>{"@README.md"});
    CHECK(texts(suggest_chat_input("@READ", project())) == std::vector<std::string>{"@README.md"});
    CHECK(suggest_chat_input("@Read", project()).candidates.empty());
}

TEST_CASE("a path with a space completes quoted", "[chat][completer]") {
    const Suggestions my = suggest_chat_input("@my", project());
    REQUIRE(my.candidates.size() == 2);
    // A file's quote is closed; a folder's stays open to go on into it.
    CHECK(my.candidates[0].text == "@\"my file.pdf\"");
    CHECK(my.candidates[1].text == "@\"my folder/");
    // The row shows what was typed, then the rest of the name.
    CHECK(my.candidates[0].label == "@my file.pdf");

    const Suggestions quoted = suggest_chat_input("@\"my f", project());
    CHECK(quoted.candidates[0].label == "@\"my file.pdf");

    // A quote runs across the space to the cursor, into the folder.
    const Suggestions inside = suggest_chat_input("see @\"my folder/", project());
    CHECK(inside.from == 4);
    CHECK(texts(inside) == std::vector<std::string>{"@\"my folder/inside.txt\""});
}

TEST_CASE("a closed quote ends the word", "[chat][completer]") {
    CHECK(suggest_chat_input("@\"my file.pdf\" ", project()).candidates.empty());
    CHECK(suggest_chat_input("@\"my file.pdf\" @no", project()).from == 15);
}

TEST_CASE("the real lister reads a directory and names its folders", "[chat][completer]") {
    const apogee::testing::TempDir dir{"completer"};
    std::filesystem::create_directory(dir.path() / "folder");
    std::ofstream{dir.path() / "file.txt"} << "x";

    auto entries = apogee::commands::list_directory(dir.path());
    std::sort(entries.begin(), entries.end(),
              [](const DirectoryEntry& a, const DirectoryEntry& b) { return a.name < b.name; });
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "file.txt");
    CHECK_FALSE(entries[0].directory);
    CHECK(entries[1].name == "folder");
    CHECK(entries[1].directory);

    // Unreadable or missing lists as nothing, never a throw.
    CHECK(apogee::commands::list_directory(dir.path() / "missing").empty());
}

TEST_CASE("the sources describe each backend by type and model", "[chat][completer]") {
    const apogee::harness::Config config = apogee::harness::parse_config(R"(
backends:
  claude:
    type: anthropic
    model: claude-sonnet-5
  local:
    type: llamacpp
    model_path: /models/qwen.gguf
  test:
    type: mock
)",
                                                                         "test");
    // A home of the test's own: the sources read the roster cache (33).
    const apogee::testing::TempDir home{"sources-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const ChatCompletionSources sources =
        apogee::commands::chat_completion_sources(config, "/work");
    REQUIRE(sources.backends.size() == 3);
    std::map<std::string, std::string> described;
    for (const auto& backend : sources.backends) {
        described[backend.name] = backend.description;
    }
    CHECK(described["claude"] == "anthropic · claude-sonnet-5");
    CHECK(described["local"] == "llamacpp · qwen.gguf");
    CHECK(described["test"] == "mock");
    CHECK(sources.working_directory == "/work");
    CHECK(static_cast<bool>(sources.list));
}

TEST_CASE("/help wraps to the terminal, short of its last column", "[chat][completer]") {
    // From just wider than the longest usage, `/capture [status|link]`:
    // narrower than that, no layout keeps a command on one row.
    for (std::size_t width = 26; width <= 120; ++width) {
        const std::vector<std::string> help = chat_help_lines(width);
        std::string joined;
        for (const std::string& line : help) {
            INFO("width " << width << ": '" << line << "'");
            CHECK(apogee::ansi::display_width(line) <= width - 1);
            joined += line + "\n";
        }
        // Every command and every word of its description survives the wrap.
        for (const ChatCommandSpec& spec : chat_commands()) {
            CHECK(joined.find("/" + std::string{spec.verb}) != std::string::npos);
        }
    }
    // Wide enough: descriptions beside the usages, continuing under their
    // own column.
    const std::vector<std::string> sixty = chat_help_lines(60);
    const std::size_t column = sixty.front().find("List these commands");
    for (const std::string& line : sixty) {
        if (line.find_first_not_of(' ') == column) {
            continue;  // a continuation row
        }
        CHECK(line.starts_with("  /"));
    }
    // Too narrow for a column: each description on its own rows beneath.
    const std::vector<std::string> narrow = chat_help_lines(30);
    CHECK(narrow[0] == "  /help");
    CHECK(narrow[1] == "    List these commands");
}

TEST_CASE("/suite completes the configured suites and off", "[chat][completer][suites]") {
    // 27d: the bundle switch is completable from the one table, as /model is.
    ChatCompletionSources sources = project();
    sources.suites = {{"fast", "All small"}, {"research", "Deep work"}};
    const Suggestions all = suggest_chat_input("/suite ", sources);
    CHECK(texts(all) == std::vector<std::string>{"fast", "research", "off"});
    CHECK(all.candidates[1].description == "Deep work");
    CHECK(texts(suggest_chat_input("/suite r", sources)) == std::vector<std::string>{"research"});
    CHECK(texts(suggest_chat_input("/suite o", sources)) == std::vector<std::string>{"off"});
    // No suites configured: off alone, never an empty list that reads as broken.
    CHECK(texts(suggest_chat_input("/suite ", project())) == std::vector<std::string>{"off"});
    REQUIRE(find_chat_command("suite") != nullptr);
    CHECK(find_chat_command("suite")->id == ChatVerb::Suite);

    // The sources read them from the config, described by what they are for.
    const apogee::harness::Config config = apogee::harness::parse_config(
        "backends:\n  a:\n    type: mock\nsuites:\n  fast:\n    description: All small\n"
        "    members:\n      chat: a\n",
        "<test>");
    const ChatCompletionSources from_config =
        apogee::commands::chat_completion_sources(config, "/work");
    REQUIRE(from_config.suites.size() == 1);
    CHECK(from_config.suites.front().name == "fast");
    CHECK(from_config.suites.front().description == "All small");
}

TEST_CASE("/suite completes its switches after the name, each one /suite reads",
          "[chat][completer][suites]") {
    // 27e: `--force` and `--warm` are parsed after the name; Tab offers them
    // there, each once, and a switch takes no value of its own.
    ChatCompletionSources sources = project();
    sources.suites = {{"fast", "All small"}};
    const Suggestions after_name = suggest_chat_input("/suite fast ", sources);
    CHECK(after_name.from == 12);
    CHECK(texts(after_name) == std::vector<std::string>{"--force", "--warm"});
    for (const auto& candidate : after_name.candidates) {
        CHECK_FALSE(candidate.description.empty());
    }
    CHECK(texts(suggest_chat_input("/suite fast --w", sources)) ==
          std::vector<std::string>{"--warm"});
    CHECK(texts(suggest_chat_input("/suite fast --warm ", sources)) ==
          std::vector<std::string>{"--force"});
    CHECK(suggest_chat_input("/suite fast --warm --force ", sources).candidates.empty());
    CHECK(suggest_chat_input("/suite fast x", sources).candidates.empty());
    // `off` loads and admits nothing, and /suite refuses `--warm` with it.
    CHECK(suggest_chat_input("/suite off ", sources).candidates.empty());
    // The name itself completes as before.
    CHECK(texts(suggest_chat_input("/suite f", sources)) == std::vector<std::string>{"fast"});

    // Each switch the table offers is one /suite reads, alone and together.
    const ChatCommandSpec* suite = find_chat_command("suite");
    REQUIRE(suite != nullptr);
    std::string line = "fast";
    for (const apogee::commands::ChatFlagSpec& flag : suite->flags) {
        CHECK(flag.values == apogee::commands::ArgumentValues::None);
        CHECK(apogee::commands::parse_suite_argument("fast --" + std::string{flag.name})
                  .error.empty());
        line += " --" + std::string{flag.name};
    }
    const apogee::commands::SuiteArgument all = apogee::commands::parse_suite_argument(line);
    CHECK(all.error.empty());
    CHECK(all.force);
    CHECK(all.warm);
}

TEST_CASE("/attach completes its flag and the flag's values after the path, from the one table",
          "[chat][completer][attachments]") {
    // 27p: Tab after `/attach <path> ` offers `--graph=`, then its values.
    const Suggestions after_path = suggest_chat_input("/attach src ", project());
    CHECK(after_path.from == 12);
    CHECK(texts(after_path) == std::vector<std::string>{"--graph="});
    REQUIRE(after_path.candidates.size() == 1);
    CHECK_FALSE(after_path.candidates[0].description.empty());
    // A word starting `-` narrows to it; one that does not offers none.
    CHECK(texts(suggest_chat_input("/attach src -", project())) ==
          std::vector<std::string>{"--graph="});
    CHECK(texts(suggest_chat_input("/attach src --g", project())) ==
          std::vector<std::string>{"--graph="});
    CHECK(suggest_chat_input("/attach src --x", project()).candidates.empty());

    // The values, after `=` -- the whole word replaced -- or as the next word.
    const Suggestions values = suggest_chat_input("/attach src --graph=", project());
    CHECK(values.from == 12);
    CHECK(texts(values) == std::vector<std::string>{"--graph=code", "--graph=off"});
    for (const auto& candidate : values.candidates) {
        CHECK_FALSE(candidate.description.empty());
    }
    CHECK(texts(suggest_chat_input("/attach src --graph=o", project())) ==
          std::vector<std::string>{"--graph=off"});
    const Suggestions spaced = suggest_chat_input("/attach src --graph ", project());
    CHECK(spaced.from == 20);
    CHECK(texts(spaced) == std::vector<std::string>{"code", "off"});
    CHECK(texts(suggest_chat_input("/attach src --graph c", project())) ==
          std::vector<std::string>{"code"});
    // Given once, it is not offered again.
    CHECK(suggest_chat_input("/attach src --graph=off ", project()).candidates.empty());
    CHECK(suggest_chat_input("/attach src --graph off ", project()).candidates.empty());

    // A quoted path ends at its quote; an open one keeps going until a `-`.
    CHECK(texts(suggest_chat_input("/attach \"my folder\" ", project())) ==
          std::vector<std::string>{"--graph="});
    CHECK(texts(suggest_chat_input("/attach \"my folder/ -", project())) ==
          std::vector<std::string>{"--graph="});
    CHECK(texts(suggest_chat_input("/attach \"my folder/", project())) ==
          std::vector<std::string>{"\"my folder/inside.txt\""});
    // In an open quote a space is the name's; right after a closing quote,
    // nothing -- a flag glued to it would not read.
    CHECK(texts(suggest_chat_input("/attach \"my ", project())) ==
          std::vector<std::string>{"\"my file.pdf\"", "\"my folder/"});
    CHECK(suggest_chat_input("/attach \"my folder\"", project()).candidates.empty());

    // While the cursor is in the path, paths -- an unquoted one with a space
    // included, as before.
    CHECK(texts(suggest_chat_input("/attach s", project())) == std::vector<std::string>{"src/"});
    CHECK(texts(suggest_chat_input("/attach my f", project())) ==
          std::vector<std::string>{"\"my file.pdf\"", "\"my folder/"});

    // The flag the table offers is the flag /attach reads, and no other.
    const ChatCommandSpec* attach = find_chat_command("attach");
    REQUIRE(attach != nullptr);
    REQUIRE(attach->flags.size() == 1);
    for (const apogee::commands::ChatFlagSpec& flag : attach->flags) {
        for (const std::string_view value : apogee::harness::attachment_graph_method_names()) {
            const auto read = apogee::commands::parse_attach_argument(
                "src --" + std::string{flag.name} + "=" + std::string{value});
            CHECK(read.error.empty());
            CHECK(read.graph.has_value());
        }
    }
    CHECK_FALSE(apogee::commands::parse_attach_argument("src --other=x").error.empty());
    // /help names it beside the path.
    bool named = false;
    for (const std::string& line : chat_help_lines()) {
        named = named || (line.find("/attach <path>") != std::string::npos &&
                          line.find("--graph=code|off") != std::string::npos);
    }
    CHECK(named);
    // Other commands' arguments are as they were: no flags, no rows.
    CHECK(suggest_chat_input("/detach x ", project()).candidates.empty());
}

TEST_CASE(
    "/model completes a configured provider's roster beside the backends, and /rerank does not",
    "[chat][completer][roster]") {
    ChatCompletionSources sources = project();
    sources.roster_models = {{"claude-opus-5-5", "anthropic's roster"},
                             {"claude", "anthropic's roster"},
                             {"claude-sonnet-5-5", "anthropic's roster"}};
    const Suggestions model = suggest_chat_input("/model ", sources);
    // The backends first, then the roster -- a name already a backend once.
    CHECK(texts(model) == std::vector<std::string>{"claude", "local", "mock", "claude-opus-5-5",
                                                   "claude-sonnet-5-5"});
    CHECK(model.candidates[0].description == "anthropic · claude-sonnet-5");
    CHECK(model.candidates[3].description == "anthropic's roster");
    CHECK(texts(suggest_chat_input("/model claude-s", sources)) ==
          std::vector<std::string>{"claude-sonnet-5-5"});
    // A reranker is a configured entry's job, never a roster pin's.
    CHECK(texts(suggest_chat_input("/rerank ", sources)) ==
          std::vector<std::string>{"off", "on", "auto", "claude", "local", "mock"});
}

TEST_CASE(
    "the sources read the configured types' rosters from the cache, and a cold cache offers none",
    "[chat][completer][roster]") {
    const apogee::testing::TempDir home{"rosters-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const apogee::harness::Config config = apogee::harness::parse_config(R"(
backends:
  claude:
    type: anthropic
    model: claude-sonnet-5
  test:
    type: mock
)",
                                                                         "test");
    CHECK(apogee::commands::chat_completion_sources(config, "/work").roster_models.empty());

    apogee::backends::RosterCache cache;
    cache.rosters["anthropic"] = apogee::backends::ProviderRoster{
        {{"claude-opus-5-5", "Opus"}, {"claude-sonnet-5-5", "Sonnet"}}, "2026-10-09"};
    cache.rosters["openai"] = apogee::backends::ProviderRoster{{{"gpt-5.2", "GPT"}}, "2026-10-09"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());
    const ChatCompletionSources sources =
        apogee::commands::chat_completion_sources(config, "/work");
    std::vector<std::string> names;
    for (const auto& model : sources.roster_models) {
        names.push_back(model.name);
        CHECK(model.description == "anthropic's roster");
    }
    // OpenAI is not configured: its roster offers nothing.
    CHECK(names == std::vector<std::string>{"claude-opus-5-5", "claude-sonnet-5-5"});
}

TEST_CASE(
    "/model <backend>: completes that entry's roster, and a colon after anything else is the "
    "name's",
    "[chat][completer][pins]") {
    ChatCompletionSources sources = project();
    sources.roster_models = {{"gpt-5.5", "codex-cli's roster"},
                             {"kimi-k3:cloud", "codex-cli's roster"}};
    sources.backend_rosters["codex"] = sources.roster_models;
    CHECK(texts(suggest_chat_input("/model codex:", sources)) ==
          std::vector<std::string>{"codex:gpt-5.5", "codex:kimi-k3:cloud"});
    const Suggestions narrowed = suggest_chat_input("/model codex:k", sources);
    CHECK(narrowed.from == 7);
    CHECK(texts(narrowed) == std::vector<std::string>{"codex:kimi-k3:cloud"});
    CHECK(narrowed.candidates.front().description == "codex-cli's roster");
    // An entry with no roster offers nothing after its colon, never a guess.
    CHECK(suggest_chat_input("/model claude:", sources).candidates.empty());
    // A roster id's own colon is not a backend's.
    CHECK(texts(suggest_chat_input("/model kimi-k3:", sources)) ==
          std::vector<std::string>{"kimi-k3:cloud"});
}

TEST_CASE("the sources carry each entry's roster by its key", "[chat][completer][pins]") {
    const apogee::testing::TempDir home{"by-key-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const apogee::harness::Config config = apogee::harness::parse_config(R"(
backends:
  codex:
    type: codex-cli
  work:
    type: codex-cli
  local-ollama:
    type: ollama-cli
    model: llama3
)",
                                                                         "test");
    apogee::backends::RosterCache cache;
    cache.rosters["codex-cli"] =
        apogee::backends::ProviderRoster{{{"gpt-5.5", "GPT-5.5"}}, "2026-10-10"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());
    const ChatCompletionSources sources =
        apogee::commands::chat_completion_sources(config, "/work");
    REQUIRE(sources.backend_rosters.size() == 2);
    CHECK(sources.backend_rosters.at("codex").front().name == "gpt-5.5");
    CHECK(sources.backend_rosters.at("work").front().name == "gpt-5.5");
    CHECK_FALSE(sources.backend_rosters.contains("local-ollama"));
}
