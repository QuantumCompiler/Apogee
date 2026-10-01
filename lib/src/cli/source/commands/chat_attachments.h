#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/attachments.h"
#include "agentloop/budget.h"
#include "agentloop/loop.h"
#include "agentloop/rag.h"
#include "harness/cancellation.h"
#include "harness/harness.h"
#include "logger/session.h"

/// A conversation's attachments at work (26d): the chat's index, what is
/// attached, the indexing that runs while the user types, and each turn's
/// share of them -- inlined whole, or retrieved.
///
/// `chat` (at a terminal and in machine mode) and `complete` drive this one
/// class, over the one core in `agentloop/attachments`, so a file is found,
/// read, indexed, inlined and cited the same way everywhere.
///
/// **Background work settles before it is needed.** Attaching queues the
/// files for a worker thread and returns; `settle()` -- called before every
/// turn, as the chat title is -- waits for it, saying how far it has got, and
/// on Ctrl-C keeps what is ready. One model call at a time: the embedding
/// model is only ever running while the user is typing.
namespace apogee::commands {

class ChatAttachments {
public:
    struct Hooks {
        /// A line for the user; `warning` for a skip or trouble.
        std::function<void(const std::string& line, bool warning)> say;
        /// How far indexing has got while a turn waits; empty clears it.
        std::function<void(const std::string& line)> progress;
        /// Asked before attaching over `kLargeAttachmentFiles` files or
        /// `kLargeAttachmentBytes`; null refuses, as a pipe must.
        std::function<bool(const std::string& question)> confirm_large;
        /// Whether a settled attach is saved to the session file: a chat's
        /// is, `complete`'s temporary one is not.
        bool save = true;
    };

    /// `store_path` is the conversation's index: `index_for(chat_id)` for a
    /// chat, a temporary file for `complete`. The session's attachments --
    /// a resumed chat's -- are taken up as they are, with nothing embedded.
    ChatAttachments(const harness::Harness& harness, logger::Session& session,
                    std::filesystem::path store_path, Hooks hooks);
    /// Cancels indexing in flight and waits for it.
    ~ChatAttachments();

    ChatAttachments(const ChatAttachments&) = delete;
    ChatAttachments& operator=(const ChatAttachments&) = delete;
    ChatAttachments(ChatAttachments&&) = delete;
    ChatAttachments& operator=(ChatAttachments&&) = delete;

    /// A saved chat's index: `attachments/<chat id>.db`.
    [[nodiscard]] static std::filesystem::path index_for(std::string_view chat_id);

    /// Removes a chat's index, with its SQLite side files.
    static void remove_index(std::string_view chat_id);

    /// Queues what `spec` names -- a file, a folder, a glob -- for indexing
    /// and returns at once. False, having said why, when it names nothing or
    /// the size guard refuses it.
    bool attach(std::string_view spec, const std::filesystem::path& working_directory);

    /// Waits for indexing in flight, then records each attachment and decides
    /// whether it is inlined. Ctrl-C keeps what is ready.
    void settle();

    /// Detaches the attachment named `name`, and its files from the index
    /// unless another attachment holds the same content.
    [[nodiscard]] bool detach(std::string_view name);

    /// One line per attachment, for `/attachments`.
    [[nodiscard]] std::vector<std::string> describe() const;
    /// The attachments' names, for `/detach`'s completion.
    [[nodiscard]] std::vector<std::string> names() const;

    /// What a turn sends of the attachments.
    struct Turn {
        std::vector<agentloop::InlineAttachment> inlined;
        /// Set when a retrieval ran over the attachments not inlined.
        std::optional<agentloop::RagResult> retrieved;
    };

    /// Whether this turn will search the index -- so the question is worth
    /// restating first.
    [[nodiscard]] bool retrieves() const;

    /// The turn whose user message sits at `user_message` in history: an
    /// attachment settled since the last turn rides it when inlined, and the
    /// ones not inlined are searched for `query`, `limit` at most.
    [[nodiscard]] Turn for_turn(std::size_t user_message, const std::string& query,
                                const agentloop::TurnBudget& budget, int limit,
                                const harness::CancellationToken& cancellation);

    /// After the run: an inlined attachment the budget could not send is
    /// retrieved from then on, and said.
    void after_turn(const std::vector<std::string>& inline_dropped);

    /// After compaction: the messages the inlined attachments rode are gone,
    /// so every one is retrieved from then on.
    void after_compaction();

private:
    struct Queued {
        std::string name;
        std::vector<agentloop::FoundFile> files;
    };

    struct Indexed {
        std::string name;
        std::vector<agentloop::AttachmentIndex::Added> added;
    };

    void start_worker();
    void record(Indexed indexed, const agentloop::TurnBudget& budget);
    [[nodiscard]] std::string inline_text(const logger::Attachment& attachment);
    [[nodiscard]] std::int64_t inline_tokens_in_use(const agentloop::TurnBudget& budget);
    void save() const;

    const harness::Harness& harness_;
    logger::Session& session_;
    std::filesystem::path store_path_;
    Hooks hooks_;
    std::optional<agentloop::Embedder> embedder_;
    std::string lexical_reason_;

    std::mutex mutex_;
    std::deque<Queued> queue_;
    std::vector<Indexed> done_;
    std::string status_;
    std::future<void> worker_;
    harness::CancellationToken cancellation_ = harness::CancellationToken::create();

    /// Settled and inlined, waiting for the next user message to ride.
    std::set<std::string> pending_inline_;
    /// Rebuilt inline blocks and their token counts, by attachment name.
    std::map<std::string, std::string> inline_texts_;
    std::map<std::string, std::int64_t> inline_costs_;
};

/// The paths `message` mentions: each word that starts with `@`, as
/// `@path` or `@"path with spaces"`. The message itself is never changed.
[[nodiscard]] std::vector<std::string> mentioned_paths(std::string_view message);

/// A mention's path as it exists: `mention` itself, or with the punctuation
/// that ends a sentence taken off its end. Nullopt when neither exists.
[[nodiscard]] std::optional<std::string> existing_mention(
    std::string_view mention, const std::filesystem::path& working_directory);

}  // namespace apogee::commands
