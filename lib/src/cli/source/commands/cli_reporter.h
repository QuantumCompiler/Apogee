#pragma once

#include <memory>
#include <optional>
#include <ostream>
#include <string>

#include "agentloop/reporter.h"
#include "ansi/ansi.h"
#include "commands/answer_view.h"
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
        /// Render the answer's Markdown where decorating. Off by default: the
        /// surfaces that render opt in (`chat`, `complete`), and `--raw` or
        /// `ui.markdown: false` turns it off there.
        bool markdown = false;
        /// Links in a rendered answer as OSC 8 hyperlinks.
        bool hyperlinks = false;
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
    /// Commits an answer still open before anything else takes the terminal:
    /// a tool status or a spinner painted over a half-written line would
    /// erase it.
    void settle_answer();

    Options options_;
    StatusLine status_;
    ThinkingView thinking_;
    /// The rendered answer, present only when decorating with Markdown on.
    /// Absent, answers go to the stream as written -- the pipe contract.
    std::optional<AnswerView> answer_view_;
    std::int64_t thinking_characters_ = 0;
    bool emitted_ = false;
    /// Whether the current answer has shown any text yet. Until it has, its
    /// whitespace is held: a thinking model opens its answer with the blank
    /// lines that followed its reasoning, which printed as a gap under the
    /// "Thought for" line.
    bool answer_began_ = false;
    /// Whitespace not yet written -- the leading run before the first text,
    /// then whatever trails the latest chunk. Written once text follows it,
    /// dropped at the end, so an answer neither starts nor ends with blank
    /// lines.
    std::string held_;
};

}  // namespace apogee::commands
