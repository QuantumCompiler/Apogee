#include "acp/translate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/web_search.h"
#include "agentloop/member_call.h"
#include "agentloop/tool_selection.h"
#include "contracts/todo.h"
#include "machine/protocol.h"
#include "tools/toolsets.h"

/// The translator's tables and functions (56a): every machine event mapped
/// or dropped on purpose, every native tool classified, and each direction
/// of the mapping as the item decided it.
namespace {

using apogee::acp::Disposition;
using Catch::Matchers::ContainsSubstring;

}  // namespace

TEST_CASE("every machine event type is mapped or deliberately dropped, and no rule is stale",
          "[acp][translate]") {
    // The parity laws' idiom: a type machine mode grows without a rule here
    // fails, so ACP never silently loses what a driver is told.
    std::set<std::string_view> declared;
    for (const std::string_view type : apogee::commands::machine_event_types()) {
        declared.insert(type);
        INFO("no rule for machine event '" << type << "'");
        const apogee::acp::EventRule* rule = apogee::acp::event_rule(type);
        REQUIRE(rule != nullptr);
        CHECK_FALSE(rule->why.empty());
    }
    for (const apogee::acp::EventRule& rule : apogee::acp::event_rules()) {
        INFO("a rule for '" << rule.type << "', which machine mode never writes");
        CHECK(declared.contains(rule.type));
    }
    CHECK(apogee::acp::event_rule("a_type_from_later") == nullptr);
    CHECK(apogee::acp::event_rule("result")->disposition == Disposition::PromptEnd);
    CHECK(apogee::acp::event_rule("question")->disposition == Disposition::Question);
    CHECK(apogee::acp::event_rule("notice")->disposition == Disposition::Notice);
    CHECK(apogee::acp::event_rule("job")->disposition == Disposition::Dropped);
}

TEST_CASE("every native tool has a kind, and no kind names a tool that is gone",
          "[acp][translate][tools]") {
    // Held to the native registry, as the doc says: a new native tool is
    // classified here, or this fails.
    std::vector<std::string> native =
        apogee::tools::native_tool_names(apogee::tools::ToolsetOptions{});
    for (const std::string_view also :
         {apogee::agent::kFetchUrlToolName, apogee::agent::kWebSearchToolName,
          apogee::harness::kTodoToolName, apogee::agentloop::kConsultToolName,
          apogee::agentloop::kDelegateToolName}) {
        native.emplace_back(also);
    }
    const std::set<std::string_view> kinds{"read",    "edit",  "delete", "move",        "search",
                                           "execute", "think", "fetch",  "switch_mode", "other"};
    for (const std::string& tool : native) {
        INFO("no kind row for native tool '" << tool << "'");
        const auto rows = apogee::acp::tool_kinds();
        CHECK(std::ranges::find(rows, std::string_view{tool}, &apogee::acp::KindRow::tool) !=
              rows.end());
    }
    for (const apogee::acp::KindRow& row : apogee::acp::tool_kinds()) {
        INFO(row.tool);
        CHECK(std::ranges::find(native, std::string{row.tool}) != native.end());
        CHECK(kinds.contains(row.kind));
    }
    CHECK(apogee::acp::tool_kind("read_file") == "read");
    CHECK(apogee::acp::tool_kind("grep_files") == "search");
    CHECK(apogee::acp::tool_kind("write_file") == "edit");
    CHECK(apogee::acp::tool_kind("delete_file") == "delete");
    CHECK(apogee::acp::tool_kind("run_command") == "execute");
    CHECK(apogee::acp::tool_kind("fetch_url") == "fetch");
    CHECK(apogee::acp::tool_kind("mcp__github__list_issues") == "other");
}

TEST_CASE("the end of a turn, a todo's states and a call's states map as decided",
          "[acp][translate]") {
    CHECK(apogee::acp::stop_reason("stop") == "end_turn");
    CHECK(apogee::acp::stop_reason("length") == "max_tokens");
    CHECK(apogee::acp::stop_reason("content_filter") == "refusal");
    CHECK(apogee::acp::stop_reason("cancelled") == "cancelled");
    CHECK(apogee::acp::stop_reason("tool_calls") == "end_turn");
    CHECK(apogee::acp::stop_reason("something_later") == "end_turn");
    CHECK(apogee::acp::plan_status("pending") == "pending");
    CHECK(apogee::acp::plan_status("active") == "in_progress");
    CHECK(apogee::acp::plan_status("done") == "completed");
    CHECK(apogee::acp::tool_call_status("pending") == "pending");
    CHECK(apogee::acp::tool_call_status("running") == "in_progress");
    CHECK(apogee::acp::tool_call_status("done") == "completed");
    CHECK(apogee::acp::tool_call_status("failed") == "failed");
}

TEST_CASE("machine events become session updates: text, calls, the plan, side calls",
          "[acp][translate]") {
    apogee::acp::Translator translator{"/work/project"};
    using J = nlohmann::json;

    auto one = translator.updates(J{{"type", "thinking_delta"}, {"text", "hmm"}});
    REQUIRE(one.size() == 1);
    CHECK(one[0] == J{{"sessionUpdate", "agent_thought_chunk"},
                      {"content", {{"type", "text"}, {"text", "hmm"}}}});
    one = translator.updates(J{{"type", "answer_delta"}, {"text", "Hi"}, {"turn", 2}});
    CHECK(one.at(0).at("sessionUpdate") == "agent_message_chunk");
    CHECK(one.at(0).at("content").at("text") == "Hi");

    // A call: first a tool_call, drawn with its kind, title and location --
    // a relative path made absolute under the session's folder.
    one = translator.updates(J{{"type", "tool_call"},
                               {"id", "c1"},
                               {"name", "read_file"},
                               {"state", "pending"},
                               {"target", "src/main.cpp"}});
    REQUIRE(one.size() == 1);
    CHECK(one[0] == J{{"sessionUpdate", "tool_call"},
                      {"toolCallId", "c1"},
                      {"title", "read_file src/main.cpp"},
                      {"kind", "read"},
                      {"status", "pending"},
                      {"locations", J::array({{{"path", "/work/project/src/main.cpp"}}})}});
    one = translator.updates(
        J{{"type", "tool_call"}, {"id", "c1"}, {"name", "read_file"}, {"state", "running"}});
    CHECK(one.at(0) == J{{"sessionUpdate", "tool_call_update"},
                         {"toolCallId", "c1"},
                         {"status", "in_progress"}});
    one = translator.updates(
        J{{"type", "tool_call"}, {"id", "c1"}, {"name", "read_file"}, {"state", "failed"}});
    CHECK(one.at(0).at("status") == "failed");

    // A command's target is no location; an MCP tool is `other`.
    one = translator.updates(J{{"type", "tool_call"},
                               {"id", "c2"},
                               {"name", "run_command"},
                               {"state", "pending"},
                               {"target", "make test"}});
    CHECK(one.at(0).at("kind") == "execute");
    CHECK_FALSE(one.at(0).contains("locations"));
    one = translator.updates(J{{"type", "tool_call"},
                               {"id", "c3"},
                               {"name", "mcp__docs__search"},
                               {"state", "pending"},
                               {"target", "/abs/path"}});
    CHECK(one.at(0).at("kind") == "other");
    CHECK_FALSE(one.at(0).contains("locations"));

    // A call with no id: the pending one is the one each later report is about.
    one =
        translator.updates(J{{"type", "tool_call"}, {"name", "git_status"}, {"state", "pending"}});
    const std::string minted = one.at(0).at("toolCallId");
    CHECK(minted.starts_with("apogee-call-"));
    one = translator.updates(J{{"type", "tool_call"}, {"name", "git_status"}, {"state", "done"}});
    CHECK(one.at(0).at("toolCallId") == minted);

    // The todo is a plan, whole.
    one = translator.updates(J{{"type", "todo"},
                               {"items", J::array({{{"text", "Read"}, {"state", "done"}},
                                                   {{"text", "Fix"}, {"state", "active"}},
                                                   {{"text", "Test"}, {"state", "pending"}}})}});
    CHECK(one.at(0) ==
          J{{"sessionUpdate", "plan"},
            {"entries",
             J::array({{{"content", "Read"}, {"priority", "medium"}, {"status", "completed"}},
                       {{"content", "Fix"}, {"priority", "medium"}, {"status", "in_progress"}},
                       {{"content", "Test"}, {"priority", "medium"}, {"status", "pending"}}})}});

    // A tool's own status line is its call's to draw; a side call is a thought.
    CHECK(translator.updates(J{{"type", "tool_status"}, {"text", "[tool] read_file"}}).empty());
    one = translator.updates(J{{"type", "tool_status"}, {"text", "utility — titling"}});
    CHECK(one.at(0).at("content").at("text") == "utility — titling\n");
    one = translator.updates(
        J{{"type", "sources"}, {"sources", J::array({{{"file", "a.md"}}, {{"file", "b.md"}}})}});
    CHECK(one.at(0).at("content").at("text") == "sources: a.md, b.md\n");
    one = translator.updates(J{{"type", "memory"}, {"chats", 2}, {"decisions", 1}});
    CHECK(one.at(0).at("content").at("text") == "recalled 2 past chats and 1 decision\n");
    // Markers say nothing.
    CHECK(translator.updates(J{{"type", "answer_start"}}).empty());
}

TEST_CASE("the permission sheet: the call it is about, its detail, four answers",
          "[acp][translate][permission]") {
    apogee::acp::Translator translator{"/p"};
    using J = nlohmann::json;
    (void)translator.updates(J{{"type", "tool_call"},
                               {"id", "w1"},
                               {"name", "write_file"},
                               {"state", "pending"},
                               {"target", "out.txt"}});
    const J call = translator.permission_tool_call(J{{"type", "question"},
                                                     {"kind", "permission"},
                                                     {"tool", "write_file"},
                                                     {"target", "out.txt"},
                                                     {"detail", "--- a\n+++ b\n+new\n"},
                                                     {"tool_call_id", "w1"}});
    CHECK(call.at("toolCallId") == "w1");
    CHECK(call.at("kind") == "edit");
    CHECK(call.at("status") == "pending");
    CHECK(call.at("locations") == J::array({{{"path", "/p/out.txt"}}}));
    CHECK(call.at("content").at(0).at("content").at("text") == "--- a\n+++ b\n+new\n");

    const J options = apogee::acp::permission_options(false);
    std::vector<std::string> ids;
    std::vector<std::string> kinds;
    for (const J& option : options) {
        ids.push_back(option.at("optionId"));
        kinds.push_back(option.at("kind"));
    }
    CHECK(ids == std::vector<std::string>{"yes", "session", "always", "no"});
    CHECK(kinds ==
          std::vector<std::string>{"allow_once", "allow_always", "allow_always", "reject_once"});
    CHECK_THAT(std::string{options.at(2).at("name")}, ContainsSubstring("config"));
    CHECK_THAT(std::string{apogee::acp::permission_options(true).at(2).at("name")},
               ContainsSubstring("tools.allowed_hosts"));

    const auto chosen = [](const J& result) { return apogee::acp::permission_answer(&result); };
    CHECK(chosen(J{{"outcome", {{"outcome", "selected"}, {"optionId", "yes"}}}}) == "yes");
    CHECK(chosen(J{{"outcome", {{"outcome", "selected"}, {"optionId", "session"}}}}) == "session");
    CHECK(chosen(J{{"outcome", {{"outcome", "selected"}, {"optionId", "always"}}}}) == "always");
    CHECK(chosen(J{{"outcome", {{"outcome", "selected"}, {"optionId", "no"}}}}) == "no");
    // A cancelled sheet, an option no sheet offered, no answer at all: no.
    CHECK(chosen(J{{"outcome", {{"outcome", "cancelled"}}}}) == "no");
    CHECK(chosen(J{{"outcome", {{"outcome", "selected"}, {"optionId", "sudo"}}}}) == "no");
    CHECK(apogee::acp::permission_answer(nullptr) == "no");

    // A question that names no call is still drawn, with an id of its own.
    const J lone = translator.permission_tool_call(
        J{{"type", "question"}, {"kind", "permission"}, {"tool", "run_command"}, {"target", "ls"}});
    CHECK_FALSE(std::string{lone.at("toolCallId")}.empty());
    CHECK(lone.at("title") == "run_command ls");
}

TEST_CASE("a location is drawn only where the tool's target is a path, ~ under the home folder",
          "[acp][translate][tools]") {
    // A note tool is an edit or a delete whose target is the note's key: the
    // note lives in the data directory, never at `<project>/<key>`, so it has
    // no location -- on its card or on its permission sheet.
    apogee::acp::Translator translator{"/work/project", "/home/me"};
    using J = nlohmann::json;
    const auto first = [&translator](std::string_view id, std::string_view tool,
                                     std::string_view target) {
        return translator
            .updates(J{{"type", "tool_call"},
                       {"id", std::string{id}},
                       {"name", std::string{tool}},
                       {"state", "pending"},
                       {"target", std::string{target}}})
            .at(0);
    };
    J card = first("n1", "write_note", "plan");
    CHECK(card.at("kind") == "edit");
    CHECK(card.at("title") == "write_note plan");
    CHECK_FALSE(card.contains("locations"));
    card = first("n2", "delete_note", "plan");
    CHECK(card.at("kind") == "delete");
    CHECK_FALSE(card.contains("locations"));
    const J sheet = translator.permission_tool_call(J{{"type", "question"},
                                                      {"kind", "permission"},
                                                      {"tool", "write_note"},
                                                      {"target", "plan"},
                                                      {"tool_call_id", "n1"}});
    CHECK(sheet.at("toolCallId") == "n1");
    CHECK(sheet.at("title") == "write_note plan");
    CHECK_FALSE(sheet.contains("locations"));

    // The file tools' `~` is the home folder, as they expand it; `~bob` is no
    // home to them, so it is a relative path as they read it.
    CHECK(first("f1", "write_file", "~/notes/x.md").at("locations") ==
          J::array({{{"path", "/home/me/notes/x.md"}}}));
    CHECK(first("f2", "read_file", "~").at("locations") == J::array({{{"path", "/home/me"}}}));
    CHECK(first("f3", "delete_file", "~bob/x").at("locations") ==
          J::array({{{"path", "/work/project/~bob/x"}}}));
    CHECK(first("f4", "edit_file", "/abs/../etc/x").at("locations") ==
          J::array({{{"path", "/etc/x"}}}));
    const J tilde_sheet = translator.permission_tool_call(J{{"type", "question"},
                                                            {"kind", "permission"},
                                                            {"tool", "write_file"},
                                                            {"target", "~/notes/x.md"},
                                                            {"tool_call_id", "f1"}});
    CHECK(tilde_sheet.at("locations") == J::array({{{"path", "/home/me/notes/x.md"}}}));

    // No home known: a `~` target is drawn with no location, never a wrong one.
    apogee::acp::Translator homeless{"/work/project"};
    const J lone = homeless
                       .updates(J{{"type", "tool_call"},
                                  {"id", "f1"},
                                  {"name", "write_file"},
                                  {"state", "pending"},
                                  {"target", "~/x"}})
                       .at(0);
    CHECK(lone.at("title") == "write_file ~/x");
    CHECK_FALSE(lone.contains("locations"));

    // Per tool, never per kind.
    CHECK(apogee::acp::tool_takes_path("read_file"));
    CHECK(apogee::acp::tool_takes_path("write_file"));
    CHECK(apogee::acp::tool_takes_path("edit_file"));
    CHECK(apogee::acp::tool_takes_path("delete_file"));
    CHECK(apogee::acp::tool_takes_path("list_directory"));
    for (const std::string_view not_a_path :
         {"read_note", "write_note", "delete_note", "list_notes", "git_show", "run_command",
          "fetch_url", "mcp__fs__read_file"}) {
        INFO(not_a_path);
        CHECK_FALSE(apogee::acp::tool_takes_path(not_a_path));
    }
}

TEST_CASE("a repeated call id opens a card of its own, and its sheet names its own file",
          "[acp][translate][permission]") {
    // Gemini's API gives a call no id, so its wire uses the tool's name: every
    // write in a session is `write_file`. Each call is still its own card, and
    // the sheet for the second names the second's file -- never the first's.
    apogee::acp::Translator translator{"/p"};
    using J = nlohmann::json;
    const auto report = [&translator](std::string_view state, std::string_view target) {
        J event{{"type", "tool_call"},
                {"id", "write_file"},
                {"name", "write_file"},
                {"state", std::string{state}}};
        if (!target.empty()) {
            event["target"] = std::string{target};
        }
        return translator.updates(event);
    };
    const auto ask = [&translator](std::string_view target) {
        return translator.permission_tool_call(J{{"type", "question"},
                                                 {"kind", "permission"},
                                                 {"tool", "write_file"},
                                                 {"target", std::string{target}},
                                                 {"tool_call_id", "write_file"}});
    };

    auto one = report("pending", "a.txt");
    REQUIRE(one.size() == 1);
    CHECK(one[0].at("sessionUpdate") == "tool_call");
    CHECK(one[0].at("toolCallId") == "write_file");
    CHECK(ask("a.txt").at("toolCallId") == "write_file");
    CHECK(report("running", "a.txt").at(0).at("toolCallId") == "write_file");
    CHECK(report("done", "a.txt").at(0).at("toolCallId") == "write_file");

    // The second call: a card of its own, under a fresh id.
    one = report("pending", "b.txt");
    REQUIRE(one.size() == 1);
    CHECK(one[0].at("sessionUpdate") == "tool_call");
    CHECK(one[0].at("toolCallId") == "write_file-2");
    CHECK(one[0].at("title") == "write_file b.txt");
    CHECK(one[0].at("status") == "pending");
    CHECK(one[0].at("locations") == J::array({{{"path", "/p/b.txt"}}}));
    const J sheet = ask("b.txt");
    CHECK(sheet.at("toolCallId") == "write_file-2");
    CHECK(sheet.at("title") == "write_file b.txt");
    CHECK(sheet.at("locations") == J::array({{{"path", "/p/b.txt"}}}));
    one = report("failed", "b.txt");
    CHECK(one.at(0) == J{{"sessionUpdate", "tool_call_update"},
                         {"toolCallId", "write_file-2"},
                         {"status", "failed"}});

    // A third, and a model that really named a call `write_file-3`: each its
    // own card still.
    CHECK(report("pending", "c.txt").at(0).at("toolCallId") == "write_file-3");
    one = translator.updates(J{{"type", "tool_call"},
                               {"id", "write_file-3"},
                               {"name", "write_file"},
                               {"state", "pending"},
                               {"target", "d.txt"}});
    CHECK(one.at(0).at("toolCallId") == "write_file-3-2");
    CHECK(report("done", "c.txt").at(0).at("toolCallId") == "write_file-3");

    // The sheet says what is being asked, even where the question's target
    // is not its card's (a tool that asks again for where it is sent).
    CHECK(ask("elsewhere.txt").at("title") == "write_file elsewhere.txt");
    CHECK(ask("elsewhere.txt").at("toolCallId") == "write_file-3");

    // Calls with no id at all: one card each.
    const std::string first =
        translator.updates(J{{"type", "tool_call"}, {"name", "git_status"}, {"state", "pending"}})
            .at(0)
            .at("toolCallId");
    const std::string second =
        translator.updates(J{{"type", "tool_call"}, {"name", "git_status"}, {"state", "pending"}})
            .at(0)
            .at("toolCallId");
    CHECK(first != second);
}

TEST_CASE("a prompt's blocks become one user line: text, and files as mentions",
          "[acp][translate][prompt]") {
    using J = nlohmann::json;
    std::string error;
    CHECK(apogee::acp::prompt_text(J::array({{{"type", "text"}, {"text", "Explain this"}},
                                             {{"type", "resource_link"},
                                              {"name", "main.swift"},
                                              {"uri", "file:///Users/me/My%20App/main.swift"}}}),
                                   error) == "Explain this\n@\"/Users/me/My App/main.swift\"");
    CHECK(error.empty());
    // A link that is no file is said as written.
    CHECK(apogee::acp::prompt_text(
              J::array({{{"type", "resource_link"}, {"name", "x"}, {"uri", "https://x.test/a"}}}),
              error) == "https://x.test/a");
    // A file's embedded content is attached from disk, as a link would be.
    CHECK(apogee::acp::prompt_text(
              J::array({{{"type", "resource"},
                         {"resource", {{"uri", "file://localhost/tmp/a.py"}, {"text", "x"}}}}}),
              error) == "@\"/tmp/a.py\"");
    CHECK(error.empty());

    for (const J& refused :
         {J::array({{{"type", "image"}, {"data", ""}, {"mimeType", "image/png"}}}),
          J::array({{{"type", "audio"}}}),
          J::array({{{"type", "resource"}, {"resource", {{"uri", "mem://x"}, {"text", "x"}}}}}),
          J::array({{{"type", "hologram"}}}), J::array(), J::object()}) {
        INFO(refused.dump());
        error.clear();
        CHECK(apogee::acp::prompt_text(refused, error).empty());
        CHECK_FALSE(error.empty());
    }
    error.clear();
    (void)apogee::acp::prompt_text(J::array({{{"type", "image"}}}), error);
    CHECK_THAT(error, ContainsSubstring("promptCapabilities.image is false"));

    CHECK(apogee::acp::file_uri_path("FILE:///a%2Fb%zz") == "/a/b%zz");
    CHECK(apogee::acp::file_uri_path("file:///a/b?x=1#y") == "/a/b");
    CHECK(apogee::acp::file_uri_path("http://a/b").empty());
}

TEST_CASE("the modes and the config options a session offers", "[acp][translate][modes]") {
    using apogee::acp::ModeChoice;
    const auto ids = [](const std::vector<ModeChoice>& modes) {
        std::vector<std::string_view> out;
        for (const ModeChoice& mode : modes) {
            out.push_back(mode.id);
        }
        return out;
    };
    CHECK(ids(apogee::acp::offered_modes(false, "manual")) ==
          std::vector<std::string_view>{"manual", "accept-edits", "plan"});
    CHECK(ids(apogee::acp::offered_modes(true, "manual")) ==
          std::vector<std::string_view>{"manual", "accept-edits", "plan", "bypass"});
    // Dont-ask is never offered -- a host is always there to ask -- but a
    // session launched in it shows what it runs in.
    CHECK(ids(apogee::acp::offered_modes(false, "dont-ask")) ==
          std::vector<std::string_view>{"manual", "accept-edits", "plan", "dont-ask"});

    const std::vector<ModeChoice> modes = apogee::acp::offered_modes(false, "manual");
    const nlohmann::json state = apogee::acp::modes_state(modes, "plan");
    CHECK(state.at("currentModeId") == "plan");
    CHECK(state.at("availableModes").size() == 3);

    const nlohmann::json options = apogee::acp::config_options(
        {{.value = "local", .name = "local", .description = "mock · m1"},
         {.value = "claude", .name = "claude"}},
        "local", modes, "manual");
    REQUIRE(options.size() == 2);
    CHECK(options.at(0).at("id") == "model");
    CHECK(options.at(0).at("category") == "model");
    CHECK(options.at(0).at("type") == "select");
    CHECK(options.at(0).at("currentValue") == "local");
    CHECK(options.at(0).at("options").at(0).at("description") == "mock · m1");
    CHECK_FALSE(options.at(0).at("options").at(1).contains("description"));
    CHECK(options.at(1).at("id") == "mode");
    CHECK(options.at(1).at("category") == "mode");
    CHECK(options.at(1).at("options").size() == 3);
}

TEST_CASE("ask_user: a form where the host takes one, else said that nobody can be asked",
          "[acp][translate][ask]") {
    using J = nlohmann::json;
    const J question{
        {"type", "question"},
        {"questions",
         J::array({{{"header", "Style"},
                    {"question", "Which style?"},
                    {"multi_select", false},
                    {"options", J::array({{{"label", "terse"}, {"description", "short"}}})}},
                   {{"header", ""},
                    {"question", "Anything else?"},
                    {"multi_select", false},
                    {"options", J::array()}}})}};
    const J params = apogee::acp::elicitation_params(question, "s1");
    CHECK(params.at("mode") == "form");
    CHECK(params.at("sessionId") == "s1");
    CHECK(params.at("message") == "Which style?\nAnything else?");
    CHECK(params.at("requestedSchema").at("required") == J::array({"answer_1", "answer_2"}));
    CHECK(params.at("requestedSchema").at("properties").at("answer_1").at("title") == "Style");
    CHECK_THAT(
        std::string{params.at("requestedSchema").at("properties").at("answer_1").at("description")},
        ContainsSubstring("- terse: short"));

    const J accepted{{"action", "accept"},
                     {"content", {{"answer_1", "terse"}, {"answer_2", J::array({"a", "b"})}}}};
    CHECK(apogee::acp::elicitation_answers(&accepted, 2) ==
          std::vector<std::string>{"terse", "a, b"});
    const J declined{{"action", "decline"}};
    for (const std::string& answer : apogee::acp::elicitation_answers(&declined, 2)) {
        CHECK_THAT(answer, ContainsSubstring("declined"));
        CHECK_THAT(answer, ContainsSubstring("say what you chose"));
    }
    CHECK(apogee::acp::elicitation_answers(nullptr, 1).size() == 1);
    CHECK_THAT(apogee::acp::nobody_to_ask(), ContainsSubstring("say what you chose"));
}

TEST_CASE("the lines a session's child reads, and the refusals it answers a setting with",
          "[acp][translate][lines]") {
    using J = nlohmann::json;
    CHECK(J::parse(apogee::acp::user_line("hi \"there\"")) ==
          J{{"type", "user"}, {"text", "hi \"there\""}});
    CHECK(J::parse(apogee::acp::cancel_line()) == J{{"type", "cancel"}});
    CHECK(J::parse(apogee::acp::set_line("permission_mode", "plan")) ==
          J{{"type", "set"}, {"permission_mode", "plan"}});
    CHECK(J::parse(apogee::acp::answer_line("yes")) == J{{"type", "answer"}, {"text", "yes"}});
    CHECK(J::parse(apogee::acp::hello_line("Xcode", "27.1")) ==
          J{{"type", "hello"},
            {"client", {{"name", "Xcode"}, {"version", "27.1"}}},
            {"wants", J::array({"acp"})}});
    // One line each: never a newline inside.
    CHECK(apogee::acp::user_line("a\nb").find('\n') == std::string::npos);

    CHECK(apogee::acp::refused_setting("set model: no backend named 'x'") == "model");
    CHECK(apogee::acp::refused_setting("set permission_mode: bypass needs ...") ==
          "permission_mode");
    CHECK(apogee::acp::refused_setting("switched to helper -- from the next turn").empty());
    CHECK(apogee::acp::refused_setting("set: nothing to set -- model").empty());

    const nlohmann::json notice = apogee::acp::notice_update("persona: coder\nmore");
    CHECK(notice.at("sessionUpdate") == "notice");
    CHECK(notice.at("severity") == "info");
    CHECK(notice.at("title") == "persona: coder");
    CHECK(notice.at("description") == "more");
}

TEST_CASE("session/list lists the chats a host began, newest first, by exact folder",
          "[acp][translate][sessions]") {
    // 56c: a chat begun in a terminal has no folder, and the protocol needs
    // one for every entry; the title is the name chats list shows, left out
    // while that is the bare id.
    const std::vector<apogee::acp::ListedChat> chats{
        {.id = "c3",
         .name = "Fix the build",
         .updated_at = "2026-11-01T12:03:00Z",
         .origin = "Xcode",
         .cwd = "/work/app"},
        {.id = "c2",
         .name = "c2",
         .updated_at = "2026-11-01T12:02:00Z",
         .origin = "acp",
         .cwd = "/work/other"},
        {.id = "c1", .name = "From a terminal", .updated_at = "2026-11-01T12:01:00Z"},
        {.id = "c0", .name = "Elsewhere", .updated_at = "", .origin = "Xcode", .cwd = "/work/app"},
    };
    const nlohmann::json all = apogee::acp::listed_sessions(chats, "");
    CHECK(all == nlohmann::json::parse(R"([
        {"sessionId": "c3", "cwd": "/work/app", "title": "Fix the build",
         "updatedAt": "2026-11-01T12:03:00Z"},
        {"sessionId": "c2", "cwd": "/work/other", "updatedAt": "2026-11-01T12:02:00Z"},
        {"sessionId": "c0", "cwd": "/work/app", "title": "Elsewhere"}])"));
    const nlohmann::json here = apogee::acp::listed_sessions(chats, "/work/app");
    REQUIRE(here.size() == 2);
    CHECK(here.at(0).at("sessionId") == "c3");
    CHECK(here.at(1).at("sessionId") == "c0");
    // Exactly the folder: never a prefix, never a parent.
    CHECK(apogee::acp::listed_sessions(chats, "/work").empty());
    CHECK(apogee::acp::listed_sessions(chats, "/work/app/").empty());
}

TEST_CASE("a load replays each saved entry as chats show reads it, every id distinct",
          "[acp][translate][sessions]") {
    // 56c: the user's words, the calls as finished cards without results,
    // the answer's parts, a compaction as words -- ids from the entry's index
    // in chats show's turns, so an unanswered turn and a compaction that
    // share a turn number never share an id.
    std::vector<apogee::acp::SavedEntry> entries(4);
    entries[0].compacted = "They chose Postgres.";
    entries[0].answer = {"Postgres it is."};
    entries[1].user = "What is in notes.md?\nattached: notes.md (1 file, 12 B)";
    entries[1].tools = {{.name = "read_file", .summary = R"({"path":"notes.md"})"},
                        {.name = "mcp__xcode__build", .summary = ""}};
    entries[1].answer = {"Reading it.", "It holds two lines."};
    entries[2].user = "and then?";  // cancelled: no answer kept
    entries[3].user = "again";
    entries[3].answer = {"Done."};
    const std::vector<nlohmann::json> updates = apogee::acp::replay_updates(entries);

    std::vector<std::string> kinds;
    std::set<std::string> ids;
    for (const nlohmann::json& update : updates) {
        kinds.push_back(update.at("sessionUpdate").get<std::string>());
        const std::string id = update.contains("messageId")
                                   ? update.at("messageId").get<std::string>()
                                   : update.at("toolCallId").get<std::string>();
        INFO(id);
        CHECK(ids.insert(id).second);
    }
    CHECK(kinds == std::vector<std::string>{
                       "agent_message_chunk", "agent_message_chunk", "user_message_chunk",
                       "tool_call", "tool_call", "agent_message_chunk", "agent_message_chunk",
                       "user_message_chunk", "user_message_chunk", "agent_message_chunk"});
    CHECK(updates.at(0) == nlohmann::json{{"sessionUpdate", "agent_message_chunk"},
                                          {"content",
                                           {{"type", "text"},
                                            {"text",
                                             "history compacted; what came before is this "
                                             "summary:\nThey chose Postgres."}}},
                                          {"messageId", "entry-0-compaction"}});
    CHECK(updates.at(1).at("messageId") == "entry-0-agent-1");
    CHECK(updates.at(2) == nlohmann::json{{"sessionUpdate", "user_message_chunk"},
                                          {"content",
                                           {{"type", "text"},
                                            {"text",
                                             "What is in notes.md?\nattached: notes.md (1 file, "
                                             "12 B)"}}},
                                          {"messageId", "entry-1-user"}});
    CHECK(updates.at(3) == nlohmann::json{{"sessionUpdate", "tool_call"},
                                          {"toolCallId", "entry-1-tool-1"},
                                          {"title", R"(read_file {"path":"notes.md"})"},
                                          {"kind", "read"},
                                          {"status", "completed"}});
    CHECK(updates.at(4).at("title") == "mcp__xcode__build");
    CHECK(updates.at(4).at("kind") == "other");
    CHECK(updates.at(6).at("content").at("text") == "It holds two lines.");
    CHECK(updates.at(6).at("messageId") == "entry-1-agent-2");
    CHECK(updates.at(7).at("messageId") == "entry-2-user");
    CHECK(updates.at(9).at("messageId") == "entry-3-agent-1");
    CHECK(apogee::acp::replay_updates({}).empty());
}

TEST_CASE("a new session's child is told who began the chat, under options no one completes",
          "[acp][translate][sessions]") {
    CHECK(apogee::acp::kOriginOption == "--acp-origin");
    CHECK(apogee::acp::kCwdOption == "--acp-cwd");
    CHECK(apogee::acp::kNoClientOrigin == "acp");
}
