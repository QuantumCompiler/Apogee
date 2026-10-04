#include "cli/chat_recall.h"

#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include "agentloop/embed_func.h"
#include "agentloop/rag.h"
#include "agentloop/recall.h"
#include "contracts/layout.h"
#include "operations/backend_names.h"
#include "operations/retrieval.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path pending_dir() {
    return harness::memory_dir() / "pending";
}

/// The process a marker names, or 0 when it names none.
[[nodiscard]] long marker_owner(const fs::path& marker) {
    std::ifstream in{marker};
    long pid = 0;
    in >> pid;
    return in ? pid : 0;
}

/// The embedder recall searches and indexes with: the embedding role's, when
/// it costs nothing per call. Recall is Apogee's own idea and is never billed.
[[nodiscard]] std::optional<agentloop::Embedder> free_embedder(const harness::Harness& harness,
                                                               const harness::Config& config) {
    std::string reason;
    std::optional<agentloop::Embedder> embedder =
        agentloop::resolve_embedder(harness, config, {}, reason);
    if (embedder.has_value() && embedder->metered) {
        return std::nullopt;
    }
    return embedder;
}

}  // namespace

bool recall_due(const logger::Session& session) {
    return !session.private_chat && session.turns >= agentloop::kRecallMinTurns;
}

fs::path recall_marker(std::string_view chat_id) {
    return pending_dir() / std::string{chat_id};
}

void forget_recall(std::string_view chat_id) {
    agentloop::RecallIndex index{harness::recall_index_path(), std::nullopt};
    (void)index.remove(chat_id);
    std::error_code code;
    fs::remove(recall_marker(chat_id), code);
}

std::string recall_summariser(const harness::Harness& harness, const harness::Config& config,
                              std::string_view conversation, std::string& reason) {
    if (std::string utility = named_utility(config); !utility.empty()) {
        return utility;
    }
    if (!conversation.empty() && !harness.generation_is_metered(conversation)) {
        return std::string{conversation};
    }
    reason = "no utility model is set, and " + std::string{conversation} +
             " is billed per call -- set models.default_utility to summarise with one";
    return {};
}

std::string summarise_for_recall(const harness::Harness& harness, const harness::Config& config,
                                 const logger::Session& session,
                                 const harness::CancellationToken& cancellation) {
    std::string reason;
    const std::string model = recall_summariser(harness, config, session.backend, reason);
    if (model.empty()) {
        return "not summarised for recall: " + reason;
    }
    std::string note;
    const std::optional<std::string> summary =
        agentloop::summarise_chat(harness, model, session.messages, cancellation, note);
    if (!summary.has_value()) {
        return "not summarised for recall: " + note;
    }
    agentloop::RecallIndex index{harness::recall_index_path(), free_embedder(harness, config)};
    const agentloop::RecallEntry entry{
        .chat_id = session.chat_id,
        .title = session.title,
        .updated_at = session.updated_at.empty() ? session.started_at : session.updated_at,
        .summary = *summary};
    if (!index.put(entry, cancellation, note)) {
        return "not summarised for recall: " + note;
    }
    std::error_code code;
    fs::remove(recall_marker(session.chat_id), code);
    return "summarised for recall by " + model + (note.empty() ? "" : " -- " + note);
}

ChatRecall::ChatRecall(const harness::Harness& harness, const harness::Config& config,
                       logger::Session& session, bool enabled)
    : harness_{harness}, config_{config}, session_{session}, enabled_{enabled} {}

Recalled ChatRecall::for_turn(const std::string& query, const agentloop::TurnBudget& budget,
                              std::int64_t share_used, std::string_view rag_collection,
                              const agentloop::SideCallSink& on_side_call) {
    Recalled out;
    if (!active()) {
        return out;
    }
    std::error_code code;
    if (const fs::path index = harness::recall_index_path(); fs::exists(index, code)) {
        // Through the one retrieval path, as notes on earlier conversations,
        // never this chat's own summary.
        agentloop::RagTurn turn;
        turn.store_path = index;
        turn.question = query;
        turn.limit = agentloop::kRecallItems;
        turn.embedder = free_embedder(harness_, config_);
        turn.rerank_flag = "off";
        turn.budget = budget;
        turn.share_used = share_used;
        turn.exclude_sources = {session_.chat_id};
        turn.header = std::string{agentloop::kRecallHeader};
        turn.harness = &harness_;
        turn.config = &config_;
        turn.on_side_call = on_side_call;
        const agentloop::RagResult result = agentloop::retrieve_for_turn(turn);
        if (result.error.empty() && !result.prefix.empty()) {
            out.prefix = result.prefix;
            out.chats = static_cast<int>(result.chunks);
            out.tokens = result.tokens;
        }
    }
    // Decisions recorded deliberately, with what is left -- unless `auto_rag`
    // has already searched that collection this turn.
    const int left = agentloop::kRecallItems - out.chats;
    if (const std::string knowledge = config_.knowledge.collection();
        left > 0 && knowledge != rag_collection) {
        const agentloop::RagResult records = retrieve_for_collection(
            harness_, config_, knowledge, query, left, {}, "off", {}, session_.backend, budget,
            share_used + out.tokens, on_side_call);
        if (records.error.empty() && !records.prefix.empty()) {
            out.prefix.insert(out.prefix.end(), records.prefix.begin(), records.prefix.end());
            out.decisions = static_cast<int>(records.chunks);
            out.tokens += records.tokens;
        }
    }
    return out;
}

void ChatRecall::after_turn() {
    if (!config_.memory.recall || !recall_due(session_)) {
        return;
    }
    const fs::path marker = recall_marker(session_.chat_id);
    if (marker_owner(marker) == platform::current_process_id()) {
        return;
    }
    std::error_code code;
    fs::create_directories(marker.parent_path(), code);
    fs::permissions(harness::memory_dir(), fs::perms::owner_all, fs::perm_options::replace, code);
    fs::permissions(marker.parent_path(), fs::perms::owner_all, fs::perm_options::replace, code);
    std::ofstream{marker, std::ios::trunc} << platform::current_process_id() << "\n";
}

void ChatRecall::make_private() {
    session_.private_chat = true;
    forget_recall(session_.chat_id);
}

std::string ChatRecall::finish(const harness::CancellationToken& cancellation) {
    // Only a chat this run changed: one resumed and left as it was keeps the
    // summary it has.
    if (!config_.memory.recall || !recall_due(session_) ||
        marker_owner(recall_marker(session_.chat_id)) != platform::current_process_id()) {
        return {};
    }
    return summarise_for_recall(harness_, config_, session_, cancellation);
}

std::vector<std::string> ChatRecall::catch_up(const harness::CancellationToken& cancellation,
                                              std::size_t limit) {
    std::vector<std::string> said;
    if (!config_.memory.recall) {
        return said;
    }
    std::error_code code;
    std::vector<fs::path> markers;
    for (fs::directory_iterator it{pending_dir(), code}, end; !code && it != end;
         it.increment(code)) {
        if (it->is_regular_file(code)) {
            markers.push_back(it->path());
        }
    }
    for (const fs::path& marker : markers) {
        if (said.size() >= limit) {
            break;
        }
        const std::string chat_id = marker.filename().string();
        const long owner = marker_owner(marker);
        // An open chat is never summarised: this one, or one whose process
        // is still running.
        if (chat_id == session_.chat_id || (owner > 0 && owner != platform::current_process_id() &&
                                            platform::process_running(owner))) {
            continue;
        }
        std::optional<logger::Session> chat;
        try {
            chat = logger::load(chat_id, logger::KnownDependencies{}).session;
        } catch (const std::exception&) {
            chat.reset();
        }
        if (!chat.has_value() || !recall_due(*chat)) {
            fs::remove(marker, code);
            continue;
        }
        said.push_back(summarise_for_recall(harness_, config_, *chat, cancellation) + " (" +
                       chat_id + ", left by a chat that did not finish)");
    }
    return said;
}

}  // namespace apogee::commands
