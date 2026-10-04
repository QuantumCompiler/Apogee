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
#include "contracts/cancellation.h"
#include "harness/harness.h"
#include "logger/session.h"

/// A conversation's attachments at work (26d): the chat's index, what is
/// attached, the indexing that runs while the user types, and each turn's
/// share of them -- inlined whole, or retrieved. Images, audio and video
/// (26e) are read into text by the models that can -- described,
/// transcribed, a video made a timeline -- and seen or heard natively, on the
/// turn they are attached, by a chat model that can.
///
/// `chat` (at a terminal and in machine mode) and `complete` drive this one
/// class, over the one core in `agentloop/attachments`, so a file is found,
/// read, indexed, inlined and cited the same way everywhere.
///
/// **Background work settles before it is needed.** Attaching queues the
/// files for a worker thread and returns; `settle()` -- called before every
/// turn, as the chat title is -- waits for it, saying how far it has got, and
/// on Ctrl-C keeps what is ready. One model call at a time: the embedding
/// model, and a helper describing or transcribing, only ever run while the
/// user is typing -- and anything else that reaches a model settles this
/// first.
namespace apogee::commands {

class ChatAttachments {
public:
    struct Hooks {
        /// A line for the user; `warning` for a skip or trouble.
        std::function<void(const std::string& line, bool warning)> say;
        /// How far indexing has got while a turn waits; empty clears it.
        std::function<void(const std::string& line)> progress;
        /// Asked before attaching over `kLargeAttachmentFiles` files or
        /// `kLargeAttachmentBytes`, or before more than
        /// `kMeteredDescriptionsAsked` descriptions by a model billed per
        /// call; null refuses, as a pipe must.
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

    /// More descriptions than this by a model billed per call -- a folder of
    /// images, a video's frames -- ask first (26e): nothing is spent at scale
    /// on Apogee's initiative.
    static constexpr std::size_t kMeteredDescriptionsAsked = 12;

    /// A saved chat's index: `attachments/<chat id>.db`.
    [[nodiscard]] static std::filesystem::path index_for(std::string_view chat_id);

    /// Removes a chat's index, with its SQLite side files and any media
    /// scratch a crash left.
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
    /// attachment settled since the last turn rides it when inlined -- an
    /// image, a sound or a short clip as it is when the chat model can read
    /// it, its text from the next turn on -- and the ones not inlined are
    /// searched for `query`, `limit` at most, with any moment it names
    /// (`4:30`) looked up in a recording's timeline.
    [[nodiscard]] Turn for_turn(std::size_t user_message, const std::string& query,
                                const agentloop::TurnBudget& budget, int limit,
                                const harness::CancellationToken& cancellation,
                                const agentloop::SideCallSink& on_side_call = {});

    /// After the run: an inlined attachment the budget could not send is
    /// retrieved from then on, and said; so is media it could not send as it
    /// is.
    void after_turn(const std::vector<std::string>& inline_dropped);

    /// After compaction: the messages the inlined attachments rode are gone,
    /// so every one is retrieved from then on -- and each folder's map rides
    /// the next message once more (26q), the structure it gives being worth
    /// its few lines again.
    void after_compaction();

private:
    struct Queued {
        std::string name;
        std::vector<agentloop::FoundFile> files;
        /// The chat model when it was attached: who reads its media natively.
        std::string chat;
    };

    struct Indexed {
        std::string name;
        std::vector<agentloop::AttachmentIndex::Added> added;
        /// Its media as the chat model reads them natively, for the next
        /// message (26e); empty when it cannot.
        std::vector<harness::ContentPart> native;
    };

    void start_worker();
    /// Media `files` no model can read, said and left out; a run of billed
    /// descriptions asked about. False when nothing is left.
    [[nodiscard]] bool readable(std::string_view spec, std::vector<agentloop::FoundFile>& files,
                                const std::string& chat);
    /// One file's media read into text by the models that can (26e).
    [[nodiscard]] agentloop::AttachmentText read_media(const agentloop::FoundFile& file,
                                                       harness::Medium medium,
                                                       const std::string& chat,
                                                       const harness::CancellationToken& token);
    /// The media of `file` as the chat model reads it natively, or empty.
    [[nodiscard]] std::vector<harness::ContentPart> native_media(
        const agentloop::FoundFile& file, const std::string& chat,
        const harness::CancellationToken& token, std::vector<std::string>& notes);
    /// A private folder for one file's frames and conversions.
    [[nodiscard]] std::filesystem::path scratch() const;
    void record(Indexed indexed, const agentloop::TurnBudget& budget);
    [[nodiscard]] std::string inline_text(const logger::Attachment& attachment);
    /// A folder's or a glob's map card (26q), rebuilt from its files and the
    /// index; empty for one file, which is its own map.
    [[nodiscard]] std::string map_text(const logger::Attachment& attachment);
    [[nodiscard]] std::int64_t map_tokens(const agentloop::TurnBudget& budget,
                                          const logger::Attachment& attachment);
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
    /// Media to be seen or heard as it is with the next user message (26e).
    std::map<std::string, std::vector<harness::ContentPart>> pending_native_;
    /// Rebuilt inline blocks and their token counts, by attachment name.
    std::map<std::string, std::string> inline_texts_;
    std::map<std::string, std::int64_t> inline_costs_;
    /// Maps settled, or folded by compaction, waiting for the next user
    /// message (26q); and the rebuilt cards and their token counts.
    std::set<std::string> pending_map_;
    std::map<std::string, std::string> map_texts_;
    std::map<std::string, std::int64_t> map_costs_;
};

/// The paths `message` mentions: each word that starts with `@`, as
/// `@path` or `@"path with spaces"`. The message itself is never changed.
[[nodiscard]] std::vector<std::string> mentioned_paths(std::string_view message);

/// A mention's path as it exists: `mention` itself, or with the punctuation
/// that ends a sentence taken off its end. Nullopt when neither exists.
[[nodiscard]] std::optional<std::string> existing_mention(
    std::string_view mention, const std::filesystem::path& working_directory);

}  // namespace apogee::commands
