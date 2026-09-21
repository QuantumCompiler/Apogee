#include "commands/knowledge_core.h"

#include <chrono>
#include <exception>
#include <utility>

#include "agentloop/embed_func.h"
#include "commands/embed.h"
#include "harness/config_edit.h"
#include "harness/errors.h"
#include "harness/layout.h"
#include "knowledge/store.h"

namespace apogee::commands {
namespace {

[[nodiscard]] agentloop::EmbedderFacts facts_for(const std::optional<agentloop::Embedder>& e) {
    agentloop::EmbedderFacts facts;
    if (e.has_value()) {
        facts.available = true;
        facts.model = e->model;
        facts.metered = e->metered;
    }
    return facts;
}

/// The embedder a capture into `db` would use, when the decision could need
/// one: through the capability probe on the collection's pin, never a type.
[[nodiscard]] std::optional<agentloop::Embedder> embedder_for(const harness::Harness& harness,
                                                              const harness::Config& config,
                                                              std::string_view db,
                                                              std::string_view retriever_flag,
                                                              std::string& reason) {
    const harness::EmbeddingConfig* registered = config.find_embedding(db);
    const std::string pin = registered != nullptr ? registered->retriever : std::string{};
    if (retriever_flag == "lexical" || pin == "lexical") {
        return std::nullopt;
    }
    const std::string collection_backend =
        registered != nullptr ? registered->backend : std::string{};
    return agentloop::resolve_embedder(harness, config, collection_backend, reason);
}

}  // namespace

std::string knowledge_collection(const harness::Config& config, std::string_view db) {
    return db.empty() ? config.knowledge.collection() : std::string{db};
}

StoreDecision decide_store(const harness::Harness& harness, const harness::Config& config,
                           std::string_view db, std::string_view retriever_flag) {
    StoreDecision decision;
    decision.db = knowledge_collection(config, db);
    const harness::EmbeddingConfig* registered = config.find_embedding(decision.db);
    const std::string pin = registered != nullptr ? registered->retriever : std::string{};
    std::string reason;
    const std::optional<agentloop::Embedder> embedder =
        embedder_for(harness, config, decision.db, retriever_flag, reason);
    const agentloop::IngestRetrieval resolved =
        agentloop::resolve_ingest_retriever(retriever_flag, pin, facts_for(embedder));
    decision.retriever = resolved.retriever;
    decision.note = resolved.note;
    if (!resolved.error.empty()) {
        decision.warning = resolved.error + (reason.empty() ? "" : " (" + reason + ")");
    } else if (resolved.retriever == agentloop::Retriever::Vector && embedder.has_value()) {
        decision.embed_model = embedder->model;
    }
    return decision;
}

CaptureResult draft_capture(const harness::Harness& harness, const harness::Config& config,
                            const CaptureInputs& inputs, const knowledge::ClerkFn& clerk) {
    CaptureResult result;
    // Decided BEFORE the clerk runs, so the dry run reports exactly the
    // decision a real run makes, from the same config.
    result.decision = decide_store(harness, config, inputs.db, inputs.retriever_flag);
    knowledge::Draft draft;
    try {
        draft = knowledge::run_capture(clerk, inputs.raw, inputs.overrides);
    } catch (const harness::HarnessError& e) {
        result.error = std::string{"the clerk could not run: "} + e.what();
        result.backend_error = true;
        return result;
    }
    if (!draft.ok()) {
        result.error = draft.error;
        // A validator's refusal after two model turns is the model's failure;
        // an override that does not validate is the user's. The clerk's
        // message starts with its own words either way.
        result.backend_error = draft.error.starts_with("the clerk");
        return result;
    }
    result.record = std::move(draft.record);
    return result;
}

CaptureResult store_record(const harness::Harness& harness, const harness::Config& config,
                           const std::filesystem::path& config_path, knowledge::Record record,
                           std::string_view raw, const StoreDecision& decision) {
    CaptureResult result;
    result.decision = decision;
    if (!decision.warning.empty()) {
        // What the dry run only warned about, a real run refuses on.
        result.error = decision.warning;
        result.record = std::move(record);
        return result;
    }
    // What only persistence mints, minted here when the caller did not: a
    // review UI's finished draft has neither; a re-stored export has both.
    const auto now = std::chrono::system_clock::now();
    if (record.id.empty()) {
        record.id = knowledge::new_id(now);
    }
    if (record.timestamp.empty()) {
        record.timestamp = knowledge::timestamp_for(now);
    }

    std::vector<float> vector;
    if (decision.retriever == agentloop::Retriever::Vector) {
        std::string reason;
        const std::optional<agentloop::Embedder> embedder =
            embedder_for(harness, config, decision.db, "vector", reason);
        if (!embedder.has_value()) {
            result.error = "vector indexing was decided but no embedding backend resolves" +
                           (reason.empty() ? std::string{} : " (" + reason + ")");
            result.record = std::move(record);
            return result;
        }
        try {
            const std::vector<std::vector<float>> vectors =
                embedder->embed({knowledge::index_text(record)}, {});
            if (vectors.empty() || vectors.front().empty()) {
                result.error = "the embedder returned no vector";
                result.backend_error = true;
                result.record = std::move(record);
                return result;
            }
            vector = vectors.front();
        } catch (const harness::HarnessError& e) {
            result.error = std::string{"embedding failed: "} + e.what();
            result.backend_error = true;
            result.record = std::move(record);
            return result;
        }
    }

    try {
        knowledge::Store store{collection_path(decision.db), harness::knowledge_raw_dir()};
        store.put(record, vector, raw);
        const embedstore::Store::Stats stats = store.chunks().stats();
        if (decision.retriever == agentloop::Retriever::Vector && stats.dimension > 0) {
            // Record the space the vectors live in, exactly as `embed ingest`
            // does, so a later query under another model falls to lexical.
            store.chunks().set_embedding_model(decision.embed_model, stats.dimension);
        } else if (stats.dimension == 0) {
            store.chunks().clear_embedding_model();
        }
        if (!record.supersedes.empty()) {
            if (!store.supersede(record.supersedes).has_value()) {
                result.notes.push_back("could not mark '" + record.supersedes +
                                       "' superseded: no such record in '" + decision.db + "'");
            }
        }
    } catch (const std::exception& e) {
        result.error = std::string{"could not store the record: "} + e.what();
        result.record = std::move(record);
        return result;
    }
    result.record = std::move(record);
    result.stored = true;

    // Register a collection the config has not met, through the one path that
    // writes a config file -- the same edit `embed ingest` makes. Reported and
    // never fatal: the record is already on disk.
    if (config.find_embedding(decision.db) == nullptr && !config_path.empty()) {
        harness::EmbeddingConfig entry;
        entry.description = "captured decision records (apogee knowledge)";
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::append_embedding(content, decision.db, entry, false);
            });
            result.registered = true;
        } catch (const std::exception& e) {
            result.notes.push_back("could not register '" + decision.db + "' in " +
                                   config_path.string() + " -- " + e.what());
        }
    }
    return result;
}

CaptureResult capture_and_store(const harness::Harness& harness, const harness::Config& config,
                                const std::filesystem::path& config_path,
                                const CaptureInputs& inputs, const knowledge::ClerkFn& clerk) {
    // The store decision BEFORE the clerk: a refusal the resolver can make
    // up front -- an explicit vector ask with no embedder -- must not cost a
    // model call first. A dry run reports the same refusal as a warning.
    const StoreDecision decision = decide_store(harness, config, inputs.db, inputs.retriever_flag);
    if (!decision.warning.empty()) {
        CaptureResult refused;
        refused.decision = decision;
        refused.error = decision.warning;
        return refused;
    }
    CaptureResult drafted = draft_capture(harness, config, inputs, clerk);
    if (!drafted.ok()) {
        return drafted;
    }
    return store_record(harness, config, config_path, std::move(drafted.record), inputs.raw,
                        drafted.decision);
}

std::string preview_text(std::string_view text, std::size_t width) {
    std::string out;
    std::size_t codepoints = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        }
        if (codepoints >= width) {
            out += "…";
            return out;
        }
        for (std::size_t k = 0; k < length && i + k < text.size(); ++k) {
            const char c = text[i + k];
            out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
        }
        i += length;
        ++codepoints;
    }
    return out;
}

}  // namespace apogee::commands
