#include "acp/session.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace apogee::acp {

namespace {

/// The most of a child's stderr kept, as a vendor CLI's is: its last lines
/// are what a failure says.
constexpr std::size_t kTailBytes = 8 * 1024;
/// How long one read waits before it looks again.
constexpr std::chrono::milliseconds kSlice{100};

[[nodiscard]] std::string string_of(const nlohmann::json& object, std::string_view key) {
    const auto found = object.find(std::string{key});
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/// The request that opened a session, as the host sent it.
[[nodiscard]] std::string_view opening_method(OpenAs as) {
    switch (as) {
        case OpenAs::Load:
            return "session/load";
        case OpenAs::Resume:
            return "session/resume";
        case OpenAs::New:
            break;
    }
    return "session/new";
}

}  // namespace

void Diagnostics::line(std::string_view text) {
    const std::lock_guard lock{mutex_};
    (*err_) << "apogee acp: " << text << "\n";
    err_->flush();
}

void Diagnostics::raw(std::string_view bytes) {
    const std::lock_guard lock{mutex_};
    (*err_) << bytes;
    err_->flush();
}

Session::Session(std::string id, std::filesystem::path cwd, const AgentSetup& setup,
                 const HostCaps& host, Peer& peer, Diagnostics& diagnostics)
    : id_{std::move(id)},
      cwd_{std::move(cwd)},
      setup_{setup},
      host_{host},
      peer_{peer},
      diagnostics_{diagnostics},
      translator_{cwd_, setup.home},
      models_{setup.models},
      modes_{offered_modes(setup.bypass_allowed, setup.mode)},
      model_{setup.model},
      fallback_model_{setup.model},
      mode_{setup.mode},
      fallback_mode_{setup.mode} {}

Session::~Session() {
    end();
    wait(std::chrono::seconds{10});
    discard_handover(handover_);
}

std::string Session::id() {
    const std::unique_lock lock{mutex_};
    return id_;
}

void Session::open(const nlohmann::json& request_id, const HostServers& servers, OpenAs as,
                   std::vector<SavedEntry> replay) {
    platform::ChildCommand command = setup_.child;
    command.working_directory = cwd_.string();
    opened_as_ = as;
    replay_ = std::move(replay);
    std::string error;
    if (!servers.empty()) {
        // Names and env keys on stderr, a host's log -- never a value.
        if (!servers.servers.empty()) {
            diagnostics_.line("session " + id_ + ": the host's MCP servers: " + describe(servers));
        }
        // The values ride a file only the child reads, never its argv or
        // its environment; a handover that cannot be made leaves the
        // session without them, said, and never fatal.
        handover_ = write_handover(servers, setup_.handover_root, error);
        if (handover_.empty()) {
            diagnostics_.line("session " + id_ +
                              ": the host's MCP servers could not be handed to it: " + error);
            error.clear();
        } else {
            command.arguments.emplace_back(kHandoverOption);
            command.arguments.push_back(handover_.string());
        }
    }
    if (as == OpenAs::New) {
        // A new chat, the host's (56c): who began it and in which folder,
        // saved with its first answered turn -- the folder as the host
        // named it, since the child's own reads back resolved (`/tmp` is
        // `/private/tmp` on macOS) and a host filters by its own spelling.
        command.arguments.emplace_back(kOriginOption);
        command.arguments.emplace_back(host_.name.empty() ? std::string{kNoClientOrigin}
                                                          : host_.name);
        command.arguments.emplace_back(kCwdOption);
        command.arguments.push_back(cwd_.string());
    } else {
        // A saved chat: execute's own resume, its notes and its fallbacks.
        command.arguments.emplace_back("--resume");
        command.arguments.push_back(id_);
    }
    std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    const std::unique_lock lock{mutex_};
    if (child == nullptr) {
        discard_handover(std::exchange(handover_, {}));
        ended_ = true;
        ended_reason_ = "apogee's session could not start: " + error;
        peer_.send(error_response(request_id, kInternalError, ended_reason_));
        return;
    }
    child_ = std::move(child);
    open_id_ = request_id;
    // Who is driving, on the first line, as a driver introduces itself.
    (void)write_child(hello_line(host_.name, host_.version));
    stderr_reader_ = std::thread{[this] { drain_stderr(); }};
    reader_ = std::thread{[this] { read_events(); }};
}

bool Session::write_child(const std::string& line) {
    if (child_ == nullptr || ended_) {
        return false;
    }
    return child_->write_stdin(line + "\n");
}

void Session::send_update(nlohmann::json update) {
    peer_.send(notification("session/update",
                            nlohmann::json{{"sessionId", id_}, {"update", std::move(update)}}));
}

nlohmann::json Session::options() const {
    return config_options(models_, model_, modes_, mode_);
}

void Session::prompt(const nlohmann::json& request_id, const nlohmann::json& params) {
    const std::unique_lock lock{mutex_};
    if (ended_ || ending_) {
        peer_.send(
            error_response(request_id, kInternalError,
                           ended_reason_.empty() ? "this session has ended" : ended_reason_));
        return;
    }
    if (!started_) {
        peer_.send(error_response(request_id, kInvalidParams,
                                  "session " + id_ + " is still opening: wait for " +
                                      std::string{opening_method(opened_as_)} + "'s answer"));
        return;
    }
    if (prompt_id_.has_value()) {
        peer_.send(error_response(request_id, kInvalidParams,
                                  "a prompt is already running in session " + id_ +
                                      ": wait for its answer, or cancel it"));
        return;
    }
    std::string error;
    const nlohmann::json blocks =
        params.is_object() && params.contains("prompt") ? params.at("prompt") : nlohmann::json{};
    const std::string text = prompt_text(blocks, error);
    if (!error.empty()) {
        peer_.send(error_response(request_id, kInvalidParams, error));
        return;
    }
    prompt_id_ = request_id;
    ++turn_;
    cancel_sent_ = false;
    // What the session said while no turn ran -- the project's context, the
    // persona, a setting moved -- leads this turn's thought stream.
    for (const std::string& said : std::exchange(pending_notices_, {})) {
        send_update(thought_update(said + "\n"));
    }
    if (!write_child(user_line(text))) {
        // Its end is said by the reader, with the child's last words.
        diagnostics_.line("session " + id_ + ": the prompt could not reach apogee's session");
    }
}

void Session::cancel() {
    const std::unique_lock lock{mutex_};
    if (prompt_id_.has_value() && !cancel_sent_) {
        cancel_sent_ = true;
        (void)write_child(cancel_line());
    }
}

bool Session::prompting(const nlohmann::json& request_id) {
    const std::unique_lock lock{mutex_};
    return prompt_id_.has_value() && *prompt_id_ == request_id;
}

void Session::set_config_option(const nlohmann::json& request_id, const nlohmann::json& params) {
    std::unique_lock lock{mutex_};
    if (ended_ || ending_) {
        peer_.send(
            error_response(request_id, kInternalError,
                           ended_reason_.empty() ? "this session has ended" : ended_reason_));
        return;
    }
    const std::string option = string_of(params, "configId");
    const std::string value = string_of(params, "value");
    if (option == "model") {
        if (std::ranges::find(models_, value, &ModelChoice::value) == models_.end()) {
            peer_.send(error_response(request_id, kInvalidParams,
                                      "no model '" + value + "' is offered in this session"));
            return;
        }
        choose("model", value);
        peer_.send(response(request_id, nlohmann::json{{"configOptions", options()}}));
        return;
    }
    if (option == "mode") {
        if (std::ranges::find(modes_, std::string_view{value}, &ModeChoice::id) == modes_.end()) {
            peer_.send(
                error_response(request_id, kInvalidParams,
                               "no permission mode '" + value + "' is offered in this session"));
            return;
        }
        choose("permission_mode", value);
        peer_.send(response(request_id, nlohmann::json{{"configOptions", options()}}));
        send_update(
            nlohmann::json{{"sessionUpdate", "current_mode_update"}, {"currentModeId", mode_}});
        return;
    }
    peer_.send(
        error_response(request_id, kInvalidParams,
                       "no config option '" + option + "': this session has model and mode"));
}

void Session::set_mode(const nlohmann::json& request_id, const nlohmann::json& params) {
    const std::unique_lock lock{mutex_};
    if (ended_ || ending_) {
        peer_.send(
            error_response(request_id, kInternalError,
                           ended_reason_.empty() ? "this session has ended" : ended_reason_));
        return;
    }
    const std::string value = string_of(params, "modeId");
    if (std::ranges::find(modes_, std::string_view{value}, &ModeChoice::id) == modes_.end()) {
        peer_.send(error_response(request_id, kInvalidParams,
                                  "no permission mode '" + value + "' is offered in this session"));
        return;
    }
    choose("permission_mode", value);
    peer_.send(response(request_id, nlohmann::json::object()));
    // The same setting as the config option: a host that draws that sees it
    // move too.
    send_update(
        nlohmann::json{{"sessionUpdate", "config_option_update"}, {"configOptions", options()}});
}

void Session::choose(std::string_view key, const std::string& value) {
    const bool model = key == "model";
    std::string& current = model ? model_ : mode_;
    std::string& fallback = model ? fallback_model_ : fallback_mode_;
    std::optional<std::string>& held = model ? held_model_ : held_mode_;
    // What the child runs is the menu's value before the first change it has
    // not been told of: a refusal sets the menu back to it.
    if (!held.has_value()) {
        fallback = current;
    }
    current = value;
    if (prompt_id_.has_value()) {
        // A turn runs: the child takes a setting between turns anyway, and a
        // line written now could be read -- and dropped -- by a question the
        // turn asks. Held, it is written as the turn ends.
        held = value;
        return;
    }
    (void)write_child(set_line(key, value));
}

void Session::write_held() {
    if (held_model_.has_value()) {
        (void)write_child(set_line("model", *held_model_));
        held_model_.reset();
    }
    if (held_mode_.has_value()) {
        (void)write_child(set_line("permission_mode", *held_mode_));
        held_mode_.reset();
    }
}

bool Session::finished() {
    const std::unique_lock lock{mutex_};
    return ended_ && asking_ == 0;
}

void Session::end() {
    const std::unique_lock lock{mutex_};
    if (ending_ || child_ == nullptr) {
        ending_ = true;
        return;
    }
    ending_ = true;
    if (!ended_) {
        if (prompt_id_.has_value() && !cancel_sent_) {
            cancel_sent_ = true;
            (void)write_child(cancel_line());
        }
        child_->close_stdin();
    }
}

void Session::wait(std::chrono::milliseconds budget) {
    {
        std::unique_lock lock{mutex_};
        if (child_ != nullptr && !done_.wait_for(lock, budget, [this] { return ended_; })) {
            diagnostics_.line("session " + id_ + ": apogee's session did not end -- terminated");
            child_->terminate();
        }
    }
    if (reader_.joinable()) {
        reader_.join();
    }
}

void Session::drain_stderr() {
    std::string chunk;
    for (;;) {
        const platform::ReadStatus status = child_->read_stderr(chunk, kSlice);
        if (status == platform::ReadStatus::Data) {
            {
                const std::lock_guard lock{tail_mutex_};
                tail_ += chunk;
                if (tail_.size() > kTailBytes) {
                    tail_.erase(0, tail_.size() - kTailBytes);
                }
            }
            if (setup_.verbose) {
                diagnostics_.raw(chunk);
            }
            continue;
        }
        if (status == platform::ReadStatus::Eof || status == platform::ReadStatus::Error) {
            return;
        }
    }
}

std::string Session::last_stderr_line() {
    const std::lock_guard lock{tail_mutex_};
    std::string_view tail{tail_};
    while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r' || tail.back() == ' ')) {
        tail.remove_suffix(1);
    }
    const std::size_t cut = tail.rfind('\n');
    return std::string{cut == std::string_view::npos ? tail : tail.substr(cut + 1)};
}

void Session::read_events() {
    std::string pending;
    std::string chunk;
    for (;;) {
        const platform::ReadStatus status = child_->read_stdout(chunk, kSlice);
        if (status == platform::ReadStatus::Data) {
            pending += chunk;
            for (std::size_t end = pending.find('\n'); end != std::string::npos;
                 end = pending.find('\n')) {
                const std::string line = pending.substr(0, end);
                pending.erase(0, end + 1);
                handle_event(line);
            }
            continue;
        }
        if (status == platform::ReadStatus::Eof || status == platform::ReadStatus::Error) {
            break;
        }
    }
    if (!pending.empty()) {
        handle_event(pending);
    }
    // Its stderr ends with it: the tail is whole before its end is said.
    if (stderr_reader_.joinable()) {
        stderr_reader_.join();
    }
    child_ended();
}

void Session::child_ended() {
    const std::optional<int> code = child_->wait_for_exit(std::chrono::seconds{10});
    const std::string said = last_stderr_line();
    std::string how;
    if (!code.has_value() || *code < 0) {
        how = " (stopped by a signal)";
    } else if (*code != 0) {
        how = " (exit " + std::to_string(*code) + ")";
    }
    how += said.empty() ? ", saying nothing on stderr" : ": " + said;
    const std::string reason = "apogee's session ended" + how;

    const std::unique_lock lock{mutex_};
    ended_ = true;
    // A child that ended before it read its handover: the values go now.
    discard_handover(std::exchange(handover_, {}));
    if (open_id_.has_value()) {
        peer_.send(error_response(*open_id_, kInternalError,
                                  "apogee's session could not open: it ended" + how));
        open_id_.reset();
    }
    if (prompt_id_.has_value()) {
        peer_.send(error_response(*prompt_id_, kInternalError, reason));
        prompt_id_.reset();
    }
    if (!ending_) {
        diagnostics_.line("session " + id_ + ": " + reason);
    }
    if (ended_reason_.empty()) {
        ended_reason_ = reason + " -- start a new session";
    }
    done_.notify_all();
}

void Session::handle_event(std::string_view line) {
    // A malformed event is said and dropped: the reader serves the session to
    // its end, whatever one line held.
    try {
        translate_event(line);
    } catch (const std::exception& e) {
        diagnostics_.line("session " + id_ +
                          ": an event from apogee's session was dropped: " + e.what());
    }
}

void Session::translate_event(std::string_view line) {
    const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
    if (event.is_discarded() || !event.is_object()) {
        if (!line.empty()) {
            diagnostics_.line("session " + id_ +
                              ": a line from apogee's session that is no "
                              "event was dropped");
        }
        return;
    }
    const std::string type = string_of(event, "type");
    const EventRule* rule = event_rule(type);
    if (rule == nullptr) {
        return;  // a later build's event: ignored, as a driver must
    }
    std::unique_lock lock{mutex_};
    switch (rule->disposition) {
        case Disposition::SessionStart: {
            started_ = true;
            // A new session is its chat from here on (56c): one id names the
            // conversation everywhere -- `chats list`, `chats show`,
            // `--resume` -- and `session/new`'s answer gives it.
            if (const std::string chat = string_of(event, "chat_id");
                opened_as_ == OpenAs::New && !chat.empty()) {
                id_ = chat;
            }
            // Open, so the child has read and removed its handover; one it
            // could not remove goes now.
            discard_handover(std::exchange(handover_, {}));
            // The child says what it runs: a suite's root, a model pinned
            // on a backend -- the menu shows it, among the others.
            if (const std::string model = string_of(event, "model"); !model.empty()) {
                if (std::ranges::find(models_, model, &ModelChoice::value) == models_.end()) {
                    models_.insert(models_.begin(), ModelChoice{.value = model, .name = model});
                }
                model_ = model;
                fallback_model_ = model;
            }
            if (open_id_.has_value()) {
                // A load replays the saved turns first, as `chats show` reads
                // them; the resume's own notes follow, as notices, never as
                // history.
                if (opened_as_ == OpenAs::Load) {
                    for (nlohmann::json& update : replay_updates(replay_)) {
                        send_update(std::move(update));
                    }
                    replay_.clear();
                }
                nlohmann::json result{{"modes", modes_state(modes_, mode_)},
                                      {"configOptions", options()}};
                if (opened_as_ == OpenAs::New) {
                    result["sessionId"] = id_;
                }
                peer_.send(response(*open_id_, std::move(result)));
                open_id_.reset();
            }
            return;
        }
        case Disposition::Update:
            for (nlohmann::json& update : translator_.updates(event)) {
                send_update(std::move(update));
            }
            return;
        case Disposition::Notice:
            notice(lock, string_of(event, "text"));
            return;
        case Disposition::Question:
            question(lock, event);
            return;
        case Disposition::PromptEnd:
            // The turn is over in the child: what was chosen meanwhile is
            // told now, ahead of the next prompt's line.
            write_held();
            if (prompt_id_.has_value()) {
                peer_.send(response(*prompt_id_,
                                    nlohmann::json{{"stopReason", std::string{stop_reason(string_of(
                                                                      event, "finish_reason"))}}}));
                prompt_id_.reset();
            }
            return;
        case Disposition::PromptError:
            write_held();
            if (prompt_id_.has_value()) {
                peer_.send(
                    error_response(*prompt_id_, kInternalError, string_of(event, "message")));
                prompt_id_.reset();
            }
            return;
        case Disposition::Dropped:
            return;
    }
}

void Session::notice(std::unique_lock<std::mutex>& lock, const std::string& text) {
    (void)lock;
    if (text.empty()) {
        return;
    }
    // Every notice reaches stderr too, the stream a host keeps as a log.
    diagnostics_.line("session " + id_ + ": " + text);
    // A setting the child would not take: the menu goes back to what runs.
    if (const std::string refused = refused_setting(text); refused == "model") {
        model_ = fallback_model_;
        send_update(nlohmann::json{{"sessionUpdate", "config_option_update"},
                                   {"configOptions", options()}});
    } else if (refused == "permission_mode") {
        mode_ = fallback_mode_;
        send_update(
            nlohmann::json{{"sessionUpdate", "current_mode_update"}, {"currentModeId", mode_}});
        send_update(nlohmann::json{{"sessionUpdate", "config_option_update"},
                                   {"configOptions", options()}});
    }
    if (host_.notices && started_) {
        send_update(notice_update(text));
    } else if (prompt_id_.has_value()) {
        send_update(thought_update(text + "\n"));
    } else {
        pending_notices_.push_back(text);
    }
}

void Session::question(std::unique_lock<std::mutex>& lock, const nlohmann::json& event) {
    const std::int64_t turn = turn_;
    if (string_of(event, "kind") == "permission") {
        nlohmann::json params{{"sessionId", id_},
                              {"toolCall", translator_.permission_tool_call(event)},
                              {"options", permission_options(event.value("outbound", false))}};
        ++asking_;
        // Asked outside the lock: a host already gone answers at once.
        lock.unlock();
        peer_.ask("session/request_permission", std::move(params),
                  [this, turn](const nlohmann::json* result, const nlohmann::json* /*error*/) {
                      answered(turn, {permission_answer(result)});
                  });
        return;
    }
    std::size_t count = 0;
    if (const auto questions = event.find("questions");
        questions != event.end() && questions->is_array()) {
        count = questions->size();
    }
    if (!host_.elicitation_form) {
        // Nobody to ask: said so, so the model goes on and says what it
        // chose -- never a pick made silently for it.
        for (std::size_t index = 0; index < count; ++index) {
            (void)write_child(answer_line(nobody_to_ask()));
        }
        return;
    }
    nlohmann::json params = elicitation_params(event, id_);
    ++asking_;
    lock.unlock();
    peer_.ask("elicitation/create", std::move(params),
              [this, turn, count](const nlohmann::json* result, const nlohmann::json* /*error*/) {
                  answered(turn, elicitation_answers(result, count));
              });
}

void Session::answered(std::int64_t turn, const std::vector<std::string>& texts) {
    const std::unique_lock lock{mutex_};
    if (asking_ > 0) {
        --asking_;
    }
    // An answer for a turn that is over, cancelled or ending reaches nobody:
    // written, it would answer the next turn's question.
    if (ended_ || ending_ || turn != turn_ || !prompt_id_.has_value() || cancel_sent_) {
        return;
    }
    for (const std::string& text : texts) {
        (void)write_child(answer_line(text));
    }
}

}  // namespace apogee::acp
