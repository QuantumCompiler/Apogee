#include "tui/runner_view.h"

#include <algorithm>
#include <utility>

#include "tui/progress.h"
#include "tui/runner_view_state.h"

namespace apogee::tui {

namespace {

/// How many of a run's newest lines the line shows above itself.
constexpr std::size_t kOutputRows = 12;

/// How many of Tab's candidates are shown.
constexpr std::size_t kShownCandidates = 12;

/// Where the word under the cursor begins: after the last blank.
[[nodiscard]] std::size_t word_start(const std::string& text) {
    const std::size_t blank = text.find_last_of(" \t");
    return blank == std::string::npos ? 0 : blank + 1;
}

/// The longest start every candidate shares.
[[nodiscard]] std::string common_prefix(const std::vector<std::string>& candidates) {
    if (candidates.empty()) {
        return {};
    }
    std::string prefix = candidates.front();
    for (const std::string& candidate : candidates) {
        std::size_t same = 0;
        while (same < prefix.size() && same < candidate.size() && prefix[same] == candidate[same]) {
            ++same;
        }
        prefix.resize(same);
    }
    return prefix;
}

/// Tab: the one candidate taken whole, a word ended; several extended to
/// what they share and listed.
void complete(ExecLineState& line) {
    if (!line.options.complete) {
        return;
    }
    line.candidates = line.options.complete(line.scope, line.text);
    const std::size_t start = word_start(line.text);
    if (line.candidates.size() == 1) {
        const std::string& only = line.candidates.front();
        line.text = line.text.substr(0, start) + only;
        if (!only.ends_with("/") && !only.ends_with("=")) {
            line.text += " ";
        }
        line.candidates.clear();
        return;
    }
    const std::string shared = common_prefix(line.candidates);
    if (shared.size() > line.text.size() - start) {
        line.text = line.text.substr(0, start) + shared;
    }
}

}  // namespace

void open_exec_line(ExecLineState& line, std::string scope) {
    line.open = true;
    line.scope = std::move(scope);
    line.text.clear();
    line.candidates.clear();
    line.said.clear();
    line.history_at.reset();
}

bool exec_line_key(ExecLineState& line, const ftxui::Event& event) {
    if (event == ftxui::Event::Escape) {
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
        if (line.text.find_first_not_of(" \t") == std::string::npos) {
            return true;
        }
        line.history.push_back(line.text);
        line.history_at.reset();
        line.said = line.options.run ? line.options.run(line.scope, line.text) : std::string{};
        line.text.clear();
        line.candidates.clear();
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
        line.candidates.clear();
        return true;
    }
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::ArrowDown) {
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
        line.candidates.clear();
        return true;
    }
    return false;
}

ftxui::Element draw_exec_line(const ExecLineState& line, const Theme& /*theme*/) {
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
    if (!line.candidates.empty()) {
        std::string offered;
        for (std::size_t i = 0; i < line.candidates.size() && i < kShownCandidates; ++i) {
            offered += (offered.empty() ? "" : "  ") + line.candidates.at(i);
        }
        if (line.candidates.size() > kShownCandidates) {
            offered += "  … " + std::to_string(line.candidates.size() - kShownCandidates) + " more";
        }
        rows.push_back(text(" " + offered) | dim);
    }
    rows.push_back(
        text(" :" + (line.scope.empty() ? std::string{} : line.scope + " ") + line.text + "▏") |
        bold);
    return vbox(std::move(rows));
}

}  // namespace apogee::tui
