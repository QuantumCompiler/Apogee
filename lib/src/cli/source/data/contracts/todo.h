#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// A session's todo (41d): the plan, visible while it runs -- a small ordered
/// list the model keeps through one tool, `todo_write`, each item a short
/// line and a state. Saved with the session, drawn by every front end the
/// same way, and deliberately small: a list, not a planner.
///
/// Here, at the floor, because every layer that touches it reads the same
/// type: the tool that writes it, the Reporter that hands it to a surface,
/// the session file that keeps it, and the four renderers.
namespace apogee::harness {

/// The one tool that writes it, which tool selection keeps in its core (26g).
inline constexpr std::string_view kTodoToolName = "todo_write";

/// An item's state.
enum class TodoState : std::uint8_t {
    Pending,
    Active,
    Done,
};

/// `pending`, `active`, `done` -- the words the tool takes and every
/// document writes.
[[nodiscard]] std::string_view to_string(TodoState state) noexcept;
[[nodiscard]] std::optional<TodoState> todo_state_from_string(std::string_view text) noexcept;

struct TodoItem {
    std::string text;
    TodoState state = TodoState::Pending;

    bool operator==(const TodoItem&) const = default;
};

using TodoList = std::vector<TodoItem>;

/// The most items a todo holds, and the most characters (code points) a
/// line holds -- past either, the tool refuses the call naming the cap.
inline constexpr std::size_t kTodoItems = 20;
inline constexpr std::size_t kTodoLineChars = 120;

/// The list as every text surface draws it: a heading -- `todo · 1 of 3
/// done` -- then one line per item, `[x]` done, `[>]` active, `[ ]` pending.
/// Empty for an empty list.
[[nodiscard]] std::vector<std::string> describe_todo(const TodoList& todo);

/// The list as every document carries it: an array of `{text, state}`.
[[nodiscard]] nlohmann::json todo_to_json(const TodoList& todo);

/// A list read back from `todo_to_json`'s shape, leniently: an entry that is
/// not an object with text and a known state is left out, never fatal -- a
/// session file degrades, never fails to load.
[[nodiscard]] TodoList todo_from_json(const nlohmann::json& json);

}  // namespace apogee::harness
