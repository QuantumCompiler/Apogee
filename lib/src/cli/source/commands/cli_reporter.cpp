#include "commands/cli_reporter.h"

#include <optional>
#include <utility>

#include "platform/platform.h"

namespace apogee::commands {
namespace {

StatusLine::Options status_options(const CliReporter::Options& options) {
    StatusLine::Options out;
    out.active = options.decorate;
    out.verbosity = options.verbosity;
    out.style = options.style;
    return out;
}

ThinkingView::Options thinking_options(const CliReporter::Options& options) {
    ThinkingView::Options out;
    out.width = options.width;
    if (options.decorate) {
        // Measured at every repaint: the terminal can be resized mid-turn.
        const std::size_t fallback = options.width;
        out.measure = [fallback]() {
            const std::optional<int> width = platform::terminal_width();
            return width.has_value() && *width > 0 ? static_cast<std::size_t>(*width) : fallback;
        };
    }
    // A non-TTY renders no thinking at all -- no escape codes, no summary.
    out.active = options.decorate;
    out.verbose = options.verbosity == ansi::Verbosity::Verbose;
    out.style = options.style;
    return out;
}

}  // namespace

CliReporter::CliReporter(TerminalWriter& status_writer, Options options)
    : options_{std::move(options)},
      status_{status_writer, status_options(options_)},
      thinking_{status_writer, thinking_options(options_)} {}

CliReporter::~CliReporter() {
    status_.stop_spinner();
}

void CliReporter::on_thinking() {
    // Back to the resting state: any open reasoning block collapses to its
    // summary before the spinner takes the line back.
    thinking_.finish();
    thinking_characters_ = 0;
    status_.start_spinner("Thinking…");
}

void CliReporter::on_thinking_token(std::string_view chunk) {
    if (chunk.empty()) {
        // Redacted-thinking models emit empty payloads. The view must not open
        // on one -- but the token estimate still moves, and on those models it
        // is the only live signal the user has that anything is happening.
        return;
    }
    if (!thinking_.open()) {
        // The reasoning text takes the line from the spinner.
        status_.stop_spinner();
    }
    thinking_characters_ += static_cast<std::int64_t>(chunk.size());
    // ~4 characters per token, the same crude estimate the loop uses.
    status_.set_token_estimate(thinking_characters_ / 4);
    thinking_.write(chunk);
}

void CliReporter::on_tool_status(std::string_view detail) {
    thinking_.finish();
    status_.stop_spinner();
    status_.set(options_.style.tag(ansi::Role::Tool) + " " + std::string{detail});
}

void CliReporter::on_clear_status() {
    thinking_.finish();
    status_.stop_spinner();
    status_.clear();
}

void CliReporter::on_answer_start() {
    // Idempotent by construction: finish() and stop_spinner() are both no-ops
    // when nothing is open, and the answer must never be preceded by a stray
    // spinner frame.
    thinking_.finish();
    status_.stop_spinner();
    status_.clear();
    answer_began_ = false;
    held_.clear();
}

void CliReporter::on_answer_token(std::string_view chunk) {
    if (chunk.empty() || options_.answer_stream == nullptr) {
        return;
    }
    constexpr std::string_view kSpace = " \t\r\n";
    std::string text = held_ + std::string{chunk};
    held_.clear();
    const std::size_t last = text.find_last_not_of(kSpace);
    if (last == std::string::npos) {
        held_ = std::move(text);  // whitespace only, so far: nothing to show
        return;
    }
    if (!answer_began_) {
        // Leading blank lines go; the first line's own indentation stays.
        const std::size_t first = text.find_first_not_of(kSpace);
        const std::size_t newline = text.rfind('\n', first);
        if (newline != std::string::npos) {
            text.erase(0, newline + 1);
        }
        answer_began_ = true;
    }
    // Trailing whitespace waits for text to follow it.
    const std::size_t keep = text.find_last_not_of(kSpace) + 1;
    held_ = text.substr(keep);
    text.resize(keep);
    // Straight to stdout, not through the status writer: the answer is the one
    // thing a pipe must receive, and it must receive nothing else.
    options_.answer_stream->write(text.data(), static_cast<std::streamsize>(text.size()));
    options_.answer_stream->flush();
    emitted_ = true;
}

void CliReporter::on_answer_end() {
    held_.clear();
    if (answer_began_ && options_.answer_stream != nullptr) {
        *options_.answer_stream << "\n";
        options_.answer_stream->flush();
    }
    answer_began_ = false;
}

}  // namespace apogee::commands
