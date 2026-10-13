#include "tools/todo.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "agent/tool.h"

/// `todo_write` (41d): the whole list each call, replacing the last; the
/// three states; the caps refused naming the cap; text mended where it
/// enters; the board current on the calling thread, and none refused.
namespace {

namespace h = apogee::harness;
namespace t = apogee::tools;
using Catch::Matchers::ContainsSubstring;

/// Runs `todo_write` from a fresh registry with `arguments`.
apogee::agent::ToolOutcome call(const std::string& arguments) {
    apogee::agent::ToolRegistry registry;
    t::register_todo_tool(registry);
    const apogee::agent::Tool* tool = registry.find("todo_write");
    REQUIRE(tool != nullptr);
    return tool->run(arguments);
}

std::string items(std::size_t count) {
    std::string out = R"({"items": [)";
    for (std::size_t i = 0; i < count; ++i) {
        out += (i == 0 ? "" : ",") + std::string{R"({"text": "step )"} + std::to_string(i) +
               R"(", "state": "pending"})";
    }
    return out + "]}";
}

}  // namespace

TEST_CASE("todo_write replaces the session's list whole, and says it changed", "[tools][todo]") {
    h::TodoList list;
    std::vector<h::TodoList> said;
    const t::TodoScope scope{t::TodoBoard{
        .list = &list, .changed = [&said](const h::TodoList& todo) { said.push_back(todo); }}};

    apogee::agent::ToolOutcome outcome = call(
        R"({"items": [{"text": "Read it", "state": "done"}, {"text": "Fix it", "state": "active"}]})");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content == "The todo now holds 2 items, 1 done. The user sees it.");
    REQUIRE(list.size() == 2);
    CHECK(list[1] == h::TodoItem{.text = "Fix it", .state = h::TodoState::Active});
    // The next call is the whole list again: the last one is gone.
    outcome = call(R"({"items": [{"text": "  Ship it  ", "state": "pending"}]})");
    CHECK(list == h::TodoList{{.text = "Ship it", .state = h::TodoState::Pending}});
    // A state left out is pending; an empty list clears it.
    (void)call(R"({"items": [{"text": "Later"}]})");
    CHECK(list.front().state == h::TodoState::Pending);
    CHECK(call(R"({"items": []})").content == "The todo is cleared.");
    CHECK(list.empty());
    REQUIRE(said.size() == 4);
    CHECK(said.back().empty());
    // Native and ungated: it writes session state only.
    apogee::agent::ToolRegistry registry;
    t::register_todo_tool(registry);
    CHECK_FALSE(registry.find("todo_write")->writes);
}

TEST_CASE("todo_write refuses what is not a todo, naming the cap, and changes nothing",
          "[tools][todo]") {
    h::TodoList list{{.text = "kept", .state = h::TodoState::Done}};
    int changes = 0;
    const t::TodoScope scope{
        t::TodoBoard{.list = &list, .changed = [&changes](const h::TodoList&) { ++changes; }}};
    const auto refused = [](const apogee::agent::ToolOutcome& outcome) {
        CHECK(outcome.is_error);
        return outcome.content;
    };
    CHECK_THAT(refused(call(items(h::kTodoItems + 1))),
               ContainsSubstring("the todo holds at most 20 items, and this call gave 21"));
    CHECK_FALSE(call(items(h::kTodoItems)).is_error);  // the cap itself is fine
    list = {{.text = "kept", .state = h::TodoState::Done}};
    const std::string long_line(h::kTodoLineChars + 1, 'x');
    CHECK_THAT(refused(call(R"({"items": [{"text": ")" + long_line + R"(", "state": "done"}]})")),
               ContainsSubstring("item 1 is 121 characters, and a todo line holds at most 120"));
    // Characters, not bytes: 120 two-byte characters fit.
    std::string accented;
    for (std::size_t i = 0; i < h::kTodoLineChars; ++i) {
        accented += "\xC3\xA9";
    }
    CHECK_FALSE(call(R"({"items": [{"text": ")" + accented + R"(", "state": "done"}]})").is_error);
    list = {{.text = "kept", .state = h::TodoState::Done}};
    CHECK_THAT(refused(call(R"({"items": [{"text": "x", "state": "doing"}]})")),
               ContainsSubstring("item 1's state 'doing' is not one of pending, active, done"));
    CHECK_THAT(refused(call(R"({"items": [{"text": "  ", "state": "done"}]})")),
               ContainsSubstring("item 1 is empty"));
    CHECK_THAT(refused(call(R"({"items": [{"state": "done"}]})")),
               ContainsSubstring("item 1 needs its text"));
    CHECK_THAT(refused(call(R"({"list": []})")), ContainsSubstring("items is required"));
    CHECK_THAT(refused(call("not json")), ContainsSubstring("items"));
    // Nothing refused touched the list.
    CHECK(list == h::TodoList{{.text = "kept", .state = h::TodoState::Done}});
    CHECK(changes == 2);
}

TEST_CASE("todo_write mends text where it enters", "[tools][todo][utf8]") {
    h::TodoList list;
    const t::TodoScope scope{t::TodoBoard{.list = &list, .changed = {}}};
    std::string error;
    const h::TodoList parsed =
        t::parse_todo_call(R"({"items": [{"text": "café", "state": "done"}]})", error);
    CHECK(error.empty());
    CHECK(parsed.front().text == "caf\xC3\xA9");
}

TEST_CASE("with no board on this thread, todo_write says there is no session", "[tools][todo]") {
    const apogee::agent::ToolOutcome outcome = call(R"({"items": []})");
    CHECK(outcome.is_error);
    CHECK(outcome.content == "Error: there is no session here to keep a todo for");
    // A scope ends, and the board before it is current again.
    h::TodoList outer;
    const t::TodoScope outer_scope{t::TodoBoard{.list = &outer, .changed = {}}};
    {
        h::TodoList inner;
        const t::TodoScope inner_scope{t::TodoBoard{.list = &inner, .changed = {}}};
        (void)call(R"({"items": [{"text": "inner", "state": "done"}]})");
        CHECK(inner.size() == 1);
    }
    (void)call(R"({"items": [{"text": "outer", "state": "done"}]})");
    CHECK(outer.front().text == "outer");
}
