#include "agentloop/recall.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <system_error>
#include <utility>

#include "contracts/errors.h"
#include "embedstore/store.h"

namespace apogee::agentloop {
namespace {

namespace fs = std::filesystem;

/// The summariser's whole instruction.
constexpr std::string_view kSummaryPrompt =
    "Summarise the conversation below for a later conversation with the same user. In at most "
    "120 words of plain sentences, say what the user asked about, what was decided, the facts and "
    "preferences the user stated -- names, versions, tools, constraints -- and the files "
    "involved. Nothing else: no preamble, no advice, no headings.";

/// Each message as the summariser sees it, and the whole transcript.
constexpr std::size_t kMessageBytes = 1200;
constexpr std::size_t kTranscriptBytes = std::size_t{24} * 1024;

/// Enough for 120 words.
constexpr std::int64_t kSummaryTokens = 320;

/// `text` cut to at most `bytes`, never inside a character.
[[nodiscard]] std::string clipped(std::string_view text, std::size_t bytes) {
    if (text.size() <= bytes) {
        return std::string{text};
    }
    std::size_t end = bytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return std::string{text.substr(0, end)} + " ...";
}

[[nodiscard]] std::string trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(first, last - first + 1)};
}

/// Keeps a private directory private: a summary is a conversation, distilled.
void make_private(const fs::path& path, fs::perms mode) {
    std::error_code code;
    fs::permissions(path, mode, fs::perm_options::replace, code);
}

}  // namespace

harness::ChatRequest recall_summary_request(std::string_view model,
                                            const std::vector<harness::ChatMessage>& messages) {
    // The user's and the model's turns, each clipped; past the whole budget
    // the opening is kept, where a conversation usually states its facts, and
    // its end, where it decides.
    std::vector<std::string> lines;
    for (const harness::ChatMessage& message : messages) {
        if (message.role != harness::Role::User && message.role != harness::Role::Assistant) {
            continue;
        }
        const std::string text = trimmed(message.content.plain_text());
        if (text.empty()) {
            continue;
        }
        lines.push_back(
            std::string{message.role == harness::Role::User ? "User: " : "Assistant: "} +
            clipped(text, kMessageBytes));
    }
    std::size_t total = 0;
    for (const std::string& line : lines) {
        total += line.size() + 1;
    }
    while (total > kTranscriptBytes && lines.size() > 2) {
        const std::size_t middle = lines.size() / 2;
        total -= lines[middle].size() + 1;
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(middle));
    }
    std::string transcript;
    for (const std::string& line : lines) {
        transcript += line + "\n";
    }

    harness::ChatRequest request;
    request.model = std::string{model};
    request.messages = {harness::ChatMessage::system(std::string{kSummaryPrompt}),
                        harness::ChatMessage::user(transcript)};
    request.max_tokens = kSummaryTokens;
    request.temperature = 0.0;
    // Not a turn of any conversation, and nothing to reason about.
    request.transient.side_request = true;
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

std::optional<std::string> summarise_chat(const harness::Harness& harness, std::string_view model,
                                          const std::vector<harness::ChatMessage>& messages,
                                          const harness::CancellationToken& cancellation,
                                          std::string& note) {
    try {
        const harness::ChatResponse response =
            harness.chat(recall_summary_request(model, messages), cancellation);
        std::string summary = trimmed(response.message.content.plain_text());
        if (summary.empty()) {
            note = "the summary came back empty";
            return std::nullopt;
        }
        return summary;
    } catch (const harness::CancelledError&) {
        throw;
    } catch (const std::exception& e) {
        note = std::string{"the summary failed: "} + e.what();
        return std::nullopt;
    }
}

std::string recall_chunk(const RecallEntry& entry) {
    // The date is the chat's own: `updated_at` is ISO 8601, its first ten
    // characters the day.
    std::string line = "An earlier conversation";
    if (entry.updated_at.size() >= 10) {
        line += " (" + entry.updated_at.substr(0, 10) + ")";
    }
    if (!entry.title.empty()) {
        line += ", \"" + entry.title + "\"";
    }
    return line + ": " + entry.summary;
}

std::string describe_recall(int chats, int decisions) {
    const auto count = [](int n, std::string_view one, std::string_view many) {
        return std::to_string(n) + " " + std::string{n == 1 ? one : many};
    };
    std::string out;
    if (chats > 0) {
        out = count(chats, "past chat", "past chats");
    }
    if (decisions > 0) {
        out += (out.empty() ? "" : ", ") + count(decisions, "decision", "decisions");
    }
    return out;
}

RecallIndex::RecallIndex(fs::path store_path, std::optional<Embedder> embedder)
    : store_path_{std::move(store_path)}, embedder_{std::move(embedder)} {}

bool RecallIndex::put(const RecallEntry& entry, const harness::CancellationToken& cancellation,
                      std::string& note) {
    const std::string text = recall_chunk(entry);
    std::vector<std::vector<float>> vectors;
    // Vectors when they cost nothing: a summary embedded on Apogee's own
    // initiative is never billed. Without them the index is lexical.
    if (embedder_.has_value() && !embedder_->metered) {
        try {
            vectors = embedder_->embed({text}, cancellation);
        } catch (const harness::CancelledError&) {
            throw;
        } catch (const std::exception& e) {
            vectors.clear();
            note = std::string{"indexed without vectors: "} + e.what();
        }
        if (vectors.size() != 1) {
            vectors.clear();
        }
    }
    try {
        std::error_code code;
        fs::create_directories(store_path_.parent_path(), code);
        make_private(store_path_.parent_path(), fs::perms::owner_all);
        embedstore::Store store{store_path_};
        if (!vectors.empty() && !store.embedding_model().recorded()) {
            store.set_embedding_model(embedder_.value().model,
                                      static_cast<std::int64_t>(vectors.front().size()));
        }
        const nlohmann::json metadata{
            {"chat_id", entry.chat_id}, {"title", entry.title}, {"updated_at", entry.updated_at}};
        store.replace_source(entry.chat_id, {text}, vectors, {metadata.dump()});
        for (const std::string_view suffix : {"", "-wal", "-shm"}) {
            const fs::path file = store_path_.string() + std::string{suffix};
            if (fs::exists(file, code)) {
                make_private(file, fs::perms::owner_read | fs::perms::owner_write);
            }
        }
        return true;
    } catch (const std::exception& e) {
        note = std::string{"the summary could not be stored: "} + e.what();
        return false;
    }
}

bool RecallIndex::remove(std::string_view chat_id) {
    std::error_code code;
    if (!fs::exists(store_path_, code)) {
        return false;
    }
    try {
        embedstore::Store store{store_path_};
        return store.delete_source(chat_id) > 0;
    } catch (const std::exception&) {
        return false;
    }
}

std::string RecallIndex::summarised_at(std::string_view chat_id) const {
    std::error_code code;
    if (!fs::exists(store_path_, code)) {
        return {};
    }
    try {
        const embedstore::Store store{store_path_};
        for (const embedstore::Chunk& chunk : store.chunks_by_source(chat_id)) {
            const nlohmann::json metadata = nlohmann::json::parse(chunk.metadata, nullptr, false);
            if (metadata.is_object()) {
                return metadata.value("updated_at", std::string{});
            }
        }
    } catch (const std::exception&) {
        // An index that cannot be read has summarised nothing.
        return {};
    }
    return {};
}

std::size_t RecallIndex::chats() const {
    std::error_code code;
    if (!fs::exists(store_path_, code)) {
        return 0;
    }
    try {
        const embedstore::Store store{store_path_};
        return store.sources().size();
    } catch (const std::exception&) {
        return 0;
    }
}

}  // namespace apogee::agentloop
