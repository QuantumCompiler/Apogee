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
/// How many completions show under the input.
constexpr std::size_t kSuggestionRows = 5;

/// One entry of the transcript.
struct Block {
    enum class Kind : std::uint8_t { User, Line, Thinking, Answer };
    Kind kind = Kind::Line;
    /// User: what was entered. Line: the line, as the core styled it.
    std::string text;
    // Thinking: the turn's reasoning and its side calls, until the answer.
    std::string reasoning;
    std::vector<std::string> notes;
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
    std::size_t picked = 0;
    std::function<void(std::string)> pick;

    std::vector<Block> blocks;
    std::string header;
    std::string status;
    std::string input;
    std::size_t cursor = 0;
    std::vector<std::string> history;
    std::optional<std::size_t> history_at;
    commands::Suggester suggest;
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
            apply(renderer.finish(answer_width()));
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
                    ansi::TextAttributes you;
                    you.bold = true;
                    you.color = ansi::Color::Green;
                    markdown::Row content{span("You: ", you), span(block.text)};
                    wrapped(content, {span("     ")});
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
                    if (!block.reasoning.empty()) {
                        std::vector<markdown::Row> reasoning = markdown::wrap(
                            {span(block.reasoning, dim_look())}, width, {span("  ")}, {span("  ")});
                        const std::size_t keep = std::min(kReasoningRows, reasoning.size());
                        rows.insert(rows.end(), reasoning.end() - static_cast<std::ptrdiff_t>(keep),
                                    reasoning.end());
                    }
                    break;
                }
                case Block::Kind::Answer:
                    rows.insert(rows.end(), block.rows.begin(), block.rows.end());
                    rows.insert(rows.end(), block.open_rows.begin(), block.open_rows.end());
                    break;
            }
        }
        return rows;
    }

    [[nodiscard]] ftxui::Element draw_picker() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        Elements rows;
        rows.push_back(text(" Chats -- Enter opens one; a new chat starts afresh") | dim);
        rows.push_back(text(""));
        for (std::size_t i = 0; i <= picker.size(); ++i) {
            std::string line = i == picked ? " › " : "   ";
            if (i == 0) {
                line += "New chat";
            } else {
                const PickerEntry& entry = picker.at(i - 1);
                line += entry.name + "  ·  " + entry.updated + "  ·  " +
                        std::to_string(entry.turns) + (entry.turns == 1 ? " turn" : " turns");
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
        if (prompt.has_value()) {
            frame.push_back(draw_prompt());
            return vbox(std::move(frame));
        }
        frame.push_back(draw_input());
        if (suggest && !input.empty()) {
            const commands::Suggestions offered =
                suggest(std::string_view{input}.substr(0, cursor));
            for (std::size_t i = 0; i < offered.candidates.size() && i < kSuggestionRows; ++i) {
                const commands::Suggestion& candidate = offered.candidates.at(i);
                std::string row =
                    "   " + (candidate.label.empty() ? candidate.text : candidate.label);
                if (!candidate.description.empty()) {
                    row += "  " + candidate.description;
                }
                frame.push_back(text(std::move(row)) | dim);
            }
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

    void complete() {
        const commands::Suggestions offered = suggest(std::string_view{input}.substr(0, cursor));
        if (offered.candidates.empty()) {
            return;
        }
        const commands::Applied applied =
            commands::apply_suggestion(input, cursor, offered, offered.candidates.front());
        input = applied.line;
        cursor = applied.cursor;
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
            complete();
            return true;
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
            picked = std::min(picked + 1, picker.size());
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
            state.apply(state.renderer.feed(text, state.answer_width()));
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
                              std::function<void(std::string)> pick) {
    state_->post([entries = std::move(entries), pick = std::move(pick)](State& state) mutable {
        state.mode = State::Mode::Picker;
        state.picker = std::move(entries);
        state.picked = 0;
        state.pick = std::move(pick);
        state.prompt.reset();
    });
}

void SessionView::begin_session() {
    state_->post([](State& state) { state.begin_conversation({}); });
}

void SessionView::set_completion(commands::Suggester suggest) {
    state_->post([completer = std::move(suggest)](State& state) mutable {
        state.suggest = std::move(completer);
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
