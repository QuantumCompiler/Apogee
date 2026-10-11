#include "tui/list_view.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <utility>

#include "ansi/text_width.h"
#include "tui/view_body.h"

namespace apogee::tui {

namespace {

/// A column's widest cell, at most this many cells: a long note runs on in
/// the last column instead of pushing every other column off the screen.
constexpr std::size_t kColumnCap = 40;

[[nodiscard]] std::string padded(const std::string& text, std::size_t width) {
    const std::size_t cells = ansi::display_width(text);
    return cells >= width ? text : text + std::string(width - cells, ' ');
}

/// What a core said, as lines: the trailing empty ones dropped.
[[nodiscard]] std::vector<std::string> said_lines(const std::string& said) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at <= said.size()) {
        const std::size_t end = said.find('\n', at);
        lines.push_back(said.substr(at, end == std::string::npos ? end : end - at));
        if (end == std::string::npos) {
            break;
        }
        at = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

}  // namespace

struct ListView::State : std::enable_shared_from_this<ListView::State> {
    State(Pump& pump_ref, Theme theme_value, ListOptions options_value)
        : pump{pump_ref}, theme{theme_value}, options{std::move(options_value)} {}

    Pump& pump;
    Theme theme;
    ListOptions options;

    // --- the worker ------------------------------------------------------------
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> jobs;
    bool running_job = false;
    bool stopping = false;
    std::thread worker;

    // --- the shell's thread only -----------------------------------------------
    std::vector<std::string> heading;
    std::vector<ListRow> rows;
    std::size_t selected = 0;
    std::optional<std::vector<std::string>> detail;
    std::string notice;
    std::optional<std::size_t> confirming;  // the action whose question is open
    std::string question;
    /// The input row's text while it is open (37c).
    std::optional<std::string> typing;
    std::string last_asked;
    bool loading = true;
    ftxui::Component component;

    void start() {
        worker = std::thread{[this]() { work(); }};
    }

    void stop() {
        {
            const std::lock_guard lock{mutex};
            stopping = true;
        }
        changed.notify_all();
        if (worker.joinable()) {
            worker.join();
        }
    }

    void work() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock{mutex};
                changed.wait(lock, [this]() { return stopping || !jobs.empty(); });
                if (stopping) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
                running_job = true;
            }
            job();
            {
                const std::lock_guard lock{mutex};
                running_job = false;
            }
            changed.notify_all();
        }
    }

    void submit(std::function<void()> job) {
        {
            const std::lock_guard lock{mutex};
            jobs.push_back(std::move(job));
        }
        changed.notify_all();
    }

    void settle() {
        std::unique_lock lock{mutex};
        changed.wait(lock, [this]() { return stopping || (jobs.empty() && !running_job); });
    }

    void post(std::function<void(State&)> change) {
        pump.post([self = shared_from_this(), change = std::move(change)]() { change(*self); });
    }

    /// Reads the table again, keeping the row selected where it still is.
    void reload() {
        submit([this]() {
            std::vector<std::string> lines;
            std::vector<ListRow> read;
            try {
                std::tie(lines, read) = options.load();
            } catch (const std::exception& e) {
                lines = {std::string{"could not read: "} + e.what()};
            }
            post([lines = std::move(lines), read = std::move(read)](State& state) mutable {
                const std::string kept = state.selected < state.rows.size()
                                             ? state.rows.at(state.selected).key
                                             : std::string{};
                state.heading = std::move(lines);
                state.rows = std::move(read);
                state.selected = 0;
                for (std::size_t i = 0; i < state.rows.size(); ++i) {
                    if (state.rows.at(i).key == kept) {
                        state.selected = i;
                    }
                }
                state.loading = false;
            });
        });
    }

    void run(std::size_t action, ListRow row) {
        notice = options.actions.at(action).label + "…";
        submit([this, action, row = std::move(row)]() {
            std::string said;
            try {
                said = options.actions.at(action).run(row);
            } catch (const std::exception& e) {
                said = std::string{"not done: "} + e.what();
            }
            post([said = std::move(said)](State& state) {
                // One line is a notice; several -- a fix pass's report -- are
                // drawn whole under the table, the first on the notice row.
                std::vector<std::string> lines = said_lines(said);
                state.notice = lines.empty() ? std::string{} : lines.front();
                if (lines.size() > 1) {
                    state.detail = std::move(lines);
                }
            });
        });
        reload();
    }

    /// Asks the view's question, the answer drawn as a detail.
    void ask(ListRow row, std::string text) {
        notice = options.ask_label + "…";
        detail.reset();
        submit([this, row = std::move(row), text = std::move(text)]() {
            std::string said;
            try {
                said = options.ask(row, text);
            } catch (const std::exception& e) {
                said = std::string{"could not ask: "} + e.what();
            }
            post([said = std::move(said)](State& state) {
                state.notice.clear();
                state.detail = said_lines(said);
            });
        });
    }

    /// Whether the question can be asked now.
    [[nodiscard]] bool askable() const {
        return options.ask && (!options.ask_needs_row || selected < rows.size());
    }

    /// Whether a view's own action takes `r` from "read again".
    [[nodiscard]] bool r_taken() const {
        return std::ranges::any_of(options.actions,
                                   [](const ListAction& action) { return action.key == "r"; });
    }

    // --- drawing --------------------------------------------------------------------

    [[nodiscard]] ftxui::Element draw() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        Elements lines;
        if (options.page) {
            // A page: its lines are the content.
            for (const std::string& line : heading) {
                lines.push_back(text(" " + line));
            }
            if (loading && heading.empty()) {
                lines.push_back(text(" reading…") | dim);
            }
            Elements frame{vbox(std::move(lines)) | yframe | flex};
            if (!notice.empty()) {
                frame.push_back(text(" " + notice) | dim);
            }
            frame.push_back(text(" " + hints()) | dim);
            return vbox(std::move(frame));
        }
        for (const std::string& line : heading) {
            lines.push_back(text(" " + line) | dim);
        }
        if (!heading.empty()) {
            lines.push_back(text(""));
        }
        std::vector<std::size_t> widths(options.columns.size(), 0);
        for (std::size_t c = 0; c < options.columns.size(); ++c) {
            widths.at(c) = ansi::display_width(options.columns.at(c));
            for (const ListRow& row : rows) {
                if (c < row.cells.size()) {
                    widths.at(c) = std::max(widths.at(c), ansi::display_width(row.cells.at(c)));
                }
            }
            widths.at(c) = std::min(widths.at(c), kColumnCap);
        }
        const auto line_of = [this, &widths](const std::vector<std::string>& cells) {
            std::string line;
            for (std::size_t c = 0; c < options.columns.size(); ++c) {
                const std::string cell = c < cells.size() ? cells.at(c) : std::string{};
                line += c + 1 < options.columns.size() ? padded(cell, widths.at(c)) + "  " : cell;
            }
            return line;
        };
        lines.push_back(text("   " + line_of(options.columns)) | bold);
        if (loading && rows.empty()) {
            lines.push_back(text("   reading…") | dim);
        } else if (rows.empty()) {
            lines.push_back(text("   nothing here yet") | dim);
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const ListRow& row = rows.at(i);
            Element element = text((i == selected ? " › " : "   ") + line_of(row.cells));
            switch (row.look) {
                case ListRow::Look::Dim:
                    element = element | dim;
                    break;
                case ListRow::Look::Attention:
                    element = theme.color ? element | color(Color::Yellow) : element;
                    break;
                case ListRow::Look::Active:
                    element = theme.color ? element | color(Color::Cyan) : element | bold;
                    break;
                case ListRow::Look::Plain:
                    break;
            }
            if (i == selected) {
                element = element | bold | focus;
            }
            lines.push_back(std::move(element));
        }
        Elements frame{vbox(std::move(lines)) | yframe | flex};
        if (detail.has_value()) {
            frame.push_back(separator());
            Elements shown;
            for (const std::string& line : *detail) {
                shown.push_back(text(" " + line));
            }
            frame.push_back(vbox(std::move(shown)));
        }
        if (typing.has_value()) {
            frame.push_back(text(" " + options.ask_label + ": " + *typing + "▏") | bold);
        } else if (confirming.has_value()) {
            frame.push_back(text(" " + question + " [y/N]") | bold);
        } else if (!notice.empty()) {
            frame.push_back(text(" " + notice) | dim);
        }
        frame.push_back(text(" " + hints()) | dim);
        return vbox(std::move(frame));
    }

    [[nodiscard]] std::string hints() const {
        std::string said;
        const auto add = [&said](const std::string& part) {
            said += (said.empty() ? "" : " · ") + part;
        };
        if (typing.has_value()) {
            return "Enter asks · Esc closes";
        }
        if (detail.has_value()) {
            add("Esc closes");
        } else if (!options.enter_label.empty()) {
            add("Enter " + options.enter_label);
        }
        if (askable()) {
            add("/ " + options.ask_label);
        }
        if (!options.detail_key.empty() && !detail.has_value()) {
            add(options.detail_key + " info");
        }
        const ListRow* row = selected < rows.size() ? &rows.at(selected) : nullptr;
        for (const ListAction& action : options.actions) {
            if (action.whole_view ||
                (row != nullptr && (!action.applies || action.applies(*row)))) {
                add(action.key + " " + action.label);
            }
        }
        if (!r_taken()) {
            add("r read again");
        }
        return said;
    }

    // --- keys -------------------------------------------------------------------------

    /// The input row's keys: typing, until Enter asks or Esc closes it.
    [[nodiscard]] bool on_typing(const ftxui::Event& event) {
        if (event == ftxui::Event::Escape) {
            typing.reset();
            return true;
        }
        if (event == ftxui::Event::Return) {
            if (!typing->empty()) {
                last_asked = *typing;
                typing.reset();
                ask(selected < rows.size() ? rows.at(selected) : ListRow{}, last_asked);
            }
            return true;
        }
        if (event == ftxui::Event::Backspace) {
            // A whole character: its UTF-8 continuation bytes with it.
            while (!typing->empty() &&
                   (static_cast<unsigned char>(typing->back()) & 0xC0U) == 0x80U) {
                typing->pop_back();
            }
            if (!typing->empty()) {
                typing->pop_back();
            }
            return true;
        }
        if (event.is_character()) {
            *typing += event.character();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool on_key(const ftxui::Event& event) {
        if (typing.has_value()) {
            return on_typing(event);
        }
        if (confirming.has_value()) {
            const std::size_t action = *confirming;
            confirming.reset();
            if (event == ftxui::Event::Character('y') &&
                (selected < rows.size() || options.actions.at(action).whole_view)) {
                run(action, selected < rows.size() ? rows.at(selected) : ListRow{});
            } else {
                notice = "not done";
            }
            return true;
        }
        if (event == ftxui::Event::ArrowUp) {
            selected = selected > 0 ? selected - 1 : 0;
            detail.reset();
            return true;
        }
        if (event == ftxui::Event::ArrowDown) {
            if (selected + 1 < rows.size()) {
                ++selected;
            }
            detail.reset();
            return true;
        }
        if (event == ftxui::Event::Escape && detail.has_value()) {
            detail.reset();
            return true;
        }
        if (event == ftxui::Event::Character('/') && askable()) {
            typing = last_asked;
            return true;
        }
        if (event == ftxui::Event::Character('r') && !r_taken()) {
            notice.clear();
            reload();
            return true;
        }
        // A pass over the whole view runs with or without a row.
        if (event.is_character()) {
            for (std::size_t i = 0; i < options.actions.size(); ++i) {
                const ListAction& action = options.actions.at(i);
                if (!action.whole_view || event.character() != action.key) {
                    continue;
                }
                const ListRow row = selected < rows.size() ? rows.at(selected) : ListRow{};
                if (action.confirm) {
                    question = action.confirm(row);
                    confirming = i;
                } else {
                    run(i, row);
                }
                return true;
            }
        }
        if (selected >= rows.size()) {
            return false;
        }
        const ListRow row = rows.at(selected);
        const bool detail_asked =
            options.detail &&
            ((event == ftxui::Event::Return && !options.open) ||
             (!options.detail_key.empty() && event == ftxui::Event::Character(options.detail_key)));
        if (event == ftxui::Event::Return && options.open) {
            options.open(row);
            return true;
        }
        if (detail_asked) {
            {
                submit([this, row]() {
                    std::vector<std::string> lines;
                    try {
                        lines = options.detail(row);
                    } catch (const std::exception& e) {
                        lines = {std::string{"could not read: "} + e.what()};
                    }
                    post([lines = std::move(lines)](State& state) mutable {
                        state.detail = std::move(lines);
                    });
                });
            }
            return true;
        }
        if (event == ftxui::Event::Return) {
            return true;
        }
        if (!event.is_character()) {
            return false;
        }
        for (std::size_t i = 0; i < options.actions.size(); ++i) {
            const ListAction& action = options.actions.at(i);
            if (event.character() != action.key || (action.applies && !action.applies(row))) {
                continue;
            }
            if (action.confirm) {
                question = action.confirm(row);
                confirming = i;
            } else {
                run(i, row);
            }
            return true;
        }
        return false;
    }
};

ListView::ListView(Pump& pump, Theme theme, ListOptions options)
    : state_{std::make_shared<State>(pump, theme, std::move(options))} {
    State* state = state_.get();
    state_->component =
        ftxui::CatchEvent(ftxui::Renderer([state](bool /*focused*/) { return state->draw(); }),
                          [state](const ftxui::Event& event) { return state->on_key(event); });
    state_->start();
}

ListView::~ListView() {
    state_->stop();
}

View ListView::view() {
    const std::weak_ptr<State> state = state_;
    // Typing while the input row is open (37c): a digit or `q` is the
    // question's, not the shell's.
    View shown{state_->options.title, std::make_shared<View::Body>(View::Body{state_->component}),
               std::function<bool()>{[state]() {
                   const std::shared_ptr<State> held = state.lock();
                   return held != nullptr && held->typing.has_value();
               }}};
    shown.when_shown([state]() {
        if (const std::shared_ptr<State> held = state.lock(); held != nullptr) {
            held->reload();
        }
    });
    return shown;
}

void ListView::refresh() {
    state_->reload();
}

void ListView::settle() {
    state_->settle();
}

}  // namespace apogee::tui
