#include "cli/tui_session.h"

#include <CLI/CLI.hpp>

#include <exception>
#include <utility>
#include <vector>

#include "cli/chat_session.h"
#include "logger/session.h"
#include "views/ask_prompt.h"

namespace apogee::commands {

namespace {

/// The REPL's line reader under the shell: the view's input.
class TuiLineReader final : public LineReader {
public:
    explicit TuiLineReader(tui::SessionView& view) : view_{view} {}

    [[nodiscard]] std::optional<std::string> read(std::string_view /*prompt*/) override {
        return view_.read_line();
    }

    [[nodiscard]] bool interactive() const noexcept override {
        return true;
    }

    /// The shell closing is an interrupt's exit: the transcript is saved
    /// either way, and nothing more -- no summary, no capture -- is asked of
    /// a model once the screen is gone.
    [[nodiscard]] bool interrupted() const noexcept override {
        return view_.closed();
    }

private:
    tui::SessionView& view_;
};

}  // namespace

TuiOutput::TuiOutput(tui::SessionView& view) : view_{view} {}

void TuiOutput::print_line(const std::string& line) {
    view_.say(line);
}

void TuiOutput::set_status(const std::string& line) {
    view_.set_status(line);
}

void TuiOutput::clear_status() {
    view_.clear_status();
}

void TuiOutput::keep_line(const std::string& line) {
    view_.say(line);
}

void TuiOutput::set_resting_label(std::string label) {
    // The terminal's spinner words; the view's thinking block says its own.
    (void)label;
}

void TuiOutput::on_model_load(std::string_view backend, const harness::StatusEvent& event) {
    if (event.type != harness::StatusEvent::Type::ModelLoading) {
        return;
    }
    if (event.phase == harness::StatusEvent::Phase::Start) {
        view_.set_status("loading " + std::string{backend} + "…");
    } else {
        view_.clear_status();
    }
}

void TuiOutput::set_header(const std::string& line) {
    view_.set_header(line);
}

std::function<void(std::string_view)> TuiOutput::mcp_status() {
    // `mcp_status_line`'s rule: a warning stays, progress is transient.
    return [this](std::string_view line) {
        if (line.find("warning") != std::string_view::npos) {
            view_.say(std::string{line});
        } else {
            view_.set_status(std::string{line});
        }
    };
}

agentloop::AskFn TuiOutput::ask_fn() {
    return [this](const agentloop::QuestionRequest& request) {
        agentloop::Answers answers;
        for (std::size_t i = 0; i < request.questions.size(); ++i) {
            const agentloop::Question& question = request.questions.at(i);
            std::string header = style_.tag(ansi::Role::Permission) + " ";
            if (request.questions.size() > 1) {
                header += "(" + std::to_string(i + 1) + "/" +
                          std::to_string(request.questions.size()) + ") ";
            }
            tui::Prompt prompt{.lines = {header + question.question}, .free_text = true};
            for (std::size_t option = 0; option < question.options.size(); ++option) {
                prompt.choices.push_back(
                    tui::Choice{.key = std::to_string(option + 1),
                                .label = question.options.at(option).label,
                                .description = question.options.at(option).description});
            }
            view_.say(prompt.lines.front());
            // The terminal's reading of what was typed: a number picks an
            // option, anything else is the answer as written.
            std::string answer = chosen_answer(question, view_.ask(prompt));
            view_.say(style_.dim("  " + (answer.empty() ? std::string{"(no answer)"} : answer)));
            answers.values.push_back(std::move(answer));
        }
        return answers;
    };
}

agent::ConfirmFn TuiOutput::confirm_fn(std::filesystem::path config_path,
                                       std::shared_ptr<SessionApprovals> approvals) {
    return [this, config_path = std::move(config_path),
            approvals = std::move(approvals)](const agent::GateRequest& request) {
        // What the terminal prints, kept in the transcript; the prompt below.
        tui::Prompt prompt;
        prompt.lines.push_back(
            style_.tag(ansi::Role::Permission) + " " + std::string{request.tool} +
            (request.target.empty() ? "" : " -> " + std::string{request.target}));
        if (!request.detail.empty()) {
            // The whole URL: what would leave the machine is in it.
            prompt.lines.push_back(style_.dim("  " + std::string{request.detail}));
        }
        prompt.lines.push_back(request.outbound ? "Allow reaching this website?" : "Allow?");
        prompt.choices = {tui::Choice{.key = "y", .label = "yes"},
                          tui::Choice{.key = "n", .label = "no"},
                          tui::Choice{.key = "a", .label = "always"},
                          tui::Choice{.key = "s", .label = "session"}};
        for (std::size_t i = 0; i + 1 < prompt.lines.size(); ++i) {
            view_.say(prompt.lines.at(i));
        }
        const std::string typed = view_.ask(prompt);
        // The one answer path the terminal and machine mode take: `always`
        // writes the config through the one editor, byte for byte theirs.
        std::string note;
        const bool allowed = answer_permission(typed, request, config_path, approvals, note);
        if (!note.empty()) {
            view_.say(style_.dim("  " + note));
        }
        view_.say(style_.dim(allowed ? "  allowed" : "  denied"));
        return allowed;
    };
}

std::function<bool(const std::string&)> TuiOutput::confirm_large() {
    return [this](const std::string& question) {
        const std::string line = style_.tag(ansi::Role::Warning) + " " + question;
        view_.say(line);
        const std::string typed =
            view_.ask(tui::Prompt{.lines = {line},
                                  .choices = {tui::Choice{.key = "y", .label = "yes"},
                                              tui::Choice{.key = "n", .label = "no"}}});
        return typed == "y";
    };
}

std::unique_ptr<LineReader> TuiOutput::line_reader(EditingLineReader::Options options) {
    view_.set_completion(std::move(options.suggest));
    return std::make_unique<TuiLineReader>(view_);
}

harness::CancellationToken TuiOutput::begin_turn() {
    harness::CancellationToken token = harness::CancellationToken::create();
    view_.begin_turn(token);
    return token;
}

void TuiOutput::end_turn() {
    view_.end_turn();
}

std::shared_ptr<SessionFlags> shell_session_flags(CLI::App& app, const std::string& chat_id) {
    auto flags = std::make_shared<SessionFlags>();
    bind_session_flags(app, flags, SessionMode::Chat);
    std::vector<std::string> args{"chat", "--tools"};
    if (!chat_id.empty()) {
        args.emplace_back("--resume");
        args.push_back(chat_id);
    }
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    app.parse(static_cast<int>(argv.size()), argv.data());
    return flags;
}

TuiSessionDriver::TuiSessionDriver(tui::SessionView& view, const RootContext& context,
                                   MachineBudgetSource machine)
    : view_{view}, context_{context}, machine_{std::move(machine)} {}

TuiSessionDriver::~TuiSessionDriver() {
    stop();
}

namespace {

[[nodiscard]] std::vector<tui::PickerEntry> saved_chats() {
    std::vector<tui::PickerEntry> entries;
    for (const logger::Session& saved : logger::list_sessions()) {
        entries.push_back(tui::PickerEntry{.id = saved.chat_id,
                                           .name = saved.display_name(),
                                           .updated = saved.updated_at,
                                           .turns = saved.turns});
    }
    return entries;
}

}  // namespace

void TuiSessionDriver::start() {
    if (saved_chats().empty()) {
        open({});
        return;
    }
    show_picker();
}

void TuiSessionDriver::show_picker() {
    view_.show_picker(saved_chats(), [this](std::string chat_id) { open(std::move(chat_id)); });
}

void TuiSessionDriver::open(std::string chat_id) {
    if (worker_.joinable()) {
        worker_.join();  // the last conversation, ended: its thread is done
    }
    if (view_.closed()) {
        return;
    }
    worker_ = std::thread{[this, id = std::move(chat_id)]() { run(id); }};
}

void TuiSessionDriver::run(const std::string& chat_id) {
    view_.begin_session();
    TuiOutput output{view_};
    try {
        CLI::App app{"the shell's conversation", "chat"};
        const std::shared_ptr<SessionFlags> flags = shell_session_flags(app, chat_id);
        run_session(context_, *flags, machine_, SessionMode::Chat, &output);
    } catch (const CLI::RuntimeError&) {
        // The session said why it could not go on, on stderr -- the shell's
        // notice row while it holds the screen.
    } catch (const std::exception& e) {
        view_.say(output.style().tag(ansi::Role::Error) + " " + e.what());
    }
    if (!view_.closed()) {
        show_picker();  // the chat just left among the saved
    }
}

void TuiSessionDriver::stop() {
    view_.close();
    if (worker_.joinable()) {
        worker_.join();
    }
}

}  // namespace apogee::commands
