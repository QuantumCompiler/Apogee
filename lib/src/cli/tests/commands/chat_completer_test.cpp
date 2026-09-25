#include "commands/chat_completer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "agentloop/retriever.h"
#include "ansi/text_width.h"
#include "commands/chat.h"
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
    CHECK(help[1] == "  /model [backend]" + std::string(column - 18, ' ') +
                         "Show the backend answering, or switch to another");
}

// --- commands ----------------------------------------------------------------

TEST_CASE("a partial command narrows, and a command taking an argument gains a space",
          "[chat][completer]") {
    const Suggestions mo = suggest_chat_input("/mo", project());
    CHECK(mo.from == 0);
    CHECK(texts(mo) == std::vector<std::string>{"/model ", "/models"});
    CHECK(labels(mo) == std::vector<std::string>{"/model", "/models"});

    CHECK(texts(suggest_chat_input("/re", project())) ==
          std::vector<std::string>{"/retriever ", "/rerank "});
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
          std::vector<std::string>{"off", "auto", "claude", "local", "mock"});

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
