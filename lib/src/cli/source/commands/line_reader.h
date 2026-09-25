#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
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
///     Tab completion, and suggestions drawn live under the input.
///
/// The interface exists so the REPL is testable without a terminal, and so the
/// non-TTY path is a *deliberate implementation* rather than an untested
/// fallback inside a conditional. A piped conversation is a first-class way to
/// use `apogee chat` — it is how the crash-safety suite drives it — and it must
/// behave exactly as it did before line editing arrived.
namespace apogee::commands {

/// One thing Tab could put in place of what the user is typing.
struct Suggestion {
    /// What replaces the span being completed.
    std::string text;
    /// What a suggestion row shows; empty means `text`. Differs only when
    /// `text` adds what the user has not typed -- the opening quote of a path
    /// with a space in it -- because a row shows the typed characters
    /// themselves and then the rest of the label.
    std::string label;
    /// One line, shown after the label. May be empty.
    std::string description;
};

/// What a suggester offers for the text before the cursor.
struct Suggestions {
    /// Byte offset in that text where the span Tab replaces begins.
    std::size_t from = 0;
    /// In the order they are shown; Tab takes the first.
    std::vector<Suggestion> candidates;
};

/// The text before the cursor in, what could complete it out. The one
/// callback protocol both of replxx's mechanisms -- Tab and the live rows --
/// are wired to, so the two can never offer different things.
using Suggester = std::function<Suggestions(std::string_view before_cursor)>;

/// A flat word list, offered for the last whitespace-separated word by
/// prefix -- what `analyze`'s loop completes.
[[nodiscard]] Suggester word_suggester(std::vector<std::string> words);

/// What replxx is handed to draw suggestion rows.
struct HintLayout {
    /// One per candidate, plus a last "N more" row when they do not all fit.
    std::vector<std::string> hints;
    /// The typed span's length in codepoints, which is how replxx counts it.
    int context = 0;
};

/// Lays suggestions out as replxx's hint rows.
///
/// replxx draws each row as the characters the user typed followed by the
/// rest of the hint, starting under the span. So a hint is its label, padded
/// to a shared column, then the description -- cut so the whole row, prompt
/// and text before the span included, stays **short of the last column**.
/// A row that reaches it leaves the cursor in the terminal's deferred-wrap
/// state and replxx's own row count wrong, which is a row its next repaint
/// fails to erase. No room at all -- a line already near the edge, a line
/// with a newline in it -- means no rows.
[[nodiscard]] HintLayout layout_hints(const Suggestions& suggestions,
                                      std::string_view before_cursor, std::size_t prompt_cells,
                                      std::size_t width, std::size_t max_rows);

/// `line` with the span before `cursor` replaced by `chosen`, and where the
/// cursor lands: just after it. What Tab does to the line.
struct Applied {
    std::string line;
    std::size_t cursor = 0;  ///< a byte offset
};

[[nodiscard]] Applied apply_suggestion(std::string_view line, std::size_t cursor,
                                       const Suggestions& suggestions, const Suggestion& chosen);

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

    /// Whether the last `read` returned nullopt because the user interrupted
    /// (Ctrl-C at the editor) rather than because the input ended. False for
    /// the plain reader, whose input only ever ends. What lets a surface
    /// tell a clean exit -- `/exit`, end of input -- from an abandoned one.
    [[nodiscard]] virtual bool interrupted() const noexcept {
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
        /// What Tab offers. Unset means Tab inserts nothing.
        Suggester suggest;
        /// Also draw the suggestions live, as rows under the input, while the
        /// user types. Tab then takes the top row rather than completing like
        /// a shell: replxx's shell-style Tab prints an ambiguous list into the
        /// scrollback, which is right with no rows on screen and wrong with
        /// them there.
        bool live = false;
        /// Draw the rows grey. Off draws them in the terminal's own colour, so
        /// `--no-color` and NO_COLOR hold.
        bool color = true;
        /// Suggestion rows at most, not counting the "N more" row.
        std::size_t max_rows = 5;
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

    [[nodiscard]] bool interrupted() const noexcept override {
        return interrupted_;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool interrupted_ = false;
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
