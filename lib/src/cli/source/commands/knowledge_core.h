#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/retriever.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "knowledge/clerk.h"
#include "knowledge/record.h"

/// The capture core every surface calls.
///
/// `apogee knowledge capture`, chat's `/capture`, the exit-time auto-capture
/// and `POST /v1/admin/knowledge/capture` all produce a record through the
/// functions here, in the same order: the clerk, the overrides, the retriever
/// decision, the embedding, the archive, the store, the supersede, and the
/// collection's registration. That is what makes the record a surface
/// produces identical to every other surface's -- "one source of truth per
/// concern", for the one concern that has four entry points.
///
/// The clerk arrives as a `knowledge::ClerkFn` already bound to a harness and
/// a model: the CLI binds the resolved chat backend, chat binds its loaded
/// model, the HTTP twin binds the served backend. Nothing here decides which
/// model runs.
namespace apogee::commands {

/// What a capture starts from.
struct CaptureInputs {
    /// The raw conversation.
    std::string raw;
    knowledge::Overrides overrides;
    /// The collection; empty means the config's `knowledge.db`, then the
    /// default.
    std::string db;
    /// `--retriever`; empty means auto. Resolved against the collection's
    /// own pin, never a chat session's document retriever.
    std::string retriever_flag;
};

/// How the record would be, or was, indexed.
struct StoreDecision {
    std::string db;
    agentloop::Retriever retriever = agentloop::Retriever::Lexical;
    /// The resolver's note, when it fell back and said why.
    std::string note;
    /// What a real capture would fail on (`--dry-run` only); a real run
    /// reports it as `error`.
    std::string warning;
    /// The embedder's model, when vectors were (or would be) written.
    std::string embed_model;
};

/// The outcome of a capture or a store.
struct CaptureResult {
    knowledge::Record record;
    StoreDecision decision;
    /// The record was stored (false for a dry run, or on `error`).
    bool stored = false;
    /// The collection was registered under `embeddings:` by this call.
    bool registered = false;
    /// Non-fatal things worth saying: a supersedes target not found, a
    /// registration that could not be written.
    std::vector<std::string> notes;
    /// Why nothing was stored: the clerk failed, the resolver refused, the
    /// store threw.
    std::string error;
    /// The failure is the model's (a backend error) rather than the user's.
    bool backend_error = false;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// The effective collection for `db`: the flag, else `knowledge.db`, else
/// the default.
[[nodiscard]] std::string knowledge_collection(const harness::Config& config, std::string_view db);

/// Decides how a record captured into `db` would be indexed: the flag, the
/// collection's pin, the embedder the harness resolves -- through the ONE
/// ingest resolver `embed ingest` uses, under the same spend rule.
[[nodiscard]] StoreDecision decide_store(const harness::Harness& harness,
                                         const harness::Config& config, std::string_view db,
                                         std::string_view retriever_flag);

/// Runs the clerk over `inputs.raw` and drafts the record -- what a dry run
/// prints: id-less, timestamp-less, nothing written. `decision` is filled
/// either way, so the dry run can say what a real one would do.
[[nodiscard]] CaptureResult draft_capture(const harness::Harness& harness,
                                          const harness::Config& config,
                                          const CaptureInputs& inputs,
                                          const knowledge::ClerkFn& clerk);

/// Stores a finished record: assigns the id and timestamp when missing,
/// embeds the index text per `decision`, archives `raw`, writes the chunk,
/// flips a superseded record, and registers the collection on first use
/// through the one config editor. `record` is updated in place with what
/// was assigned.
[[nodiscard]] CaptureResult store_record(const harness::Harness& harness,
                                         const harness::Config& config,
                                         const std::filesystem::path& config_path,
                                         knowledge::Record record, std::string_view raw,
                                         const StoreDecision& decision);

/// The whole thing: `draft_capture`, then `store_record`.
[[nodiscard]] CaptureResult capture_and_store(const harness::Harness& harness,
                                              const harness::Config& config,
                                              const std::filesystem::path& config_path,
                                              const CaptureInputs& inputs,
                                              const knowledge::ClerkFn& clerk);

/// The first `width` codepoints of `text` on one line, with an ellipsis
/// when cut -- for the one-line "Captured ..." confirmation.
[[nodiscard]] std::string preview_text(std::string_view text, std::size_t width);

}  // namespace apogee::commands
