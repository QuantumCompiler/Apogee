#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "agentloop/reporter.h"
#include "ansi/ansi.h"
#include "commands/status_line.h"
#include "commands/terminal.h"
#include "commands/thinking_view.h"

/// The one adapter from `agentloop::Reporter` to a terminal.
///
/// **One implementation, not one per command.** `complete`, `chat`, and
/// anything else interactive all render through this; a second adapter would
/// drift from the first, and a capability that reaches only one surface is the
/// exact parity bug the Reporter interface exists to prevent.
///
/// The split of streams is load-bearing: **the answer goes to stdout, progress
/// goes to the status line on stderr.** That is what keeps
/// `apogee complete "..." | jq` working — otherwise spinner frames and tool
/// notices land in the piped output.
namespace apogee::commands {

class CliReporter final : public agentloop::Reporter {
public:
    struct Options {
        /// Where the answer goes. Usually std::cout.
        std::ostream* answer_stream = nullptr;
        /// Whether the terminal is interactive: gates every escape code.
        bool decorate = true;
        ansi::Verbosity verbosity = ansi::Verbosity::Line;
        ansi::Style style;
        /// Terminal width for the thinking view's pre-wrapping.
        std::size_t width = 80;
    };

    /// `status_writer` is the progress channel (stderr); `options.answer_stream`
    /// is the answer channel (stdout). They are separate objects because they
    /// are separate streams -- and conflating them is how decoration ends up in
    /// a pipe.
    CliReporter(TerminalWriter& status_writer, Options options);
    ~CliReporter() override;

    CliReporter(const CliReporter&) = delete;
    CliReporter& operator=(const CliReporter&) = delete;
    CliReporter(CliReporter&&) = delete;
    CliReporter& operator=(CliReporter&&) = delete;

    void on_thinking() override;
    void on_thinking_token(std::string_view chunk) override;
    void on_tool_status(std::string_view detail) override;
    void on_clear_status() override;
    void on_answer_start() override;
    void on_answer_token(std::string_view chunk) override;
    void on_answer_end() override;

    /// Whether any answer text was emitted. A caller uses this to decide
    /// whether to print a fallback.
    [[nodiscard]] bool emitted_answer() const noexcept {
        return emitted_;
    }

    /// The status line, so a surface can route its own startup notices through
    /// it rather than writing raw stderr.
    [[nodiscard]] StatusLine& status() noexcept {
        return status_;
    }

private:
    Options options_;
    StatusLine status_;
    ThinkingView thinking_;
    std::int64_t thinking_characters_ = 0;
    bool emitted_ = false;
};

}  // namespace apogee::commands
