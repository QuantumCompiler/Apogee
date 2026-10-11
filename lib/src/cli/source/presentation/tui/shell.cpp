#include "tui/shell.h"

#include <array>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>
#include <utility>

#include "tui/paint.h"
#include "tui/shell_state.h"
#include "tui/view_body.h"

namespace apogee::tui {

namespace {

/// How many views the number keys reach.
constexpr std::size_t kNumberedViews = 9;

[[nodiscard]] ftxui::Event to_event(const Key& key) {
    using ftxui::Event;
    switch (key.name) {
        case Key::Name::Text:
            return Event::Character(key.text);
        case Key::Name::Return:
            return Event::Return;
        case Key::Name::Escape:
            return Event::Escape;
        case Key::Name::Tab:
            return Event::Tab;
        case Key::Name::BackTab:
            return Event::TabReverse;
        case Key::Name::Up:
            return Event::ArrowUp;
        case Key::Name::Down:
            return Event::ArrowDown;
        case Key::Name::Left:
            return Event::ArrowLeft;
        case Key::Name::Right:
            return Event::ArrowRight;
        case Key::Name::PageUp:
            return Event::PageUp;
        case Key::Name::PageDown:
            return Event::PageDown;
        case Key::Name::Home:
            return Event::Home;
        case Key::Name::End:
            return Event::End;
        case Key::Name::Backspace:
            return Event::Backspace;
        case Key::Name::Delete:
            return Event::Delete;
        case Key::Name::CtrlC:
            return Event::CtrlC;
        case Key::Name::CtrlD:
            return Event::CtrlD;
        case Key::Name::Function: {
            static const std::array<Event, 12> keys{Event::F1, Event::F2,  Event::F3,  Event::F4,
                                                    Event::F5, Event::F6,  Event::F7,  Event::F8,
                                                    Event::F9, Event::F10, Event::F11, Event::F12};
            return key.number >= 1 && key.number <= 12
                       ? keys.at(static_cast<std::size_t>(key.number - 1))
                       : Event::Special("");
        }
        case Key::Name::Alt:
            return Event::Special("\x1b" + key.text);
    }
    return Event::Special("");
}

/// The view a key names by its number: F1-F9, Alt+1-9, or -- `bare` -- a
/// digit alone. Nothing when the key names no view.
[[nodiscard]] std::optional<std::size_t> numbered_view(const ftxui::Event& event, bool bare) {
    static const std::array<ftxui::Event, kNumberedViews> function_keys{
        ftxui::Event::F1, ftxui::Event::F2, ftxui::Event::F3, ftxui::Event::F4, ftxui::Event::F5,
        ftxui::Event::F6, ftxui::Event::F7, ftxui::Event::F8, ftxui::Event::F9};
    for (std::size_t i = 0; i < kNumberedViews; ++i) {
        const std::string digit(1, static_cast<char>('1' + i));
        if (event == function_keys.at(i) || event == ftxui::Event::Special("\x1b" + digit) ||
            (bare && event == ftxui::Event::Character(digit))) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] ftxui::Element hints(const Shell::State& state) {
    const bool typing = !state.views.empty() &&
                        state.views.at(static_cast<std::size_t>(state.selected)).takes_text();
    std::string text = typing ? " Ctrl-D quit · Tab next view · F1–F9 a view"
                              : " q quit · Tab next view · 1–9 a view";
    if (!typing && state.exec_line.has_value()) {
        text += " · : a command";
    }
    return ftxui::text(std::move(text)) | ftxui::dim;
}

/// The tab strip's labels at `width` (37b): every view named while they fit;
/// past that, the views not shown as their numbers alone, the shown one named
/// still; past even that, a window of numbers around the shown one, an
/// ellipsis where the strip goes on. The view shown is always named.
[[nodiscard]] std::vector<std::string> tab_labels(const Shell::State& state, int width) {
    const auto measure = [](const std::vector<std::string>& labels) {
        int cells = 0;
        for (const std::string& label : labels) {
            cells += ftxui::string_width(label);
        }
        return cells;
    };
    const std::size_t count = state.views.size();
    const auto selected = static_cast<std::size_t>(state.selected);
    std::vector<std::string> full;
    std::vector<std::string> compact;
    for (std::size_t i = 0; i < count; ++i) {
        const std::string number = std::to_string(i + 1);
        const std::string named = number + " " + state.views.at(i).title();
        full.push_back(i == selected ? "[" + named + "]" : " " + named + " ");
        compact.push_back(i == selected ? "[" + named + "]" : " " + number + " ");
    }
    const int room = width - 1;  // the strip's right margin
    if (measure(full) <= room) {
        return full;
    }
    if (measure(compact) <= room) {
        return compact;
    }
    // A window around the shown view, widened while it fits.
    std::size_t first = selected;
    std::size_t last = selected;
    const auto window = [&compact, &first, &last, count]() {
        std::vector<std::string> shown;
        if (first > 0) {
            shown.emplace_back(" … ");
        }
        for (std::size_t i = first; i <= last; ++i) {
            shown.push_back(compact.at(i));
        }
        if (last + 1 < count) {
            shown.emplace_back(" … ");
        }
        return shown;
    };
    for (bool grew = true; grew;) {
        grew = false;
        if (last + 1 < count) {
            ++last;
            if (measure(window()) <= room) {
                grew = true;
            } else {
                --last;
            }
        }
        if (first > 0) {
            --first;
            if (measure(window()) <= room) {
                grew = true;
            } else {
                ++first;
            }
        }
    }
    return window();
}

/// Whether the title fits beside the tabs at `width`.
[[nodiscard]] bool shows_title(const Shell::State& state, int width) {
    int cells = ftxui::string_width(" " + state.options.title + " ") + 1;
    for (const std::string& label : tab_labels(state, width)) {
        cells += ftxui::string_width(label);
    }
    return cells <= width;
}

[[nodiscard]] ftxui::Element draw_frame(Shell::State& state) {
    using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
    // What the views wrap to, before any of them draws.
    const int width = state.fixed_width > 0 ? state.fixed_width : Terminal::Size().dimx;
    set_frame_width(width);
    const std::vector<std::string> labels = tab_labels(state, width);
    Elements tabs;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (labels.at(i).starts_with("[")) {
            Element tab = text(labels.at(i)) | bold;
            tabs.push_back(state.options.theme.color ? tab | color(Color::Cyan) : tab);
        } else {
            tabs.push_back(text(labels.at(i)) | dim);
        }
    }
    Elements rows;
    if (shows_title(state, width)) {
        Element title = text(" " + state.options.title + " ") | bold;
        if (state.options.theme.color) {
            title = title | color(Color::Cyan);
        }
        rows.push_back(hbox({title, filler(), hbox(std::move(tabs)), text(" ")}));
    } else {
        rows.push_back(hbox({filler(), hbox(std::move(tabs)), text(" ")}));
    }
    rows.push_back(separator());
    rows.push_back(state.views.empty() ? text(" nothing to show yet") | dim | flex
                                       : clip(state.stage->Render()));
    if (state.exec_line.has_value() && state.exec_line->open) {
        rows.push_back(draw_exec_line(*state.exec_line, state.options.theme));
    }
    if (!state.notice.empty()) {
        rows.push_back(text(" " + state.notice) | dim);
    }
    rows.push_back(separator());
    if (state.bottom.has_value()) {
        // The bar (32e) beside the one key every view needs.
        const bool typing = !state.views.empty() &&
                            state.views.at(static_cast<std::size_t>(state.selected)).takes_text();
        rows.push_back(hbox({state.bottom->body()->component->Render() | flex,
                             text(typing ? " Ctrl-D quit " : " q quit ") | dim}));
    } else {
        rows.push_back(hints(state));
    }
    return vbox(std::move(rows));
}

/// The shell's keys, before or after the active view's as each needs.
[[nodiscard]] bool handle(Shell& shell, Shell::State& state, const ftxui::Event& event) {
    // The exec line, while it is open, takes the keys it uses (37h).
    if (state.exec_line.has_value() && state.exec_line->open &&
        exec_line_key(*state.exec_line, event)) {
        return true;
    }
    const bool typing = !state.views.empty() &&
                        state.views.at(static_cast<std::size_t>(state.selected)).takes_text();
    // `:` opens it scoped to the view's group, `!` unscoped -- on a view that
    // is not taking typing, where neither key is a character of anything.
    if (!typing && state.exec_line.has_value() && !state.exec_line->open &&
        (event == ftxui::Event::Character(':') || event == ftxui::Event::Character('!'))) {
        open_exec_line(*state.exec_line,
                       event == ftxui::Event::Character(':') && !state.views.empty()
                           ? state.views.at(static_cast<std::size_t>(state.selected)).group()
                           : std::string{});
        return true;
    }
    // A view by its number comes first: F-keys and Alt never type.
    if (const std::optional<std::size_t> index = numbered_view(event, !typing); index.has_value()) {
        if (*index < state.views.size()) {
            shell.activate(*index);
        }
        return true;
    }
    if (!typing && event == ftxui::Event::Character('q')) {
        shell.request_quit();
        return true;
    }
    // The rest is the view's first: Tab may move its own focus, and Ctrl-C
    // may stop something it is running before it quits anything.
    if (!state.views.empty() && state.stage->OnEvent(event)) {
        return true;
    }
    if (event == ftxui::Event::Tab || event == ftxui::Event::TabReverse) {
        if (!state.views.empty()) {
            const auto count = static_cast<int>(state.views.size());
            const int step = event == ftxui::Event::Tab ? 1 : count - 1;
            shell.activate(static_cast<std::size_t>((state.selected + step) % count));
        }
        return true;
    }
    if (event == ftxui::Event::CtrlC || event == ftxui::Event::CtrlD) {
        shell.request_quit();
        return true;
    }
    return false;
}

}  // namespace

Key Key::character(std::string_view text) {
    return Key{.name = Name::Text, .text = std::string{text}};
}

Key Key::named(Name name) {
    return Key{.name = name};
}

Key Key::function(int number) {
    return Key{.name = Name::Function, .number = number};
}

Key Key::alt(std::string_view text) {
    return Key{.name = Name::Alt, .text = std::string{text}};
}

Shell::Shell(ShellOptions options) : state_{std::make_unique<State>()} {
    State& state = *state_;
    state.options = std::move(options);
    state.stage = ftxui::Container::Tab({}, &state.selected);
    ftxui::Component frame = ftxui::Renderer(state.stage, [&state]() { return draw_frame(state); });
    state.root = ftxui::CatchEvent(
        frame, [this, &state](const ftxui::Event& event) { return handle(*this, state, event); });
}

Shell::~Shell() = default;

std::size_t Shell::add(View view) {
    state_->stage->Add(view.body()->component);
    state_->views.push_back(std::move(view));
    return state_->views.size() - 1;
}

void Shell::activate(std::size_t index) {
    if (index < state_->views.size()) {
        state_->selected = static_cast<int>(index);
        state_->views.at(index).shown();
    }
}

std::size_t Shell::active() const noexcept {
    return static_cast<std::size_t>(state_->selected);
}

std::size_t Shell::size() const noexcept {
    return state_->views.size();
}

void Shell::set_exec_line(ExecLineOptions options) {
    state_->exec_line = ExecLineState{.options = std::move(options)};
}

void Shell::set_bottom_bar(View bar) {
    state_->bottom = std::move(bar);
}

void Shell::notice(std::string line) {
    state_->notice = std::move(line);
}

void Shell::request_quit() {
    state_->quit = true;
    if (state_->exit) {
        state_->exit();
    }
}

bool Shell::quit_requested() const noexcept {
    return state_->quit;
}

void Shell::on_quit(std::function<void()> exit) {
    state_->exit = std::move(exit);
}

bool Shell::press(const Key& key) {
    return state_->root->OnEvent(to_event(key));
}

std::string Shell::render_text(int width, int height) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    state_->fixed_width = width;
    ftxui::Render(screen, state_->root->Render());
    state_->fixed_width = 0;
    std::string out;
    for (int y = 0; y < height; ++y) {
        std::string row;
        // As the terminal draws it: an empty cell is a space, and the cell
        // after a double-width character is that character's second half.
        bool previous_wide = false;
        for (int x = 0; x < width; ++x) {
            const std::string& character = screen.CellAt(x, y).character;
            if (!previous_wide) {
                row += character.empty() ? std::string{" "} : character;
            }
            previous_wide = character.size() > 1 && ftxui::string_width(character) == 2;
        }
        row.erase(row.find_last_not_of(' ') + 1);
        out += row + "\n";
    }
    return out;
}

std::vector<std::string> Shell::key_lines() {
    return {
        "F1–F9, Alt+1–9     show a view by its number",
        "1–9                the same, while the view is not taking typing",
        "Tab, Shift+Tab     the next view, the previous one",
        "↑ ↓ Page Up/Down   scroll the view",
        "q                  quit, while the view is not taking typing",
        "Ctrl-D             quit",
        "Ctrl-C             quit, once the view has nothing running to stop",
        ":                  a command, scoped to the view (:pull in Models)",
        "!                  a command, unscoped; Tab completes, Esc closes",
    };
}

}  // namespace apogee::tui
