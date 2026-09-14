#include "httpserver/admin_knowledge.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

#include "agentloop/retriever.h"
#include "commands/knowledge_core.h"
#include "harness/config.h"
#include "httpserver/handler.h"
#include "knowledge/clerk.h"
#include "knowledge/record.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kConfigError = "config_error";
constexpr std::string_view kBackendError = "backend_error";
constexpr int kNotImplemented = 501;
constexpr int kBadGateway = 502;

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

[[nodiscard]] HttpResponse failure(const commands::CaptureResult& result) {
    if (result.backend_error) {
        return error_response(kBadGateway, result.error, kBackendError);
    }
    return error_response(400, result.error);
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
    if (plane.options().served.empty()) {
        return error_response(kNotImplemented,
                              "this server has no generation backend to run the clerk on",
                              kBackendUnavailable);
    }
    std::string backend;
    try {
        backend = plane.resolve_served(model);
    } catch (const HttpError& e) {
        return to_response(e);
    }

    std::optional<harness::Config> config;
    try {
        config = harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        return error_response(500, std::string{"the config cannot be read: "} + e.what(),
                              kConfigError);
    }

    const commands::CaptureResult result =
        commands::capture_and_store(plane.harness(), *config, context.config_path, inputs,
                                    knowledge::make_structured_clerk(plane.harness(), backend));
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

    std::optional<harness::Config> config;
    try {
        config = harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        return error_response(500, std::string{"the config cannot be read: "} + e.what(),
                              kConfigError);
    }
    const commands::StoreDecision decision =
        commands::decide_store(plane.harness(), *config, db, retriever);
    const commands::CaptureResult result = commands::store_record(
        plane.harness(), *config, context.config_path, std::move(record), raw, decision);
    if (!result.ok()) {
        return failure(result);
    }
    return json_response(201, envelope(result));
}

}  // namespace apogee::httpserver
