#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "cli/chat_session.h"
#include "cli/command.h"
#include "cli/session_output.h"
#include "cli/suite_residency.h"
#include "tui/session_view.h"

/// The full-screen shell's conversation (32c): chat's own session core,
/// `run_session`, on a thread of its own, with the shell's session view as
/// its front-end -- the composition root's half of the session view.
///
/// Nothing of a conversation is written here: the turns, the slash commands,
/// resume, recall, compaction, the per-turn save and the title are the core's,
/// exactly as `apogee chat` runs them, so a chat started in the shell resumes
/// with `apogee chat --resume` and one started there opens in the shell.
namespace apogee::commands {

/// The session view as the session core's front-end.
class TuiOutput final : public SessionOutput {
public:
    /// `held`, when given, hears what the session holds (the monitor bar's).
    explicit TuiOutput(tui::SessionView& view,
                       std::function<void(std::vector<std::string>)> held = {});

    [[nodiscard]] agentloop::Reporter& reporter() override {
        return view_.reporter();
    }

    /// The terminal's looks, always: the view paints them, dropping colour
    /// itself when the theme has none.
    [[nodiscard]] const ansi::Style& style() const override {
        return style_;
    }

    [[nodiscard]] bool terminal() const override {
        return false;
    }

    [[nodiscard]] std::size_t width() const override {
        return 0;
    }

    void print_line(const std::string& line) override;
    void set_status(const std::string& line) override;
    void clear_status() override;
    void keep_line(const std::string& line) override;
    void set_resting_label(std::string label) override;
    void on_model_load(std::string_view backend, const harness::StatusEvent& event) override;
    void set_header(const std::string& line) override;
    void set_held(std::vector<std::string> backends) override;

    [[nodiscard]] std::function<void(std::string_view)> mcp_status() override;
    [[nodiscard]] agentloop::AskFn ask_fn() override;
    [[nodiscard]] agent::ConfirmFn confirm_fn(std::filesystem::path config_path,
                                              std::shared_ptr<SessionApprovals> approvals) override;
    [[nodiscard]] std::function<bool(const std::string&)> confirm_large() override;
    [[nodiscard]] std::unique_ptr<LineReader> line_reader(
        EditingLineReader::Options options) override;

    [[nodiscard]] harness::CancellationToken begin_turn() override;
    void end_turn() override;

private:
    tui::SessionView& view_;
    std::function<void(std::vector<std::string>)> held_;
    ansi::Style style_{true};
};

/// The flags a conversation in the shell runs under: `apogee chat --tools`,
/// resuming `chat_id` when one is named, parsed through chat's own flag table
/// into `app` -- which must outlive them, since a flag's presence is read
/// from its option. Tools are on because a person is at the screen to answer
/// every gated call -- the condition `--tools` exists to require. In execute
/// mode (37d) it is `apogee execute --tools`, through execute's own table.
[[nodiscard]] std::shared_ptr<SessionFlags> shell_session_flags(
    CLI::App& app, const std::string& chat_id, const std::string& suite = {},
    const std::string& model = {}, SessionMode mode = SessionMode::Chat);

/// The session view's driver: the picker over the saved conversations --
/// straight into a new chat when there are none -- and a chosen one run as
/// chat's session on a thread of its own, back to the picker when it ends.
/// Since 37d the picker's execute door: one per configured suite, the default
/// first, each opening execute's session under it.
class TuiSessionDriver {
public:
    /// `held` hears what each conversation holds in memory (the monitor bar).
    TuiSessionDriver(tui::SessionView& view, tui::Pump& pump, const RootContext& context,
                     MachineBudgetSource machine,
                     std::function<void(std::vector<std::string>)> held = {});
    ~TuiSessionDriver();

    TuiSessionDriver(const TuiSessionDriver&) = delete;
    TuiSessionDriver& operator=(const TuiSessionDriver&) = delete;
    TuiSessionDriver(TuiSessionDriver&&) = delete;
    TuiSessionDriver& operator=(TuiSessionDriver&&) = delete;

    /// The picker, or a new chat when nothing is saved.
    void start();
    /// The picker, whatever is saved: where a conversation that ended leaves
    /// the view.
    void show_picker();
    /// The shell is ending: the conversation stops at its next line -- a
    /// running turn cancelled, nothing summarised or captured on the way out
    /// (an interrupt's exit), the transcript already saved -- and its thread
    /// is joined.
    void stop();

    /// Opens saved chat `chat_id` in the view -- the conversation open now,
    /// if any, ended first as `/exit` ends it. Returns what was done, or why
    /// not: a turn running is never cut off. The shell's thread.
    [[nodiscard]] std::string open_chat(const std::string& chat_id);
    /// Moves the conversation to suite `suite` through its own `/suite`, or,
    /// with none open, opens a new chat under it (`chat --suite`). Returns
    /// what was done, or why not. The shell's thread.
    [[nodiscard]] std::string use_suite(const std::string& suite);
    /// Moves the conversation onto `model` through its own `/model` (35), or,
    /// with none open, opens a new chat on it (`chat -m`). Returns what was
    /// done, or why not. The shell's thread.
    [[nodiscard]] std::string use_model(const std::string& model);
    /// Opens execute's session under suite `suite` (37d), as `apogee execute
    /// --suite` opens one -- the conversation open now ended first, as
    /// `/exit` ends it. Returns what was done, or why not. The shell's thread.
    [[nodiscard]] std::string open_execute(const std::string& suite);

    /// What handing a play to the session came to.
    struct Handed {
        /// The line reached the session: it is the session's to answer now.
        bool sent = false;
        std::string said;
    };

    /// Plays `symphony` through the session's own `/play` (37d): the line
    /// entered in the execute session open now, or, with none open, the
    /// first line of a new one under the config's default suite. Refused,
    /// saying why, when the conversation open is a chat (`/play` is not in
    /// its table), is busy, or no suite is the default. The walk's own
    /// refusals are the session's to say. The shell's thread.
    [[nodiscard]] Handed play(const std::string& symphony, const std::string& input);

private:
    /// A conversation to open: what `chat` or `execute` is given.
    struct Opening {
        std::string chat_id;
        std::string suite;
        std::string model;
        SessionMode mode = SessionMode::Chat;
        /// A line it takes first, as if typed (37d: a play asked for).
        std::string first_line;
    };

    void open(Opening opening);
    void run(const Opening& opening);

    tui::SessionView& view_;
    tui::Pump& pump_;
    std::function<void(std::vector<std::string>)> held_;
    /// A conversation is open on the worker.
    std::atomic<bool> open_{false};
    /// The one open is execute's (37d).
    std::atomic<bool> execute_{false};
    /// What to open once the conversation now ending has ended.
    std::optional<Opening> pending_;
    const RootContext& context_;
    MachineBudgetSource machine_;
    std::thread worker_;
};

}  // namespace apogee::commands
