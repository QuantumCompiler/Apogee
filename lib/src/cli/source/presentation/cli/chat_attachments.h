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
#include "cli/attachment_graph.h"
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
///
/// **A folder of code builds its graph** (27n): after its chunks are
/// indexed, the worker parses the folder's files -- the ones the attach
/// found, never a second walk -- into the same index with 27k's model-free
/// build (`cli/attachment_graph`), says it in one line, and keeps it with the
/// chat: re-attached, only what changed is parsed again; detached, its part
/// is forgotten; the chat deleted, it goes with the index file. Ctrl-C keeps
/// the chunks that are ready and leaves the graph absent, and says so.
///
/// **And turns reach it** (27o): while the index holds a graph, an
/// attachment turn's excerpts seed the retrieval-time expansion through it,
/// its section riding after them, and `graph_scope` hands the chat the
/// store its scoped `graph` tools read -- so a model walks the attached code
/// rather than inventing paths.
///
/// **How, per attach** (27p): whether a folder's code graph is built is a
/// method -- `code` or `off` -- resolved once per attach by one function: a
/// `--graph` on the attach beats the config's `attachments.graph`, which
/// beats the surface's built-in (a chat's `code`, `complete`'s `off`). The
/// attach line names the method when the built-in did not choose it. An `@`
/// mention is a bare path, so it takes the default.
namespace apogee::commands {

/// What chose an attach's method (27p).
enum class GraphMethodSource : std::uint8_t {
    /// The surface's own default: a chat's `code`, `complete`'s `off` (27n).
    BuiltIn,
    /// The config's `attachments.graph`.
    Config,
    /// A `--graph` on the attach itself.
    Flag,
};

/// The method an attach takes, and what chose it.
struct GraphMethod {
    harness::AttachmentGraphMethod method = harness::AttachmentGraphMethod::Code;
    GraphMethodSource from = GraphMethodSource::BuiltIn;

    bool operator==(const GraphMethod&) const = default;
};

/// The one resolution (27p): `flag` when given, else `config` when set, else
/// `built_in`. Pure.
[[nodiscard]] GraphMethod resolve_graph_method(std::optional<harness::AttachmentGraphMethod> flag,
                                               std::optional<harness::AttachmentGraphMethod> config,
                                               harness::AttachmentGraphMethod built_in);

/// What an attach line adds for a method the built-in did not choose --
/// `with its code graph (--graph=code)`, `without its code graph
/// (attachments.graph: off in the config)` -- and nothing for the built-in's.
[[nodiscard]] std::string graph_method_note(const GraphMethod& method);

/// `/attach`'s argument, read (27p): the path first -- quoted when it holds a
/// space, or everything before the first `--` word -- then its flags.
struct AttachArgument {
    std::string spec;
    /// `--graph=<method>` or `--graph <method>`; nullopt when not given.
    std::optional<harness::AttachmentGraphMethod> graph;
    /// Why the line is refused, naming the shape or the valid set; empty
    /// when it reads.
    std::string error;
};

/// `/attach`'s shape, as a refusal names it.
inline constexpr std::string_view kAttachShape = "/attach <path> [--graph=code|off]";

/// Reads `/attach`'s argument (after `parse_slash`'s trim). A flag before the
/// path, an unknown flag, a flag given twice, a missing or unknown value and
/// a word after the path's closing quote are refused, each naming the shape
/// or the valid set. An open quote -- completion leaves a folder's open --
/// reads as no quote.
[[nodiscard]] AttachArgument parse_attach_argument(std::string_view argument);

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
        /// This surface's built-in method for a folder of code (27n): a
        /// chat's builds its code graph; `complete`'s one-shot store does not
        /// -- a one-shot has no follow-up to walk it in. The lowest rung of
        /// each attach's resolution (27p): the config's `attachments.graph`
        /// beats it, and a `--graph` beats both.
        harness::AttachmentGraphMethod built_in_graph = harness::AttachmentGraphMethod::Code;
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

    /// The method an attach takes here (27p): `flag` over the config's
    /// `attachments.graph` over this surface's built-in, by
    /// `resolve_graph_method`.
    [[nodiscard]] GraphMethod graph_method(
        std::optional<harness::AttachmentGraphMethod> flag = std::nullopt) const;

    /// Queues what `spec` names -- a file, a folder, a glob -- for indexing
    /// and returns at once, a folder of code graphed or not as `method` says
    /// -- and when something other than the built-in chose it, the attach
    /// line says which method and what chose it. False, having said why,
    /// when it names nothing or the size guard refuses it.
    bool attach(std::string_view spec, const std::filesystem::path& working_directory,
                const GraphMethod& method);
    /// The same, with nothing on the line: `graph_method()`'s -- what an `@`
    /// mention and a launch's `--image` take.
    bool attach(std::string_view spec, const std::filesystem::path& working_directory);

    /// Waits for indexing in flight, then records each attachment and decides
    /// whether it is inlined. Ctrl-C keeps what is ready.
    void settle();

    /// Detaches the attachment named `name`, and its files from the index
    /// unless another attachment holds the same content -- and its part of
    /// the code graph (27n), the rest re-linked.
    [[nodiscard]] bool detach(std::string_view name);

    /// One line per attachment, for `/attachments` -- a graphed folder's
    /// ending with its graph's line, counted from the index as it is now.
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

    /// The chat's code graph, while a recorded folder carries one and the
    /// index holds it (27o): what an attachment turn expands through and what
    /// the scoped `graph` toolset reads -- nullopt for a chat whose
    /// attachments are chunks only, and again once the last graphed folder
    /// is detached. Decided from the chat's own state, never from `graphs:`.
    [[nodiscard]] std::optional<AttachmentGraphScope> graph_scope() const;

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
        /// Its code graph's pass (27n): built for a folder of code, or an
        /// earlier one forgotten; none for anything else.
        std::optional<AttachmentGraphJob> graph;
    };

    struct Indexed {
        std::string name;
        std::vector<agentloop::AttachmentIndex::Added> added;
        /// Its media as the chat model reads them natively, for the next
        /// message (26e); empty when it cannot.
        std::vector<harness::ContentPart> native;
        /// What its graph pass did; none when it ran none -- no job, or
        /// nothing of the attach was indexed, so the earlier one stands.
        std::optional<AttachmentGraphOutcome> graph;
    };

    void start_worker();
    /// The graph pass of a queued folder (27n), on the worker after its
    /// chunks: built -- or cancelled, failed, no longer asked for, its member
    /// left for the settle that records it to forget.
    [[nodiscard]] AttachmentGraphOutcome graph_pass(const AttachmentGraphJob& job,
                                                    const harness::CancellationToken& token);

    /// A folder attach's files, relative to it, and whether they include a
    /// language a vendored grammar parses -- the only attach a method
    /// changes anything for (27n).
    struct SourceFolder {
        std::filesystem::path root;
        std::vector<std::string> files;
        bool code = false;
    };

    /// The pass `spec` takes, if any: built when `method` is `code` and the
    /// folder offers code, an earlier graph forgotten when not.
    [[nodiscard]] std::optional<AttachmentGraphJob> graph_job(
        std::string_view spec, std::optional<SourceFolder> folder,
        harness::AttachmentGraphMethod method);
    /// The member `name`'s code graph is kept under, as the session records
    /// it, if it has one.
    [[nodiscard]] std::optional<std::string> graph_label_of(const std::string& name) const;
    /// The chat's graphed attachments' members but `name`'s.
    [[nodiscard]] std::set<std::string> graph_members_but(const std::string& name) const;

    /// What an indexed attachment's graph pass leaves the session and says
    /// (27n): its record, its line, whether the line is a warning -- the
    /// member labels kept in step.
    struct GraphSaid {
        std::optional<logger::AttachmentGraph> record;
        std::string line;
        bool warning = false;
    };

    [[nodiscard]] GraphSaid take_graph(const Indexed& indexed);
    /// After a settle's records, and a detach: any member of the index's
    /// code graph no recorded attachment owns is forgotten (27n), so the
    /// graph is exactly the session's graphed folders'.
    void reconcile_graph();
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
    /// The members of folders queued since the last settle, by name (27n):
    /// the session holds a recorded folder's, and these keep two folders of
    /// one name, attached before either settles, apart.
    std::map<std::string, std::string> graph_in_flight_;

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
