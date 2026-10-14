#include "tui/session_view.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "agentloop/recall.h"
#include "ansi/ansi.h"
#include "ansi/text_width.h"
#include "contracts/errors.h"
#include "markdown/layout.h"
#include "markdown/stream_renderer.h"
#include "tui/paint.h"
#include "tui/styled_text.h"
#include "tui/view_body.h"

namespace apogee::tui {

namespace {

using Clock = std::chrono::steady_clock;

/// How many rows of the live reasoning the thinking block shows.
constexpr std::size_t kReasoningRows = 3;
/// How many completions the menu under the input shows at once; the rest
/// scroll into its window as the choice moves.
constexpr std::size_t kMenuRows = 8;
/// The cells a box takes on each side of what it holds: its edge and a
/// column of padding.
constexpr std::size_t kBoxSide = 2;
/// The share of the width a person's box may take before their line wraps:
/// their lines sit at the right, a chat's other side.
constexpr std::size_t kUserShareNumerator = 2;
constexpr std::size_t kUserShareDenominator = 3;

/// One entry of the transcript.
struct Block {
    enum class Kind : std::uint8_t { User, Line, Thinking, Answer };
    Kind kind = Kind::Line;
    /// User: what was entered. Line: the line, as the core styled it.
    std::string text;
    // Thinking: the turn's reasoning and its side calls, until the answer;
    // and its one-line summary (43), unless it is drawn raw after all.
    std::string reasoning;
    std::vector<std::string> notes;
    std::string summary;
    bool raw = false;
    bool open = true;
    bool budget = false;
    Clock::time_point started{};
    long long seconds = 0;
    // Answer: the rows `markdown/` committed, and its open area.
    std::vector<markdown::Row> rows;
    std::vector<markdown::Row> open_rows;
};

[[nodiscard]] markdown::Span span(std::string text, ansi::TextAttributes look = {}) {
    return markdown::Span{.text = std::move(text), .attributes = look};
}

[[nodiscard]] ansi::TextAttributes dim_look() {
    ansi::TextAttributes look;
    look.dim = true;
    return look;
}

/// `content` in a rounded box `indent` cells from the left, as wide as its
/// widest row and a column of padding inside each edge -- the person's line
/// and Apogee's answer drawn as Claude Code draws its prompt.
[[nodiscard]] std::vector<markdown::Row> boxed(const std::vector<markdown::Row>& content,
                                               std::size_t indent, ansi::TextAttributes edge) {
    std::size_t inner = 0;
    for (const markdown::Row& row : content) {
        inner = std::max(inner, markdown::row_width(row));
    }
    std::string rule;
    for (std::size_t i = 0; i < inner + 2; ++i) {
        rule += "─";
    }
    const markdown::Span margin = span(std::string(indent, ' '));
    std::vector<markdown::Row> rows;
    rows.push_back({margin, span("╭" + rule + "╮", edge)});
    for (const markdown::Row& row : content) {
        markdown::Row line{margin, span("│", edge), span(" ")};
        line.insert(line.end(), row.begin(), row.end());
        line.push_back(span(std::string(inner - markdown::row_width(row) + 1, ' ')));
        line.push_back(span("│", edge));
        rows.push_back(std::move(line));
    }
    rows.push_back({margin, span("╰" + rule + "╯", edge)});
    return rows;
}

/// The byte offset one codepoint before or after `at` in `text`.
[[nodiscard]] std::size_t step_left(const std::string& text, std::size_t at) {
    if (at == 0) {
        return 0;
    }
    --at;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U) {
        --at;
    }
    return at;
}

[[nodiscard]] std::size_t step_right(const std::string& text, std::size_t at) {
    if (at >= text.size()) {
        return text.size();
    }
    ++at;
    while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xC0U) == 0x80U) {
        ++at;
    }
    return at;
}

}  // namespace

struct SessionView::State : std::enable_shared_from_this<SessionView::State> {
    State(Pump& pump_ref, Theme theme_value) : pump{pump_ref}, theme{theme_value} {}

    Pump& pump;
    Theme theme;

    // --- shared with the session's thread, under `mutex` --------------------
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::string> entered;
    bool waiting_for_line = false;
    std::optional<std::string> answer;
    harness::CancellationToken turn;
    bool turn_running = false;
    bool is_closed = false;

    // --- the shell's thread only --------------------------------------------
    enum class Mode : std::uint8_t { Empty, Picker, Conversation };
    Mode mode = Mode::Empty;
    std::vector<PickerEntry> picker;
    std::vector<PickerDoor> doors;
    std::size_t picked = 0;
    std::function<void(std::string)> pick;

    std::vector<Block> blocks;
    std::string header;
    std::string status;
    /// The session's todo (41d), as `describe_todo` words it: a fold above
    /// the input, redrawn whole when the list changes; empty when none.
    std::vector<std::string> todo;
    std::string input;
    std::size_t cursor = 0;
    std::vector<std::string> history;
    std::optional<std::size_t> history_at;
    commands::Suggester suggest;
    /// Whether a turn's reasoning is drawn as its one-line summary (43).
    bool summarize_reasoning = false;
    // The completion menu (2026-10-11): the row chosen and the first shown,
    // for the text before the cursor they were chosen against; whether the
    // person moved the choice; and the line the menu was closed for (Esc, a
    // history recall) -- open again as soon as the line changes.
    std::string menu_line;
    std::size_t menu_chosen = 0;
    std::size_t menu_top = 0;
    bool menu_moved = false;
    std::optional<std::string> menu_closed;
    /// The session's permission mode (39b): Shift+Tab's step and the row's
    /// words, set by the session's line reader.
    std::function<void()> mode_next;
    std::function<std::string()> mode_label;
    std::optional<Prompt> prompt;
    /// Rows scrolled back from the end of the transcript; 0 follows it.
    int scrollback = 0;
    bool stopping = false;
    markdown::StreamRenderer renderer;

    std::unique_ptr<agentloop::Reporter> reporter;
    ftxui::Component component;

    /// Runs `change` on the shell's thread.
    void post(std::function<void(State&)> change) {
        pump.post([self = shared_from_this(), change = std::move(change)]() { change(*self); });
    }

    [[nodiscard]] bool line_wanted() {
        const std::lock_guard lock{mutex};
        return waiting_for_line;
    }

    [[nodiscard]] bool running() {
        const std::lock_guard lock{mutex};
        return turn_running;
    }

    [[nodiscard]] Block* open_thinking() {
        if (!blocks.empty() && blocks.back().kind == Block::Kind::Thinking && blocks.back().open) {
            return &blocks.back();
        }
        return nullptr;
    }

    [[nodiscard]] Block& thinking() {
        if (Block* open = open_thinking(); open != nullptr) {
            return *open;
        }
        Block block{.kind = Block::Kind::Thinking, .started = Clock::now()};
        blocks.push_back(std::move(block));
        return blocks.back();
    }

    /// The thinking block folds to its summary, or goes when it said nothing.
    void close_thinking() {
        Block* block = open_thinking();
        if (block == nullptr) {
            return;
        }
        block->open = false;
        block->seconds =
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - block->started).count();
        if (block->reasoning.empty() && block->notes.empty()) {
            blocks.pop_back();
        }
    }

    [[nodiscard]] Block* open_answer() {
        if (!blocks.empty() && blocks.back().kind == Block::Kind::Answer && blocks.back().open) {
            return &blocks.back();
        }
        return nullptr;
    }

    [[nodiscard]] std::size_t answer_width() const {
        return static_cast<std::size_t>(std::max(20, frame_width() - 2));
    }

    /// What an answer is rendered at: the width less its box's sides.
    [[nodiscard]] std::size_t answer_text_width() const {
        return std::max<std::size_t>(20, answer_width() - 2 * kBoxSide);
    }

    void apply(const markdown::RenderOps& ops) {
        Block* block = open_answer();
        if (block == nullptr) {
            return;
        }
        block->rows.insert(block->rows.end(), ops.commit.begin(), ops.commit.end());
        block->open_rows = ops.open;
    }

    void finish_answer() {
        if (Block* block = open_answer(); block != nullptr) {
            apply(renderer.finish(answer_text_width()));
            block->open_rows.clear();
            block->open = false;
        }
    }

    void say(std::string line) {
        close_thinking();
        blocks.push_back(Block{.kind = Block::Kind::Line, .text = std::move(line)});
    }

    void begin_conversation(commands::Suggester completer) {
        mode = Mode::Conversation;
        blocks.clear();
        header.clear();
        status.clear();
        input.clear();
        cursor = 0;
        prompt.reset();
        scrollback = 0;
        stopping = false;
        renderer.reset();
        suggest = std::move(completer);
        mode_next = {};
        mode_label = {};
    }

    // --- drawing --------------------------------------------------------------

    [[nodiscard]] std::vector<markdown::Row> transcript_rows() const {
        const std::size_t width = answer_width();
        std::vector<markdown::Row> rows;
        const auto wrapped = [&rows, width](const markdown::Row& content,
                                            const markdown::Row& rest) {
            for (markdown::Row& row : markdown::wrap(content, width, {}, rest)) {
                rows.push_back(std::move(row));
            }
        };
        for (const Block& block : blocks) {
            switch (block.kind) {
                case Block::Kind::User: {
                    // The person's side: boxed, wrapped to a share of the
                    // width and set against the right edge.
                    const std::size_t share = std::max<std::size_t>(
                        20, width * kUserShareNumerator / kUserShareDenominator);
                    const std::vector<markdown::Row> said =
                        markdown::wrap({span(block.text)}, share - 2 * kBoxSide, {}, {});
                    std::size_t widest = 0;
                    for (const markdown::Row& row : said) {
                        widest = std::max(widest, markdown::row_width(row) + 2 * kBoxSide);
                    }
                    for (markdown::Row& row :
                         boxed(said, widest < width ? width - widest : 0, dim_look())) {
                        rows.push_back(std::move(row));
                    }
                    break;
                }
                case Block::Kind::Line:
                    if (block.text.empty()) {
                        rows.emplace_back();
                    } else {
                        wrapped(spans_from_sgr(block.text), {span("  ")});
                    }
                    break;
                case Block::Kind::Thinking: {
                    if (!block.open) {
                        std::string summary =
                            std::string{block.reasoning.empty() ? "✻ Worked for "
                                                                : "✻ Thought for "} +
                            std::to_string(block.seconds) + "s";
                        if (block.budget) {
                            summary += " (budget reached)";
                        }
                        rows.push_back({span(std::move(summary), dim_look())});
                        break;
                    }
                    rows.push_back({span("✻ Thinking…", dim_look())});
                    for (const std::string& note : block.notes) {
                        markdown::Row content = spans_from_sgr(note);
                        for (markdown::Span& piece : content) {
                            piece.attributes.dim = true;
                        }
                        content.insert(content.begin(), span("  "));
                        wrapped(content, {span("    ")});
                    }
                    if (summarize_reasoning && !block.raw) {
                        // The summary in the reasoning's place (43); nothing
                        // until its first line.
                        for (markdown::Row& row :
                             markdown::wrap({span(block.summary, dim_look())}, width, {span("  ")},
                                            {span("    ")})) {
                            if (!block.summary.empty()) {
                                rows.push_back(std::move(row));
                            }
                        }
                    } else if (!block.reasoning.empty()) {
                        std::vector<markdown::Row> reasoning = markdown::wrap(
                            {span(block.reasoning, dim_look())}, width, {span("  ")}, {span("  ")});
                        const std::size_t keep = std::min(kReasoningRows, reasoning.size());
                        rows.insert(rows.end(), reasoning.end() - static_cast<std::ptrdiff_t>(keep),
                                    reasoning.end());
                    }
                    break;
                }
                case Block::Kind::Answer: {
                    // Apogee's side: `markdown/`'s rows boxed at the left,
                    // the blank rows at either end left out of the box.
                    std::vector<markdown::Row> said = block.rows;
                    said.insert(said.end(), block.open_rows.begin(), block.open_rows.end());
                    const auto blank = [](const markdown::Row& row) {
                        return markdown::row_width(row) == 0;
                    };
                    const auto first = std::find_if_not(said.begin(), said.end(), blank);
                    const auto last = std::find_if_not(said.rbegin(), said.rend(), blank).base();
                    if (first >= last) {
                        break;  // nothing said yet
                    }
                    ansi::TextAttributes apogee;
                    apogee.color = ansi::Color::Cyan;
                    for (markdown::Row& row :
                         boxed(std::vector<markdown::Row>(first, last), 0, apogee)) {
                        rows.push_back(std::move(row));
                    }
                    break;
                }
            }
        }
        return rows;
    }

    [[nodiscard]] ftxui::Element draw_picker() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        Elements rows;
        rows.push_back(text(" Chats -- Enter opens one; a new chat starts afresh") | dim);
        rows.push_back(text(""));
        for (std::size_t i = 0; i <= doors.size() + picker.size(); ++i) {
            std::string line = i == picked ? " › " : "   ";
            if (i == 0) {
                line += "New chat";
            } else if (i <= picker.size()) {
                const PickerEntry& entry = picker.at(i - 1);
                line += entry.name + "  ·  " + entry.updated + "  ·  " +
                        std::to_string(entry.turns) + (entry.turns == 1 ? " turn" : " turns");
            } else {
                line += doors.at(i - 1 - picker.size()).label;
            }
            Element row = text(std::move(line));
            if (i == picked) {
                row = row | bold;
                row = theme.color ? row | color(Color::Cyan) : row;
                row = row | focus;
            }
            rows.push_back(std::move(row));
        }
        return vbox(std::move(rows)) | yframe | flex;
    }

    [[nodiscard]] ftxui::Element draw_input() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        const std::size_t at = std::min(cursor, input.size());
        const std::size_t next = step_right(input, at);
        Element under_cursor =
            text(at < input.size() ? input.substr(at, next - at) : std::string{" "}) | inverted;
        return hbox({text(" › "), text(input.substr(0, at)), std::move(under_cursor),
                     text(next < input.size() ? input.substr(next) : std::string{})});
    }

    [[nodiscard]] ftxui::Element draw_prompt() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        Elements rows;
        for (const std::string& line : prompt->lines) {
            rows.push_back(paint_row(spans_from_sgr(line), theme));
        }
        std::string keys;
        for (const Choice& choice : prompt->choices) {
            if (choice.description.empty() && choice.key.size() == 1 && !prompt->free_text) {
                keys += (keys.empty() ? "" : " / ") + std::string{"["} + choice.key + "]" +
                        choice.label.substr(std::min<std::size_t>(1, choice.label.size()));
            } else {
                std::string row = "  " + choice.key + ") " + choice.label;
                if (!choice.description.empty()) {
                    row += " -- " + choice.description;
                }
                rows.push_back(text(std::move(row)));
            }
        }
        if (!keys.empty()) {
            rows.push_back(text(" " + keys) | dim);
        }
        if (prompt->free_text) {
            rows.push_back(text(" Choose a number, or type your own answer:") | dim);
            rows.push_back(draw_input());
        }
        return vbox(std::move(rows)) | border;
    }

    /// The completion menu under the input: a window of rows, the chosen one
    /// marked, the descriptions in one column, and how many more there are
    /// above and below.
    [[nodiscard]] ftxui::Elements draw_menu() {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        const commands::Suggestions offered = open_menu();
        Elements rows;
        if (offered.candidates.empty()) {
            return rows;
        }
        const std::size_t count = offered.candidates.size();
        const std::size_t end = std::min(count, menu_top + kMenuRows);
        const auto label_of = [](const commands::Suggestion& candidate) {
            return candidate.label.empty() ? candidate.text : candidate.label;
        };
        std::size_t widest = 0;
        for (std::size_t i = menu_top; i < end; ++i) {
            widest = std::max(widest, ansi::display_width(label_of(offered.candidates.at(i))));
        }
        if (menu_top > 0) {
            rows.push_back(text("   ↑ " + std::to_string(menu_top) + " more") | dim);
        }
        for (std::size_t i = menu_top; i < end; ++i) {
            const commands::Suggestion& candidate = offered.candidates.at(i);
            const std::string label = label_of(candidate);
            std::string row = (i == menu_chosen ? " › " : "   ") + label;
            if (!candidate.description.empty()) {
                row += std::string(widest - ansi::display_width(label) + 2, ' ') +
                       candidate.description;
            }
            Element line = text(std::move(row));
            if (i == menu_chosen) {
                line = theme.color ? line | bold | color(Color::Cyan) : line | bold;
            } else {
                line = line | dim;
            }
            rows.push_back(std::move(line));
        }
        if (end < count) {
            rows.push_back(text("   ↓ " + std::to_string(count - end) + " more") | dim);
        }
        return rows;
    }

    /// The permission mode's row under the input (39b), as Claude Code's
    /// status bar words it; none in manual.
    [[nodiscard]] std::optional<ftxui::Element> draw_mode() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        if (!mode_label) {
            return std::nullopt;
        }
        const std::string label = mode_label();
        if (label.empty()) {
            return std::nullopt;
        }
        return hbox({text(" "), paint_row(spans_from_sgr(label), theme),
                     text(" (shift+tab to cycle)") | dim});
    }

    [[nodiscard]] ftxui::Element draw_conversation() {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        const std::vector<markdown::Row> rows = transcript_rows();
        Elements painted;
        painted.reserve(rows.size() + 1);
        const int last = static_cast<int>(rows.size()) - 1;
        scrollback = std::clamp(scrollback, 0, std::max(0, last));
        const int focused = last - scrollback;
        for (int i = 0; i <= last; ++i) {
            Element row = hbox({text(" "), paint_row(rows.at(static_cast<std::size_t>(i)), theme)});
            painted.push_back(i == focused ? row | focus : row);
        }
        if (painted.empty()) {
            painted.push_back(text(""));
        }
        Elements frame;
        if (!header.empty()) {
            frame.push_back(hbox({text(" "), paint_row(spans_from_sgr(header), theme)}) | dim);
        }
        frame.push_back(vbox(std::move(painted)) | yframe | flex);
        std::string state_line = status;
        const bool running_now = running();
        if (state_line.empty()) {
            if (stopping) {
                state_line = "stopping…";
            } else if (running_now) {
                state_line = "working… · Ctrl-C stops the turn";
            } else if (!line_wanted()) {
                state_line = "working…";
            }
        }
        if (scrollback > 0) {
            state_line += (state_line.empty() ? "" : " · ") +
                          std::string{"scrolled back -- Page Down returns"};
        }
        frame.push_back(hbox({text(" "), paint_row(spans_from_sgr(state_line), theme)}) | dim);
        for (const std::string& line : todo) {
            frame.push_back(hbox({text(" "), text(line)}) | dim);
        }
        if (prompt.has_value()) {
            frame.push_back(draw_prompt());
            if (std::optional<Element> row = draw_mode(); row.has_value()) {
                frame.push_back(std::move(*row));
            }
            return vbox(std::move(frame));
        }
        frame.push_back(draw_input());
        if (std::optional<Element> row = draw_mode(); row.has_value()) {
            frame.push_back(std::move(*row));
        }
        for (Element& row : draw_menu()) {
            frame.push_back(std::move(row));
        }
        return vbox(std::move(frame));
    }

    [[nodiscard]] ftxui::Element draw() {
        switch (mode) {
            case Mode::Picker:
                return draw_picker();
            case Mode::Conversation:
                return draw_conversation();
            case Mode::Empty:
                break;
        }
        return ftxui::text(" opening…") | ftxui::dim;
    }

    // --- keys -----------------------------------------------------------------

    /// A prompt's answer, handed to the session's thread.
    void answer_prompt(std::string text) {
        prompt.reset();
        input.clear();
        cursor = 0;
        {
            const std::lock_guard lock{mutex};
            answer = std::move(text);
        }
        changed.notify_all();
    }

    /// Ctrl-C: the running turn stops. False when none runs -- the shell's.
    [[nodiscard]] bool stop_turn() {
        harness::CancellationToken token;
        {
            const std::lock_guard lock{mutex};
            if (!turn_running) {
                return false;
            }
            token = turn;
        }
        token.cancel();
        stopping = true;
        prompt.reset();
        changed.notify_all();
        return true;
    }

    /// The completion menu for the line as it stands: what the completer
    /// offers, the choice kept in range and in the window -- or nothing, with
    /// nothing typed or the menu closed for this line.
    [[nodiscard]] commands::Suggestions open_menu() {
        if (!suggest || input.empty()) {
            return {};
        }
        std::string before = input.substr(0, std::min(cursor, input.size()));
        if (before != menu_line) {
            menu_line = before;
            menu_chosen = 0;
            menu_top = 0;
            menu_moved = false;
        }
        if (menu_closed.has_value()) {
            if (*menu_closed == before) {
                return {};
            }
            menu_closed.reset();
        }
        commands::Suggestions offered = suggest(before);
        if (offered.candidates.empty()) {
            return offered;
        }
        menu_chosen = std::min(menu_chosen, offered.candidates.size() - 1);
        menu_top = std::min(menu_top, menu_chosen);
        if (menu_chosen >= menu_top + kMenuRows) {
            menu_top = menu_chosen + 1 - kMenuRows;
        }
        return offered;
    }

    /// Closes the menu until the line changes.
    void close_menu() {
        menu_closed = input.substr(0, std::min(cursor, input.size()));
    }

    /// Takes the chosen row into the line.
    void take(const commands::Suggestions& offered) {
        const commands::Applied applied =
            commands::apply_suggestion(input, cursor, offered, offered.candidates.at(menu_chosen));
        input = applied.line;
        cursor = applied.cursor;
    }

    /// Enter with the menu open on a slash command: the chosen row is what
    /// was meant -- unless nothing was typed for it and the choice never
    /// moved, or it is what the line already says. True when the line now
    /// wants more typed (a command taking an argument, a folder), so Enter
    /// stops there rather than sending it.
    [[nodiscard]] bool take_on_enter() {
        if (!input.starts_with('/')) {
            return false;  // a prompt's `@` path: sent as typed
        }
        const commands::Suggestions offered = open_menu();
        if (offered.candidates.empty()) {
            return false;
        }
        const std::size_t from = std::min(offered.from, cursor);
        if (from == cursor && !menu_moved) {
            return false;
        }
        const commands::Suggestion& chosen = offered.candidates.at(menu_chosen);
        const commands::Applied applied =
            commands::apply_suggestion(input, cursor, offered, chosen);
        const auto trimmed = [](std::string_view text) {
            while (!text.empty() && text.back() == ' ') {
                text.remove_suffix(1);
            }
            return text;
        };
        if (trimmed(applied.line) == trimmed(input)) {
            return false;  // `/model` typed whole: run as typed
        }
        input = applied.line;
        cursor = applied.cursor;
        return chosen.text.ends_with(' ') || chosen.text.ends_with('/');
    }

    /// Editing keys the input and a prompt's free text share.
    [[nodiscard]] bool edit(const ftxui::Event& event) {
        if (event.is_character()) {
            input.insert(cursor, event.character());
            cursor += event.character().size();
            return true;
        }
        if (event == ftxui::Event::Backspace) {
            if (cursor > 0) {
                const std::size_t from = step_left(input, cursor);
                input.erase(from, cursor - from);
                cursor = from;
            }
            return true;
        }
        if (event == ftxui::Event::Delete) {
            if (cursor < input.size()) {
                input.erase(cursor, step_right(input, cursor) - cursor);
            }
            return true;
        }
        if (event == ftxui::Event::ArrowLeft) {
            cursor = step_left(input, cursor);
            return true;
        }
        if (event == ftxui::Event::ArrowRight) {
            cursor = step_right(input, cursor);
            return true;
        }
        if (event == ftxui::Event::Home || event == ftxui::Event::CtrlA) {
            cursor = 0;
            return true;
        }
        if (event == ftxui::Event::End || event == ftxui::Event::CtrlE) {
            cursor = input.size();
            return true;
        }
        if (event == ftxui::Event::CtrlU) {
            input.erase(0, cursor);
            cursor = 0;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool on_prompt_key(const ftxui::Event& event) {
        if (event == ftxui::Event::CtrlC) {
            (void)stop_turn();
            return true;  // a prompt's Ctrl-C never reaches the shell
        }
        if (!prompt->free_text && event.is_character()) {
            for (const Choice& choice : prompt->choices) {
                if (event.character() == choice.key) {
                    answer_prompt(choice.key);
                    return true;
                }
            }
            return true;  // a key that answers nothing is swallowed
        }
        if (event == ftxui::Event::Escape) {
            // No answer: the core reads it as a no.
            answer_prompt({});
            return true;
        }
        if (event == ftxui::Event::Return) {
            answer_prompt(prompt->free_text ? input : std::string{});
            return true;
        }
        if (prompt->free_text) {
            (void)edit(event);
        }
        return true;  // a prompt holds the keys while it is open
    }

    [[nodiscard]] bool on_conversation_key(const ftxui::Event& event) {
        if (event == ftxui::Event::TabReverse && mode_next) {
            // Shift+Tab moves the session's permission mode on (39b),
            // whatever is open: the conversation takes it, never the shell's
            // previous view.
            mode_next();
            return true;
        }
        if (prompt.has_value()) {
            return on_prompt_key(event);
        }
        if (event == ftxui::Event::CtrlC) {
            return stop_turn();
        }
        if (event == ftxui::Event::Return) {
            if (input.empty()) {
                return true;
            }
            if (take_on_enter()) {
                return true;  // the command's argument comes next
            }
            bool taken = false;
            {
                const std::lock_guard lock{mutex};
                if (waiting_for_line) {
                    entered.push_back(input);
                    waiting_for_line = false;
                    taken = true;
                }
            }
            if (taken) {
                changed.notify_all();
                blocks.push_back(Block{.kind = Block::Kind::User, .text = input});
                history.push_back(input);
                history_at.reset();
                input.clear();
                cursor = 0;
                scrollback = 0;
                stopping = false;
            }
            return true;  // while a turn runs, the line waits in the input
        }
        if (event == ftxui::Event::Tab) {
            if (!suggest || input.empty()) {
                return false;  // the shell's: the next view
            }
            if (const commands::Suggestions offered = open_menu(); !offered.candidates.empty()) {
                take(offered);
            }
            return true;
        }
        if (event == ftxui::Event::Escape) {
            if (!open_menu().candidates.empty()) {
                close_menu();
                return true;
            }
            return false;
        }
        if (event == ftxui::Event::PageUp) {
            scrollback += std::max(1, frame_width() / 8);
            return true;
        }
        if (event == ftxui::Event::PageDown) {
            scrollback = std::max(0, scrollback - std::max(1, frame_width() / 8));
            return true;
        }
        if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
            // The menu's choice while it is open, wrapping at its ends.
            if (const commands::Suggestions offered = open_menu(); !offered.candidates.empty()) {
                const std::size_t count = offered.candidates.size();
                menu_chosen = event == ftxui::Event::ArrowDown ? (menu_chosen + 1) % count
                                                               : (menu_chosen + count - 1) % count;
                menu_moved = true;
                (void)open_menu();  // the window follows the choice
                return true;
            }
            if (history.empty()) {
                return true;
            }
            if (event == ftxui::Event::ArrowUp) {
                history_at = history_at.has_value() ? (*history_at > 0 ? *history_at - 1 : 0)
                                                    : history.size() - 1;
            } else if (history_at.has_value()) {
                history_at = *history_at + 1 < history.size()
                                 ? std::optional<std::size_t>{*history_at + 1}
                                 : std::nullopt;
            }
            input = history_at.has_value() ? history.at(*history_at) : std::string{};
            cursor = input.size();
            // A recalled line opens no menu: ↑ and ↓ go on through history
            // until the line is edited.
            close_menu();
            return true;
        }
        return edit(event);
    }

    [[nodiscard]] bool on_picker_key(const ftxui::Event& event) {
        if (event == ftxui::Event::ArrowUp) {
            picked = picked > 0 ? picked - 1 : 0;
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            picked = std::min(picked + 1, doors.size() + picker.size());
            return true;
        }
        if (event == ftxui::Event::Return && picked > picker.size()) {
            const std::function<void()> open = doors.at(picked - 1 - picker.size()).open;
            mode = Mode::Empty;
            if (open) {
                open();
            }
            return true;
        }
        if (event == ftxui::Event::Return && pick) {
            const std::string id = picked == 0 ? std::string{} : picker.at(picked - 1).id;
            const std::function<void(std::string)> chosen = pick;
            mode = Mode::Empty;
            chosen(id);
            return true;
        }
        return false;
    }

    [[nodiscard]] bool on_key(const ftxui::Event& event) {
        switch (mode) {
            case Mode::Picker:
                return on_picker_key(event);
            case Mode::Conversation:
                return on_conversation_key(event);
            case Mode::Empty:
                break;
        }
        return false;
    }
};

namespace {

/// The terminal's looks, as codes `spans_from_sgr` reads back: whether they
/// show in colour is the theme's call, at paint time.
const ansi::Style kStyle{true};

/// The fourth Reporter adapter: what a turn says, posted to the view.
class TuiReporter final : public agentloop::Reporter {
public:
    explicit TuiReporter(std::weak_ptr<SessionView::State> state) : state_{std::move(state)} {}

    void on_thinking() override {
        post([](SessionView::State& state) {
            (void)state.thinking();
            state.status.clear();
        });
    }

    void on_thinking_token(std::string_view chunk) override {
        post([text = std::string{chunk}](SessionView::State& state) {
            state.thinking().reasoning += text;
        });
    }

    void on_thinking_summary(std::string_view line) override {
        post([text = std::string{line}](SessionView::State& state) {
            state.thinking().summary = text;
        });
    }

    void on_thinking_raw(std::string_view why) override {
        post([text = std::string{why}](SessionView::State& state) {
            Block& block = state.thinking();
            block.raw = true;
            if (!text.empty()) {
                block.notes.push_back("reasoning shown in full -- " + text);
            }
        });
    }

    void on_thinking_budget_reached() override {
        post([](SessionView::State& state) { state.thinking().budget = true; });
    }

    void on_tool_status(std::string_view detail) override {
        // The terminal's words: `[tool] <detail>`, kept in the turn's block.
        post([line = kStyle.tag(ansi::Role::Tool) + " " +
                     std::string{detail}](SessionView::State& state) {
            state.thinking().notes.push_back(line);
            state.status = line;
        });
    }

    void on_side_call(const agentloop::SideCall& call) override {
        // The terminal's words (26n): `<role> — <detail>`, and once it is over
        // the same line with what it took.
        post([call](SessionView::State& state) {
            Block& block = state.thinking();
            const std::string label = call.role + " — " + call.detail;
            if (call.done) {
                const std::string finished = label + agentloop::side_call_suffix(call);
                for (auto note = block.notes.rbegin(); note != block.notes.rend(); ++note) {
                    if (*note == label) {
                        *note = finished;
                        return;
                    }
                }
                block.notes.push_back(finished);
                return;
            }
            block.notes.push_back(label);
        });
    }

    void on_notice(std::string_view text) override {
        post([line = kStyle.tag(ansi::Role::Warning) + " " +
                     std::string{text}](SessionView::State& state) { state.say(line); });
    }

    void on_progress(std::string_view text) override {
        post([line = std::string{text}](SessionView::State& state) { state.status = line; });
    }

    void on_todo(const harness::TodoList& todo) override {
        // The fold above the input (41d): the same lines the terminal keeps.
        post([lines = harness::describe_todo(todo)](SessionView::State& state) {
            state.todo = lines;
        });
    }

    void on_recall(int chats, int decisions) override {
        if (chats <= 0 && decisions <= 0) {
            return;
        }
        post([line = kStyle.dim("[memory] " + agentloop::describe_recall(chats, decisions))](
                 SessionView::State& state) { state.say(line); });
    }

    void on_clear_status() override {
        post([](SessionView::State& state) { state.status.clear(); });
    }

    void on_answer_start() override {
        post([](SessionView::State& state) {
            state.close_thinking();
            state.status.clear();
            state.renderer.reset();
            state.blocks.push_back(Block{.kind = Block::Kind::Answer});
        });
    }

    void on_answer_token(std::string_view chunk) override {
        post([text = std::string{chunk}](SessionView::State& state) {
            state.apply(state.renderer.feed(text, state.answer_text_width()));
        });
    }

    void on_answer_end() override {
        post([](SessionView::State& state) { state.finish_answer(); });
    }

private:
    void post(std::function<void(SessionView::State&)> change) {
        if (const std::shared_ptr<SessionView::State> state = state_.lock(); state != nullptr) {
            state->post(std::move(change));
        }
    }

    std::weak_ptr<SessionView::State> state_;
};

}  // namespace

SessionView::SessionView(Pump& pump, Theme theme) : state_{std::make_shared<State>(pump, theme)} {
    state_->reporter = std::make_unique<TuiReporter>(state_);
    State* state = state_.get();
    state_->component =
        ftxui::CatchEvent(ftxui::Renderer([state](bool /*focused*/) { return state->draw(); }),
                          [state](const ftxui::Event& event) { return state->on_key(event); });
}

SessionView::~SessionView() {
    close();
}

View SessionView::view() {
    const std::weak_ptr<State> state = state_;
    return View{"Session", std::make_shared<View::Body>(View::Body{state_->component}), [state]() {
                    const std::shared_ptr<State> held = state.lock();
                    return held != nullptr && held->mode == State::Mode::Conversation;
                }};
}

void SessionView::show_picker(std::vector<PickerEntry> entries,
                              std::function<void(std::string)> pick,
                              std::vector<PickerDoor> doors) {
    state_->post([entries = std::move(entries), pick = std::move(pick),
                  doors = std::move(doors)](State& state) mutable {
        state.mode = State::Mode::Picker;
        state.picker = std::move(entries);
        state.doors = std::move(doors);
        state.picked = 0;
        state.pick = std::move(pick);
        state.prompt.reset();
    });
}

void SessionView::begin_session() {
    {
        // A line left for a conversation that never read it -- one that
        // refused to start -- is not the next one's.
        const std::lock_guard lock{state_->mutex};
        state_->entered.clear();
    }
    state_->post([](State& state) { state.begin_conversation({}); });
}

void SessionView::set_completion(commands::Suggester suggest) {
    state_->post([completer = std::move(suggest)](State& state) mutable {
        state.suggest = std::move(completer);
    });
}

void SessionView::set_thinking_summarized(bool on) {
    state_->post([on](State& state) { state.summarize_reasoning = on; });
}

void SessionView::set_mode_switch(std::function<void()> next, std::function<std::string()> label) {
    state_->post([next = std::move(next), label = std::move(label)](State& state) mutable {
        state.mode_next = std::move(next);
        state.mode_label = std::move(label);
    });
}

void SessionView::set_header(std::string line) {
    state_->post([text = std::move(line)](State& state) { state.header = text; });
}

void SessionView::say(std::string line) {
    state_->post([text = std::move(line)](State& state) { state.say(text); });
}

void SessionView::set_status(std::string line) {
    state_->post([text = std::move(line)](State& state) { state.status = text; });
}

void SessionView::clear_status() {
    state_->post([](State& state) { state.status.clear(); });
}

agentloop::Reporter& SessionView::reporter() {
    return *state_->reporter;
}

std::optional<std::string> SessionView::read_line() {
    std::unique_lock lock{state_->mutex};
    state_->waiting_for_line = true;
    lock.unlock();
    state_->post([](State& /*state*/) {});  // redraw: the input is open
    lock.lock();
    state_->changed.wait(lock, [this]() { return !state_->entered.empty() || state_->is_closed; });
    state_->waiting_for_line = false;
    if (state_->entered.empty()) {
        return std::nullopt;
    }
    std::string line = std::move(state_->entered.front());
    state_->entered.pop_front();
    return line;
}

std::string SessionView::ask(const Prompt& prompt) {
    std::unique_lock lock{state_->mutex};
    if (state_->is_closed) {
        throw std::runtime_error("the shell closed with a question unanswered");
    }
    const harness::CancellationToken turn = state_->turn;
    state_->answer.reset();
    lock.unlock();
    state_->post([prompt](State& state) {
        state.close_thinking();
        state.prompt = prompt;
        state.input.clear();
        state.cursor = 0;
        state.scrollback = 0;
    });
    lock.lock();
    state_->changed.wait(lock, [this, &turn]() {
        return state_->answer.has_value() || state_->is_closed || turn.stop_requested();
    });
    if (state_->answer.has_value()) {
        std::string answer = std::move(*state_->answer);
        state_->answer.reset();
        return answer;
    }
    const bool closing = state_->is_closed;
    lock.unlock();
    state_->post([](State& state) { state.prompt.reset(); });
    if (closing) {
        throw std::runtime_error("the shell closed with a question unanswered");
    }
    throw harness::CancelledError();
}

void SessionView::begin_turn(harness::CancellationToken token) {
    {
        const std::lock_guard lock{state_->mutex};
        state_->turn = std::move(token);
        state_->turn_running = true;
    }
    state_->post([](State& state) { state.stopping = false; });
}

void SessionView::end_turn() {
    {
        const std::lock_guard lock{state_->mutex};
        state_->turn = {};
        state_->turn_running = false;
    }
    state_->post([](State& state) {
        // An answer cut off -- a cancel, a backend gone -- keeps what came.
        state.close_thinking();
        state.finish_answer();
        state.stopping = false;
        state.status.clear();
    });
}

void SessionView::close() {
    harness::CancellationToken token;
    {
        const std::lock_guard lock{state_->mutex};
        state_->is_closed = true;
        token = state_->turn;
    }
    token.cancel();
    state_->changed.notify_all();
}

void SessionView::queue_line(std::string line) {
    {
        const std::lock_guard lock{state_->mutex};
        state_->entered.push_back(line);
    }
    state_->changed.notify_all();
    state_->post([line = std::move(line)](State& state) {
        state.blocks.push_back(Block{.kind = Block::Kind::User, .text = line});
        state.scrollback = 0;
    });
}

bool SessionView::enter(std::string line) {
    {
        const std::lock_guard lock{state_->mutex};
        if (!state_->waiting_for_line || state_->mode != State::Mode::Conversation) {
            return false;
        }
        state_->entered.push_back(line);
        state_->waiting_for_line = false;
    }
    state_->changed.notify_all();
    state_->blocks.push_back(Block{.kind = Block::Kind::User, .text = std::move(line)});
    state_->scrollback = 0;
    return true;
}

bool SessionView::waiting_for_line() const {
    const std::lock_guard lock{state_->mutex};
    return state_->waiting_for_line;
}

std::vector<std::string> SessionView::key_lines() {
    return {
        "Enter              send the line; / lists the chat's commands",
        "Tab                take the first completion offered",
        "↑ ↓                the lines sent before",
        "Page Up/Down       scroll the conversation",
        "Ctrl-C             stop the turn running (the session goes on)",
        "y n a s            answer a permission prompt: yes, no, always, session",
        "Esc                answer a prompt with no",
    };
}

bool SessionView::closed() const {
    const std::lock_guard lock{state_->mutex};
    return state_->is_closed;
}

}  // namespace apogee::tui
