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

TuiOutput::TuiOutput(tui::SessionView& view, std::function<void(std::vector<std::string>)> held)
    : view_{view}, held_{std::move(held)} {}

void TuiOutput::set_held(std::vector<std::string> backends) {
    if (held_) {
        held_(std::move(backends));
    }
}

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

std::shared_ptr<SessionFlags> shell_session_flags(CLI::App& app, const std::string& chat_id,
                                                  const std::string& suite,
                                                  const std::string& model) {
    auto flags = std::make_shared<SessionFlags>();
    bind_session_flags(app, flags, SessionMode::Chat);
    std::vector<std::string> args{"chat", "--tools"};
    if (!chat_id.empty()) {
        args.emplace_back("--resume");
        args.push_back(chat_id);
    }
    if (!suite.empty()) {
        args.emplace_back("--suite");
        args.push_back(suite);
    }
    if (!model.empty()) {
        args.emplace_back("--model");
        args.push_back(model);
    }
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    app.parse(static_cast<int>(argv.size()), argv.data());
    return flags;
}

TuiSessionDriver::TuiSessionDriver(tui::SessionView& view, tui::Pump& pump,
                                   const RootContext& context, MachineBudgetSource machine,
                                   std::function<void(std::vector<std::string>)> held)
    : view_{view},
      pump_{pump},
      held_{std::move(held)},
      context_{context},
      machine_{std::move(machine)} {}

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

void TuiSessionDriver::open(std::string chat_id, std::string suite, std::string model) {
    if (worker_.joinable()) {
        worker_.join();  // the last conversation, ended: its thread is done
    }
    if (view_.closed()) {
        return;
    }
    open_ = true;
    worker_ = std::thread{[this, id = std::move(chat_id), suite = std::move(suite),
                           model = std::move(model)]() { run(id, suite, model); }};
}

std::string TuiSessionDriver::open_chat(const std::string& chat_id) {
    if (!open_) {
        open(chat_id);
        return "opened " + chat_id;
    }
    if (!view_.waiting_for_line()) {
        return "not now: the conversation is busy -- Ctrl-C there stops a turn";
    }
    // The open one ends as /exit ends it -- saved, summarised when due --
    // and the chosen one opens after it.
    pending_ = chat_id;
    (void)view_.enter("/exit");
    return "opened " + chat_id;
}

std::string TuiSessionDriver::use_suite(const std::string& suite) {
    if (!open_) {
        open({}, suite);
        return "a new chat under suite " + suite;
    }
    if (!view_.enter("/suite " + suite)) {
        return "not now: the conversation is busy -- Ctrl-C there stops a turn";
    }
    return "/suite " + suite + " sent to the conversation";
}

std::string TuiSessionDriver::use_model(const std::string& model) {
    if (!open_) {
        open({}, {}, model);
        return "a new chat on " + model;
    }
    if (!view_.enter("/model " + model)) {
        return "not now: the conversation is busy -- Ctrl-C there stops a turn";
    }
    return "/model " + model + " sent to the conversation";
}

void TuiSessionDriver::run(const std::string& chat_id, const std::string& suite,
                           const std::string& model) {
    view_.begin_session();
    TuiOutput output{view_, held_};
    try {
        CLI::App app{"the shell's conversation", "chat"};
        const std::shared_ptr<SessionFlags> flags = shell_session_flags(app, chat_id, suite, model);
        run_session(context_, *flags, machine_, SessionMode::Chat, &output);
    } catch (const CLI::RuntimeError&) {
        // The session said why it could not go on, on stderr -- the shell's
        // notice row while it holds the screen.
    } catch (const std::exception& e) {
        view_.say(output.style().tag(ansi::Role::Error) + " " + e.what());
    }
    open_ = false;
    if (view_.closed()) {
        return;
    }
    // Opened from the workbench while this one was open: that one next, from
    // the shell's thread, which joins this one first.
    pump_.post([this]() {
        if (pending_.has_value()) {
            open(*std::exchange(pending_, std::nullopt));
            return;
        }
        show_picker();  // the chat just left among the saved
    });
}

void TuiSessionDriver::stop() {
    view_.close();
    if (worker_.joinable()) {
        worker_.join();
    }
}

}  // namespace apogee::commands
