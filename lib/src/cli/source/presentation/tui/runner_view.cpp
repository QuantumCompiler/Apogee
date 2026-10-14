#include "tui/runner_view.h"

#include <algorithm>
#include <utility>

#include "tui/completion_menu.h"
#include "tui/progress.h"
#include "tui/runner_view_state.h"

namespace apogee::tui {

namespace {

/// How many of a run's newest lines the line shows above itself.
constexpr std::size_t kOutputRows = 12;

/// How many of the menu's items its window shows (45b): the session's
/// menu's count.
constexpr std::size_t kMenuRows = 8;

/// Where the word under the cursor begins: after the last blank.
[[nodiscard]] std::size_t word_start(const std::string& text) {
    const std::size_t blank = text.find_last_of(" \t");
    return blank == std::string::npos ? 0 : blank + 1;
}

/// The longest start every candidate shares.
[[nodiscard]] std::string common_prefix(const std::vector<MenuItem>& candidates) {
    if (candidates.empty()) {
        return {};
    }
    std::string prefix = candidates.front().text;
    for (const MenuItem& candidate : candidates) {
        std::size_t same = 0;
        while (same < prefix.size() && same < candidate.text.size() &&
               prefix[same] == candidate.text[same]) {
            ++same;
        }
        prefix.resize(same);
    }
    return prefix;
}

/// The word under the cursor replaced by `chosen`, a word ended unless it
/// goes on -- a folder, a `key=`, a shelf's `key:`.
void fill(ExecLineState& line, const std::string& chosen) {
    line.text = line.text.substr(0, word_start(line.text)) + chosen;
    if (!chosen.ends_with("/") && !chosen.ends_with("=") && !chosen.ends_with(":")) {
        line.text += " ";
    }
}

void close_menu(ExecLineState& line) {
    line.menu = false;
    line.candidates.clear();
    line.chosen = 0;
    line.top = 0;
}

/// The menu's choice moved by `step`, wrapping, its window following.
void move_choice(ExecLineState& line, bool down) {
    const std::size_t count = line.candidates.size();
    line.chosen = down ? (line.chosen + 1) % count : (line.chosen + count - 1) % count;
    line.top = menu_window(line.chosen, line.top, kMenuRows, count);
}

/// Tab: the one candidate taken whole, a word ended; several extended to
/// what they share and offered in the menu (45b); with the menu open, its
/// choice moved on.
void complete(ExecLineState& line) {
    if (line.menu && !line.candidates.empty()) {
        move_choice(line, true);
        return;
    }
    if (!line.options.complete) {
        return;
    }
    line.candidates = line.options.complete(line.scope, line.text);
    const std::size_t start = word_start(line.text);
    if (line.candidates.size() == 1) {
        fill(line, line.candidates.front().text);
        close_menu(line);
        return;
    }
    if (line.candidates.empty()) {
        close_menu(line);
        return;
    }
    const std::string shared = common_prefix(line.candidates);
    if (shared.size() > line.text.size() - start) {
        line.text = line.text.substr(0, start) + shared;
    }
    line.menu = true;
    line.chosen = 0;
    line.top = 0;
}

/// The line edited with the menu open: its items narrowed to what the line
/// now completes to -- and closed when that is nothing.
void refilter(ExecLineState& line) {
    if (!line.menu) {
        line.candidates.clear();
        return;
    }
    line.candidates =
        line.options.complete ? line.options.complete(line.scope, line.text) : line.candidates;
    if (line.candidates.empty()) {
        close_menu(line);
        return;
    }
    line.chosen = 0;
    line.top = 0;
}

}  // namespace

void open_exec_line(ExecLineState& line, std::string scope) {
    line.open = true;
    line.scope = std::move(scope);
    line.text.clear();
    close_menu(line);
    line.said.clear();
    line.history_at.reset();
}

bool exec_line_key(ExecLineState& line, const ftxui::Event& event) {
    if (event == ftxui::Event::Escape) {
        if (line.menu) {
            close_menu(line);  // the menu first, the line on the next
            return true;
        }
        line.open = false;  // a run goes on behind it
        return true;
    }
    if (event == ftxui::Event::CtrlC) {
        // The running command and nothing else; with none, the line closes.
        if (line.options.output && line.options.output->running()) {
            if (line.options.output->cancel()) {
                line.said = "asked the command to stop";
            }
        } else {
            line.open = false;
        }
        return true;
    }
    if (event == ftxui::Event::Return) {
        // With the menu open, Enter fills its choice; it runs nothing yet.
        if (line.menu && !line.candidates.empty()) {
            fill(line, line.candidates.at(line.chosen).text);
            close_menu(line);
            return true;
        }
        if (line.text.find_first_not_of(" \t") == std::string::npos) {
            return true;
        }
        line.history.push_back(line.text);
        line.history_at.reset();
        line.said = line.options.run ? line.options.run(line.scope, line.text) : std::string{};
        line.text.clear();
        close_menu(line);
        return true;
    }
    if (event == ftxui::Event::Tab) {
        complete(line);
        return true;
    }
    if (event == ftxui::Event::Backspace) {
        // A whole character: its UTF-8 continuation bytes with it.
        while (!line.text.empty() &&
               (static_cast<unsigned char>(line.text.back()) & 0xC0U) == 0x80U) {
            line.text.pop_back();
        }
        if (!line.text.empty()) {
            line.text.pop_back();
        }
        refilter(line);
        return true;
    }
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
        // The menu's choice while it is open; the history otherwise.
        if (line.menu && !line.candidates.empty()) {
            move_choice(line, event == ftxui::Event::ArrowDown);
            return true;
        }
        if (line.history.empty()) {
            return true;
        }
        if (event == ftxui::Event::ArrowUp) {
            line.history_at = line.history_at.has_value() && *line.history_at > 0
                                  ? *line.history_at - 1
                                  : (line.history_at.has_value() ? 0 : line.history.size() - 1);
        } else if (line.history_at.has_value()) {
            line.history_at = *line.history_at + 1 < line.history.size()
                                  ? std::optional<std::size_t>{*line.history_at + 1}
                                  : std::nullopt;
        }
        line.text = line.history_at.has_value() ? line.history.at(*line.history_at) : "";
        return true;
    }
    if (event.is_character()) {
        line.text += event.character();
        refilter(line);  // typing narrows the menu (45b)
        return true;
    }
    return false;
}

ftxui::Element draw_exec_line(const ExecLineState& line, const Theme& theme) {
    using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
    Elements rows;
    rows.push_back(separator());
    if (line.options.output && line.options.output->started()) {
        const Progress& run = *line.options.output;
        rows.push_back(
            text(" " + run.heading() + (run.running() ? " -- running · Ctrl-C stops it" : "")) |
            dim);
        const std::vector<std::string>& said = run.lines();
        const std::size_t from = said.size() > kOutputRows ? said.size() - kOutputRows : 0;
        for (std::size_t i = from; i < said.size(); ++i) {
            rows.push_back(text(" " + said.at(i)));
        }
    }
    if (!line.said.empty()) {
        rows.push_back(text(" " + line.said) | dim);
    }
    if (line.menu) {
        for (Element& row : menu_rows(line.candidates, line.chosen, line.top, kMenuRows, theme)) {
            rows.push_back(std::move(row));
        }
    }
    rows.push_back(
        text(" :" + (line.scope.empty() ? std::string{} : line.scope + " ") + line.text + "▏") |
        bold);
    return vbox(std::move(rows));
}

}  // namespace apogee::tui
