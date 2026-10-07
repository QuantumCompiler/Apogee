#include "httpserver/admin_symphonies.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "scaffold/symphony.h"
#include "symphony/definition.h"
#include "symphony/view.h"

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

/// A string at `key`, "" when absent; `error` set when it is not one.
std::string text_at(const nlohmann::json& object, std::string_view key, std::string& error,
                    std::string_view where) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        error = std::string{where} + std::string{key} + " must be a string";
        return {};
    }
    return it->get<std::string>();
}

bool flag_at(const nlohmann::json& object, std::string_view key, std::string& error,
             std::string_view where) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return false;
    }
    if (!it->is_boolean()) {
        error = std::string{where} + std::string{key} + " must be true or false";
        return false;
    }
    return it->get<bool>();
}

std::optional<std::int64_t> count_at(const nlohmann::json& object, std::string_view key,
                                     std::string& error, std::string_view where) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_number_integer()) {
        error = std::string{where} + std::string{key} + " must be a whole number";
        return std::nullopt;
    }
    return it->get<std::int64_t>();
}

/// The body as a definition: the view's own shape, so a client reads a
/// symphony, changes it and sends it back.
std::optional<harness::SymphonySpec> spec_from(const nlohmann::json& body, std::string& error) {
    harness::SymphonySpec spec;
    spec.description = text_at(body, "description", error, "");
    if (const auto input = body.find("input"); input != body.end() && !input->is_null()) {
        if (!input->is_object()) {
            error = "input must be an object (description, image)";
            return std::nullopt;
        }
        spec.input.description = text_at(*input, "description", error, "input.");
        spec.input.image = flag_at(*input, "image", error, "input.");
    }
    const auto stages = body.find("stages");
    if (stages == body.end() || !stages->is_array()) {
        error = "stages must be a list of stages (name, role, prompt -- or name, play, input)";
        return std::nullopt;
    }
    std::size_t index = 0;
    for (const nlohmann::json& item : *stages) {
        const std::string where = "stages[" + std::to_string(index++) + "].";
        if (!item.is_object()) {
            error = where.substr(0, where.size() - 1) + " must be an object";
            return std::nullopt;
        }
        harness::SymphonyStage stage;
        stage.name = text_at(item, "name", error, where);
        stage.role = text_at(item, "role", error, where);
        stage.play = text_at(item, "play", error, where);
        stage.prompt = text_at(item, "prompt", error, where);
        stage.input = text_at(item, "input", error, where);
        stage.schema = text_at(item, "schema", error, where);
        stage.image = flag_at(item, "image", error, where);
        stage.brief_tokens = count_at(item, "brief_tokens", error, where);
        stage.answer_tokens = count_at(item, "answer_tokens", error, where);
        // A backend on a stage is the CLI's refusal too: the parser says why.
        for (const std::string_view key :
             {std::string_view{"backend"}, std::string_view{"model"}}) {
            if (item.contains(key)) {
                error = where + std::string{key} +
                        ": a stage names the role it plays, never a backend -- the suite decides "
                        "which backend plays it";
            }
        }
        spec.stages.push_back(std::move(stage));
    }
    if (!error.empty()) {
        return std::nullopt;
    }
    return spec;
}

HttpResponse write(const AdminConfigContext& context, const HttpRequest& request,
                   std::string_view path_name, bool force) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    std::string error;
    std::optional<harness::SymphonySpec> spec = spec_from(body, error);
    const std::string name =
        path_name.empty() ? text_at(body, "name", error, "") : std::string{path_name};
    const bool forced = force || flag_at(body, "force", error, "");
    if (!error.empty() || !spec.has_value()) {
        return error_response(400, error);
    }
    if (name.empty()) {
        return error_response(400, "name is required");
    }
    spec->name = name;
    scaffold::SymphonyResult result;
    try {
        result = scaffold::create_symphony(context.config_path, *spec, forced);
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
    const symphony::Found found = symphony::find_definition(
        *after.config, symphony::directory_for(context.config_path), result.name);
    if (!found.definition.has_value()) {
        return error_response(500, found.error);
    }
    const int status = force || result.replaced ? 200 : 201;
    return json_response(status, symphony::definition_document(*found.definition, found.catalog));
}

}  // namespace

HttpResponse admin_list_symphonies(const AdminConfigContext& context) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    return json_response(200, symphony::list_document(symphony::catalog(
                                  *loaded.config, symphony::directory_for(context.config_path))));
}

HttpResponse admin_create_symphony(const AdminConfigContext& context, const HttpRequest& request) {
    return write(context, request, {}, false);
}

HttpResponse admin_get_symphony(const AdminConfigContext& context, std::string_view name) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    // A name, never a path: the plane does not read files a client names.
    if (!harness::is_symphony_name(name)) {
        return error_response(404, "no symphony named '" + std::string{name} + "'", kNotFoundError);
    }
    const symphony::Found found = symphony::find_definition(
        *loaded.config, symphony::directory_for(context.config_path), name);
    if (!found.definition.has_value()) {
        return error_response(404, found.error, kNotFoundError);
    }
    return json_response(200, symphony::definition_document(*found.definition, found.catalog));
}

HttpResponse admin_put_symphony(const AdminConfigContext& context, std::string_view name,
                                const HttpRequest& request) {
    return write(context, request, name, true);
}

HttpResponse admin_delete_symphony(const AdminConfigContext& context, std::string_view name) {
    const Loaded loaded = load_now(context);
    if (!loaded.config.has_value()) {
        return loaded.failure;
    }
    if (loaded.config->find_symphony(name) == nullptr) {
        const bool shipped = harness::find_bundled_symphony(name) != nullptr;
        return error_response(404,
                              shipped ? "'" + std::string{name} +
                                            "' is a shipped starter, with no config entry to "
                                            "delete"
                                      : "no symphony entry named '" + std::string{name} + "'",
                              kNotFoundError);
    }
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::delete_symphony(content, name);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    return json_response(200, nlohmann::json{{"deleted", std::string{name}}});
}

}  // namespace apogee::httpserver
