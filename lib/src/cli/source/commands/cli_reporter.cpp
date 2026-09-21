#include "commands/cli_reporter.h"

#include <utility>

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
}

void CliReporter::on_answer_token(std::string_view chunk) {
    if (chunk.empty() || options_.answer_stream == nullptr) {
        return;
    }
    // Straight to stdout, not through the status writer: the answer is the one
    // thing a pipe must receive, and it must receive nothing else.
    options_.answer_stream->write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    options_.answer_stream->flush();
    emitted_ = true;
}

void CliReporter::on_answer_end() {
    if (emitted_ && options_.answer_stream != nullptr) {
        *options_.answer_stream << "\n";
        options_.answer_stream->flush();
    }
}

}  // namespace apogee::commands
