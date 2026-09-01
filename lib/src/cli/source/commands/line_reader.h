#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Reading a line of input from the user.
///
/// Two implementations behind one interface, chosen by whether there is a
/// terminal:
///
///   * **`PlainLineReader`** — `std::getline`. A pipe, a heredoc,
///     `apogee chat < script.txt`. Reads bytes and nothing more.
///   * **`EditingLineReader`** — replxx. Arrow keys, in-line editing, recall,
///     and Tab completion.
///
/// The interface exists so the REPL is testable without a terminal, and so the
/// non-TTY path is a *deliberate implementation* rather than an untested
/// fallback inside a conditional. A piped conversation is a first-class way to
/// use `apogee chat` — it is how the crash-safety suite drives it — and it must
/// behave exactly as it did before line editing arrived.
namespace apogee::commands {

class LineReader {
public:
    LineReader() = default;
    virtual ~LineReader() = default;
    LineReader(const LineReader&) = delete;
    LineReader& operator=(const LineReader&) = delete;
    LineReader(LineReader&&) = delete;
    LineReader& operator=(LineReader&&) = delete;

    /// Reads one line. Returns nullopt at end of input.
    ///
    /// The returned string never includes the trailing newline. `prompt` is
    /// written by the editor on an interactive reader and ignored otherwise —
    /// a piped run must not have prompts interleaved into whatever is reading
    /// its output.
    [[nodiscard]] virtual std::optional<std::string> read(std::string_view prompt) = 0;

    /// Records a line in the input history. A no-op without an editor.
    virtual void remember(std::string_view line) {
        (void)line;
    }

    /// Whether this reader edits. False for the plain one.
    [[nodiscard]] virtual bool interactive() const noexcept {
        return false;
    }
};

/// `std::getline`, for a pipe or a redirect.
class PlainLineReader final : public LineReader {
public:
    explicit PlainLineReader(std::istream& in);

    [[nodiscard]] std::optional<std::string> read(std::string_view prompt) override;

private:
    std::istream& in_;
};

/// replxx, for a terminal.
class EditingLineReader final : public LineReader {
public:
    struct Options {
        /// Where the input history is kept. **Per-user, not per-session** — a
        /// session file records the conversation, not the keystrokes that
        /// produced it, and recall across sessions is the whole point.
        std::filesystem::path history_path;
        /// Entries retained. Bounded so a long-lived history cannot grow
        /// without limit.
        std::size_t history_limit = 1000;
        /// Words offered on Tab. The REPL passes its slash commands.
        std::vector<std::string> completions;
    };

    explicit EditingLineReader(Options options);
    ~EditingLineReader() override;
    EditingLineReader(const EditingLineReader&) = delete;
    EditingLineReader& operator=(const EditingLineReader&) = delete;
    EditingLineReader(EditingLineReader&&) = delete;
    EditingLineReader& operator=(EditingLineReader&&) = delete;

    [[nodiscard]] std::optional<std::string> read(std::string_view prompt) override;
    void remember(std::string_view line) override;

    [[nodiscard]] bool interactive() const noexcept override {
        return true;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Picks a reader: editing when stdin AND stdout are both terminals, plain
/// otherwise.
///
/// Both, deliberately. replxx draws on stdout, so a run with a terminal stdin
/// but a redirected stdout would write escape sequences into the redirect —
/// and that redirect is the answer the user asked for.
[[nodiscard]] std::unique_ptr<LineReader> make_line_reader(EditingLineReader::Options options,
                                                           std::istream& fallback_input);

/// The default history path: `<APOGEE_HOME>/chat_history`.
[[nodiscard]] std::filesystem::path default_history_path();

}  // namespace apogee::commands
