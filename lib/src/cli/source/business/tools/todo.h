#pragma once

#include <functional>
#include <string_view>

#include "agent/tool.h"
#include "contracts/todo.h"

/// The session todo's one tool (41d): `todo_write`, the whole list each call
/// -- items with `pending|active|done`, the simplest shape a small model
/// holds, idempotent, nothing to diff.
///
/// **Native and ungated**, as the notes are: it writes session state only.
/// Registered once, it writes to the board current on the calling thread --
/// a session's turn, or a served request -- so one registry, shared or
/// copied, serves every conversation, and the loop, which runs a tool on its
/// own thread, never has to know a todo exists. With no board current the
/// call is refused, saying so.
namespace apogee::tools {

/// Where a turn's todo lives, and who hears that it changed.
struct TodoBoard {
    /// The list -- the session's own, saved with it. Not owned.
    harness::TodoList* list = nullptr;
    /// Told the list after every change that took: the Reporter's `on_todo`.
    std::function<void(const harness::TodoList&)> changed;
};

/// Makes `board` the one `todo_write` writes to on this thread, for the
/// life of the object; the one before is current again after.
class TodoScope {
public:
    explicit TodoScope(TodoBoard board);
    ~TodoScope();

    TodoScope(const TodoScope&) = delete;
    TodoScope& operator=(const TodoScope&) = delete;
    TodoScope(TodoScope&&) = delete;
    TodoScope& operator=(TodoScope&&) = delete;

private:
    TodoBoard board_;
    TodoBoard* previous_ = nullptr;
};

/// `arguments` (`{"items": [{"text", "state"}]}`) read into a list, or -- in
/// `error` -- why the call is refused: not the shape, a state that is not
/// one of the three, an empty line, more than `kTodoItems` items or a line
/// past `kTodoLineChars` characters, each named with its cap. Text is mended
/// to UTF-8 where it enters.
[[nodiscard]] harness::TodoList parse_todo_call(std::string_view arguments, std::string& error);

/// Registers `todo_write` into `registry`.
void register_todo_tool(agent::ToolRegistry& registry);

}  // namespace apogee::tools
