#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/embed_func.h"
#include "harness/cancellation.h"
#include "harness/types.h"

/// Tool selection by relevance (26g): each turn offers the tools that bear on
/// its question rather than every tool registered -- native tools one by
/// one, an MCP server's tools whole.
///
/// Every definition costs prompt tokens on every step and room in the window,
/// and a small model picks worse from a long menu. Past a threshold the loop
/// offers a small always-on core, the tools the question ranks highest, and
/// `find_tools`, which searches the rest. A tool the model names without
/// having been offered it is still dispatched and gated -- models remember
/// tools from earlier turns -- and is offered from then on.
///
/// **The prompt's start is kept still.** A tool list sits at the top of a
/// prompt, so a different one is a different prefix, and a model that caches
/// its prompt (a local one, a cloud one with prompt caching) reads the whole
/// conversation again. So the offered set is fixed for a turn's steps, grows
/// only when a step needs more, and a new turn keeps the last turn's set
/// while that set already offers what the new question ranks first.
///
/// The ranking is the embedding role's vectors when one resolves that costs
/// nothing to call -- each tool's name and description embedded once and
/// cached by its definition's hash -- and BM25 over the same words when not.
/// The embedder arrives as a closure, like every model call this package
/// makes.
namespace apogee::agentloop {

/// Selection starts above this many registered tools; at or below it every
/// tool is offered, exactly as before (26g, default taken).
inline constexpr std::size_t kToolSelectionThreshold = 16;

/// How many of the ranked tools a turn offers beside the core (26g, default
/// taken).
inline constexpr std::size_t kSelectedTools = 8;

/// A new turn keeps the last turn's offered set when it already offers this
/// many of the new question's top-ranked tools: the ones it most likely
/// needs. Below that the set is chosen afresh, and the prompt read again.
inline constexpr std::size_t kSelectionKeep = 3;

/// The most definitions one `find_tools` call returns.
inline constexpr std::size_t kFoundTools = 5;

inline constexpr std::string_view kFindToolsName = "find_tools";

/// The tools offered whenever selection is on and the registry has them --
/// the ones almost every task begins with -- and `find_tools` (26g, default
/// taken).
[[nodiscard]] const std::vector<std::string>& core_tools();

/// The most names `find_tools`'s description lists; past it, how many more.
inline constexpr std::size_t kHiddenNamesListed = 100;

/// `find_tools`'s definition, its description naming the tools not shown
/// yet -- names only, never their schemas. Told nothing of them, a small
/// model makes do with the wrong tool rather than search: Qwen3-VL-8B,
/// asked to add someone to a meeting, reassigned a ticket (26g). Claude
/// Code lists its deferred tools the same way.
[[nodiscard]] harness::Tool find_tools_tool(const std::vector<std::string>& hidden = {});

/// The query a `find_tools` call carries; empty when it carries none.
[[nodiscard]] std::string find_tools_query(std::string_view arguments);

/// The words a ranking compares: a tool's name split at its underscores
/// (`mcp__tickets__create_ticket` is "mcp tickets create ticket"), then its
/// description.
[[nodiscard]] std::string tool_text(const harness::Tool& definition);

/// Vectors by cache key.
using ToolVectors = std::map<std::string, std::vector<float>, std::less<>>;

/// Where tool vectors come from: the embedding role, called through `embed`,
/// with `load` and `store` reading and writing vectors cached under a key
/// made of `model` and a definition's hash -- `store` given every vector one
/// ranking made, at once. `load` and `store` may be null.
struct ToolEmbedding {
    EmbedFunc embed;
    /// Names the vector space: part of every cache key.
    std::string model;
    std::function<std::optional<std::vector<float>>(const std::string& key)> load;
    std::function<void(const ToolVectors& made)> store;
};

/// One tool's place in a ranking.
struct RankedTool {
    std::string name;
    double score = 0.0;
};

/// BM25 over each definition's `tool_text`: the order every tool takes for
/// `query`, highest first, ties by name. Exposed so the lexical ranking can
/// be tested as a table.
[[nodiscard]] std::vector<RankedTool> rank_by_words(const std::vector<harness::Tool>& tools,
                                                    std::string_view query);

/// The server an MCP tool's name puts it under -- `mcp__tickets__` for
/// `mcp__tickets__create_ticket` -- or empty for a native tool.
[[nodiscard]] std::string tool_server(std::string_view name);

/// What a question is ranked by: itself, then each of its clauses -- split at
/// `.`, `;`, `:`, `?`, `!` and line ends -- of three words or more, when
/// there is more than one. A question with two intents ("the fix needs
/// review: add Sam to Tuesday's meeting") blends them into one vector,
/// which the stronger wins; each clause on its own still finds its tools.
[[nodiscard]] std::vector<std::string> ranking_queries(std::string_view question);

/// Cosine similarity, 0 when either vector is empty or their widths differ.
[[nodiscard]] double cosine(const std::vector<float>& a, const std::vector<float>& b);

/// Ranks a fixed set of tools against a question. Safe to share between
/// threads: the tools' vectors are made once, on the first ranking that
/// wants them, under a lock.
class ToolRanker {
public:
    /// `hashes` are the definitions' `agent::definition_hash`es, by name.
    /// Without an `embedding` every ranking is BM25.
    ToolRanker(std::vector<harness::Tool> tools,
               std::map<std::string, std::string, std::less<>> hashes,
               std::optional<ToolEmbedding> embedding);

    /// Every tool, highest first. By vectors when they could be made --
    /// each tool scored by its closest of `ranking_queries` -- and by words
    /// when there is no embedder, or it failed; `semantic()` says which ran
    /// last.
    [[nodiscard]] std::vector<RankedTool> rank(
        std::string_view query, const harness::CancellationToken& cancellation) const;

    /// Whether the last ranking compared vectors.
    [[nodiscard]] bool semantic() const;

    /// Why a ranking fell back to words, when an embedder was given and
    /// could not be used; empty otherwise.
    [[nodiscard]] std::string fallback_reason() const;

    [[nodiscard]] const std::vector<harness::Tool>& tools() const noexcept {
        return tools_;
    }

private:
    /// Fills `vectors_`, from the cache where it can; false when the
    /// embedder failed.
    bool ensure_vectors(const harness::CancellationToken& cancellation) const;

    std::vector<harness::Tool> tools_;
    std::map<std::string, std::string, std::less<>> hashes_;
    std::optional<ToolEmbedding> embedding_;
    mutable std::mutex mutex_;
    mutable bool vectors_tried_ = false;
    mutable std::vector<std::vector<float>> vectors_;
    mutable bool semantic_ = false;
    mutable std::string fallback_;
};

/// What a turn offers, and why -- for the `--verbose` line.
struct ToolOffer {
    /// Selection is on: the registry is past the threshold.
    bool active = false;
    /// The tools offered, by name.
    std::set<std::string, std::less<>> names;
    /// The question's top-ranked tools, highest first.
    std::vector<std::string> ranked;
    /// The last turn's set was kept: it already offered the question's top
    /// tools.
    bool kept = false;
    /// The ranking compared vectors rather than words.
    bool semantic = false;
};

/// One conversation's selection: the offered set, carried from turn to turn.
/// Not thread-safe -- one per conversation, as a conversation's turns are.
class ToolSelection {
public:
    /// `registry_size` is the registry's tool count, which decides whether
    /// selection is on at all.
    ToolSelection(std::shared_ptr<const ToolRanker> ranker, std::size_t registry_size);

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

    /// The set a turn asking `query` starts with: the core, the top
    /// `kSelectedTools` -- each MCP tool among them with the rest of its
    /// server -- or the last turn's set when that already offers the
    /// question's top `kSelectionKeep`. Inactive, it offers nothing and says
    /// so -- the caller then offers every tool.
    ToolOffer begin_turn(std::string_view query, const harness::CancellationToken& cancellation);

    /// Whether `name` is offered.
    [[nodiscard]] bool offered(std::string_view name) const;

    /// Offers `name` from the next step on, with the rest of its server;
    /// false when it already was.
    bool offer(const std::string& name);

    /// Answers a `find_tools` call: up to `kFoundTools` registered tools not
    /// yet offered, ranked for `query`, each offered from the next step on.
    /// The text is the tool result the model reads -- each one's name,
    /// description and argument schema.
    [[nodiscard]] std::string find(std::string_view query,
                                   const harness::CancellationToken& cancellation,
                                   std::vector<std::string>& added);

    [[nodiscard]] const ToolRanker& ranker() const noexcept {
        return *ranker_;
    }

private:
    /// Offers `name` and, for an MCP tool, every other tool of its server:
    /// a server's tools refer to each other's ids -- an invite needs the
    /// event `list_events` finds -- and on a local model a tool not offered
    /// cannot be called at all, its name being outside the grammar (the
    /// user's call, 2026-10-03, after a cross-server task failed on every
    /// family with single tools). False when `name` already was offered.
    bool offer_with_server(const std::string& name);

    std::shared_ptr<const ToolRanker> ranker_;
    bool active_ = false;
    std::set<std::string, std::less<>> offered_;
};

/// The `--verbose` line for a turn's offer: how many of how many, what the
/// question ranked first, and whether the set was kept.
[[nodiscard]] std::string describe_offer(const ToolOffer& offer, std::size_t registered);

}  // namespace apogee::agentloop
