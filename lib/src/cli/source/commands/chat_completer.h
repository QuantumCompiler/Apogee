#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "commands/line_reader.h"
#include "harness/config.h"

/// What `apogee chat` offers as the user types: its commands after a `/` at
/// the start of the line, each command's own values after it, and paths after
/// an `@`.
///
/// **One table.** The commands, their argument shapes, their one-line
/// descriptions and what their arguments complete from are declared once,
/// below. `/help` prints it, completion offers it, and the REPL dispatches
/// through it -- so a command cannot be dispatchable and yet uncompletable or
/// undocumented, which is what had happened to `/retriever` and `/rerank`
/// while the list was a bare vector of strings.
///
/// Everything here is pure over what it is handed: the listings arrive as
/// data and a closure, so the tests never touch a real filesystem or config.
namespace apogee::commands {

/// Which handler a chat command runs. `/exit` and `/quit` share one.
enum class ChatVerb : std::uint8_t {
    Help,
    Model,
    Models,
    System,
    Temperature,
    MaxTokens,
    Compact,
    Title,
    Retriever,
    Rerank,
    Branch,
    Capture,
    Exit,
};

/// What a command's argument completes from.
enum class ArgumentValues : std::uint8_t {
    None,
    Backends,
    Retrievers,
    /// `off`, `auto`, and the backends.
    RerankTargets,
    CaptureStatuses,
};

/// One row of the table.
struct ChatCommandSpec {
    /// Without the leading `/`.
    std::string_view verb;
    ChatVerb id;
    /// Its shape for `/help`: `<required>`, `[optional]`, or empty for none.
    std::string_view argument;
    std::string_view description;
    ArgumentValues values = ArgumentValues::None;
};

/// Every chat command, in the order `/help` and completion show them.
[[nodiscard]] std::span<const ChatCommandSpec> chat_commands() noexcept;

/// The command spelled `verb` (without the `/`), or null.
[[nodiscard]] const ChatCommandSpec* find_chat_command(std::string_view verb) noexcept;

/// `/help`'s rows: each command with its argument shape, then its
/// description in a shared column, wrapped to stay short of `width`'s last
/// column. A width of 0 is a pipe: one row per command, unwrapped.
[[nodiscard]] std::vector<std::string> chat_help_lines(std::size_t width = 0);

/// One entry of a directory, as the `@` completer needs it.
struct DirectoryEntry {
    std::string name;
    bool directory = false;
};

using DirectoryLister = std::function<std::vector<DirectoryEntry>(const std::filesystem::path&)>;

/// The real lister. An unreadable directory lists as empty -- completion
/// offers nothing rather than failing a keystroke -- and a huge one is read
/// only as far as `kMaxListing` entries, so a keystroke never waits on it.
[[nodiscard]] std::vector<DirectoryEntry> list_directory(const std::filesystem::path& directory);
inline constexpr std::size_t kMaxListing = 10000;

/// A value a command's argument can take, and what it means.
struct NamedChoice {
    std::string name;
    std::string description;
};

/// What the provider draws on.
struct ChatCompletionSources {
    /// Each configured backend, in config order, described by type and model.
    std::vector<NamedChoice> backends;
    /// What `@` paths are relative to.
    std::filesystem::path working_directory;
    DirectoryLister list;
};

/// The sources for a chat over `config`, with paths under `working_directory`.
[[nodiscard]] ChatCompletionSources chat_completion_sources(
    const harness::Config& config, std::filesystem::path working_directory);

/// The provider: the text before the cursor in, what could complete it out.
///
///   * `/` at the start of the line, the cursor still in the first word: the
///     commands whose name starts with what follows it.
///   * `@` starting the word the cursor is in: files and folders, relative to
///     the working directory, folders with a trailing `/`, hidden entries only
///     when the typed name starts with `.`, a name matched ignoring case
///     unless it has a capital in it. A path with a space completes quoted:
///     `@"my file.pdf"`, or `@"my folder/` still open to go further.
///   * After a command and a space: that command's values, if it has any.
///
/// Anything else offers nothing -- which is what keeps backend names from
/// completing in the middle of a message.
[[nodiscard]] Suggestions suggest_chat_input(std::string_view before_cursor,
                                             const ChatCompletionSources& sources);

}  // namespace apogee::commands
