#include "contracts/todo.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace apogee::harness {

std::string_view to_string(TodoState state) noexcept {
    switch (state) {
        case TodoState::Pending:
            return "pending";
        case TodoState::Active:
            return "active";
        case TodoState::Done:
            return "done";
    }
    return "pending";
}

std::optional<TodoState> todo_state_from_string(std::string_view text) noexcept {
    for (const TodoState state : {TodoState::Pending, TodoState::Active, TodoState::Done}) {
        if (to_string(state) == text) {
            return state;
        }
    }
    return std::nullopt;
}

std::vector<std::string> describe_todo(const TodoList& todo) {
    std::vector<std::string> out;
    if (todo.empty()) {
        return out;
    }
    const auto done = std::ranges::count_if(
        todo, [](const TodoItem& item) { return item.state == TodoState::Done; });
    out.push_back("todo · " + std::to_string(done) + " of " + std::to_string(todo.size()) +
                  " done");
    for (const TodoItem& item : todo) {
        const char* mark = item.state == TodoState::Done     ? "[x] "
                           : item.state == TodoState::Active ? "[>] "
                                                             : "[ ] ";
        out.push_back(std::string{"  "} + mark + item.text);
    }
    return out;
}

nlohmann::json todo_to_json(const TodoList& todo) {
    nlohmann::json out = nlohmann::json::array();
    for (const TodoItem& item : todo) {
        out.push_back({{"text", item.text}, {"state", std::string{to_string(item.state)}}});
    }
    return out;
}

TodoList todo_from_json(const nlohmann::json& json) {
    TodoList out;
    if (!json.is_array()) {
        return out;
    }
    for (const nlohmann::json& entry : json) {
        if (!entry.is_object() || !entry.contains("text") || !entry.at("text").is_string()) {
            continue;
        }
        const std::optional<TodoState> state =
            todo_state_from_string(entry.value("state", std::string{}));
        if (!state.has_value()) {
            continue;
        }
        out.push_back(TodoItem{.text = entry.at("text").get<std::string>(), .state = *state});
    }
    return out;
}

}  // namespace apogee::harness
