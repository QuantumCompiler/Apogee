#include "agentloop/rerank.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>

#include "embedstore/chunk.h"
#include "harness/types.h"

namespace apogee::agentloop {
namespace {

/// Cut to `limit` (<= 0 means no cut).
[[nodiscard]] std::vector<embedstore::SearchHit> truncated(
    const std::vector<embedstore::SearchHit>& hits, int limit) {
    if (limit > 0 && static_cast<std::size_t>(limit) < hits.size()) {
        return {hits.begin(), hits.begin() + limit};
    }
    return hits;
}

/// The first `count` codepoints, marked when clipped.
[[nodiscard]] std::string clipped(std::string_view text, std::size_t count) {
    if (embedstore::codepoint_count(text) <= count) {
        return std::string{text};
    }
    const std::vector<std::string> pieces =
        embedstore::chunk_text(text, {.size = count, .overlap = 0});
    return (pieces.empty() ? std::string{} : pieces.front()) + "…";
}

}  // namespace

RerankChoice resolve_turn_rerank(std::string_view flag, std::string_view pin,
                                 const harness::Config& config) {
    RerankChoice out;
    if (flag == kRerankOff) {
        return out;
    }
    const std::string_view wanted = !flag.empty() ? flag : pin;
    if (wanted.empty() || wanted == kRerankOff) {
        return out;
    }
    if (config.find_backend(wanted) == nullptr) {
        out.note = "rerank backend '" + std::string{wanted} +
                   "' is not configured -- reranking disabled (add it under backends:, or "
                   "pass --rerank off)";
        return out;
    }
    out.backend = std::string{wanted};
    return out;
}

int rerank_fetch_limit(int limit, bool reranking) noexcept {
    if (!reranking || limit <= 0) {
        return limit;
    }
    return std::min(limit * kRerankWidenFactor, kRerankMaxCandidates);
}

std::string build_rerank_prompt(std::string_view question,
                                const std::vector<embedstore::SearchHit>& candidates, int limit) {
    std::string out =
        "You are ranking retrieved passages by how much they help answer a "
        "question.\n\nQuestion:\n";
    out += question;
    out += "\n\nPassages:\n";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        out += "[" + std::to_string(index + 1) + "] " +
               clipped(candidates[index].chunk.text, kRerankSnippetCodepoints) + "\n";
    }
    out += "\nReply with ONLY a JSON array of passage numbers, most useful first, at most " +
           std::to_string(limit) +
           " of them.\nLeave out any passage that does not help answer the question -- a "
           "passage that merely shares words with it is not useful. If none of them help, "
           "reply with [].\n\nExample reply: [3, 1]";
    return out;
}

std::optional<std::vector<int>> parse_rerank_ids(std::string_view reply, std::size_t candidates) {
    const std::size_t start = reply.find('[');
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t end = reply.find(']', start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const nlohmann::json parsed =
        nlohmann::json::parse(reply.substr(start, end - start + 1), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        return std::nullopt;
    }
    std::vector<int> ids;
    std::vector<bool> seen(candidates + 1, false);
    for (const nlohmann::json& item : parsed) {
        if (!item.is_number_integer()) {
            continue;
        }
        const auto id = item.get<long long>();
        if (id < 1 || id > static_cast<long long>(candidates) ||
            seen[static_cast<std::size_t>(id)]) {
            // A mostly good ranking is still better than raw order.
            continue;
        }
        seen[static_cast<std::size_t>(id)] = true;
        ids.push_back(static_cast<int>(id));
    }
    return ids;
}

RerankOutcome rerank(const harness::Harness& harness, std::string_view backend,
                     std::string_view question, const std::vector<embedstore::SearchHit>& hits,
                     int limit, const harness::CancellationToken& cancellation) {
    RerankOutcome out;
    if (hits.size() <= 1) {
        // Nothing to reorder. Not a failure and not an application either.
        out.hits = truncated(hits, limit);
        return out;
    }
    if (backend.empty()) {
        out.hits = truncated(hits, limit);
        out.note = "no rerank backend";
        return out;
    }

    std::vector<embedstore::SearchHit> candidates =
        hits.size() > static_cast<std::size_t>(kRerankMaxCandidates)
            ? std::vector<embedstore::SearchHit>{hits.begin(), hits.begin() + kRerankMaxCandidates}
            : hits;

    harness::ChatRequest request;
    request.model = std::string{backend};
    request.messages = {
        harness::ChatMessage::user(build_rerank_prompt(question, candidates, limit))};
    request.temperature = 0.0;
    request.max_tokens = kRerankMaxTokens;

    std::string reply;
    try {
        reply = harness.chat(request, cancellation).message.content.plain_text();
    } catch (const std::exception& e) {
        // A broken judge degrades to raw order. It never costs context.
        out.hits = truncated(hits, limit);
        out.note = std::string{"rerank via '"} + std::string{backend} + "' failed (" + e.what() +
                   ") -- using raw retrieval order";
        return out;
    }

    const std::optional<std::vector<int>> ids = parse_rerank_ids(reply, candidates.size());
    if (!ids.has_value()) {
        out.hits = truncated(hits, limit);
        out.note = "rerank via '" + std::string{backend} +
                   "' returned something that was not a ranking -- using raw retrieval order";
        return out;
    }

    // A verdict, applied. An empty one means nothing retrieved bears on the
    // question: the relevance floor a lexical match cannot provide.
    out.applied = true;
    for (const int id : *ids) {
        out.hits.push_back(candidates[static_cast<std::size_t>(id - 1)]);
    }
    out.hits = truncated(out.hits, limit);
    return out;
}

}  // namespace apogee::agentloop
