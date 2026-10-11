#include "httpserver/admin_auth_routes.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <stdexcept>
#include <string>

#include "contracts/config.h"
#include "operations/credential_views.h"
#include "secrets/store.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kForbidden = "forbidden";
constexpr std::string_view kStoreError = "store_error";

std::string accepted_slots() {
    std::string out;
    for (const std::string_view type : harness::backend_type_names()) {
        if (const std::optional<harness::BackendType> parsed =
                harness::backend_type_from_string(type);
            parsed.has_value() && secrets::takes_api_key(*parsed)) {
            out += out.empty() ? "" : ", ";
            out += type;
        }
    }
    return out;
}

/// The slot for `provider`, or the refusal: a vendor-CLI type is refused with
/// the principle named, anything else with the accepted names.
std::optional<std::string> require_slot(std::string_view provider, HttpResponse& refusal) {
    if (const std::optional<harness::BackendType> type = secrets::slot_type(provider)) {
        return secrets::slot_name(*type);
    }
    if (harness::backend_type_from_string(provider).has_value()) {
        refusal = error_response(
            400, "'" + std::string{provider} +
                     "' takes no stored key: a vendor CLI authenticates itself and Apogee never "
                     "stores, reads, or proxies its credentials; a local model needs none. Keys "
                     "are kept for " +
                     accepted_slots());
        return std::nullopt;
    }
    refusal = error_response(400, "unknown provider '" + std::string{provider} +
                                      "' (keys are kept for " + accepted_slots() + ")");
    return std::nullopt;
}

nlohmann::json metadata_json(const secrets::CredentialMetadata& entry) {
    // CredentialMetadata has no key field; this cannot serialize one.
    return nlohmann::json{{"provider", entry.provider}, {"stored_at", entry.stored_at}};
}

}  // namespace

HttpResponse admin_list_credentials(const AdminAuthContext& context) {
    // The CLI's `auth list --output-format json`, byte for byte (37g).
    return json_response(
        200, operations::credentials_document(
                 context.config_path,
                 context.env != nullptr ? *context.env : secrets::EnvSnapshot::process()));
}

HttpResponse admin_put_credential(const AdminAuthContext& context, std::string_view provider,
                                  const HttpRequest& request) {
    // The socket's own peer address decides, never a header a client could
    // set. Checked FIRST: a remote caller learns nothing about the body's
    // shape, and the key it sent is never looked at.
    if (!is_loopback_host(request.remote_address)) {
        return error_response(403,
                              "this route accepts a secret and is served to loopback peers only "
                              "-- connect from the same machine, or use 'apogee auth add' on "
                              "the host",
                              kForbidden);
    }
    HttpResponse refusal;
    const std::optional<std::string> slot = require_slot(provider, refusal);
    if (!slot.has_value()) {
        return refusal;
    }
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    const auto key = body.find("key");
    if (key == body.end() || !key->is_string() || key->get<std::string>().empty()) {
        return error_response(400, "key is required, in the body");
    }
    secrets::CredentialStore store{secrets::credentials_path(context.config_path)};
    try {
        store.put(*slot, key->get<std::string>());
    } catch (const std::runtime_error& e) {
        return error_response(500, e.what(), kStoreError);
    }
    for (const secrets::CredentialMetadata& entry : store.list()) {
        if (entry.provider == *slot) {
            return json_response(200, metadata_json(entry));
        }
    }
    return json_response(200, nlohmann::json{{"provider", *slot}});
}

HttpResponse admin_clear_credential(const AdminAuthContext& context, std::string_view provider) {
    HttpResponse refusal;
    const std::optional<std::string> slot = require_slot(provider, refusal);
    if (!slot.has_value()) {
        return refusal;
    }
    secrets::CredentialStore store{secrets::credentials_path(context.config_path)};
    try {
        if (!store.clear(*slot)) {
            return error_response(404, "no stored key for " + *slot, kNotFoundError);
        }
    } catch (const std::runtime_error& e) {
        return error_response(500, e.what(), kStoreError);
    }
    return json_response(200, nlohmann::json{{"cleared", *slot}});
}

}  // namespace apogee::httpserver
