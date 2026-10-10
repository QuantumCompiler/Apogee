#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "tui/theme.h"
#include "tui/view.h"

/// The shell (32b): the full-screen frame bare `apogee` opens at a terminal.
///
/// A title, the tab strip of the views registered with it, the stage the
/// active view draws on, a notice row, and the bottom bar -- the key hints,
/// or a component given it (the system monitor, 32e). The shell owns the
/// terminal whole while it runs: one painter, the compositor (M1's
/// discipline at screen scale); what anything else in the process writes to
/// stderr meanwhile is said on its notice row instead (`run_full_screen`).
///
/// The views render what the existing cores produce and act by calling them
/// -- a view that reimplemented an action would be the hand-built-views
/// anti-pattern returning. `tui/` composes and paints, reaching down only;
/// the composition root (`cli/`) builds the views over its cores.
namespace apogee::tui {

/// A key, as the shell reads one: FTXUI's events, named without it.
struct Key {
    enum class Name : std::uint8_t {
        Text,
        Return,
        Escape,
        Tab,
        BackTab,
        Up,
        Down,
        Left,
        Right,
        PageUp,
        PageDown,
        Home,
        End,
        Backspace,
        Delete,
        CtrlC,
        CtrlD,
        /// F1-F12, `number` saying which.
        Function,
        /// Alt held with `text`'s key.
        Alt,
    };
    Name name = Name::Text;
    std::string text;
    int number = 0;

    [[nodiscard]] static Key character(std::string_view text);
    [[nodiscard]] static Key named(Name name);
    [[nodiscard]] static Key function(int number);
    [[nodiscard]] static Key alt(std::string_view text);
};

struct ShellOptions {
    /// The frame's title: `apogee 0.1.6`.
    std::string title;
    Theme theme;
};

class Shell {
public:
    explicit Shell(ShellOptions options);
    ~Shell();

    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;
    Shell(Shell&&) = delete;
    Shell& operator=(Shell&&) = delete;

    /// Registers `view` on the tab strip, numbered in the order added; the
    /// first registered is active. Returns its index.
    std::size_t add(View view);
    /// Shows the view at `index`; one past the last is ignored.
    void activate(std::size_t index);
    [[nodiscard]] std::size_t active() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

    /// The bottom bar: `bar`'s body drawn in place of the key hints.
    void set_bottom_bar(View bar);

    /// One line on the notice row, replacing the last -- what the process
    /// said on stderr while the shell held the screen. The shell's thread
    /// only: from another, through the pump.
    void notice(std::string line);

    /// Ends the shell: what a quit key does. Calls what `on_quit` set --
    /// the running pump's exit.
    void request_quit();
    [[nodiscard]] bool quit_requested() const noexcept;
    void on_quit(std::function<void()> exit);

    /// A key, dispatched as the terminal would deliver it; true when the
    /// shell or its view handled it.
    bool press(const Key& key);

    /// The frame at `width` x `height`, as text: a line per row, trailing
    /// spaces trimmed, styling dropped -- what the headless goldens pin.
    [[nodiscard]] std::string render_text(int width, int height);

    /// The shell's own keys, as its Keys view lists them.
    [[nodiscard]] static std::vector<std::string> key_lines();

    struct State;

    /// The shell's state, for `tui/`'s pump: its FTXUI root.
    [[nodiscard]] State& state() noexcept {
        return *state_;
    }

private:
    std::unique_ptr<State> state_;
};

}  // namespace apogee::tui
