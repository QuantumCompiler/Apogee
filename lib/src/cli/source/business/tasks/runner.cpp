#include "tasks/runner.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "logger/session.h"
#include "tasks/ledger.h"

namespace apogee::tasks {
namespace {

/// Watches for a halt or cancel another process leaves, and for the
/// command's own interrupt, while the task runs: a cancel -- either -- is
/// passed to the turn's token at once, so the in-flight turn ends through
/// the loop's own cancellation; a halt waits for the round to end.
class RequestWatcher {
public:
    RequestWatcher(std::filesystem::path root, std::string id, harness::CancellationToken interrupt,
                   harness::CancellationToken turn, std::chrono::milliseconds poll)
        : root_{std::move(root)},
          id_{std::move(id)},
          interrupt_{std::move(interrupt)},
          turn_{std::move(turn)},
          poll_{poll},
          thread_{[this] { watch(); }} {}

    RequestWatcher(const RequestWatcher&) = delete;
    RequestWatcher& operator=(const RequestWatcher&) = delete;
    RequestWatcher(RequestWatcher&&) = delete;
    RequestWatcher& operator=(RequestWatcher&&) = delete;

    ~RequestWatcher() {
        {
            const std::scoped_lock lock{mutex_};
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }

    /// Whether `task cancel` asked -- as opposed to Ctrl-C.
    [[nodiscard]] bool cancel_requested() const noexcept {
        return cancel_requested_.load();
    }

    /// The request standing now, read again at a round's end.
    [[nodiscard]] Request standing() const {
        const Request request = read_request(root_, id_);
        if (request == Request::Cancel) {
            cancel_requested_.store(true);
        }
        if (interrupt_.stop_requested()) {
            return Request::Cancel;
        }
        return request;
    }

private:
    void watch() {
        std::unique_lock lock{mutex_};
        while (!stop_) {
            if (read_request(root_, id_) == Request::Cancel) {
                cancel_requested_.store(true);
                turn_.cancel();
            }
            if (interrupt_.stop_requested()) {
                turn_.cancel();
            }
            wake_.wait_for(lock, poll_, [this] { return stop_; });
        }
    }

    std::filesystem::path root_;
    std::string id_;
    harness::CancellationToken interrupt_;
    harness::CancellationToken turn_;
    std::chrono::milliseconds poll_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    mutable std::atomic<bool> cancel_requested_{false};
    // Last: started once everything it reads exists.
    std::thread thread_;
};

/// The session's completed turns and messages; nothing when it cannot be
/// read.
[[nodiscard]] std::optional<logger::Session> load_session(const std::string& id) {
    if (id.empty()) {
        return std::nullopt;
    }
    try {
        return logger::load(id, logger::KnownDependencies{}).session;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

/// Why a call was refused, as the run says it.
[[nodiscard]] std::string denial_words(const Denial& denial) {
    if (denial.by == kByConfig) {
        return "the config denies it";
    }
    if (denial.by == kByPerson) {
        return "refused at the prompt";
    }
    return "nobody is present to allow it";
}

[[nodiscard]] std::string checks_line(const Round& round) {
    int passed = 0;
    for (const CheckResult& result : round.checks) {
        passed += result.passed ? 1 : 0;
    }
    std::string line = std::to_string(passed) + " of " + std::to_string(round.checks.size()) +
                       " check" + (round.checks.size() == 1 ? "" : "s") + " passed";
    line += ", " + std::string{report_words(self_report_from_string(round.self_report))};
    if (!round.denied.empty()) {
        line += ", " + std::to_string(round.denied.size()) + " tool call" +
                (round.denied.size() == 1 ? "" : "s") + " denied";
    }
    return line;
}

class Runner {
public:
    explicit Runner(const RunRequest& request)
        : request_{request},
          task_{request.task},
          turn_token_{harness::CancellationToken::create()},
          announced_{request.task.transitions.size()} {}

    RunOutcome run() {
        if (const std::string refused = resume_refusal(task_); !refused.empty()) {
            return RunOutcome{.task = task_, .error = refused};
        }
        // A request is for a running process; one standing now was left for a
        // process that ended before reading it, and this run is a new decision.
        clear_request(request_.root, task_.id);
        if (task_.status == kStalled) {
            task_.no_progress = 0;  // resuming is the breaker's way out, as the cycle's
        }
        task_.status = std::string{task_.plan.empty() ? kPlanning : kRunning};
        task_.reason.clear();
        record(request_.resume ? kResumedEvent : kStartedEvent);
        if (!save()) {
            return outcome();
        }
        if (const std::optional<logger::Session> session = load_session(task_.session_id);
            session.has_value()) {
            session_turns_ = session->turns;
        }

        const RequestWatcher watcher{request_.root, task_.id, request_.interrupt, turn_token_,
                                     request_.poll};
        watcher_ = &watcher;
        if (task_.plan.empty()) {
            plan();
        }
        while (error_.empty() && task_.status == kRunning) {
            round();
        }
        watcher_ = nullptr;
        if (error_.empty()) {
            record(kFinishedEvent, 0, task_.reason);
            (void)save();
        }
        clear_request(request_.root, task_.id);
        return outcome();
    }

private:
    [[nodiscard]] RunOutcome outcome() const {
        return RunOutcome{.task = task_, .error = error_};
    }

    void say(const std::string& line) const {
        if (request_.say) {
            request_.say(line);
        }
    }

    void record(std::string_view event, int round = 0, std::string detail = {}) {
        record_transition(task_, event, now_timestamp(), round, std::move(detail));
    }

    bool save() {
        if (const std::string failure = save_task(request_.root, task_); !failure.empty()) {
            error_ = "the task's ledger could not be written: " + failure;
            return false;
        }
        // What the ledger now holds that nobody was told of: each transition
        // once, in order, and only once it is on disk (27j).
        for (; announced_ < task_.transitions.size(); ++announced_) {
            if (request_.on_transition) {
                request_.on_transition(task_, announced_);
            }
        }
        return true;
    }

    /// The round to run: the one left unfinished, or a new one.
    Round& open_round(int index, std::string_view kind) {
        if (!task_.rounds.empty() && task_.rounds.back().index == index &&
            task_.rounds.back().kind == kind && task_.rounds.back().outcome != kCompleted) {
            return task_.rounds.back();
        }
        task_.rounds.push_back(Round{.index = index, .kind = std::string{kind}});
        return task_.rounds.back();
    }

    /// One turn of the round `round_index` (0 the plan): the answer, or
    /// nullopt with the task stopped and saying why.
    std::optional<std::string> turn(int round_index, std::string_view kind,
                                    const std::string& message) {
        Round& round = open_round(round_index, kind);
        // A round a restart left unfinished whose turn did finish: the
        // session holds its answer, and asking again would say it twice.
        if (!round.started_at.empty() && round.outcome.empty()) {
            if (std::optional<logger::Session> session = load_session(task_.session_id);
                session.has_value() && session->turns > round.session_turns_before) {
                if (std::optional<std::string> answer =
                        answer_in_session(session->messages, message);
                    answer.has_value()) {
                    round.adopted = true;
                    round.ended_at = now_timestamp();
                    session_turns_ = session->turns;
                    say("[task] " + label(round_index) +
                        ": its turn had finished before the restart -- its answer is taken "
                        "from the conversation");
                    return answer;
                }
            }
        }
        round.started_at = now_timestamp();
        round.ended_at.clear();
        round.outcome.clear();
        round.adopted = false;
        round.session_turns_before = session_turns_;
        round.tools.clear();
        round.denied.clear();
        // What an earlier attempt at this round let through and answered is
        // kept: its turn was rolled back, its effects were not, and the audit
        // trail is of what happened (27i).
        round.tokens = 0;
        round.tokens_estimated = false;
        task_.status = std::string{round_index == 0 ? kPlanning : kRunning};
        record(round_index == 0 ? kPlanStartedEvent : kRoundStartedEvent, round_index,
               std::string{kind});
        if (!save()) {
            return std::nullopt;
        }

        const TurnResult result = request_.turn(TurnRequest{.message = message,
                                                            .round = round_index,
                                                            .kind = std::string{kind},
                                                            .budget = task_.rounds_budget,
                                                            .cancellation = turn_token_});
        Round& done = task_.rounds.back();
        done.ended_at = now_timestamp();
        done.tools = result.tools;
        done.denied = result.denied;
        done.allowed.insert(done.allowed.end(), result.allowed.begin(), result.allowed.end());
        done.answered.insert(done.answered.end(), result.answered.begin(), result.answered.end());
        done.tokens = result.tokens;
        done.tokens_estimated = result.tokens_estimated;
        for (const Permit& permit : result.allowed) {
            // The config's standing allows are the config's to say; what the
            // task's own authority let through is said as it happens.
            if (permit.by == kByGrant) {
                say("[task] " + permit.tool +
                    (permit.target.empty() ? std::string{} : " on " + permit.target) +
                    " -- allowed by this task's grant");
                if (request_.on_grant) {
                    request_.on_grant(task_, round_index, permit);
                }
            }
        }
        for (const Denial& denial : result.denied) {
            say("[task] denied: " + denial.tool +
                (denial.target.empty() ? std::string{} : " on " + denial.target) + " -- " +
                denial_words(denial));
        }
        for (const Answered& question : result.answered) {
            // The question, never the answer: the ledger and `task status`
            // hold what was declared.
            if (question.by == kByDeclared) {
                say("[task] a question answered with the declared answer: " + question.question);
            }
        }
        if (result.completed) {
            ++session_turns_;
            return result.answer;
        }

        done.outcome = std::string{kInterrupted};
        if (!result.question.empty()) {
            task_.status = std::string{kFailed};
            task_.reason = label(round_index) +
                           ": the model asked a question and no one is present to answer it: " +
                           result.question;
        } else if (result.cancelled || turn_token_.stop_requested()) {
            task_.status = std::string{kCancelled};
            task_.reason = (watcher_ != nullptr && watcher_->cancel_requested()
                                ? "cancelled by 'apogee task cancel'"
                                : std::string{"interrupted"}) +
                           " during " + label(round_index);
        } else {
            task_.status = std::string{kFailed};
            task_.reason =
                label(round_index) + " failed: " +
                (result.error.empty() ? std::string{"the turn did not finish"} : result.error);
        }
        // A round's end is a transition of its own; a plan turn cut short is
        // said by the `finished` that follows -- no plan was recorded.
        if (round_index > 0) {
            record(kRoundEndedEvent, round_index, std::string{kInterrupted});
        }
        (void)save();
        return std::nullopt;
    }

    [[nodiscard]] static std::string label(int round_index) {
        return round_index == 0 ? std::string{"the plan turn"}
                                : "round " + std::to_string(round_index);
    }

    void plan() {
        say("[task] planning");
        const std::optional<std::string> answer = turn(0, kPlanRound, plan_message(task_));
        if (!answer.has_value()) {
            return;
        }
        task_.rounds.back().outcome = std::string{kCompleted};
        task_.plan = *answer;
        task_.status = std::string{kRunning};
        record(kPlanRecordedEvent);
        if (!save()) {
            return;
        }
        say("[task] plan recorded");
        stop_if_asked();
    }

    void round() {
        if (rounds_used(task_) >= task_.rounds_budget) {
            // A budget already spent -- a ledger edited by hand -- stops here.
            task_.status = std::string{kExhausted};
            task_.reason = "the round budget (" + std::to_string(task_.rounds_budget) +
                           ") is spent and the task is not done -- " + shortfall(task_);
            return;
        }
        const int index = rounds_used(task_) + 1;
        const std::string_view kind = index == 1 ? kExecuteRound : kCorrectRound;
        if (const Round* before = last_completed_round(task_); before == nullptr) {
            say("[task] round " + std::to_string(index) + " of " +
                std::to_string(task_.rounds_budget) + " -- carrying out the plan");
        } else {
            int failing = 0;
            for (const CheckResult& result : before->checks) {
                failing += result.passed ? 0 : 1;
            }
            say("[task] round " + std::to_string(index) + " of " +
                std::to_string(task_.rounds_budget) + " -- correcting: " + std::to_string(failing) +
                " check" + (failing == 1 ? "" : "s") + " failing" +
                (self_report_from_string(before->self_report) == SelfReport::Done
                     ? std::string{}
                     : ", not reported done"));
        }
        const std::optional<std::string> answer = turn(index, kind, round_message(task_, index));
        if (!answer.has_value()) {
            return;
        }
        conclude_round(task_, *answer);
        const Round& ended = task_.rounds.back();
        record(kRoundEndedEvent, index, checks_line(ended));
        if (!save()) {
            return;
        }
        say("[task] round " + std::to_string(index) + ": " + checks_line(ended));
        if (task_.status == kRunning) {
            stop_if_asked();
        }
    }

    /// A halt or cancel standing at a round's end stops the task there.
    void stop_if_asked() {
        if (watcher_ == nullptr) {
            return;
        }
        const Request request = watcher_->standing();
        if (request == Request::Halt) {
            task_.status = std::string{kHalted};
            task_.reason =
                "halted by 'apogee task halt' after " +
                (rounds_used(task_) == 0 ? std::string{"the plan"}
                                         : "round " + std::to_string(rounds_used(task_)));
        } else if (request == Request::Cancel) {
            task_.status = std::string{kCancelled};
            task_.reason =
                (watcher_->cancel_requested() ? "cancelled by 'apogee task cancel'"
                                              : std::string{"interrupted"}) +
                " after " +
                (rounds_used(task_) == 0 ? std::string{"the plan"}
                                         : "round " + std::to_string(rounds_used(task_)));
        }
    }

    const RunRequest& request_;
    Task task_;
    harness::CancellationToken turn_token_;
    const RequestWatcher* watcher_ = nullptr;
    int session_turns_ = 0;
    std::string error_;
    /// The transitions `on_transition` has been told of -- those the task
    /// came in with are its history, not this run's.
    std::size_t announced_ = 0;
};

}  // namespace

std::string resume_refusal(const Task& task) {
    if (task.status == kDone) {
        return "task " + task.id + " is done -- there is nothing to resume";
    }
    if (task.status == kExhausted) {
        return "task " + task.id + " spent its round budget (" +
               std::to_string(task.rounds_budget) +
               ") -- a task is bounded by construction, so "
               "it does not run again; start a new one";
    }
    return {};
}

RunOutcome run_task(const RunRequest& request) {
    return Runner{request}.run();
}

std::optional<std::string> answer_in_session(const std::vector<harness::ChatMessage>& messages,
                                             std::string_view message) {
    std::optional<std::size_t> asked;
    for (std::size_t index = messages.size(); index > 0; --index) {
        const harness::ChatMessage& candidate = messages[index - 1];
        if (candidate.role == harness::Role::User && candidate.content.plain_text() == message) {
            asked = index - 1;
            break;
        }
    }
    if (!asked.has_value()) {
        return std::nullopt;
    }
    std::optional<std::string> answer;
    for (std::size_t index = *asked + 1; index < messages.size(); ++index) {
        const harness::ChatMessage& next = messages[index];
        if (next.role == harness::Role::User) {
            break;
        }
        if (next.role == harness::Role::Assistant && next.tool_calls.empty()) {
            answer = next.content.plain_text();
        }
    }
    return answer;
}

}  // namespace apogee::tasks
