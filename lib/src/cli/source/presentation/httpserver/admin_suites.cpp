#include "httpserver/admin_suites.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "operations/suites.h"

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

[[nodiscard]] nlohmann::json member_json(const harness::SuiteMember& member) {
    nlohmann::json out{{"backend", member.backend}};
    if (member.context_size.has_value()) {
        out["context_size"] = *member.context_size;
    }
    if (member.toolset.has_value()) {
        out["toolset"] = *member.toolset;
    }
    return out;
}

[[nodiscard]] nlohmann::json caps_json(const harness::ConsultCaps& caps) {
    nlohmann::json out = nlohmann::json::object();
    if (caps.per_turn.has_value()) {
        out["per_turn"] = *caps.per_turn;
    }
    if (caps.brief_tokens.has_value()) {
        out["brief_tokens"] = *caps.brief_tokens;
    }
    if (caps.answer_tokens.has_value()) {
        out["answer_tokens"] = *caps.answer_tokens;
    }
    return out;
}

[[nodiscard]] nlohmann::json suite_json(const harness::Config& config, std::string_view name,
                                        const harness::SuiteConfig& suite) {
    nlohmann::json members = nlohmann::json::object();
    for (const auto& [role, member] : suite.members) {
        members[role] = member_json(member);
    }
    nlohmann::json out{{"name", std::string{name}},
                       {"members", std::move(members)},
                       {"default", harness::active_suite(config) == &suite}};
    if (!suite.description.empty()) {
        out["description"] = suite.description;
    }
    if (!suite.consultable.empty()) {
        out["consultable"] = suite.consultable;
    }
    if (suite.consult_caps.any()) {
        out["consult_caps"] = caps_json(suite.consult_caps);
    }
    return out;
}

/// `consultable` from a body: a list of role names (27f).
[[nodiscard]] std::optional<std::vector<std::string>> parse_consultable(const nlohmann::json& in,
                                                                        std::string& error) {
    if (!in.is_array()) {
        error = "consultable must be a list of roles";
        return std::nullopt;
    }
    std::vector<std::string> roles;
    for (const nlohmann::json& item : in) {
        if (!item.is_string()) {
            error = "consultable must be a list of roles";
            return std::nullopt;
        }
        roles.push_back(item.get<std::string>());
    }
    return roles;
}

/// `consult_caps` from a body: an object of `consult_cap_names()` to a
/// positive integer, null for the default (27f).
[[nodiscard]] std::optional<harness::ConsultCaps> parse_caps(const nlohmann::json& in,
                                                             std::string& error) {
    if (!in.is_object()) {
        error = "consult_caps must be an object of per_turn, brief_tokens, answer_tokens";
        return std::nullopt;
    }
    harness::ConsultCaps caps;
    for (const auto& [name, value] : in.items()) {
        std::optional<std::int64_t>* slot = nullptr;
        if (name == "per_turn") {
            slot = &caps.per_turn;
        } else if (name == "brief_tokens") {
            slot = &caps.brief_tokens;
        } else if (name == "answer_tokens") {
            slot = &caps.answer_tokens;
        } else {
            error = "consult_caps." + name + ": not a cap (per_turn, brief_tokens, answer_tokens)";
            return std::nullopt;
        }
        if (value.is_null()) {
            continue;
        }
        if (!value.is_number_integer() || value.get<std::int64_t>() < 1) {
            error = "consult_caps." + name + " must be a positive integer";
            return std::nullopt;
        }
        *slot = value.get<std::int64_t>();
    }
    return caps;
}

/// A member from the body: a backend's name, or an object with `backend` and
/// the knobs.
[[nodiscard]] std::optional<harness::SuiteMember> parse_member(const nlohmann::json& in,
                                                               std::string& error) {
    harness::SuiteMember member;
    if (in.is_string()) {
        member.backend = in.get<std::string>();
        return member;
    }
    if (!in.is_object()) {
        error = "a member is a backend's name or an object with backend";
        return std::nullopt;
    }
    if (const auto it = in.find("backend"); it != in.end() && it->is_string()) {
        member.backend = it->get<std::string>();
    } else {
        error = "a member's backend must be a string";
        return std::nullopt;
    }
    if (const auto it = in.find("context_size"); it != in.end() && !it->is_null()) {
        if (!it->is_number_integer()) {
            error = "context_size must be a positive integer";
            return std::nullopt;
        }
        member.context_size = it->get<std::int64_t>();
    }
    if (const auto it = in.find("toolset"); it != in.end() && !it->is_null()) {
        if (!it->is_array()) {
            error = "toolset must be a list of strings";
            return std::nullopt;
        }
        std::vector<std::string> toolset;
        for (const nlohmann::json& item : *it) {
            if (!item.is_string()) {
                error = "toolset must be a list of strings";
                return std::nullopt;
            }
            toolset.push_back(item.get<std::string>());
        }
        member.toolset = std::move(toolset);
    }
    return member;
}

struct Parsed {
    std::string name;
    harness::SuiteConfig suite;
    std::string error;
};

/// The body -> entry translation for POST and PUT. `path_name` is the PUT
/// path's name (empty on POST, where the body names it).
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
    if (const auto it = body.find("description"); it != body.end() && !it->is_null()) {
        if (!it->is_string()) {
            out.error = "description must be a string";
            return out;
        }
        out.suite.description = it->get<std::string>();
    }
    if (const auto it = body.find("members"); it != body.end() && !it->is_null()) {
        if (!it->is_object()) {
            out.error = "members must be an object of role -> member";
            return out;
        }
        for (const auto& [role, value] : it->items()) {
            std::optional<harness::SuiteMember> member = parse_member(value, out.error);
            if (!member.has_value()) {
                out.error = role + ": " + out.error;
                return out;
            }
            out.suite.members[role] = std::move(*member);
        }
    }
    if (const auto it = body.find("consultable"); it != body.end() && !it->is_null()) {
        std::optional<std::vector<std::string>> roles = parse_consultable(*it, out.error);
        if (!roles.has_value()) {
            return out;
        }
        out.suite.consultable = std::move(*roles);
    }
    if (const auto it = body.find("consult_caps"); it != body.end() && !it->is_null()) {
        std::optional<harness::ConsultCaps> caps = parse_caps(*it, out.error);
        if (!caps.has_value()) {
            return out;
        }
        out.suite.consult_caps = *caps;
    }
    return out;
}

/// Runs `transform` through the one editor and answers with the entry as it
/// now reads, and whether the server must restart to resolve under it.
template <typename Transform>
[[nodiscard]] HttpResponse write(const AdminConfigContext& context, std::string_view name,
                                 int status, Transform&& transform) {
    try {
        harness::edit_config_file(context.config_path, std::forward<Transform>(transform));
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    } catch (const std::exception& e) {
        return error_response(500, e.what());
    }
    HttpResponse failure;
    const std::optional<harness::Config> after = load_now(context, failure);
    if (!after.has_value()) {
        return failure;
    }
    nlohmann::json out{{"restart_required",
                        context.startup != nullptr && config_drifted(*context.startup, *after)}};
    if (const auto it = after->suites.find(name); it != after->suites.end()) {
        out["data"] = suite_json(*after, it->first, it->second);
    }
    return json_response(status, out);
}

}  // namespace

HttpResponse admin_list_suites(const AdminConfigContext& context) {
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    nlohmann::json data = nlohmann::json::array();
    for (const auto& [name, suite] : config->suites) {
        data.push_back(suite_json(*config, name, suite));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse admin_create_suite(const AdminConfigContext& context, const HttpRequest& request) {
    const Parsed parsed = parse_body(request, "");
    if (!parsed.error.empty()) {
        return error_response(400, parsed.error);
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    if (const std::string refused =
            commands::validate_suite(*config, parsed.name, parsed.suite, context.metered);
        !refused.empty()) {
        return error_response(400, refused);
    }
    if (config->find_suite(parsed.name) != nullptr) {
        return error_response(409,
                              "suite '" + parsed.name + "' already exists (PUT /v1/admin/suites/" +
                                  parsed.name + " replaces it)",
                              kConflict);
    }
    return write(context, parsed.name, 201, [&](std::string_view content) {
        return harness::append_suite(content, parsed.name, parsed.suite, /*force=*/false);
    });
}

HttpResponse admin_get_suite(const AdminConfigContext& context, std::string_view name) {
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    // Reported under the name the file spells, however the path spelled it.
    if (const auto it = config->suites.find(name); it != config->suites.end()) {
        return json_response(200,
                             nlohmann::json{{"data", suite_json(*config, it->first, it->second)}});
    }
    return error_response(404, "suite '" + std::string{name} + "' is not configured",
                          kNotFoundError);
}

HttpResponse admin_put_suite(const AdminConfigContext& context, std::string_view name,
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
    if (config->find_suite(name) == nullptr) {
        return error_response(404, "suite '" + std::string{name} + "' is not configured",
                              kNotFoundError);
    }
    if (const std::string refused =
            commands::validate_suite(*config, parsed.name, parsed.suite, context.metered);
        !refused.empty()) {
        return error_response(400, refused);
    }
    return write(context, parsed.name, 200, [&](std::string_view content) {
        return harness::append_suite(content, parsed.name, parsed.suite, /*force=*/true);
    });
}

HttpResponse admin_delete_suite(const AdminConfigContext& context, std::string_view name) {
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    if (const std::string refused = commands::validate_suite_delete(*config, name);
        !refused.empty()) {
        return error_response(409, refused, kConflict);
    }
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::delete_suite(content, name);
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

HttpResponse admin_set_suite_member(const AdminConfigContext& context, std::string_view name,
                                    const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    const auto role_it = body.find("role");
    if (role_it == body.end() || !role_it->is_string()) {
        return error_response(400, "role is required");
    }
    const std::string role = role_it->get<std::string>();
    const auto member_it = body.find("member");
    if (member_it == body.end()) {
        return error_response(400, "member is required -- null removes it");
    }
    std::optional<harness::SuiteMember> member;
    if (!member_it->is_null()) {
        std::string error;
        member = parse_member(*member_it, error);
        if (!member.has_value()) {
            return error_response(400, error);
        }
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const auto found = config->suites.find(name);
    if (found == config->suites.end()) {
        return error_response(404, "suite '" + std::string{name} + "' is not configured",
                              kNotFoundError);
    }
    // The suite as it will read, held to the CLI's rules as a whole.
    harness::SuiteConfig after = found->second;
    if (member.has_value()) {
        after.members[role] = *member;
    } else {
        after.members.erase(role);
    }
    if (const std::string refused =
            member.has_value()
                ? commands::validate_suite_member(*config, role, *member)
                : commands::validate_suite(*config, found->first, after, context.metered);
        !refused.empty()) {
        return error_response(400, refused);
    }
    // A consultable member moved to a backend that bills per call is refused
    // here, as the CLI refuses it (27f).
    if (const std::string refused =
            commands::validate_suite_consult(*config, after, context.metered);
        !refused.empty()) {
        return error_response(400, refused);
    }
    return write(context, found->first, 200, [&](std::string_view content) {
        return harness::set_suite_member(content, found->first, role, member);
    });
}

HttpResponse admin_set_suite_consult(const AdminConfigContext& context, std::string_view name,
                                     const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const auto found = config->suites.find(name);
    if (found == config->suites.end()) {
        return error_response(404, "suite '" + std::string{name} + "' is not configured",
                              kNotFoundError);
    }
    const harness::SuiteConfig before = found->second;
    harness::SuiteConfig after = before;
    std::string error;
    if (const auto it = body.find("consultable"); it != body.end()) {
        if (it->is_null()) {
            after.consultable.clear();
        } else {
            std::optional<std::vector<std::string>> roles = parse_consultable(*it, error);
            if (!roles.has_value()) {
                return error_response(400, error);
            }
            after.consultable = std::move(*roles);
        }
    }
    if (const auto it = body.find("consult_caps"); it != body.end()) {
        if (it->is_null()) {
            after.consult_caps = {};
        } else {
            // Each cap named is set, or with null put back to its default;
            // a cap not named keeps what it had -- as `--consult-cap` does.
            std::optional<harness::ConsultCaps> caps = parse_caps(*it, error);
            if (!caps.has_value()) {
                return error_response(400, error);
            }
            for (const auto& [cap, value] : it->items()) {
                if (cap == "per_turn") {
                    after.consult_caps.per_turn = caps->per_turn;
                } else if (cap == "brief_tokens") {
                    after.consult_caps.brief_tokens = caps->brief_tokens;
                } else {
                    after.consult_caps.answer_tokens = caps->answer_tokens;
                }
            }
        }
    }
    if (const std::string refused =
            commands::validate_suite(*config, found->first, after, context.metered);
        !refused.empty()) {
        return error_response(400, refused);
    }
    return write(context, found->first, 200, [&](std::string_view content) {
        std::string edited{content};
        if (after.consultable != before.consultable) {
            edited = harness::set_suite_consultable(edited, found->first, after.consultable);
        }
        if (after.consult_caps != before.consult_caps) {
            edited = harness::set_suite_consult_caps(edited, found->first, after.consult_caps);
        }
        return edited;
    });
}

HttpResponse admin_set_default_suite(const AdminConfigContext& context,
                                     const HttpRequest& request) {
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return error_response(400, "the request body must be a JSON object");
    }
    const auto it = body.find("name");
    if (it == body.end() || !it->is_string() || it->get<std::string>().empty()) {
        return error_response(400, "name is required -- a suite, or off for none");
    }
    const std::string wanted = it->get<std::string>();
    HttpResponse failure;
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    std::string value;
    if (wanted != harness::kSuiteOff) {
        const auto found = config->suites.find(wanted);
        if (found == config->suites.end()) {
            return error_response(400, "no suite named '" + wanted + "' in this config");
        }
        value = found->first;
    }
    try {
        harness::edit_config_file(context.config_path, [&](std::string_view content) {
            return harness::set_default_suite(content, value);
        });
    } catch (const harness::ConfigEditError& e) {
        return error_response(400, e.what());
    } catch (const harness::ConfigError& e) {
        return error_response(400, e.what(), kConfigError);
    }
    const std::optional<harness::Config> after = load_now(context, failure);
    if (!after.has_value()) {
        return failure;
    }
    return json_response(
        200, nlohmann::json{{"field", "default_suite"},
                            {"name", value},
                            {"restart_required", context.startup != nullptr &&
                                                     config_drifted(*context.startup, *after)}});
}

}  // namespace apogee::httpserver
