#include "tools/todo.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

#include "contracts/utf8.h"
#include "tools/args.h"

namespace apogee::tools {
namespace {

/// The board `todo_write` writes to on this thread, or none.
thread_local TodoBoard* current_board = nullptr;

/// Code points in `text`, which is valid UTF-8.
[[nodiscard]] std::size_t characters(std::string_view text) {
    std::size_t count = 0;
    for (const char c : text) {
        // Every byte but a continuation byte starts a character.
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

constexpr std::string_view kExample =
    R"({"items": [{"text": "Read the failing test", "state": "done"}, )"
    R"({"text": "Fix the parser", "state": "active"}, {"text": "Run the tests", "state": "pending"}]})";

}  // namespace

TodoScope::TodoScope(TodoBoard board) : board_{std::move(board)}, previous_{current_board} {
    current_board = &board_;
}

TodoScope::~TodoScope() {
    current_board = previous_;
}

harness::TodoList parse_todo_call(std::string_view arguments, std::string& error) {
    error.clear();
    agent::ToolOutcome failure;
    const std::optional<Arguments> args = parse_arguments(arguments, kExample, failure);
    if (!args.has_value()) {
        error = failure.content;
        return {};
    }
    if (!args->has("items") || !args->object.at("items").is_array()) {
        error = std::string{"items is required: the whole list, as an array -- e.g. "} +
                std::string{kExample};
        return {};
    }
    const nlohmann::json& items = args->object.at("items");
    if (items.size() > harness::kTodoItems) {
        error = "the todo holds at most " + std::to_string(harness::kTodoItems) +
                " items, and this call gave " + std::to_string(items.size()) +
                " -- merge or drop some, and send the whole list again";
        return {};
    }
    harness::TodoList list;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const nlohmann::json& item = items.at(i);
        const std::string where = "item " + std::to_string(i + 1);
        if (!item.is_object() || !item.contains("text") || !item.at("text").is_string()) {
            error = where + " needs its text, as a string";
            return {};
        }
        const std::string text = trim(harness::valid_utf8(item.at("text").get<std::string>()));
        if (text.empty()) {
            error = where + " is empty -- a todo line says what is to be done";
            return {};
        }
        if (characters(text) > harness::kTodoLineChars) {
            error = where + " is " + std::to_string(characters(text)) +
                    " characters, and a todo line holds at most " +
                    std::to_string(harness::kTodoLineChars) + " -- say it shorter";
            return {};
        }
        const std::string state = item.contains("state") && item.at("state").is_string()
                                      ? item.at("state").get<std::string>()
                                      : std::string{"pending"};
        const std::optional<harness::TodoState> parsed = harness::todo_state_from_string(state);
        if (!parsed.has_value()) {
            error = where + "'s state '" + state + "' is not one of pending, active, done";
            return {};
        }
        list.push_back(harness::TodoItem{.text = text, .state = *parsed});
    }
    return list;
}

void register_todo_tool(agent::ToolRegistry& registry) {
    agent::Tool tool;
    tool.name = std::string{harness::kTodoToolName};
    tool.description =
        "Keep a short todo list for a task with several steps, shown to the user while you "
        "work. Send the WHOLE list every time -- it replaces the last one: each item a short "
        "line and its state, pending, active (the one you are doing now) or done. At most " +
        std::to_string(harness::kTodoItems) + " items of " +
        std::to_string(harness::kTodoLineChars) +
        " characters. Update it as you start and finish each step; an empty list clears it.";
    tool.parameters_schema =
        R"({"type":"object","properties":{"items":{"type":"array","description":"The whole list, in order","items":{"type":"object","properties":{"text":{"type":"string","description":"What is to be done, in a short line"},"state":{"type":"string","enum":["pending","active","done"]}},"required":["text","state"]}}},"required":["items"]})";
    tool.run = [](std::string_view arguments) -> agent::ToolOutcome {
        TodoBoard* board = current_board;
        if (board == nullptr || board->list == nullptr) {
            return error("there is no session here to keep a todo for");
        }
        std::string refused;
        harness::TodoList list = parse_todo_call(arguments, refused);
        if (!refused.empty()) {
            return error(refused);
        }
        *board->list = std::move(list);
        if (board->changed) {
            board->changed(*board->list);
        }
        if (board->list->empty()) {
            return ok("The todo is cleared.");
        }
        std::size_t done = 0;
        for (const harness::TodoItem& item : *board->list) {
            done += item.state == harness::TodoState::Done ? 1 : 0;
        }
        return ok("The todo now holds " + std::to_string(board->list->size()) + " items, " +
                  std::to_string(done) + " done. The user sees it.");
    };
    registry.add(std::move(tool));
}

}  // namespace apogee::tools
