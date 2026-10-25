#include "cli/chat_history.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cli/complete_protocol.h"
#include "cli/complete_sources.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "logger/session.h"
#include "support/cli_home.h"

/// `apogee chats show` (M27): a saved conversation's turns, read through the
/// loader a resume reads it with -- the text a person reads, the document a
/// host draws, and the turn numbers a resumed machine session carries on
/// from.
namespace {

using apogee::harness::ChatMessage;
using apogee::harness::ContentPart;
using apogee::harness::MessageContent;
using apogee::harness::ToolCall;
using apogee::harness::ToolResult;
using apogee::testing::CliHome;

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
    model: mock-1

models:
  default: local
)";

constexpr const char* kChatId = "20261022-120000-abcd";

/// A PNG's first bytes, base64: what a served client's image leaves in a
/// saved message, and what no read may print.
constexpr const char* kImage = "iVBORw0KGgoAAAANSUhEUg==";

/// The saved chat every case reads, written through the logger's own save: a
/// compaction (the turns through 2 folded, the answer it kept), a plain turn,
/// a turn with a tool call and its result, and a turn with an attachment --
/// the chat's, inlined on its message, and an image the message carries.
void save_fixture() {
    apogee::logger::Session session;
    session.chat_id = kChatId;
    session.title = "Project setup";
    session.backend = "local";
    session.turns = 5;
    session.compactions = 1;
    session.params.system_prompt = "You are terse.";
    ChatMessage calls = ChatMessage::assistant("");
    calls.tool_calls = {ToolCall{"c1", "read_file", R"({"path": "notes.md"})"}};
    session.messages = {
        ChatMessage::system("You are terse."),
        ChatMessage::system(std::string{apogee::logger::kCompactionPreamble} +
                            "They set up the project.\nThey chose Postgres."),
        ChatMessage::assistant("Postgres it is."),
        ChatMessage::user("Which port?"),
        ChatMessage::assistant("5432."),
        ChatMessage::user("What is in notes.md?"),
        calls,
        ChatMessage::from_tool_result(ToolResult{"c1", "read_file", "line one\nline two\n", false}),
        ChatMessage::assistant("It holds two lines."),
        ChatMessage::user(MessageContent::from_parts(
            {ContentPart::from_text("Summarise the report and this chart."),
             ContentPart::from_image_url(std::string{"data:image/png;base64,"} + kImage)})),
        ChatMessage::assistant("The report covers Q3."),
    };
    session.attachments = {apogee::logger::Attachment{.name = "report.pdf",
                                                      .files = {{.name = "report.pdf",
                                                                 .path = "/work/report.pdf",
                                                                 .sha256 = "ab12",
                                                                 .reader = "pdftotext",
                                                                 .bytes = 9000}},
                                                      .inline_at = 9,
                                                      .map_at = {},
                                                      .graph = {}}};
    apogee::logger::save(session);
}

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

/// `apogee <args>` in this home, with `input` on stdin.
[[nodiscard]] Run run(const CliHome& home, const std::vector<std::string>& args,
                      const std::string& input = {}) {
    Run result;
    std::istringstream fed{input};
    std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
    try {
        result.code = home.run(args, &result.out, &result.err);
    } catch (...) {
        std::cin.rdbuf(old_in);
        std::cin.clear();
        throw;
    }
    std::cin.rdbuf(old_in);
    std::cin.clear();
    return result;
}

/// stdout is one JSON document on one line, and nothing else.
[[nodiscard]] nlohmann::json document(const Run& read) {
    INFO(read.out << read.err);
    REQUIRE(read.code == 0);
    REQUIRE(!read.out.empty());
    CHECK(read.out.back() == '\n');
    CHECK(std::ranges::count(read.out, '\n') == 1);
    const nlohmann::json parsed = nlohmann::json::parse(read.out, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    return parsed;
}

/// The `session` event a driven chat resumed on `id` opens with -- stdin
/// closed at once, so it reads no turn.
[[nodiscard]] nlohmann::json resumed_session(const CliHome& home, const std::string& id) {
    const Run resumed = run(home, {"chat", "--resume", id, "--input-format", "stream-json",
                                   "--output-format", "stream-json"});
    INFO(resumed.err);
    REQUIRE(resumed.code == 0);
    const nlohmann::json first =
        nlohmann::json::parse(resumed.out.substr(0, resumed.out.find('\n')), nullptr, false);
    REQUIRE_FALSE(first.is_discarded());
    REQUIRE(first.at("type") == "session");
    return first;
}

}  // namespace

TEST_CASE("chats show prints a saved chat's turns as the chat drew them, plainly",
          "[chats][show][history]") {
    const CliHome home{kConfig};
    save_fixture();
    const Run shown = run(home, {"chats", "show", kChatId});
    INFO(shown.err);
    REQUIRE(shown.code == 0);
    CHECK(shown.out ==
          "Project setup -- 20261022-120000-abcd, on local\n"
          "attached to the chat: report.pdf (1 file, 8 KiB)\n"
          "\n"
          "turn 2 -- history compacted; what came before is this summary:\n"
          "  They set up the project.\n"
          "  They chose Postgres.\n"
          "Postgres it is.\n"
          "\n"
          "turn 3\n"
          "You: Which port?\n"
          "5432.\n"
          "\n"
          "turn 4\n"
          "You: What is in notes.md?\n"
          "[tool] read_file {\"path\":\"notes.md\"} -- returned 18 B\n"
          "It holds two lines.\n"
          "\n"
          "turn 5\n"
          "You: Summarise the report and this chart.\n"
          "attached: image/png (1 file, 16 B), report.pdf (1 file, 8 KiB)\n"
          "The report covers Q3.\n");
    // The system prompt is a setting; the image is named, never its bytes;
    // the tool's result is sized, never printed.
    CHECK(shown.out.find("You are terse.") == std::string::npos);
    CHECK(shown.out.find(kImage) == std::string::npos);
    CHECK(shown.out.find("line one") == std::string::npos);
    // By its name as well as its id, as `chats info` takes either.
    CHECK(run(home, {"chats", "show", "Project setup"}).out == shown.out);
}

TEST_CASE("chats show --output-format json carries the same turns typed",
          "[chats][show][history][reads][json]") {
    const CliHome home{kConfig};
    save_fixture();
    const nlohmann::json shown =
        document(run(home, {"chats", "show", kChatId, "--output-format", "json"}));
    CHECK(shown == nlohmann::json::parse(R"({
        "object": "chat",
        "id": "20261022-120000-abcd",
        "name": "Project setup",
        "title": "Project setup",
        "backend": "local",
        "model": "",
        "attachments": [{"name": "report.pdf", "files": 1, "bytes": 9000}],
        "turns": [
            {"turn": 2,
             "user": {"text": "", "attachments": []},
             "answer": {"text": "Postgres it is.", "tools": []},
             "answered": true,
             "compacted": {"summary": "They set up the project.\nThey chose Postgres."}},
            {"turn": 3,
             "user": {"text": "Which port?", "attachments": []},
             "answer": {"text": "5432.", "tools": []},
             "answered": true},
            {"turn": 4,
             "user": {"text": "What is in notes.md?", "attachments": []},
             "answer": {"text": "It holds two lines.",
                        "tools": [{"name": "read_file", "summary": "{\"path\":\"notes.md\"}",
                                   "result_bytes": 18}]},
             "answered": true},
            {"turn": 5,
             "user": {"text": "Summarise the report and this chart.",
                      "attachments": [{"name": "image/png", "files": 1, "bytes": 16},
                                      {"name": "report.pdf", "files": 1, "bytes": 9000}]},
             "answer": {"text": "The report covers Q3.", "tools": []},
             "answered": true}
        ],
        "warnings": []
    })"));
    // The chat's facts as `chats list`'s row says them.
    const nlohmann::json row =
        document(run(home, {"chats", "list", "--output-format", "json"})).at("data").at(0);
    for (const char* key : {"id", "name", "backend", "model"}) {
        CHECK(shown.at(key) == row.at(key));
    }
    const std::string dumped = shown.dump();
    CHECK(dumped.find(kImage) == std::string::npos);
    CHECK(dumped.find("/work/") == std::string::npos);
}

TEST_CASE("a resumed machine session's next turn follows the last turn chats show lists",
          "[chats][show][history][machine][turns]") {
    const CliHome home{kConfig};
    save_fixture();
    const nlohmann::json before =
        document(run(home, {"chats", "show", kChatId, "--output-format", "json"}));
    const nlohmann::json session = resumed_session(home, kChatId);
    CHECK(session.at("next_turn") == before.at("turns").back().at("turn").get<int>() + 1);
    // A resume that reads no line changes no turn.
    CHECK(document(run(home, {"chats", "show", kChatId, "--output-format", "json"})) == before);
}

TEST_CASE("a chat driven through turns, a compaction and an attach reads back as it went",
          "[chats][show][history][machine][turns][cleanliness]") {
    // The real path end to end: a driven chat on scripted models -- the
    // first answer reasoned first, the utility model titling and compacting
    // -- then the read, then a resume.
    const CliHome home{kConfig};
    const std::filesystem::path root = home.home();
    const auto script = [&root](const std::string& name, const nlohmann::json& turns) {
        const std::filesystem::path path = root / (name + ".json");
        std::ofstream{path, std::ios::binary} << nlohmann::json{{"turns", turns}}.dump();
        return path.string();
    };
    const nlohmann::json chatty =
        nlohmann::json::array({{{"text", "first answer"}, {"reasoning", "pondering in private"}},
                               {{"text", "second answer"}},
                               {{"text", "third answer"}}});
    const nlohmann::json helper =
        nlohmann::json::array({{{"text", "Helper Title"}}, {{"text", "what was said, in short"}}});
    std::ofstream{home.config_path(), std::ios::binary}
        << "models:\n  default: chatty\n  default_utility: helper\nbackends:\n"
        << "  chatty:\n    type: mock\n    context_size: 8000\n    model_path: "
        << script("chatty", chatty) << "\n"
        << "  helper:\n    type: mock\n    model_path: " << script("helper", helper) << "\n";
    const std::filesystem::path notes = root / "notes.md";
    std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
    const std::string input = std::string{R"({"type":"user","text":"first question"})"} + "\n" +
                              R"({"type":"user","text":"second question"})" + "\n" +
                              R"({"type":"compact"})" + "\n" + R"({"type":"attach","path":")" +
                              notes.generic_string() + "\"}\n" +
                              R"({"type":"user","text":"what is the code?"})" + "\n";
    const Run driven = run(
        home, {"chat", "--input-format", "stream-json", "--output-format", "stream-json"}, input);
    INFO(driven.out << driven.err);
    REQUIRE(driven.code == 0);
    // The reasoning streamed, and the attachment's text went to the model
    // whole, beside the message it rode.
    REQUIRE(driven.out.find(R"("type":"thinking_delta")") != std::string::npos);
    REQUIRE(driven.out.find("-- inlined whole") != std::string::npos);
    REQUIRE(driven.out.find("history compacted by helper") != std::string::npos);
    const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    const std::string id = saved.front().chat_id;

    const nlohmann::json shown =
        document(run(home, {"chats", "show", id, "--output-format", "json"}));
    CHECK(shown.at("name") == "Helper Title");
    CHECK(shown.at("backend") == "chatty");
    // Named as the attach line gave it, and sized.
    const nlohmann::json attached =
        nlohmann::json::array({{{"name", notes.generic_string()}, {"files", 1}, {"bytes", 25}}});
    CHECK(shown.at("attachments") == attached);
    nlohmann::json turns = nlohmann::json::parse(R"([
        {"turn": 2,
         "user": {"text": "", "attachments": []},
         "answer": {"text": "second answer", "tools": []},
         "answered": true,
         "compacted": {"summary": "what was said, in short"}},
        {"turn": 3,
         "user": {"text": "what is the code?", "attachments": []},
         "answer": {"text": "third answer", "tools": []},
         "answered": true}
    ])");
    turns[1]["user"]["attachments"] = attached;
    CHECK(shown.at("turns") == turns);
    // Nothing the transcript never held: no thinking, no injected text.
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{"chats", "show", id},
          std::vector<std::string>{"chats", "show", id, "--output-format", "json"}}) {
        const Run read = run(home, args);
        CHECK(read.out.find("pondering") == std::string::npos);
        CHECK(read.out.find("7731") == std::string::npos);
    }
    // And the resumed session numbers on from the last turn listed.
    CHECK(resumed_session(home, id).at("next_turn") == 4);
}

namespace {

/// A session file written as an older Apogee, or another writer, left it:
/// `record`'s keys as given, nothing stamped.
void write_raw(const std::string& id, const nlohmann::json& record) {
    std::filesystem::create_directories(apogee::logger::sessions_dir());
    std::ofstream{apogee::logger::session_path(id), std::ios::binary} << record.dump(2);
}

/// Each `turn` of a `chats show` document, in order.
[[nodiscard]] std::vector<int> turn_numbers(const nlohmann::json& shown) {
    std::vector<int> out;
    for (const nlohmann::json& entry : shown.at("turns")) {
        out.push_back(entry.at("turn").get<int>());
    }
    return out;
}

}  // namespace

TEST_CASE("chats show's document says what the loader could not read, in its warnings",
          "[chats][show][history][reads][json][record-truth]") {
    // 55b: the gap was said on stderr alone, even under --output-format json,
    // and a host keeping only the document drew a degraded chat as whole.
    const CliHome home{kConfig};
    write_raw("20261022-140000-bad0", nlohmann::json::parse(R"({
        "schema_version": 2, "chat_id": "20261022-140000-bad0", "backend": "local", "turns": 1,
        "messages": [{"role": "user", "content": "q1"}, {"role": "assistant", "content": "a1"},
                     {"role": "nonsense", "content": 42}]})"));
    const Run json =
        run(home, {"chats", "show", "20261022-140000-bad0", "--output-format", "json"});
    const nlohmann::json shown = document(json);
    CHECK(shown.at("warnings") == nlohmann::json::parse(R"([{"kind": "field_dropped",
                                     "message": "a message could not be read and was skipped"}])"));
    CHECK(json.err.empty());
    CHECK(turn_numbers(shown) == std::vector<int>{1});
    // The text view says it on stderr, as it did.
    const Run text = run(home, {"chats", "show", "20261022-140000-bad0"});
    REQUIRE(text.code == 0);
    CHECK(text.err == "apogee chats: a message could not be read and was skipped\n");
    // A file read whole has none.
    save_fixture();
    CHECK(
        document(run(home, {"chats", "show", kChatId, "--output-format", "json"})).at("warnings") ==
        nlohmann::json::array());
}

TEST_CASE("a chat whose count is short of its answers numbers on from them when resumed",
          "[chats][show][history][machine][turns][record-truth]") {
    // 55b, the probe's fixture: two answered turns and no count. `chats show`
    // numbered them 1 and 2 while the resumed session announced next_turn 1,
    // and its live turn reused a number already drawn.
    const CliHome home{kConfig};
    write_raw("nocount", nlohmann::json::parse(R"({
        "schema_version": 2, "chat_id": "nocount", "backend": "local",
        "started_at": "2026-10-22T00:00:00Z", "updated_at": "2026-10-22T00:00:00Z",
        "messages": [{"role": "user", "content": "q1"}, {"role": "assistant", "content": "a1"},
                     {"role": "user", "content": "q2"}, {"role": "assistant", "content": "a2"}]})"));
    CHECK(turn_numbers(document(run(home, {"chats", "show", "nocount", "--output-format",
                                           "json"}))) == std::vector<int>{1, 2});
    CHECK(document(run(home, {"chats", "list", "--output-format", "json"}))
              .at("data")
              .at(0)
              .at("turns") == 2);
    CHECK(resumed_session(home, "nocount").at("next_turn") == 3);

    // A live turn takes the next number, and the read numbers it the same.
    const Run live = run(home,
                         {"chat", "--resume", "nocount", "--no-recall", "--input-format",
                          "stream-json", "--output-format", "stream-json"},
                         std::string{R"({"type":"user","text":"q3"})"} + "\n");
    INFO(live.err);
    REQUIRE(live.code == 0);
    std::vector<int> results;
    std::istringstream lines{live.out};
    for (std::string line; std::getline(lines, line);) {
        const nlohmann::json event = nlohmann::json::parse(line);
        if (event.at("type") == "result") {
            results.push_back(event.at("turn").get<int>());
        }
    }
    CHECK(results == std::vector<int>{3});
    CHECK(turn_numbers(document(run(home, {"chats", "show", "nocount", "--output-format",
                                           "json"}))) == std::vector<int>{1, 2, 3});
    CHECK(apogee::logger::load("nocount", {}).session.turns == 3);
}

TEST_CASE("chats show refuses a name no chat answers as chats info refuses it",
          "[chats][show][history]") {
    const CliHome home{kConfig};
    save_fixture();
    const Run info = run(home, {"chats", "info", "no-such-chat"});
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{"chats", "show", "no-such-chat"},
          std::vector<std::string>{"chats", "show", "no-such-chat", "--output-format", "json"}}) {
        const Run shown = run(home, args);
        CHECK(shown.code == info.code);
        CHECK(shown.code != 0);
        CHECK(shown.out.empty());
        CHECK(shown.err == info.err);
    }
    CHECK(info.err == "apogee chats: no chat named 'no-such-chat'\n");
    // A turn's format is refused by name, as every read refuses it.
    const Run stream = run(home, {"chats", "show", kChatId, "--output-format", "stream-json"});
    CHECK(stream.code != 0);
    CHECK(stream.err.find("expected 'text' or 'json'") != std::string::npos);
}

TEST_CASE("chats show completes the saved chats by id and name, and takes each it offers",
          "[chats][show][history][completion]") {
    const CliHome home{kConfig};
    save_fixture();
    apogee::logger::Session named;
    named.chat_id = "20261022-130000-ef01";
    named.custom_name = "standup";
    named.backend = "local";
    apogee::logger::save(named);
    const apogee::commands::RootCommand root{apogee::commands::default_registry()};
    const apogee::commands::Completion offered = apogee::commands::complete_words(
        {.words = {"chats", "show"}, .current = ""}, apogee::harness::parse_config(kConfig, "test"),
        apogee::commands::specs_from_app(root.app()),
        apogee::commands::default_completion_sources());
    // The store's chats as `chats info` offers them: each id, and a name
    // that is one word.
    const apogee::commands::Completion info = apogee::commands::complete_words(
        {.words = {"chats", "info"}, .current = ""}, apogee::harness::parse_config(kConfig, "test"),
        apogee::commands::specs_from_app(root.app()),
        apogee::commands::default_completion_sources());
    CHECK(offered.candidates == info.candidates);
    CHECK(std::ranges::find(offered.candidates, kChatId) != offered.candidates.end());
    CHECK(std::ranges::find(offered.candidates, "standup") != offered.candidates.end());
    // What it offers, it takes (ADR 0007).
    for (const std::string& candidate : offered.candidates) {
        INFO(candidate);
        CHECK(run(home, {"chats", "show", candidate}).code == 0);
    }
}
