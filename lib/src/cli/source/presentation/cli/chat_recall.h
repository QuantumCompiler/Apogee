#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/budget.h"
#include "agentloop/side_call.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "contracts/types.h"
#include "harness/harness.h"
#include "logger/session.h"

/// Recall across chats, as `chat` runs it (26l). The summaries and their index
/// are `agentloop/recall`; this is the chat's side of them: which chats are
/// due a summary, summarising at a clean exit, catching up on a chat a
/// process that died left behind, and what each turn recalls. `complete`,
/// agents and `serve` never reach it.
namespace apogee::commands {

/// Whether `session` is due a summary: at least two turns, not private.
[[nodiscard]] bool recall_due(const logger::Session& session);

/// The marker naming the process that has `chat_id` open, and so is due to
/// summarise it (`memory/pending/<id>`).
[[nodiscard]] std::filesystem::path recall_marker(std::string_view chat_id);

/// Forgets a chat entirely: its summary and its marker. `chats delete`, and
/// `/private`.
void forget_recall(std::string_view chat_id);

/// Who summarises: the utility model when one is named, else the chat's own
/// backend only when it costs nothing per call -- a summary is Apogee's idea,
/// never billed for it. Empty when neither, with `reason` saying so.
[[nodiscard]] std::string recall_summariser(const harness::Harness& harness,
                                            const harness::Config& config,
                                            std::string_view conversation, std::string& reason);

/// Summarises `session` into the index and removes its marker. Returns the
/// line to say: what was kept, or why nothing was.
[[nodiscard]] std::string summarise_for_recall(const harness::Harness& harness,
                                               const harness::Config& config,
                                               const logger::Session& session,
                                               const harness::CancellationToken& cancellation);

/// What a turn recalled.
struct Recalled {
    std::vector<harness::ChatMessage> prefix;
    int chats = 0;
    int decisions = 0;
    std::int64_t tokens = 0;
};

class ChatRecall {
public:
    /// `enabled` is the run's word -- `memory.recall` and no `--no-recall`;
    /// summarising follows `memory.recall` alone.
    ChatRecall(const harness::Harness& harness, const harness::Config& config,
               logger::Session& session, bool enabled);

    ChatRecall(const ChatRecall&) = delete;
    ChatRecall& operator=(const ChatRecall&) = delete;
    ChatRecall(ChatRecall&&) = delete;
    ChatRecall& operator=(ChatRecall&&) = delete;
    ~ChatRecall() = default;

    /// `/recall on|off`, for this session.
    void set_session(bool on) noexcept {
        session_on_ = on;
    }

    [[nodiscard]] bool session_on() const noexcept {
        return session_on_;
    }

    /// Whether turns recall: the run allows it and the session has not
    /// turned it off.
    [[nodiscard]] bool active() const noexcept {
        return enabled_ && session_on_;
    }

    /// What `query` recalls, inside what the turn's retrieval share has left
    /// (`share_used`): past chats first, then recorded decisions from the
    /// knowledge collection -- unless that is the collection `auto_rag`
    /// already searched -- at most `agentloop::kRecallItems` in all.
    [[nodiscard]] Recalled for_turn(const std::string& query, const agentloop::TurnBudget& budget,
                                    std::int64_t share_used, std::string_view rag_collection,
                                    const agentloop::SideCallSink& on_side_call = {});

    /// After each turn: a chat that has become due is marked as this
    /// process's, so a crash leaves it to the next start.
    void after_turn();

    /// `/private`: never summarised, and any summary it had removed.
    void make_private();

    /// At a clean exit: summarises the chat when it is due and recall is on
    /// in the config. Returns the line to say, empty when there was nothing
    /// to do.
    [[nodiscard]] std::string finish(const harness::CancellationToken& cancellation);

    /// At a chat's start: summarises up to `limit` chats a process that died
    /// left marked -- never one another process has open, never this one.
    /// Returns a line per chat.
    [[nodiscard]] std::vector<std::string> catch_up(const harness::CancellationToken& cancellation,
                                                    std::size_t limit = 3);

private:
    const harness::Harness& harness_;
    const harness::Config& config_;
    logger::Session& session_;
    bool enabled_;
    bool session_on_ = true;
};

}  // namespace apogee::commands
