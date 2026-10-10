#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "agent/tool.h"
#include "agentloop/question.h"
#include "agentloop/reporter.h"
#include "ansi/ansi.h"
#include "cli/permissions.h"
#include "contracts/cancellation.h"
#include "contracts/provider.h"
#include "views/cli_reporter.h"
#include "views/line_reader.h"
#include "views/terminal.h"

/// Where a session's REPL writes and reads (32c): the terminal -- the status
/// line, the line editor, the answer view -- or the full-screen shell's
/// session view. One seam, so chat's session core (`run_session`) runs
/// unchanged under either: every line it keeps, every status it sets, every
/// question and permission prompt it puts and every line it reads crosses
/// here, and nothing else in the core knows which it is talking to.
namespace apogee::commands {

class SessionOutput {
public:
    SessionOutput() = default;
    SessionOutput(const SessionOutput&) = delete;
    SessionOutput& operator=(const SessionOutput&) = delete;
    SessionOutput(SessionOutput&&) = delete;
    SessionOutput& operator=(SessionOutput&&) = delete;
    virtual ~SessionOutput() = default;

    /// What the session's turns report to.
    [[nodiscard]] virtual agentloop::Reporter& reporter() = 0;
    /// The look the session writes its lines in.
    [[nodiscard]] virtual const ansi::Style& style() const = 0;
    /// Whether this is the terminal's own discipline: the registration offer
    /// asked on stdin, the startup typeahead discarded, keystrokes hidden while
    /// a turn runs.
    [[nodiscard]] virtual bool terminal() const = 0;
    /// The width `/help` lays its rows out to; 0 for none.
    [[nodiscard]] virtual std::size_t width() const = 0;

    /// A line kept above whatever is transient.
    virtual void print_line(const std::string& line) = 0;
    /// The transient status, replaced by the next.
    virtual void set_status(const std::string& line) = 0;
    virtual void clear_status() = 0;
    /// A line kept above a turn's thinking block (26n): a turn's warning.
    virtual void keep_line(const std::string& line) = 0;
    /// What the turn's resting state says: `Thinking… · base model` (26r).
    virtual void set_resting_label(std::string label) = 0;
    /// A model loading for any reason, said where the turn is (27e).
    virtual void on_model_load(std::string_view backend, const harness::StatusEvent& event) = 0;

    /// The session's model, suite and chat, for a surface that shows them
    /// apart from the transcript; the terminal says them in its banner.
    virtual void set_header(const std::string& line) {
        (void)line;
    }

    /// Where an MCP server's startup lines go.
    [[nodiscard]] virtual std::function<void(std::string_view)> mcp_status() = 0;
    /// The `ask_user` answerer, or null where nobody can answer.
    [[nodiscard]] virtual agentloop::AskFn ask_fn() = 0;
    /// The permission prompt, or null where nobody can answer -- `always`
    /// and `session` applied through `answer_permission`, the one path.
    [[nodiscard]] virtual agent::ConfirmFn confirm_fn(
        std::filesystem::path config_path, std::shared_ptr<SessionApprovals> approvals) = 0;
    /// Asks whether a folder over the size guard should be attached (26d);
    /// null where nobody can answer, and it is refused.
    [[nodiscard]] virtual std::function<bool(const std::string&)> confirm_large() = 0;
    /// The REPL's line reader, completing through `options.suggest`.
    [[nodiscard]] virtual std::unique_ptr<LineReader> line_reader(
        EditingLineReader::Options options) = 0;

    /// A turn begins: the token that stops it. The terminal's never does --
    /// Ctrl-C there ends the process, as it always has; the shell's is
    /// stopped by Ctrl-C and the session goes on, as a driver's `cancel` ends
    /// a machine-mode turn (28f).
    [[nodiscard]] virtual harness::CancellationToken begin_turn() = 0;
    virtual void end_turn() = 0;
};

/// The terminal's: `CliReporter` over a status line on stderr and the answer
/// on stdout, decorated when stdout is a terminal -- exactly what the session
/// core built for itself before the seam.
class TerminalOutput final : public SessionOutput {
public:
    struct Options {
        bool decorate = false;
        bool verbose = false;
        bool no_color = false;
        bool markdown = false;
    };

    explicit TerminalOutput(Options options);
    ~TerminalOutput() override = default;

    TerminalOutput(const TerminalOutput&) = delete;
    TerminalOutput& operator=(const TerminalOutput&) = delete;
    TerminalOutput(TerminalOutput&&) = delete;
    TerminalOutput& operator=(TerminalOutput&&) = delete;

    [[nodiscard]] agentloop::Reporter& reporter() override {
        return reporter_;
    }

    [[nodiscard]] const ansi::Style& style() const override {
        return style_;
    }

    [[nodiscard]] bool terminal() const override {
        return true;
    }

    [[nodiscard]] std::size_t width() const override;

    void print_line(const std::string& line) override;
    void set_status(const std::string& line) override;
    void clear_status() override;
    void keep_line(const std::string& line) override;
    void set_resting_label(std::string label) override;
    void on_model_load(std::string_view backend, const harness::StatusEvent& event) override;

    [[nodiscard]] std::function<void(std::string_view)> mcp_status() override;
    [[nodiscard]] agentloop::AskFn ask_fn() override;
    [[nodiscard]] agent::ConfirmFn confirm_fn(std::filesystem::path config_path,
                                              std::shared_ptr<SessionApprovals> approvals) override;
    [[nodiscard]] std::function<bool(const std::string&)> confirm_large() override;
    [[nodiscard]] std::unique_ptr<LineReader> line_reader(
        EditingLineReader::Options options) override;

    [[nodiscard]] harness::CancellationToken begin_turn() override {
        return {};
    }

    void end_turn() override {}

private:
    [[nodiscard]] static CliReporter::Options reporter_options(const Options& options);

    CliReporter::Options reporter_options_;
    ansi::Style style_;
    TerminalWriter status_writer_{std::cerr};
    CliReporter reporter_;
};

}  // namespace apogee::commands
