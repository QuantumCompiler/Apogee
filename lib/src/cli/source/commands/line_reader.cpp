#include "commands/line_reader.h"

#include <algorithm>
#include <cerrno>
#include <iostream>
#include <replxx.hxx>
#include <utility>

#include "ansi/text_width.h"
#include "harness/paths.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

/// Codepoints in `text` -- how replxx counts positions and spans.
std::size_t codepoints(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t at = 0; at < text.size();
         at += ansi::utf8_sequence_length(static_cast<unsigned char>(text[at]))) {
        ++count;
    }
    return count;
}

/// The byte offset of codepoint `index`, or the end.
std::size_t byte_offset(std::string_view text, std::size_t index) {
    std::size_t at = 0;
    for (std::size_t seen = 0; at < text.size() && seen < index; ++seen) {
        at += ansi::utf8_sequence_length(static_cast<unsigned char>(text[at]));
    }
    return std::min(at, text.size());
}

/// What a codepoint costs a row: its cells, and never less than one. replxx
/// stops a row by counting codepoints and the terminal wraps it by counting
/// cells; the larger of the two is the one that must stay short of the edge.
std::size_t cost(std::string_view text) {
    std::size_t total = 0;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t length = ansi::utf8_sequence_length(static_cast<unsigned char>(text[at]));
        total +=
            std::max<std::size_t>(1, ansi::codepoint_cells(ansi::decode_utf8(text, at, length)));
        at += length;
    }
    return total;
}

/// The longest whole-codepoint prefix of `text` costing at most `budget`.
std::string_view cut_to_cost(std::string_view text, std::size_t budget) {
    std::size_t used = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t length = std::min(
            ansi::utf8_sequence_length(static_cast<unsigned char>(text[at])), text.size() - at);
        const std::size_t cells =
            std::max<std::size_t>(1, ansi::codepoint_cells(ansi::decode_utf8(text, at, length)));
        if (used + cells > budget) {
            break;
        }
        used += cells;
        at += length;
    }
    return text.substr(0, at);
}

/// Cells the prompt's last line takes, its colour escapes not counted.
std::size_t prompt_cells(std::string_view prompt) {
    std::string visible;
    for (std::size_t at = 0; at < prompt.size(); ++at) {
        if (prompt[at] == '\x1b' && at + 1 < prompt.size() && prompt[at + 1] == '[') {
            at += 2;
            while (at < prompt.size() && (prompt[at] < 0x40 || prompt[at] > 0x7e)) {
                ++at;
            }
            continue;
        }
        if (prompt[at] == '\n') {
            visible.clear();
            continue;
        }
        visible += prompt[at];
    }
    return ansi::display_width(visible);
}

}  // namespace

Suggester word_suggester(std::vector<std::string> words) {
    return [words = std::move(words)](std::string_view before_cursor) {
        // The last whitespace-separated word, so "/model cla" completes the
        // model rather than the whole line.
        Suggestions out;
        const std::size_t space = before_cursor.find_last_of(" \t");
        out.from = space == std::string_view::npos ? 0 : space + 1;
        const std::string_view prefix = before_cursor.substr(out.from);
        for (const std::string& word : words) {
            if (word.starts_with(prefix)) {
                out.candidates.push_back({word, {}, {}});
            }
        }
        return out;
    };
}

HintLayout layout_hints(const Suggestions& suggestions, std::string_view before_cursor,
                        std::size_t prompt_cells, std::size_t width, std::size_t max_rows) {
    HintLayout layout;
    if (suggestions.candidates.empty() || suggestions.from > before_cursor.size() ||
        before_cursor.find('\n') != std::string_view::npos || width < 2 || max_rows == 0) {
        return layout;
    }
    const std::string_view span = before_cursor.substr(suggestions.from);
    const std::size_t context = codepoints(span);
    const std::size_t span_cost = cost(span);

    // A row is the prompt's width and one column per character before the
    // span, then the span as typed, then the rest of the hint -- and the
    // last column is never written.
    const std::size_t lead = prompt_cells + cost(before_cursor.substr(0, suggestions.from));
    const std::size_t limit = width - 1;
    // Rows that could show only what was typed and an ellipsis say nothing:
    // a few characters past the span, or no rows.
    constexpr std::size_t kLeastRevealed = 3;
    if (lead + span_cost + kLeastRevealed + 1 > limit) {
        return layout;
    }
    const std::size_t room = limit - lead;

    // What a row shows of a label: the typed span, then the label past it.
    const auto shown_cost = [&](std::string_view label) {
        return span_cost + cost(label.substr(byte_offset(label, context)));
    };

    struct Row {
        std::string label;
        std::string_view description;
    };

    std::vector<Row> rows;
    const std::size_t shown = std::min(suggestions.candidates.size(), max_rows);
    for (std::size_t i = 0; i < shown; ++i) {
        const Suggestion& candidate = suggestions.candidates[i];
        rows.push_back(
            {candidate.label.empty() ? candidate.text : candidate.label, candidate.description});
    }
    std::string more;
    if (suggestions.candidates.size() > shown) {
        more = std::to_string(suggestions.candidates.size() - shown) + " more — type to narrow";
        rows.push_back({std::string{span} + "…", more});
    }

    // Descriptions line up after the widest label -- but never further out
    // than half the room, so one long path cannot push every description off
    // the screen.
    std::size_t column = 0;
    for (const Row& row : rows) {
        column = std::max(column, shown_cost(row.label));
    }
    column = std::min(column, std::max<std::size_t>(room / 2, 1));

    constexpr std::size_t kGap = 2;
    layout.context = static_cast<int>(context);
    for (const Row& row : rows) {
        std::string hint;
        const std::size_t used = shown_cost(row.label);
        if (used > room) {
            // The label alone is too wide: keep the span, cut the rest, and
            // mark the cut.
            const std::size_t split = byte_offset(row.label, context);
            hint = row.label.substr(0, split);
            hint += cut_to_cost(std::string_view{row.label}.substr(split), room - 1 - span_cost);
            hint += "…";
        } else {
            hint = row.label;
            const std::size_t at = std::max(used, column) + kGap;
            if (!row.description.empty() && at < room) {
                hint.append(at - used, ' ');
                if (cost(row.description) <= room - at) {
                    hint += row.description;
                } else {
                    hint += cut_to_cost(row.description, room - at - 1);
                    hint += "…";
                }
            }
        }
        layout.hints.push_back(std::move(hint));
    }
    return layout;
}

Applied apply_suggestion(std::string_view line, std::size_t cursor, const Suggestions& suggestions,
                         const Suggestion& chosen) {
    cursor = std::min(cursor, line.size());
    const std::size_t from = std::min(suggestions.from, cursor);
    Applied applied;
    applied.line.reserve(line.size() + chosen.text.size());
    applied.line.append(line.substr(0, from));
    applied.line.append(chosen.text);
    applied.cursor = applied.line.size();
    applied.line.append(line.substr(cursor));
    return applied;
}

std::filesystem::path default_history_path() {
    return harness::apogee_home() / "chat_history";
}

// ---------------------------------------------------------------------------
// PlainLineReader
// ---------------------------------------------------------------------------

PlainLineReader::PlainLineReader(std::istream& in) : in_{in} {}

std::optional<std::string> PlainLineReader::read(std::string_view prompt) {
    // The prompt is deliberately NOT written. On a pipe there is nobody to
    // read it, and whatever IS reading the output would receive "You: "
    // interleaved with the answers.
    (void)prompt;

    std::string line;
    if (!std::getline(in_, line)) {
        return std::nullopt;
    }
    // getline strips '\n' but leaves '\r' from a CRLF source.
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

// ---------------------------------------------------------------------------
// EditingLineReader
// ---------------------------------------------------------------------------

struct EditingLineReader::Impl {
    replxx::Replxx editor;
    Options options;
    bool history_loaded = false;
    /// The current prompt's width, for laying out the rows under it.
    std::size_t prompt_cells = 0;
    /// Set as a line is sent or abandoned, so the redraw replxx makes then
    /// draws no suggestions.
    bool finishing = false;
};

EditingLineReader::EditingLineReader(Options options) : impl_{std::make_unique<Impl>()} {
    impl_->options = std::move(options);

    impl_->editor.set_max_history_size(static_cast<int>(impl_->options.history_limit));

    if (!impl_->options.history_path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(impl_->options.history_path.parent_path(), ec);
        // A missing or unreadable history file is not an error: a first run has
        // none, and a session must never fail to start over recall.
        impl_->editor.history_load(impl_->options.history_path.string());
        impl_->history_loaded = true;
    }

    if (!impl_->options.suggest) {
        return;
    }
    // The callbacks live in the editor, which lives in the Impl, so a
    // reference to the Impl outlives every call.
    Impl& state = *impl_;
    if (!state.options.live) {
        // No rows on screen: Tab completes like a shell -- the common prefix,
        // then the list on a second press.
        state.editor.set_completion_callback([&state](const std::string& input, int& context) {
            const Suggestions suggestions = state.options.suggest(input);
            context = static_cast<int>(codepoints(
                std::string_view{input}.substr(std::min(suggestions.from, input.size()))));
            replxx::Replxx::completions_t matches;
            for (const Suggestion& candidate : suggestions.candidates) {
                matches.emplace_back(candidate.text);
            }
            return matches;
        });
        return;
    }

    // One extra row for "N more".
    state.editor.set_max_hint_rows(static_cast<int>(state.options.max_rows + 1));
    state.editor.set_hint_callback([&state](const std::string& input, int& context,
                                            replxx::Replxx::Color& color) {
        color = state.options.color ? replxx::Replxx::Color::GRAY : replxx::Replxx::Color::DEFAULT;
        if (state.finishing) {
            return replxx::Replxx::hints_t{};
        }
        // The live width, read per keystroke: a resized terminal must not
        // be laid out against the size it had when the prompt appeared.
        const auto width = static_cast<std::size_t>(platform::terminal_width().value_or(80));
        HintLayout layout = layout_hints(state.options.suggest(input), input, state.prompt_cells,
                                         width, state.options.max_rows);
        replxx::Replxx::hints_t hints;
        if (!layout.hints.empty()) {
            context = layout.context;
            hints = std::move(layout.hints);
        }
        return hints;
    });
    // Tab takes the top row. Bound directly rather than through the
    // completion callback, whose ambiguous case prints a shell-style list
    // into the scrollback under rows that already show it.
    state.editor.bind_key(replxx::Replxx::KEY::TAB, [&state](char32_t) {
        const replxx::Replxx::State now = state.editor.get_state();
        const std::string line = now.text();
        const std::size_t cursor =
            byte_offset(line, static_cast<std::size_t>(std::max(now.cursor_position(), 0)));
        const Suggestions suggestions =
            state.options.suggest(std::string_view{line}.substr(0, cursor));
        if (!suggestions.candidates.empty()) {
            const Applied applied =
                apply_suggestion(line, cursor, suggestions, suggestions.candidates.front());
            state.editor.set_state(replxx::Replxx::State{
                applied.line.c_str(),
                static_cast<int>(
                    codepoints(std::string_view{applied.line}.substr(0, applied.cursor)))});
        }
        return replxx::Replxx::ACTION_RESULT::CONTINUE;
    });
    // replxx repaints a line as it sends it, and when the keys came faster
    // than it repaints -- type-ahead, a paste -- that repaint draws the
    // suggestions afresh: a row under the sent line, or a description inline
    // on it, left in the transcript. Enter and Ctrl-C say so first, and the
    // callback answers nothing.
    const auto finishing = [&state](replxx::Replxx::ACTION action) {
        return [&state, action](char32_t code) {
            state.finishing = true;
            return state.editor.invoke(action, code);
        };
    };
    state.editor.bind_key(replxx::Replxx::KEY::ENTER,
                          finishing(replxx::Replxx::ACTION::COMMIT_LINE));
    state.editor.bind_key(replxx::Replxx::KEY::control('C'),
                          finishing(replxx::Replxx::ACTION::ABORT_LINE));
    // replxx's row browsing would show one row as chosen while Tab took
    // another, so it is off: the top row is always the one Tab takes, and
    // typing narrows the rest.
    const auto ignore = [](char32_t) { return replxx::Replxx::ACTION_RESULT::CONTINUE; };
    state.editor.bind_key(replxx::Replxx::KEY::control(replxx::Replxx::KEY::UP), ignore);
    state.editor.bind_key(replxx::Replxx::KEY::control(replxx::Replxx::KEY::DOWN), ignore);
}

EditingLineReader::~EditingLineReader() {
    if (impl_ && impl_->history_loaded && !impl_->options.history_path.empty()) {
        // Best-effort: a full disk costs recall, never the conversation.
        impl_->editor.history_sync(impl_->options.history_path.string());
    }
}

std::optional<std::string> EditingLineReader::read(std::string_view prompt) {
    // replxx reports an aborted line (Ctrl-C) by setting errno to EAGAIN
    // before returning null; a plain end of input (Ctrl-D) leaves it alone.
    impl_->prompt_cells = prompt_cells(prompt);
    impl_->finishing = false;
    errno = 0;
    const char* line = impl_->editor.input(std::string{prompt});
    const int error = errno;
    if (impl_->options.live && impl_->options.suggest) {
        // And whatever a cached repaint still drew below the sent line: the
        // rows are the only thing ever there.
        std::cout << "\x1b[J" << std::flush;
    }
    if (line == nullptr) {
        // EOF (Ctrl-D) or an interrupt (Ctrl-C). Both end the session, which
        // is what a user pressing either expects -- but only one of them is
        // a clean exit, and the surface may want to know which.
        interrupted_ = error == EAGAIN;
        return std::nullopt;
    }
    interrupted_ = false;
    return std::string{line};
}

void EditingLineReader::remember(std::string_view line) {
    if (line.empty()) {
        return;
    }
    impl_->editor.history_add(std::string{line});
}

// ---------------------------------------------------------------------------

std::unique_ptr<LineReader> make_line_reader(EditingLineReader::Options options,
                                             std::istream& fallback_input) {
    // BOTH ends must be a terminal. replxx draws on stdout, so a terminal stdin
    // with a redirected stdout would write escape sequences into the redirect --
    // and that redirect is the answer the user asked for.
    const bool interactive = platform::is_terminal(platform::StandardStream::In) &&
                             platform::is_terminal(platform::StandardStream::Out);
    if (!interactive) {
        return std::make_unique<PlainLineReader>(fallback_input);
    }
    return std::make_unique<EditingLineReader>(std::move(options));
}

}  // namespace apogee::commands
