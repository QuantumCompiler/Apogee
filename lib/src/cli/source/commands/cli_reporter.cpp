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

AnswerView::Options answer_options(const CliReporter::Options& options) {
    AnswerView::Options out;
    out.out = options.answer_stream;
    out.style = options.style;
    out.hyperlinks = options.hyperlinks;
    out.width = options.width;
    out.measure_width = []() -> std::size_t {
        const std::optional<int> width = platform::terminal_width();
        return width.has_value() && *width > 0 ? static_cast<std::size_t>(*width) : 0;
    };
    out.measure_height = []() -> std::size_t {
        const std::optional<int> height = platform::terminal_height();
        return height.has_value() && *height > 0 ? static_cast<std::size_t>(*height) : 0;
    };
    return out;
}

}  // namespace

CliReporter::CliReporter(TerminalWriter& status_writer, Options options)
    : options_{std::move(options)},
      status_{status_writer, status_options(options_)},
      thinking_{status_writer, thinking_options(options_)} {
    if (options_.decorate && options_.markdown && options_.answer_stream != nullptr) {
        answer_view_.emplace(answer_options(options_));
    }
}

CliReporter::~CliReporter() {
    settle_answer();
    status_.stop_spinner();
}

void CliReporter::settle_answer() {
    if (answer_view_.has_value() && answer_view_->open()) {
        answer_view_->finish();
    }
}

void CliReporter::on_thinking() {
    settle_answer();
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
    settle_answer();
    thinking_.finish();
    status_.stop_spinner();
    status_.set(options_.style.tag(ansi::Role::Tool) + " " + std::string{detail});
}

void CliReporter::on_notice(std::string_view text) {
    // A line that stays, above the spinner -- the way an MCP server that
    // failed to connect stays readable once the prompt is up.
    settle_answer();
    thinking_.finish();
    status_.print_line(options_.style.tag(ansi::Role::Warning) + " " + std::string{text});
}

void CliReporter::on_clear_status() {
    settle_answer();
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
    if (answer_view_.has_value()) {
        answer_view_->begin();
        return;
    }
    answer_began_ = false;
    held_.clear();
}

void CliReporter::on_answer_token(std::string_view chunk) {
    if (chunk.empty() || options_.answer_stream == nullptr) {
        return;
    }
    if (answer_view_.has_value()) {
        answer_view_->write(chunk);
        emitted_ = emitted_ || answer_view_->began();
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
    if (answer_view_.has_value()) {
        answer_view_->finish();
        return;
    }
    held_.clear();
    if (answer_began_ && options_.answer_stream != nullptr) {
        *options_.answer_stream << "\n";
        options_.answer_stream->flush();
    }
    answer_began_ = false;
}

}  // namespace apogee::commands
