#include "httpserver/admin_config.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "harness/config_edit.h"
#include "harness/roles.h"
#include "tools/toolsets.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kConfigError = "config_error";
constexpr std::string_view kConflict = "conflict";
constexpr std::string_view kForbidden = "forbidden";

/// The config as it is on disk right now, or the response that says why not.
struct Loaded {
    std::optional<harness::Config> config;
    HttpResponse failure;
};

Loaded load_now(const AdminConfigContext& context) {
    Loaded loaded;
    try {
        loaded.config = harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        loaded.failure = error_response(
            500, std::string{"the config could not be read: "} + e.what(), kConfigError);
    }
    return loaded;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += out.empty() ? "" : ", ";
        out += name;
    }
    return out;
}

nlohmann::json role_view(const harness::Config& config, harness::ModelRole role) {
    const harness::Resolution resolution =
        harness::resolve_backend(config, harness::RoleRequest{.role = role});
    std::string_view from;
    switch (resolution.from) {
        case harness::ResolvedFrom::Nothing:
            from = "nothing";
            break;
        case harness::ResolvedFrom::Override:
            from = "override";
            break;
        case harness::ResolvedFrom::EntryBackend:
            from = "entry";
            break;
        case harness::ResolvedFrom::RolePointer:
            from = "role_pointer";
            break;
        case harness::ResolvedFrom::Default:
            from = "default";
            break;
    }
    return nlohmann::json{{"backend", resolution.key}, {"from", std::string{from}}};
}

bool drifted(const AdminConfigContext& context, const harness::Config& now) {
    return context.startup != nullptr && config_drifted(*context.startup, now);
}

std::optional<std::string> optional_string(const nlohmann::json& in, const char* key,
                                           std::string& error) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        error = std::string{key} + " must be a string";
        return std::nullopt;
    }
    return it->get<std::string>();
}

}  // namespace

nlohmann::json backend_view(std::string_view name, const harness::BackendConfig& backend) {
    // Deliberately NOT a to_json over BackendConfig: that would carry api_key
    // the day someone wrote one. The view names every field but the secret.
    nlohmann::json out{{"name", std::string{name}},
                       {"type", std::string{harness::to_string(backend.type)}},
                       {"api_key_set", !backend.api_key.empty()}};
    if (!backend.model.empty()) {
        out["model"] = backend.model;
    }
    if (!backend.model_path.empty()) {
        out["model_path"] = backend.model_path;
    }
    if (!backend.embedding_model.empty()) {
        out["embedding_model"] = backend.embedding_model;
    }
    if (!backend.mmproj_path.empty()) {
        out["mmproj_path"] = backend.mmproj_path;
    }
    if (!backend.system_prompt.empty()) {
        out["system_prompt"] = backend.system_prompt;
    }
    if (backend.context_size.has_value()) {
        out["context_size"] = *backend.context_size;
    }
    if (backend.max_tokens.has_value()) {
        out["max_tokens"] = *backend.max_tokens;
    }
    if (backend.temperature.has_value()) {
        out["temperature"] = *backend.temperature;
    }
    if (!backend.binary.empty()) {
        out["binary"] = backend.binary;
    }
    if (!backend.host.empty()) {
        out["host"] = backend.host;
    }
    if (!backend.mode.empty()) {
        out["mode"] = backend.mode;
    }
    if (backend.idle_unload_seconds.has_value()) {
        out["idle_unload_seconds"] = *backend.idle_unload_seconds;
    }
    return out;
}

bool config_drifted(const harness::Config& startup, const harness::Config& now) {
    if (startup.backend_names() != now.backend_names()) {
        return true;
    }
    return !(startup.models == now.models);
}

bool is_literal_api_key(std::string_view api_key) noexcept {
    if (api_key.empty()) {
        return false;
    }
    return !(api_key.starts_with("${") && api_key.ends_with("}"));
}

HttpResponse admin_list_backends(const AdminConfigContext& context) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    nlohmann::json data = nlohmann::json::array();
    for (const auto& [name, backend] : loaded.config->backends) {
        data.push_back(backend_view(name, backend));
    }
    return json_response(
        200,
        nlohmann::json{
            {"object", "list"},
            {"data", std::move(data)},
            {"roles",
             {{"default", role_view(*loaded.config, harness::ModelRole::Chat)},
              {"default_embedding", role_view(*loaded.config, harness::ModelRole::Embedding)},
              {"default_extraction", role_view(*loaded.config, harness::ModelRole::Extraction)}}},
            {"restart_required", drifted(context, *loaded.config)}});
}

HttpResponse admin_create_backend(const AdminConfigContext& context, const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    const std::string name = optional_string(body, "name", error).value_or("");
    const std::string type_name = optional_string(body, "type", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (name.empty()) {
        return error_response(400, "name is required");
    }
    const std::optional<harness::BackendType> type = harness::backend_type_from_string(type_name);
    if (!type.has_value()) {
        std::string accepted;
        for (const std::string_view candidate : harness::backend_type_names()) {
            accepted += accepted.empty() ? "" : ", ";
            accepted += candidate;
        }
        return error_response(
            400, "type: unknown value '" + type_name + "' (accepted: " + accepted + ")");
    }

    harness::BackendConfig backend;
    backend.type = *type;
    backend.api_key = optional_string(body, "api_key", error).value_or("");
    backend.model = optional_string(body, "model", error).value_or("");
    backend.model_path = optional_string(body, "model_path", error).value_or("");
    backend.embedding_model = optional_string(body, "embedding_model", error).value_or("");
    backend.system_prompt = optional_string(body, "system_prompt", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    for (const char* key : {"context_size", "max_tokens"}) {
        if (const auto it = body.find(key); it != body.end() && !it->is_null()) {
            if (!it->is_number_integer()) {
                return error_response(400, std::string{key} + " must be an integer");
            }
            if (std::string_view{key} == "context_size") {
                backend.context_size = it->get<std::int64_t>();
            } else {
                backend.max_tokens = it->get<std::int64_t>();
            }
        }
    }
    if (const auto it = body.find("temperature"); it != body.end() && !it->is_null()) {
        if (!it->is_number()) {
            return error_response(400, "temperature must be a number");
        }
        backend.temperature = it->get<double>();
    }
    bool force = false;
    if (const auto it = body.find("force"); it != body.end() && !it->is_null()) {
        if (!it->is_boolean()) {
            return error_response(400, "force must be true or false");
        }
        force = it->get<bool>();
    }

    // A literal key is a secret arriving over the network. The socket's own
    // peer address decides, never a header a client could set.
    if (is_literal_api_key(backend.api_key) && !is_loopback_host(request.remote_address)) {
        return error_response(403,
                              "a literal api_key is accepted from loopback peers only -- pass a "
                              "${ENV_VAR} reference, or add the key on the host",
                              kForbidden);
    }

    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::append_backend(content, name, backend, force);
        });
    } catch (const harness::ConfigEditError& e) {
        const std::string what = e.what();
        const bool collision = what.find("already exists") != std::string::npos;
        return error_response(collision ? 409 : 400, what,
                              collision ? kConflict : kInvalidRequestError);
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }

    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    nlohmann::json out = backend_view(name, backend);
    out["restart_required"] = drifted(context, *loaded.config);
    return json_response(201, out);
}

HttpResponse admin_get_backend(const AdminConfigContext& context, std::string_view name) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    for (const auto& [key, backend] : loaded.config->backends) {
        if (loaded.config->find_backend(name) == &backend) {
            return json_response(200, backend_view(key, backend));
        }
    }
    return error_response(404, "no backend named '" + std::string{name} + "'", kNotFoundError);
}

HttpResponse admin_delete_backend(const AdminConfigContext& context, std::string_view name) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    if (loaded.config->find_backend(name) == nullptr) {
        return error_response(404, "no backend named '" + std::string{name} + "'", kNotFoundError);
    }
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::delete_backend(content, name);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    const Loaded after = load_now(context);
    if (!after.config.has_value()) {
        return after.failure;
    }
    return json_response(200,
                         nlohmann::json{{"deleted", std::string{name}},
                                        {"restart_required", drifted(context, *after.config)}});
}

HttpResponse admin_format_config(const AdminConfigContext& context) {
    try {
        harness::edit_config_file(context.config_path, [](std::string_view content) {
            return harness::format_config(content);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    const Loaded after = load_now(context);
    if (!after.config.has_value()) {
        return after.failure;
    }
    return json_response(
        200,
        nlohmann::json{{"formatted", true}, {"restart_required", drifted(context, *after.config)}});
}

namespace {

nlohmann::json permissions_view(const harness::Config& config) {
    nlohmann::json data = nlohmann::json::array();
    std::vector<std::string> listed;
    for (const std::string_view tool : tools::destructive_tool_names()) {
        data.push_back(
            {{"tool", std::string{tool}},
             {"level", std::string{harness::to_string(config.permissions.level(tool))}}});
        listed.emplace_back(tool);
    }
    for (const auto& [tool, level] : config.permissions.levels) {
        if (std::find(listed.begin(), listed.end(), tool) == listed.end()) {
            data.push_back({{"tool", tool}, {"level", std::string{harness::to_string(level)}}});
        }
    }
    return data;
}

}  // namespace

HttpResponse admin_list_permissions(const AdminConfigContext& context) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    return json_response(
        200, nlohmann::json{{"object", "list"}, {"data", permissions_view(*loaded.config)}});
}

HttpResponse admin_put_permission(const AdminConfigContext& context, std::string_view tool,
                                  const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    const std::string level = optional_string(body, "level", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (!harness::permission_level_from_string(level).has_value()) {
        return error_response(400, "level is required: ask, allow, or deny");
    }
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::set_permission(content, tool, level);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    const Loaded after = load_now(context);
    if (!after.config.has_value()) {
        return after.failure;
    }
    // A served run reads its levels once at startup, so a change here is
    // honoured by the CLI now and by this server after a restart.
    const bool restart = context.startup != nullptr && context.startup->permissions.level(tool) !=
                                                           after.config->permissions.level(tool);
    return json_response(
        200, nlohmann::json{
                 {"tool", std::string{tool}},
                 {"level", std::string{harness::to_string(after.config->permissions.level(tool))}},
                 {"restart_required", restart}});
}

HttpResponse admin_set_role(const AdminConfigContext& context, std::string_view field,
                            const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    const std::string name = optional_string(body, "name", error).value_or("");
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (name.empty()) {
        return error_response(400, "name is required");
    }
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    // The same refusal the CLI makes: a pointer at nothing loads fine and then
    // fails every request that follows.
    if (loaded.config->find_backend(name) == nullptr) {
        const std::vector<std::string> known = loaded.config->backend_names();
        return error_response(400,
                              "no backend named '" + name + "' in this config" +
                                  (known.empty() ? " (it has no backends yet)"
                                                 : " (known backends: " + join_names(known) + ")"));
    }
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::set_models_role(content, field, name);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    const Loaded after = load_now(context);
    if (!after.config.has_value()) {
        return after.failure;
    }
    return json_response(200,
                         nlohmann::json{{"field", std::string{field}},
                                        {"name", name},
                                        {"restart_required", drifted(context, *after.config)}});
}

}  // namespace apogee::httpserver
