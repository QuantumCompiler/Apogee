#include "cli/session_output.h"

#include <iostream>
#include <utility>

#include "cli/helpers.h"
#include "platform/platform.h"
#include "views/ask_prompt.h"

namespace apogee::commands {

CliReporter::Options TerminalOutput::reporter_options(const Options& options) {
    CliReporter::Options out;
    out.answer_stream = &std::cout;
    out.decorate = options.decorate;
    out.verbosity = options.verbose ? ansi::Verbosity::Verbose : ansi::Verbosity::Line;
    out.style =
        ansi::Style::detect(options.no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
    out.width = static_cast<std::size_t>(platform::terminal_width().value_or(80));
    out.markdown = options.markdown;
    out.hyperlinks = ansi::hyperlinks_supported();
    return out;
}

TerminalOutput::TerminalOutput(Options options)
    : reporter_options_{reporter_options(options)},
      style_{reporter_options_.style},
      reporter_{status_writer_, reporter_options_} {}

std::size_t TerminalOutput::width() const {
    return static_cast<std::size_t>(platform::terminal_width().value_or(0));
}

void TerminalOutput::print_line(const std::string& line) {
    reporter_.status().print_line(line);
}

void TerminalOutput::set_status(const std::string& line) {
    reporter_.status().set(line);
}

void TerminalOutput::clear_status() {
    reporter_.status().clear();
}

void TerminalOutput::keep_line(const std::string& line) {
    reporter_.keep_line(line);
}

void TerminalOutput::set_resting_label(std::string label) {
    reporter_.set_resting_label(std::move(label));
}

void TerminalOutput::on_model_load(std::string_view backend, const harness::StatusEvent& event) {
    reporter_.on_model_load(backend, event);
}

std::function<void(std::string_view)> TerminalOutput::mcp_status() {
    return mcp_status_line(reporter_.status());
}

agentloop::AskFn TerminalOutput::ask_fn() {
    return terminal_ask_fn(reporter_.status(), style_);
}

agent::ConfirmFn TerminalOutput::confirm_fn(std::filesystem::path config_path,
                                            std::shared_ptr<SessionApprovals> approvals) {
    return terminal_confirm_fn(reporter_.status(), style_, std::move(config_path),
                               std::move(approvals));
}

std::function<bool(const std::string&)> TerminalOutput::confirm_large() {
    // A pipe cannot be asked, so a folder over the size guard is refused there.
    if (!platform::is_terminal(platform::StandardStream::In)) {
        return {};
    }
    return [this](const std::string& question) {
        reporter_.status().print_line(style_.tag(ansi::Role::Warning) + " " + question + " [y/N]");
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            return false;
        }
        return answer == "y" || answer == "Y" || answer == "yes";
    };
}

std::unique_ptr<LineReader> TerminalOutput::line_reader(EditingLineReader::Options options) {
    return make_line_reader(std::move(options), std::cin);
}

}  // namespace apogee::commands
