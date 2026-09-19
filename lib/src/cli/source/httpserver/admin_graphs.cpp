#include "httpserver/admin_graphs.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "agentloop/graph_context.h"
#include "commands/graph.h"
#include "harness/config.h"
#include "harness/config_edit.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kConfigError = "config_error";
constexpr std::string_view kConflict = "conflict";

[[nodiscard]] std::optional<harness::Config> load_now(const AdminConfigContext& context,
                                                      HttpResponse& failure) {
    try {
        return harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        failure = error_response(500, std::string{"the config cannot be read: "} + e.what(),
                                 kConfigError);
        return std::nullopt;
    }
}

[[nodiscard]] nlohmann::json graph_json(std::string_view name,
                                        const harness::NamedGraphConfig& graph) {
    std::error_code code;
    nlohmann::json out{{"name", std::string{name}},
                       {"collections", graph.collections},
                       {"hops", graph.hops},
                       {"max_entities", graph.max_entities},
                       {"built", std::filesystem::exists(agentloop::graph_db_path(name), code)}};
    if (!graph.extract_backend.empty()) {
        out["extract_backend"] = graph.extract_backend;
    }
    return out;
}

/// The body -> entry translation for POST and PUT. `path_name` is the PUT
/// path's name (empty on POST, where the body names it).
struct Parsed {
    std::string name;
    harness::NamedGraphConfig graph;
    std::string error;
};

[[nodiscard]] Parsed parse_body(const HttpRequest& request, std::string_view path_name) {
    Parsed out;
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        out.error = "the request body must be a JSON object";
        return out;
    }
    if (const auto it = body.find("name"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) {
            out.error = "name must be a string";
            return out;
        }
        out.name = it->get<std::string>();
        if (!path_name.empty() && out.name != path_name) {
            out.error = "body name must match the path (or be omitted)";
            return out;
        }
    }
    if (out.name.empty()) {
        out.name = std::string{path_name};
    }
    if (const auto it = body.find("collections"); it != body.end() && !it->is_null()) {
        if (!it->is_array()) {
            out.error = "collections must be a list of strings";
            return out;
        }
        for (const nlohmann::json& item : *it) {
            if (!item.is_string()) {
                out.error = "collections must be a list of strings";
                return out;
            }
            out.graph.collections.push_back(item.get<std::string>());
        }
    }
    if (const auto it = body.find("extract_backend"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) {
            out.error = "extract_backend must be a string";
            return out;
        }
        out.graph.extract_backend = it->get<std::string>();
    }
    if (const auto it = body.find("hops"); it != body.end() && !it->is_null()) {
        if (!it->is_number_integer()) {
            out.error = "hops must be 1 or 2";
            return out;
        }
        out.graph.hops = it->get<int>();
    }
    if (const auto it = body.find("max_entities"); it != body.end() && !it->is_null()) {
        if (!it->is_number_integer()) {
            out.error = "max_entities must be a positive integer";
            return out;
        }
        out.graph.max_entities = it->get<int>();
    }
    return out;
}

[[nodiscard]] HttpResponse write_entry(const AdminConfigContext& context, const Parsed& parsed,
                                       const commands::NamedGraphValidation& validation, bool force,
                                       int status) {
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::append_graph(content, parsed.name, parsed.graph, force);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const std::exception& e) {
        return error_response(500, e.what());
    }
    nlohmann::json out{{"data", graph_json(parsed.name, parsed.graph)}};
    if (!validation.warnings.empty()) {
        out["warnings"] = validation.warnings;
    }
    return json_response(status, out);
}

}  // namespace

HttpResponse admin_list_graphs(const AdminConfigContext& context) {
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    nlohmann::json data = nlohmann::json::array();
    for (const auto& [name, graph] : config->graphs) {
        data.push_back(graph_json(name, graph));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse admin_create_graph(const AdminConfigContext& context, const HttpRequest& request) {
    const Parsed parsed = parse_body(request, "");
    if (!parsed.error.empty()) {
        return error_response(400, parsed.error);
    }
    if (parsed.name.empty()) {
        return error_response(400, "name is required");
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    // The CLI's rules, then the CLI's collision answer: an existing name is
    // a conflict a client resolves with PUT, never a silent replace.
    const commands::NamedGraphValidation validation =
        commands::validate_named_graph(*config, parsed.name, parsed.graph);
    if (!validation.error.empty()) {
        return error_response(400, validation.error);
    }
    if (config->find_graph(parsed.name) != nullptr) {
        return error_response(409,
                              "graph '" + parsed.name + "' already exists (PUT /v1/admin/graphs/" +
                                  parsed.name + " replaces it)",
                              kConflict);
    }
    return write_entry(context, parsed, validation, /*force=*/false, 201);
}

HttpResponse admin_get_graph(const AdminConfigContext& context, std::string_view name) {
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    // Reported under the name the file spells, however the path spelled it.
    const harness::CaseInsensitiveLess less;
    for (const auto& [key, graph] : config->graphs) {
        if (!less(key, name) && !less(name, key)) {
            return json_response(200, nlohmann::json{{"data", graph_json(key, graph)}});
        }
    }
    return error_response(404, "graph '" + std::string{name} + "' is not configured",
                          kNotFoundError);
}

HttpResponse admin_put_graph(const AdminConfigContext& context, std::string_view name,
                             const HttpRequest& request) {
    const Parsed parsed = parse_body(request, name);
    if (!parsed.error.empty()) {
        return error_response(400, parsed.error);
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    if (config->find_graph(name) == nullptr) {
        return error_response(404, "graph '" + std::string{name} + "' is not configured",
                              kNotFoundError);
    }
    const commands::NamedGraphValidation validation =
        commands::validate_named_graph(*config, parsed.name, parsed.graph);
    if (!validation.error.empty()) {
        return error_response(400, validation.error);
    }
    return write_entry(context, parsed, validation, /*force=*/true, 200);
}

HttpResponse admin_delete_graph_config(const AdminConfigContext& context, std::string_view name) {
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::delete_graph(content, name);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(404, e.what(), kNotFoundError);
    } catch (const harness::ConfigError& e) {
        return error_response(500, std::string{"the config cannot be read: "} + e.what(),
                              kConfigError);
    } catch (const std::exception& e) {
        return error_response(500, e.what());
    }
    return json_response(200, nlohmann::json{{"deleted", std::string{name}}});
}

}  // namespace apogee::httpserver
