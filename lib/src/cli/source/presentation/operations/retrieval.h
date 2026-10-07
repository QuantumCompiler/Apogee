#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "agentloop/budget.h"
#include "agentloop/rag.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// Which collection a turn retrieves from, the retrieval itself, and what is
/// said about it -- one implementation for the command line, chat and the
/// served plane (A4: moved out of `cli/helpers`).
namespace apogee::commands {

/// Where a turn's retrieval collection came from.
enum class RagSource : std::uint8_t {
    /// No retrieval this turn.
    None,
    /// `--rag <name>` on the command line.
    Flag,
    /// The config's `auto_rag` key, with no flag given.
    Config,
};

/// The collection a turn retrieves from, and why.
struct RagChoice {
    std::string collection;
    RagSource source = RagSource::None;

    [[nodiscard]] bool active() const noexcept {
        return !collection.empty();
    }
};

/// Decides which collection a turn retrieves from.
///
/// **One function, called by every surface**, because the precedence is the
/// whole contract: the flag beats the config, and an explicitly empty flag
/// (`--rag ""`) means *no retrieval this run* rather than "fall back to
/// `auto_rag`". A key that cannot be switched off for a single invocation is
/// a key people stop using. `flag_given` is therefore distinct from
/// `flag_value.empty()` -- an absent flag falls through to the config, a
/// present-but-empty one does not.
[[nodiscard]] RagChoice choose_rag_collection(bool flag_given, std::string_view flag_value,
                                              std::string_view auto_rag);

/// What to tell the user about a turn's retrieval, without the surface's own
/// tag or framing.
///
/// Always names the chunk count, the top score, and the retriever -- and says
/// when the collection came from `auto_rag` rather than a flag. **Silent
/// injection is the failure mode**: a user who does not know context was added
/// cannot tell why an answer went sideways, and that is doubly true when
/// nothing on the command line asked for it.
/// The status line for a turn's search of the chat's attachments (26d):
/// excerpts, top score and the retriever, like a collection's -- and, when
/// the chat's code graph expanded the turn, `+N graph entities` (27o).
[[nodiscard]] std::string describe_attachment_retrieval(const agentloop::RagResult& result);

[[nodiscard]] std::string describe_retrieval(const RagChoice& choice,
                                             const agentloop::RagResult& result);

/// One turn's retrieval against a named collection, for every surface.
///
/// Gathers what the resolver needs -- the collection's pins from the config,
/// the embedder that would answer (through the capability probe, never a
/// type), the judge -- and hands them to `agentloop::retrieve_for_turn`, which
/// decides once and reports honestly. Lives here so `complete` and `chat`
/// cannot assemble the facts differently.
///
/// `conversation` is the backend the chat is on, where there is one: what
/// `rerank: on` falls back to when no utility model is set. `budget` is the
/// turn's context budget (26c): what is injected fits its retrieval share,
/// less `share_used` -- what the chat's attachments already took (26d).
[[nodiscard]] agentloop::RagResult retrieve_for_collection(
    const harness::Harness& harness, const harness::Config& config, std::string_view collection,
    const std::string& question, int limit, std::string_view retriever_flag,
    std::string_view rerank_flag, const harness::CancellationToken& cancellation,
    std::string_view conversation = {}, const agentloop::TurnBudget& budget = {},
    std::int64_t share_used = 0, const agentloop::SideCallSink& on_side_call = {});

}  // namespace apogee::commands
