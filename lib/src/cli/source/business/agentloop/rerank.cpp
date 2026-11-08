#include "agentloop/rerank.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>

#include "contracts/errors.h"
#include "contracts/types.h"
#include "embedstore/chunk.h"
#include "harness/roles.h"

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

/// A whole number written as a string -- `"2"`, `" 3 "` -- as that number;
/// nothing for anything else (57r).
[[nodiscard]] std::optional<long long> whole_number(std::string_view text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    text = text.substr(first, text.find_last_not_of(' ') - first + 1);
    // Nine digits is far past any candidate count, and never overflows.
    if (text.size() > 9 ||
        !std::ranges::all_of(text, [](char digit) { return digit >= '0' && digit <= '9'; })) {
        return std::nullopt;
    }
    long long value = 0;
    for (const char digit : text) {
        value = (value * 10) + (digit - '0');
    }
    return value;
}

/// Every bracketed span of `reply` that parses as a JSON array, in order --
/// a bracket's span closing where its nesting does. Bracketed prose (`[see
/// below]`) is not one; an unclosed bracket ends the reading.
[[nodiscard]] std::vector<nlohmann::json> arrays_in(std::string_view reply) {
    std::vector<nlohmann::json> arrays;
    std::size_t start = reply.find('[');
    while (start != std::string_view::npos) {
        std::size_t depth = 0;
        std::size_t end = std::string_view::npos;
        for (std::size_t at = start; at < reply.size(); ++at) {
            if (reply[at] == '[') {
                ++depth;
            } else if (reply[at] == ']' && --depth == 0) {
                end = at;
                break;
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        nlohmann::json parsed =
            nlohmann::json::parse(reply.substr(start, end - start + 1), nullptr, false);
        if (!parsed.is_discarded() && parsed.is_array()) {
            arrays.push_back(std::move(parsed));
        }
        start = reply.find('[', end + 1);
    }
    return arrays;
}

}  // namespace

bool valid_rerank(std::string_view value, const harness::Config& config) {
    return value.empty() || value == kRerankOff || value == kRerankOn ||
           config.find_backend(value) != nullptr;
}

RerankChoice resolve_turn_rerank(std::string_view flag, std::string_view pin,
                                 const harness::Config& config, std::string_view conversation) {
    RerankChoice out;
    if (flag == kRerankOff) {
        return out;
    }
    std::string_view wanted = !flag.empty() ? flag : pin;
    if (wanted.empty() || wanted == kRerankOff) {
        return out;
    }
    std::string helper;
    if (wanted == kRerankOn) {
        helper = harness::resolve_backend_key(
            config, harness::RoleRequest{.role = harness::ModelRole::Utility,
                                         .conversation = conversation});
        if (helper.empty()) {
            out.note =
                "rerank is on, but there is no utility model or chat backend to judge -- "
                "reranking disabled (set models.default_utility, or name a backend)";
            return out;
        }
        wanted = helper;
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

RerankReading read_rerank_reply(std::string_view reply, std::size_t candidates) {
    const std::vector<nlohmann::json> arrays = arrays_in(reply);
    if (arrays.empty()) {
        return RerankReading{.ids = std::nullopt,
                             .problem = "returned something that was not a ranking"};
    }
    if (arrays.size() > 1) {
        // `Passage [3] is off-topic; the ranking is [2, 1]`: taking either
        // one is a guess, and the first was once taken as the ranking (57r).
        return RerankReading{.ids = std::nullopt,
                             .problem = "returned " + std::to_string(arrays.size()) +
                                        " bracketed lists, so which one ranks is a guess"};
    }
    const nlohmann::json& parsed = arrays.front();
    std::vector<int> ids;
    std::vector<bool> seen(candidates + 1, false);
    for (const nlohmann::json& item : parsed) {
        std::optional<long long> id;
        if (item.is_number_integer()) {
            id = item.get<long long>();
        } else if (item.is_string()) {
            // The prompt asks for numbers, but a quoted id is plainly an id.
            id = whole_number(item.get_ref<const std::string&>());
        }
        if (!id.has_value() || *id < 1 || *id > static_cast<long long>(candidates) ||
            seen[static_cast<std::size_t>(*id)]) {
            // A mostly good ranking is still better than raw order.
            continue;
        }
        seen[static_cast<std::size_t>(*id)] = true;
        ids.push_back(static_cast<int>(*id));
    }
    if (ids.empty() && !parsed.empty()) {
        // Entries, none of them a passage: not the judge's "none help",
        // which is `[]` and nothing else (57r).
        return RerankReading{.ids = std::nullopt,
                             .problem = "returned a ranking with no usable passage number"};
    }
    return RerankReading{.ids = std::move(ids), .problem = {}};
}

std::optional<std::vector<int>> parse_rerank_ids(std::string_view reply, std::size_t candidates) {
    return read_rerank_reply(reply, candidates).ids;
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
    // Not a turn of the conversation: a local judge runs on its own context,
    // and a chat judging with its own model keeps its cache (26b).
    request.transient.side_request = true;

    std::string reply;
    try {
        reply = harness.chat(request, cancellation).message.content.plain_text();
    } catch (const harness::CancelledError&) {
        // A stopped turn, not a broken judge (57r): never "raw order".
        throw;
    } catch (const std::exception& e) {
        // A broken judge degrades to raw order. It never costs context.
        out.hits = truncated(hits, limit);
        out.note = std::string{"rerank via '"} + std::string{backend} + "' failed (" + e.what() +
                   ") -- using raw retrieval order";
        return out;
    }

    const RerankReading reading = read_rerank_reply(reply, candidates.size());
    if (!reading.ids.has_value()) {
        out.hits = truncated(hits, limit);
        out.note = "rerank via '" + std::string{backend} + "' " + reading.problem +
                   " -- using raw retrieval order";
        return out;
    }
    const std::vector<int>& ids = *reading.ids;

    // A verdict, applied. An empty one means nothing retrieved bears on the
    // question: the relevance floor a lexical match cannot provide.
    out.applied = true;
    for (const int id : ids) {
        out.hits.push_back(candidates[static_cast<std::size_t>(id - 1)]);
    }
    out.hits = truncated(out.hits, limit);
    return out;
}

}  // namespace apogee::agentloop
