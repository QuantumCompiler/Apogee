#include "httpserver/admin_agents.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "harness/assets.h"
#include "harness/config_edit.h"
#include "harness/paths.h"
#include "scaffold/agent.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kConfigError = "config_error";
constexpr std::string_view kConflict = "conflict";

struct Loaded {
    std::optional<harness::Config> config;
    HttpResponse failure;
};

Loaded load_now(const AdminConfigContext& context) {
    Loaded loaded;
    try {
        loaded.config = harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        loaded.failure = error_response(500, std::string{"the config cannot be read: "} + e.what(),
                                        kConfigError);
    }
    return loaded;
}

std::optional<std::string> string_field(const nlohmann::json& body, std::string_view key,
                                        std::string& error) {
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

bool bool_field(const nlohmann::json& body, std::string_view key, std::string& error) {
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

std::vector<std::string> list_field(const nlohmann::json& body, std::string_view key,
                                    std::string& error) {
    std::vector<std::string> out;
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        return out;
    }
    if (!it->is_array()) {
        error = std::string{key} + " must be a list of strings";
        return out;
    }
    for (const nlohmann::json& item : *it) {
        if (!item.is_string()) {
            error = std::string{key} + " must be a list of strings";
            return out;
        }
        out.push_back(item.get<std::string>());
    }
    return out;
}

/// The shared body -> spec translation for POST and PUT.
HttpResponse scaffold_agent(const AdminConfigContext& context, const HttpRequest& request,
                            std::string_view path_name, bool force) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    scaffold::AgentSpec spec;
    spec.name =
        path_name.empty() ? string_field(body, "name", error).value_or("") : std::string{path_name};
    spec.description = string_field(body, "description", error).value_or("");
    spec.model = string_field(body, "model", error).value_or("");
    spec.tools = string_field(body, "tools", error).value_or("");
    spec.no_schema = bool_field(body, "no_schema", error);
    spec.prompt_body = string_field(body, "prompt_body", error).value_or("");
    spec.schema_body = string_field(body, "schema_body", error).value_or("");
    spec.force = force || bool_field(body, "force", error);
    spec.output_format = string_field(body, "output_format", error).value_or("");
    spec.collection = string_field(body, "collection", error).value_or("");
    spec.questions = bool_field(body, "questions", error);
    spec.save_dir = string_field(body, "save_dir", error).value_or("");
    spec.save_filename = string_field(body, "save_filename", error).value_or("");
    spec.save_subdir = string_field(body, "save_subdir", error).value_or("");
    spec.mcp = list_field(body, "mcp", error);
    if (!error.empty()) {
        return error_response(400, error);
    }
    if (spec.name.empty()) {
        return error_response(400, "name is required");
    }
    scaffold::AgentResult result;
    try {
        result = scaffold::create_agent(context.config_path, spec);
    } catch (const std::exception& e) {
        const std::string what = e.what();
        const bool collision = what.find("already exists") != std::string::npos ||
                               what.find("collides") != std::string::npos;
        return error_response(collision ? 409 : 400, what,
                              collision ? kConflict : std::string_view{});
    }
    const Loaded after = load_now(context);
    if (!after.config.has_value()) {
        return after.failure;
    }
    nlohmann::json view;
    for (const harness::NamedAgent& agent : harness::all_agents(*after.config)) {
        if (agent.name == result.name) {
            view = agent_view(agent);
        }
    }
    if (view.is_null()) {
        view = nlohmann::json{{"name", result.name}};
    }
    view["prompt_path"] = result.prompt_path.string();
    view["schema_path"] = result.schema_path.string();
    return json_response(force ? 200 : 201, view);
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

nlohmann::json agent_view(const harness::NamedAgent& agent) {
    return nlohmann::json{{"name", agent.name},
                          {"description", agent.config.description},
                          {"model", agent.config.model},
                          {"prompts", agent.config.prompts},
                          {"schemas", agent.config.schemas},
                          {"output_format", std::string{to_string(agent.config.output_format)}},
                          {"tools", std::string{to_string(agent.config.tools)}},
                          {"mcp", agent.config.mcp},
                          {"questions", agent.config.questions},
                          {"collection", agent.config.collection},
                          {"save_dir", agent.config.save_dir},
                          {"save_filename", agent.config.save_filename},
                          {"save_subdir", agent.config.save_subdir},
                          {"bundled", agent.bundled},
                          {"overrides_bundled", agent.overrides_bundled}};
}

HttpResponse admin_list_agents(const AdminConfigContext& context) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    nlohmann::json data = nlohmann::json::array();
    for (const harness::NamedAgent& agent : harness::all_agents(*loaded.config)) {
        data.push_back(agent_view(agent));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse admin_create_agent(const AdminConfigContext& context, const HttpRequest& request) {
    return scaffold_agent(context, request, {}, false);
}

HttpResponse admin_get_agent(const AdminConfigContext& context, std::string_view name) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    for (const harness::NamedAgent& agent : harness::all_agents(*loaded.config)) {
        if (agent.name != name) {
            continue;
        }
        nlohmann::json view = agent_view(agent);
        const std::filesystem::path home = harness::home_for_config(context.config_path);
        const harness::BundledAgent* bundled = harness::find_bundled_agent(name);
        const auto bodies = [&](const std::vector<std::string>& paths, bool prompts) {
            nlohmann::json out = nlohmann::json::array();
            for (const std::string& relative : paths) {
                const std::filesystem::path path = harness::resolve_agent_path(home, relative);
                std::optional<std::string> text = read_text(path);
                if (!text.has_value() && bundled != nullptr) {
                    // Not seeded: the compiled-in text is what would run.
                    text = std::string{prompts ? bundled->prompt : bundled->schema};
                }
                out.push_back({{"path", path.string()},
                               {"body", text.value_or("")},
                               {"present", read_text(path).has_value()}});
            }
            return out;
        };
        view["prompt_bodies"] = bodies(agent.config.prompts, true);
        view["schema_bodies"] = bodies(agent.config.schemas, false);
        return json_response(200, view);
    }
    return error_response(404, "no agent named '" + std::string{name} + "'", kNotFoundError);
}

HttpResponse admin_put_agent(const AdminConfigContext& context, std::string_view name,
                             const HttpRequest& request) {
    return scaffold_agent(context, request, name, true);
}

HttpResponse admin_delete_agent(const AdminConfigContext& context, std::string_view name,
                                const HttpRequest& request) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    const harness::AgentConfig* entry = loaded.config->find_agent(name);
    if (entry == nullptr) {
        const bool bundled = harness::find_bundled_agent(name) != nullptr;
        return error_response(404,
                              bundled ? "'" + std::string{name} +
                                            "' is a bundled agent with no config entry; "
                                            "override it before deleting it"
                                      : "no agent named '" + std::string{name} + "'",
                              kNotFoundError);
    }
    const std::vector<std::filesystem::path> files =
        scaffold::agent_files(context.config_path, *entry);
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::delete_agent(content, name);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    nlohmann::json removed = nlohmann::json::array();
    if (request.query_value("purge") == "true") {
        for (const std::filesystem::path& file : files) {
            std::error_code code;
            if (std::filesystem::remove(file, code)) {
                removed.push_back(file.string());
            }
        }
    }
    return json_response(
        200, nlohmann::json{{"deleted", std::string{name}}, {"files_removed", std::move(removed)}});
}

}  // namespace apogee::httpserver
