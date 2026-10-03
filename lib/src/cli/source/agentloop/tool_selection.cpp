#include "agentloop/tool_selection.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <utility>

#include "harness/errors.h"

namespace apogee::agentloop {
namespace {

/// Words too common in tool descriptions and questions to tell one tool from
/// another.
const std::set<std::string, std::less<>>& stop_words() {
    static const std::set<std::string, std::less<>> words{
        "a",     "an",    "and",  "are",  "as",   "at",     "be",     "by",    "can",  "do",
        "does",  "for",   "from", "has",  "have", "how",    "i",      "in",    "into", "is",
        "it",    "its",   "me",   "my",   "of",   "on",     "or",     "our",   "so",   "that",
        "the",   "their", "them", "then", "this", "to",     "use",    "was",   "what", "when",
        "which", "will",  "with", "you",  "your", "please", "should", "would", "could"};
    return words;
}

/// A word reduced to a shared form: lower case, and a plural's last `s`
/// dropped, so "files" meets "file".
std::string normalized(std::string word) {
    if (word.size() > 4 && word.ends_with("ies")) {
        word.replace(word.size() - 3, 3, "y");
    } else if (word.size() > 3 && word.ends_with('s') && !word.ends_with("ss")) {
        word.pop_back();
    }
    return word;
}

/// The words of `text` a ranking compares: letters and digits, lower case,
/// stop words dropped.
std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    const auto flush = [&] {
        if (!word.empty() && !stop_words().contains(word)) {
            out.push_back(normalized(word));
        }
        word.clear();
    };
    for (const char raw : text) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0) {
            word.push_back(static_cast<char>(std::tolower(character)));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

/// Highest first; ties by name, so a ranking is the same every time.
void sort_ranking(std::vector<RankedTool>& ranking) {
    std::ranges::sort(ranking, [](const RankedTool& a, const RankedTool& b) {
        return a.score != b.score ? a.score > b.score : a.name < b.name;
    });
}

}  // namespace

const std::vector<std::string>& core_tools() {
    static const std::vector<std::string> core{"read_file", "list_directory", "run_command",
                                               std::string{kFindToolsName}};
    return core;
}

harness::Tool find_tools_tool(const std::vector<std::string>& hidden) {
    harness::Tool tool;
    tool.name = std::string{kFindToolsName};
    tool.description =
        "Find tools you have not been shown and add them to your tools from your next step. "
        "The tools you have are only the ones that looked closest to the task: when it needs "
        "something none of them does, call this first, saying in a few words what you need.";
    if (!hidden.empty()) {
        tool.description += " Tools not shown yet:";
        for (std::size_t index = 0; index < hidden.size() && index < kHiddenNamesListed; ++index) {
            tool.description += (index == 0 ? " " : ", ") + hidden[index];
        }
        if (hidden.size() > kHiddenNamesListed) {
            tool.description +=
                ", and " + std::to_string(hidden.size() - kHiddenNamesListed) + " more";
        }
        tool.description += ".";
    }
    tool.parameters_schema =
        R"({"type":"object","properties":{"query":{"type":"string","description":)"
        R"("What you need a tool for, in a few words"}},"required":["query"]})";
    return tool;
}

std::string find_tools_query(std::string_view arguments) {
    const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    if (parsed.is_object()) {
        if (const auto query = parsed.find("query"); query != parsed.end() && query->is_string()) {
            return query->get<std::string>();
        }
    }
    return {};
}

std::string tool_text(const harness::Tool& definition) {
    std::string name;
    for (const char character : definition.name) {
        if (character == '_') {
            if (!name.empty() && name.back() != ' ') {
                name.push_back(' ');
            }
        } else {
            name.push_back(character);
        }
    }
    while (!name.empty() && name.back() == ' ') {
        name.pop_back();
    }
    return name + ": " + definition.description;
}

std::vector<RankedTool> rank_by_words(const std::vector<harness::Tool>& tools,
                                      std::string_view query) {
    // BM25, with the usual constants, over each tool's name and description.
    constexpr double k1 = 1.2;
    constexpr double b = 0.75;

    std::vector<std::vector<std::string>> documents;
    documents.reserve(tools.size());
    double total_length = 0.0;
    std::map<std::string, std::size_t, std::less<>> containing;
    for (const harness::Tool& tool : tools) {
        documents.push_back(words_of(tool_text(tool)));
        total_length += static_cast<double>(documents.back().size());
        const std::set<std::string, std::less<>> distinct{documents.back().begin(),
                                                          documents.back().end()};
        for (const std::string& word : distinct) {
            ++containing[word];
        }
    }
    const auto count = static_cast<double>(tools.size());
    const double average = tools.empty() ? 0.0 : total_length / count;

    const std::vector<std::string> asked = words_of(query);
    const std::set<std::string, std::less<>> terms{asked.begin(), asked.end()};

    std::vector<RankedTool> ranking;
    ranking.reserve(tools.size());
    for (std::size_t index = 0; index < tools.size(); ++index) {
        const std::vector<std::string>& document = documents[index];
        const auto length = static_cast<double>(document.size());
        double score = 0.0;
        for (const std::string& term : terms) {
            const auto frequency = static_cast<double>(std::ranges::count(document, term));
            if (frequency == 0.0) {
                continue;
            }
            const auto with = static_cast<double>(containing[term]);
            const double idf = std::log(1.0 + ((count - with + 0.5) / (with + 0.5)));
            const double norm = 1.0 - b + (b * length / (average > 0 ? average : 1));
            score += idf * frequency * (k1 + 1.0) / (frequency + (k1 * norm));
        }
        ranking.push_back(RankedTool{.name = tools[index].name, .score = score});
    }
    sort_ranking(ranking);
    return ranking;
}

std::string tool_server(std::string_view name) {
    constexpr std::string_view prefix = "mcp__";
    if (!name.starts_with(prefix)) {
        return {};
    }
    const std::size_t end = name.find("__", prefix.size());
    if (end == std::string_view::npos || end == prefix.size()) {
        return {};
    }
    return std::string{name.substr(0, end + 2)};
}

std::vector<std::string> ranking_queries(std::string_view question) {
    std::vector<std::string> out{std::string{question}};
    std::vector<std::string> clauses;
    std::string clause;
    const auto flush = [&] {
        if (words_of(clause).size() >= 3) {
            clauses.push_back(clause);
        }
        clause.clear();
    };
    for (const char character : question) {
        if (character == '.' || character == ';' || character == ':' || character == '?' ||
            character == '!' || character == '\n') {
            flush();
        } else {
            clause.push_back(character);
        }
    }
    flush();
    if (clauses.size() > 1) {
        out.insert(out.end(), clauses.begin(), clauses.end());
    }
    return out;
}

double cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) {
        return 0.0;
    }
    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    for (std::size_t index = 0; index < a.size(); ++index) {
        dot += static_cast<double>(a[index]) * static_cast<double>(b[index]);
        norm_a += static_cast<double>(a[index]) * static_cast<double>(a[index]);
        norm_b += static_cast<double>(b[index]) * static_cast<double>(b[index]);
    }
    if (norm_a == 0.0 || norm_b == 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

ToolRanker::ToolRanker(std::vector<harness::Tool> tools,
                       std::map<std::string, std::string, std::less<>> hashes,
                       std::optional<ToolEmbedding> embedding)
    : tools_{std::move(tools)}, hashes_{std::move(hashes)}, embedding_{std::move(embedding)} {}

bool ToolRanker::ensure_vectors(const harness::CancellationToken& cancellation) const {
    if (!embedding_.has_value()) {
        return false;
    }
    if (vectors_tried_) {
        return !vectors_.empty();
    }
    std::vector<std::vector<float>> vectors(tools_.size());
    std::vector<std::size_t> missing;
    std::vector<std::string> keys(tools_.size());
    for (std::size_t index = 0; index < tools_.size(); ++index) {
        const auto hash = hashes_.find(tools_[index].name);
        keys[index] =
            embedding_->model + ":" + (hash != hashes_.end() ? hash->second : tools_[index].name);
        std::optional<std::vector<float>> cached;
        if (embedding_->load) {
            cached = embedding_->load(keys[index]);
        }
        if (cached.has_value() && !cached->empty()) {
            vectors[index] = std::move(*cached);
        } else {
            missing.push_back(index);
        }
    }
    if (!missing.empty()) {
        std::vector<std::string> texts;
        texts.reserve(missing.size());
        for (const std::size_t index : missing) {
            texts.push_back(tool_text(tools_[index]));
        }
        std::vector<std::vector<float>> made;
        try {
            made = embedding_->embed(texts, cancellation);
        } catch (const harness::CancelledError&) {
            // Not a failure of the embedder: the next turn tries again.
            throw;
        } catch (const std::exception& e) {
            vectors_tried_ = true;
            fallback_ = std::string{"the embedder failed: "} + e.what();
            return false;
        }
        if (made.size() != missing.size()) {
            vectors_tried_ = true;
            fallback_ = "the embedder returned " + std::to_string(made.size()) + " vectors for " +
                        std::to_string(missing.size()) + " tools";
            return false;
        }
        ToolVectors stored;
        for (std::size_t at = 0; at < missing.size(); ++at) {
            vectors[missing[at]] = std::move(made[at]);
            stored.emplace(keys[missing[at]], vectors[missing[at]]);
        }
        if (embedding_->store) {
            embedding_->store(stored);
        }
    }
    vectors_tried_ = true;
    vectors_ = std::move(vectors);
    return true;
}

std::vector<RankedTool> ToolRanker::rank(std::string_view query,
                                         const harness::CancellationToken& cancellation) const {
    const std::scoped_lock lock{mutex_};
    semantic_ = false;
    if (embedding_.has_value() && embedding_->embed && ensure_vectors(cancellation)) {
        const std::vector<std::string> queries = ranking_queries(query);
        std::vector<std::vector<float>> asked;
        try {
            asked = embedding_->embed(queries, cancellation);
        } catch (const harness::CancelledError&) {
            throw;
        } catch (const std::exception& e) {
            fallback_ = std::string{"the embedder failed: "} + e.what();
        }
        if (asked.size() == queries.size()) {
            std::vector<RankedTool> ranking;
            ranking.reserve(tools_.size());
            for (std::size_t index = 0; index < tools_.size(); ++index) {
                double best = 0.0;
                for (const std::vector<float>& vector : asked) {
                    best = std::max(best, cosine(vector, vectors_[index]));
                }
                ranking.push_back(RankedTool{.name = tools_[index].name, .score = best});
            }
            sort_ranking(ranking);
            semantic_ = true;
            return ranking;
        }
    }
    return rank_by_words(tools_, query);
}

bool ToolRanker::semantic() const {
    const std::scoped_lock lock{mutex_};
    return semantic_;
}

std::string ToolRanker::fallback_reason() const {
    const std::scoped_lock lock{mutex_};
    return fallback_;
}

ToolSelection::ToolSelection(std::shared_ptr<const ToolRanker> ranker, std::size_t registry_size)
    : ranker_{std::move(ranker)},
      active_{ranker_ != nullptr && registry_size > kToolSelectionThreshold} {}

ToolOffer ToolSelection::begin_turn(std::string_view query,
                                    const harness::CancellationToken& cancellation) {
    ToolOffer offer;
    if (!active_) {
        return offer;
    }
    offer.active = true;
    const std::vector<RankedTool> ranking = ranker_->rank(query, cancellation);
    offer.semantic = ranker_->semantic();
    for (const RankedTool& ranked : ranking) {
        if (offer.ranked.size() == kSelectedTools) {
            break;
        }
        offer.ranked.push_back(ranked.name);
    }

    const std::size_t keep = std::min(kSelectionKeep, offer.ranked.size());
    const bool covered =
        !offered_.empty() &&
        std::all_of(offer.ranked.begin(), offer.ranked.begin() + static_cast<std::ptrdiff_t>(keep),
                    [this](const std::string& name) { return offered_.contains(name); });
    if (covered) {
        offer.kept = true;
    } else {
        offered_.clear();
        for (const std::string& name : core_tools()) {
            const bool registered =
                name == kFindToolsName ||
                std::ranges::any_of(ranker_->tools(), [&name](const harness::Tool& tool) {
                    return tool.name == name;
                });
            if (registered) {
                offered_.insert(name);
            }
        }
        for (const std::string& name : offer.ranked) {
            (void)offer_with_server(name);
        }
    }
    offer.names = offered_;
    return offer;
}

bool ToolSelection::offered(std::string_view name) const {
    return !active_ || offered_.contains(name);
}

bool ToolSelection::offer(const std::string& name) {
    return offer_with_server(name);
}

bool ToolSelection::offer_with_server(const std::string& name) {
    const bool added = offered_.insert(name).second;
    const std::string server = tool_server(name);
    if (!server.empty()) {
        for (const harness::Tool& tool : ranker_->tools()) {
            if (tool.name.starts_with(server)) {
                offered_.insert(tool.name);
            }
        }
    }
    return added;
}

std::string ToolSelection::find(std::string_view query,
                                const harness::CancellationToken& cancellation,
                                std::vector<std::string>& added) {
    added.clear();
    if (query.empty()) {
        return "Error: find_tools needs a query -- what you need a tool for, in a few words.";
    }
    const std::vector<RankedTool> ranking = ranker_->rank(query, cancellation);
    const bool semantic = ranker_->semantic();
    std::string out;
    for (const RankedTool& ranked : ranking) {
        if (added.size() == kFoundTools) {
            break;
        }
        // Words that share nothing with the query are no match at all; a
        // vector always scores, so its order alone decides.
        if ((!semantic && ranked.score <= 0.0) || offered_.contains(ranked.name)) {
            continue;
        }
        const auto tool = std::ranges::find_if(
            ranker_->tools(), [&ranked](const harness::Tool& t) { return t.name == ranked.name; });
        if (tool == ranker_->tools().end()) {
            continue;
        }
        (void)offer_with_server(ranked.name);
        added.push_back(ranked.name);
        out += "\n- " + tool->name + ": " + tool->description +
               "\n  arguments: " + tool->parameters_schema;
    }
    if (added.empty()) {
        return "No other tool matches \"" + std::string{query} +
               "\". The tools you already have are the ones there are for it.";
    }
    return "These tools are yours from your next step, with the rest of each one's server:" + out;
}

std::string describe_offer(const ToolOffer& offer, std::size_t registered) {
    const std::size_t offered = offer.names.size() - (offer.names.contains(kFindToolsName) ? 1 : 0);
    std::string line = "tools: " + std::to_string(offered) + " of " + std::to_string(registered) +
                       " offered, and find_tools, ranked by " +
                       (offer.semantic ? "meaning" : "words");
    if (!offer.ranked.empty()) {
        line += " -- for this question: ";
        for (std::size_t index = 0; index < offer.ranked.size(); ++index) {
            line += (index == 0 ? "" : ", ") + offer.ranked[index];
        }
    }
    if (offer.kept) {
        line += " (the last turn's tools kept)";
    }
    return line;
}

}  // namespace apogee::agentloop
