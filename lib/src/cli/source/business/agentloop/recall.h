#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/embed_func.h"
#include "contracts/cancellation.h"
#include "contracts/types.h"
#include "harness/harness.h"

/// Recall across chats (26l): what earlier conversations established, found
/// again for a new one without being told.
///
/// Each finished chat of two turns or more is summarised once by a model --
/// what was asked, what was decided, the facts and preferences stated, the
/// files involved -- and the summary indexed under the chat's id in a private
/// index of its own. A turn then retrieves the few that bear on its question
/// through the one retrieval path, injected like RAG: transient, inside the
/// retrieval share, and always reported. Never on `serve`: nothing here is
/// reachable from the server, a structural rule rather than a setting.
namespace apogee::agentloop {

/// The most past items a turn is handed (the user's call, 2026-10-03).
inline constexpr int kRecallItems = 3;

/// The fewest turns a chat needs to be summarised (the user's call): a single
/// question rarely establishes anything worth recalling.
inline constexpr std::int64_t kRecallMinTurns = 2;

/// How a recalled summary is introduced to the model: notes on earlier
/// conversations, which it may use and must not mistake for the user's words.
inline constexpr std::string_view kRecallHeader =
    "The following notes summarise the user's earlier conversations with you, retrieved because "
    "they may bear on this question. Use them if they help; ignore them if they do not, and do "
    "not mention them unless they informed your answer.";

/// The request a summariser is sent: the conversation's user and assistant
/// turns, each clipped, asked for what a later conversation would want to
/// know -- in plain sentences, as a side request with its thinking off.
[[nodiscard]] harness::ChatRequest recall_summary_request(
    std::string_view model, const std::vector<harness::ChatMessage>& messages);

/// A chat's summary from `model`, or nullopt with `note` saying why not: a
/// failed call, or a blank reply.
[[nodiscard]] std::optional<std::string> summarise_chat(
    const harness::Harness& harness, std::string_view model,
    const std::vector<harness::ChatMessage>& messages,
    const harness::CancellationToken& cancellation, std::string& note);

/// One chat as the index holds it.
struct RecallEntry {
    std::string chat_id;
    std::string title;
    /// The chat's `updated_at` when it was summarised: a chat updated since
    /// is summarised again.
    std::string updated_at;
    std::string summary;
};

/// The text a summary is indexed and recalled as: dated and titled, so the
/// model reads where it came from.
[[nodiscard]] std::string recall_chunk(const RecallEntry& entry);

/// What a turn recalled, as the status line says it: `2 past chats, 1
/// decision`.
[[nodiscard]] std::string describe_recall(int chats, int decisions);

/// The private index of chat summaries: one source per chat, its id.
class RecallIndex {
public:
    /// `embedder`, when there is one, gives the summaries vectors; without
    /// one the index is lexical, which recall searches just the same.
    RecallIndex(std::filesystem::path store_path, std::optional<Embedder> embedder);

    /// Indexes `entry`, replacing what the chat had. Returns false with
    /// `note` when it could not be stored.
    [[nodiscard]] bool put(const RecallEntry& entry, const harness::CancellationToken& cancellation,
                           std::string& note);

    /// Removes a chat's summary; true when there was one.
    bool remove(std::string_view chat_id);

    /// When a chat was last summarised (its `updated_at` then), or empty.
    [[nodiscard]] std::string summarised_at(std::string_view chat_id) const;

    /// How many chats the index holds.
    [[nodiscard]] std::size_t chats() const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return store_path_;
    }

private:
    std::filesystem::path store_path_;
    std::optional<Embedder> embedder_;
};

}  // namespace apogee::agentloop
