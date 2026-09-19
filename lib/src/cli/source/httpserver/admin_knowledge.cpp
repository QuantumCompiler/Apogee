#include "httpserver/admin_knowledge.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "agentloop/embed_func.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "commands/embed.h"
#include "commands/knowledge_core.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "httpserver/handler.h"
#include "knowledge/clerk.h"
#include "knowledge/export.h"
#include "knowledge/query.h"
#include "knowledge/record.h"
#include "knowledge/refine.h"
#include "knowledge/store.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kConfigError = "config_error";
constexpr std::string_view kBackendError = "backend_error";
constexpr int kNotImplemented = 501;
constexpr int kBadGateway = 502;
/// The pool a query over HTTP returns: a GUI page, and the judge's widened
/// set when it reranks.
constexpr int kHttpQueryTopK = 20;

[[nodiscard]] std::optional<std::string> string_field(const nlohmann::json& body,
                                                      std::string_view key, std::string& error) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        error = std::string{key} + " must be a string";
        return std::nullopt;
    }
    return it->get<std::string>();
}

[[nodiscard]] bool bool_field(const nlohmann::json& body, std::string_view key,
                              std::string& error) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        return false;
    }
    if (!it->is_boolean()) {
        error = std::string{key} + " must be true or false";
        return false;
    }
    return it->get<bool>();
}

[[nodiscard]] bool plain_name(std::string_view name) {
    return name.find("..") == std::string_view::npos && name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos;
}

[[nodiscard]] nlohmann::json envelope(const commands::CaptureResult& result) {
    nlohmann::json out{{"record", result.record},
                       {"db", result.decision.db},
                       {"retriever", std::string{agentloop::to_string(result.decision.retriever)}},
                       {"registered", result.registered}};
    if (!result.decision.note.empty()) {
        out["note"] = result.decision.note;
    }
    if (!result.notes.empty()) {
        out["notes"] = result.notes;
    }
    return out;
}

[[nodiscard]] nlohmann::json draft_envelope(const commands::CaptureResult& result) {
    nlohmann::json out{{"draft", true},
                       {"record", result.record},
                       {"db", result.decision.db},
                       {"retriever", std::string{agentloop::to_string(result.decision.retriever)}}};
    if (!result.decision.warning.empty()) {
        out["warning"] = result.decision.warning;
    }
    if (!result.decision.note.empty()) {
        out["note"] = result.decision.note;
    }
    return out;
}

[[nodiscard]] HttpResponse failure(const commands::CaptureResult& result) {
    if (result.backend_error) {
        return error_response(kBadGateway, result.error, kBackendError);
    }
    return error_response(400, result.error);
}

struct Loaded {
    std::optional<harness::Config> config;
    HttpResponse failure;
};

[[nodiscard]] Loaded load_now(const AdminConfigContext& context) {
    Loaded loaded;
    try {
        loaded.config = harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        loaded.failure = error_response(500, std::string{"the config cannot be read: "} + e.what(),
                                        kConfigError);
    }
    return loaded;
}

/// A collection that must already exist: a read or an edit never creates
/// an empty database out of a typo.
[[nodiscard]] std::optional<knowledge::Store> open_existing(const std::string& db) {
    const std::filesystem::path path = commands::collection_path(db);
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return std::nullopt;
    }
    return knowledge::Store{path, harness::knowledge_raw_dir()};
}

[[nodiscard]] HttpResponse no_collection(const std::string& db) {
    return error_response(404, "no knowledge collection named '" + db + "'", kNotFoundError);
}

[[nodiscard]] HttpResponse no_record(const std::string& id, const std::string& db) {
    return error_response(404, "no record '" + id + "' in '" + db + "'", kNotFoundError);
}

/// The clerk's backend for a generation route: served backends only, or the
/// honest 501 when the server serves none.
[[nodiscard]] std::optional<HttpResponse> refuse_clerk(Handler& plane, std::string_view model,
                                                       std::string& backend) {
    if (plane.options().served.empty()) {
        return error_response(kNotImplemented,
                              "this server has no generation backend to run the clerk on",
                              kBackendUnavailable);
    }
    try {
        backend = plane.resolve_served(model);
    } catch (const HttpError& e) {
        return to_response(e);
    }
    return std::nullopt;
}

}  // namespace

HttpResponse admin_capture_knowledge(const AdminConfigContext& context, Handler& plane,
                                     const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    commands::CaptureInputs inputs;
    inputs.raw = string_field(body, "raw", error).value_or("");
    const std::string model = string_field(body, "model", error).value_or("");
    inputs.db = string_field(body, "db", error).value_or("");
    inputs.overrides.status = string_field(body, "status", error).value_or("");
    inputs.overrides.discipline = string_field(body, "discipline", error).value_or("");
    inputs.overrides.source = string_field(body, "source", error).value_or("");
    inputs.overrides.link = string_field(body, "link", error).value_or("");
    inputs.overrides.supersedes = string_field(body, "supersedes", error).value_or("");
    inputs.retriever_flag = string_field(body, "retriever", error).value_or("");
    const bool draft = bool_field(body, "draft", error);
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (inputs.raw.empty()) {
        return error_response(400, "raw is required: the conversation to distil");
    }
    if (!plain_name(inputs.db)) {
        return error_response(400, "db is not a plain collection name");
    }
    if (!inputs.overrides.status.empty()) {
        inputs.overrides.status = knowledge::normalize_status(inputs.overrides.status);
        if (!knowledge::is_valid_status(inputs.overrides.status)) {
            return error_response(400, "status must be shipped, rejected, or superseded");
        }
    }
    if (!agentloop::valid_retriever(inputs.retriever_flag)) {
        return error_response(
            400, agentloop::retriever_values_message("retriever", inputs.retriever_flag));
    }
    if (inputs.retriever_flag == "auto") {
        inputs.retriever_flag.clear();
    }

    // The clerk's backend: served backends only, the way a chat request's
    // `model` resolves -- and an honest 501 on a server with nothing to
    // generate with, rather than a 400 that blames the request.
    std::string backend;
    if (std::optional<HttpResponse> refused = refuse_clerk(plane, model, backend);
        refused.has_value()) {
        return *refused;
    }
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const knowledge::ClerkFn clerk = knowledge::make_structured_clerk(plane.harness(), backend);

    if (draft) {
        // The clerk, the overrides, the normalisation, the validation -- and
        // then nothing: the HTTP twin of `capture --dry-run`. The draft lives
        // with the client from here.
        const commands::CaptureResult result =
            commands::draft_capture(plane.harness(), *loaded.config, inputs, clerk);
        if (!result.ok()) {
            return failure(result);
        }
        return json_response(200, draft_envelope(result));
    }
    const commands::CaptureResult result = commands::capture_and_store(
        plane.harness(), *loaded.config, context.config_path, inputs, clerk);
    if (!result.ok()) {
        return failure(result);
    }
    return json_response(201, envelope(result));
}

HttpResponse admin_create_knowledge(const AdminConfigContext& context, Handler& plane,
                                    const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    const std::string raw = string_field(body, "raw", error).value_or("");
    const std::string db = string_field(body, "db", error).value_or("");
    std::string retriever = string_field(body, "retriever", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (!plain_name(db)) {
        return error_response(400, "db is not a plain collection name");
    }
    if (!agentloop::valid_retriever(retriever)) {
        return error_response(400, agentloop::retriever_values_message("retriever", retriever));
    }
    if (retriever == "auto") {
        retriever.clear();
    }

    knowledge::Record record;
    try {
        record = body.get<knowledge::Record>();
    } catch (const nlohmann::json::exception& e) {
        return error_response(400, std::string{"the record could not be read: "} + e.what());
    }
    // A finished record from a review UI carries no id: the store mints one.
    // One that carries an id -- a re-store of an exported record -- keeps it.
    record.raw_ref.clear();
    knowledge::normalize(record);
    if (const std::string why = knowledge::validate(record); !why.empty()) {
        return error_response(400, why);
    }

    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const commands::StoreDecision decision =
        commands::decide_store(plane.harness(), *loaded.config, db, retriever);
    const commands::CaptureResult result = commands::store_record(
        plane.harness(), *loaded.config, context.config_path, std::move(record), raw, decision);
    if (!result.ok()) {
        return failure(result);
    }
    return json_response(201, envelope(result));
}

HttpResponse admin_refine_knowledge(const AdminConfigContext& context, Handler& plane,
                                    const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    // Input guards BEFORE any clerk call: a bad request costs no model turn.
    const auto record_it = body.find("record");
    if (record_it == body.end() || !record_it->is_object()) {
        return error_response(400, "record (the draft to refine) is required");
    }
    std::string error;
    const std::string instruction = string_field(body, "instruction", error).value_or("");
    const std::string raw = string_field(body, "raw", error).value_or("");
    const std::string model = string_field(body, "model", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (const std::string why = knowledge::validate_refine_instruction(instruction); !why.empty()) {
        return error_response(400, why);
    }
    knowledge::Record draft;
    try {
        draft = record_it->get<knowledge::Record>();
    } catch (const nlohmann::json::exception& e) {
        return error_response(400, std::string{"the record could not be read: "} + e.what());
    }

    std::string backend;
    if (std::optional<HttpResponse> refused = refuse_clerk(plane, model, backend);
        refused.has_value()) {
        return *refused;
    }
    const knowledge::Draft revised = knowledge::run_refine(
        knowledge::make_structured_clerk(plane.harness(), backend), draft, instruction, raw);
    if (!revised.ok()) {
        if (revised.error.starts_with("the clerk")) {
            return error_response(kBadGateway,
                                  "refinement produced no conforming record: " + revised.error,
                                  kBackendError);
        }
        return error_response(400, revised.error);
    }
    // Never stored, never archived: the loop is draft -> refine* -> POST.
    return json_response(200, nlohmann::json{{"draft", true}, {"record", revised.record}});
}

HttpResponse admin_list_knowledge(const AdminConfigContext& context, Handler& plane,
                                  const HttpRequest& request) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const std::string db_param = request.query_value("db");
    if (!plain_name(db_param)) {
        return error_response(400, "db is not a plain collection name");
    }
    const std::string db = commands::knowledge_collection(*loaded.config, db_param);
    const std::string status = request.query_value("status");
    const std::string discipline = request.query_value("discipline");
    const std::string question = request.query_value("q");
    const bool anonymize = request.query_value("anonymize") == "true";
    std::string retriever = request.query_value("retriever");
    if (!agentloop::valid_retriever(retriever)) {
        return error_response(400, agentloop::retriever_values_message("retriever", retriever));
    }
    if (retriever == "auto") {
        retriever.clear();
    }
    int limit = kHttpQueryTopK;
    if (request.has_query("limit")) {
        try {
            limit = std::stoi(request.query_value("limit"));
        } catch (const std::exception&) {
            return error_response(400, "limit must be a whole number");
        }
    }

    // A missing collection is an empty list, not an error -- and a read
    // never creates an empty database file.
    std::optional<knowledge::Store> store = open_existing(db);
    if (!store.has_value()) {
        return json_response(200,
                             nlohmann::json{{"object", "list"}, {"data", nlohmann::json::array()}});
    }

    if (question.empty()) {
        std::vector<knowledge::Record> records;
        for (knowledge::Record& record : store->list()) {
            if (!status.empty() && record.status != status) {
                continue;
            }
            if (!discipline.empty() && record.discipline != discipline) {
                continue;
            }
            records.push_back(std::move(record));
        }
        return json_response(
            200,
            nlohmann::json{{"object", "list"},
                           {"data", knowledge::export_records(std::move(records), anonymize)}});
    }

    // An explicit vector ask with nothing to run it: the honest 501, with
    // the lexical way out named -- decided before anything is searched.
    if (retriever == "vector") {
        const harness::EmbeddingConfig* entry = loaded.config->find_embedding(db);
        std::string reason;
        if (!agentloop::resolve_embedder(plane.harness(), *loaded.config,
                                         entry != nullptr ? entry->backend : std::string{}, reason)
                 .has_value()) {
            return error_response(kNotImplemented,
                                  "vector search requires an embedding backend (pass "
                                  "?retriever=lexical for text search)" +
                                      (reason.empty() ? std::string{} : " -- " + reason),
                                  kBackendUnavailable);
        }
    }
    knowledge::QueryOptions options;
    options.question = question;
    options.status = status;
    options.discipline = discipline;
    options.top_k = limit;
    options.retriever_flag = retriever;
    options.rerank_flag = request.query_value("rerank");
    if (!options.rerank_flag.empty() && options.rerank_flag != agentloop::kRerankOff &&
        loaded.config->find_backend(options.rerank_flag) == nullptr) {
        return error_response(400, "rerank: no backend named '" + options.rerank_flag + "'");
    }
    const knowledge::QueryResult result =
        knowledge::query(*store, plane.harness(), *loaded.config, db, options);
    if (!result.ok()) {
        if (result.backend_error) {
            return error_response(kBadGateway, result.error, kBackendError);
        }
        return error_response(400, result.error);
    }
    nlohmann::json data = nlohmann::json::array();
    for (const knowledge::ScoredRecord& scored : result.records) {
        const std::vector<knowledge::Record> shaped =
            knowledge::export_records({scored.record}, anonymize);
        data.push_back({{"record", shaped.front()}, {"score", scored.score}});
    }
    nlohmann::json out{{"object", "list"},
                       {"data", std::move(data)},
                       {"retriever", std::string{agentloop::to_string(result.retriever)}},
                       {"reranked", result.reranked}};
    if (!result.notes.empty()) {
        std::string note;
        for (const std::string& line : result.notes) {
            note += note.empty() ? "" : "; ";
            note += line;
        }
        out["note"] = note;
    }
    return json_response(200, out);
}

HttpResponse admin_get_knowledge(const AdminConfigContext& context, std::string_view id,
                                 const HttpRequest& request) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const std::string db_param = request.query_value("db");
    if (!plain_name(db_param)) {
        return error_response(400, "db is not a plain collection name");
    }
    const std::string db = commands::knowledge_collection(*loaded.config, db_param);
    const std::optional<knowledge::Store> store = open_existing(db);
    if (!store.has_value()) {
        return no_collection(db);
    }
    const std::optional<knowledge::Record> record = store->get(id);
    if (!record.has_value()) {
        return no_record(std::string{id}, db);
    }
    return json_response(200, *record);
}

HttpResponse admin_patch_knowledge(const AdminConfigContext& context, std::string_view id,
                                   const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    const std::optional<std::string> link = string_field(body, "link", error);
    const std::optional<std::string> status = string_field(body, "status", error);
    const std::string db_param = string_field(body, "db", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    // One edit, one intent, one audit line.
    if (link.has_value() == status.has_value()) {
        return error_response(400, "pass exactly one of link or status");
    }
    std::string canonical;
    if (status.has_value()) {
        canonical = knowledge::normalize_status(*status);
        if (!knowledge::is_valid_status(canonical)) {
            return error_response(400, "status must be shipped, rejected, or superseded");
        }
    }
    if (!plain_name(db_param)) {
        return error_response(400, "db is not a plain collection name");
    }
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const std::string db = commands::knowledge_collection(*loaded.config, db_param);
    std::optional<knowledge::Store> store = open_existing(db);
    if (!store.has_value()) {
        return no_collection(db);
    }
    const std::optional<knowledge::Record> record =
        link.has_value() ? store->set_link(id, *link) : store->set_status(id, canonical);
    if (!record.has_value()) {
        return no_record(std::string{id}, db);
    }
    return json_response(200, *record);
}

HttpResponse admin_delete_knowledge(const AdminConfigContext& context, std::string_view id,
                                    const HttpRequest& request) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const std::string db_param = request.query_value("db");
    if (!plain_name(db_param)) {
        return error_response(400, "db is not a plain collection name");
    }
    const std::string db = commands::knowledge_collection(*loaded.config, db_param);
    std::optional<knowledge::Store> store = open_existing(db);
    if (!store.has_value()) {
        return no_collection(db);
    }
    if (!store->remove(id)) {
        return no_record(std::string{id}, db);
    }
    return json_response(200, nlohmann::json{{"deleted", std::string{id}}});
}

HttpResponse admin_reindex_knowledge(const AdminConfigContext& context, Handler& plane,
                                     const HttpRequest& request) {
    // An empty body is valid: the default collection, every record.
    nlohmann::json body = nlohmann::json::object();
    if (!request.body.empty()) {
        body = nlohmann::json::parse(request.body, nullptr, false);
        if (body.is_discarded() || !body.is_object()) {
            return error_response(400, "the request body must be a JSON object");
        }
    }
    std::string error;
    const std::string db_param = string_field(body, "db", error).value_or("");
    const std::string id = string_field(body, "id", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (!plain_name(db_param)) {
        return error_response(400, "db is not a plain collection name");
    }
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const std::string db = commands::knowledge_collection(*loaded.config, db_param);
    std::optional<knowledge::Store> store = open_existing(db);
    if (!store.has_value()) {
        return no_collection(db);
    }
    const std::vector<knowledge::Record> records = store->list();
    if (records.empty()) {
        return json_response(200, nlohmann::json{{"reindexed", 0},
                                                 {"note",
                                                  "the collection holds no records -- "
                                                  "nothing to reindex"}});
    }
    if (!store->has_vectors()) {
        return json_response(
            200, nlohmann::json{{"reindexed", 0},
                                {"note",
                                 "the collection holds no embedded records -- nothing to "
                                 "reindex; its text index is maintained automatically"}});
    }
    const harness::EmbeddingConfig* entry = loaded.config->find_embedding(db);
    std::string reason;
    const std::optional<agentloop::Embedder> embedder = agentloop::resolve_embedder(
        plane.harness(), *loaded.config, entry != nullptr ? entry->backend : std::string{}, reason);
    if (!embedder.has_value()) {
        return error_response(kNotImplemented,
                              "reindex requires an embedding backend" +
                                  (reason.empty() ? std::string{} : " -- " + reason),
                              kBackendUnavailable);
    }
    std::string note;
    if (id.empty()) {
        if (const std::size_t lexical = store->vectorless_count(); lexical > 0) {
            note = std::to_string(lexical) + " of " + std::to_string(records.size()) +
                   " record(s) had no vectors (captured lexically) and were embedded by this "
                   "reindex; pass an id to reindex a single record";
        }
    }
    std::vector<std::string> ids;
    if (!id.empty()) {
        ids.push_back(id);
    }
    const knowledge::Store::ReindexOutcome outcome = store->reindex(
        [&embedder](std::string_view text) {
            const std::vector<std::vector<float>> vectors =
                embedder->embed({std::string{text}}, {});
            return vectors.empty() ? std::vector<float>{} : vectors.front();
        },
        ids);
    if (!outcome.error.empty()) {
        if (outcome.error.starts_with("no record")) {
            return no_record(id, db);
        }
        return error_response(kBadGateway, outcome.error, kBackendError);
    }
    const embedstore::Store::Stats stats = store->chunks().stats();
    if (stats.dimension > 0) {
        store->chunks().set_embedding_model(embedder->model, stats.dimension);
    }
    nlohmann::json out{{"reindexed", outcome.reindexed}};
    if (!note.empty()) {
        out["note"] = note;
    }
    return json_response(200, out);
}

}  // namespace apogee::httpserver
