#include "httpserver/admin.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace apogee::httpserver {

AdminHandler::AdminHandler(AdminOptions options, JobRegistry& jobs, events::Bus& bus)
    : options_{std::move(options)}, jobs_{&jobs}, bus_{&bus} {}

AdminConfigContext AdminHandler::config_context() const {
    return AdminConfigContext{.config_path = options_.config_path, .startup = &options_.startup};
}

HttpResponse AdminHandler::list_backends(const HttpRequest& /*request*/) {
    return admin_list_backends(config_context());
}

HttpResponse AdminHandler::create_backend(const HttpRequest& request) {
    return admin_create_backend(config_context(), request);
}

HttpResponse AdminHandler::get_backend(const HttpRequest& /*request*/, std::string_view name) {
    return admin_get_backend(config_context(), name);
}

HttpResponse AdminHandler::delete_backend(const HttpRequest& /*request*/, std::string_view name) {
    return admin_delete_backend(config_context(), name);
}

HttpResponse AdminHandler::list_agents(const HttpRequest&) {
    return admin_list_agents(config_context());
}

HttpResponse AdminHandler::create_agent(const HttpRequest& request) {
    return admin_create_agent(config_context(), request);
}

HttpResponse AdminHandler::get_agent(const HttpRequest&, std::string_view name) {
    return admin_get_agent(config_context(), name);
}

HttpResponse AdminHandler::put_agent(const HttpRequest& request, std::string_view name) {
    return admin_put_agent(config_context(), name, request);
}

HttpResponse AdminHandler::delete_agent(const HttpRequest& request, std::string_view name) {
    return admin_delete_agent(config_context(), name, request);
}

HttpResponse AdminHandler::list_mcp_servers(const HttpRequest&) {
    return admin_list_mcp_servers(config_context());
}

HttpResponse AdminHandler::create_mcp_server(const HttpRequest& request) {
    return admin_create_mcp_server(config_context(), request);
}

HttpResponse AdminHandler::get_mcp_server(const HttpRequest&, std::string_view name) {
    return admin_get_mcp_server(config_context(), name);
}

HttpResponse AdminHandler::delete_mcp_server(const HttpRequest&, std::string_view name) {
    return admin_delete_mcp_server(config_context(), name);
}

HttpResponse AdminHandler::set_mcp_server_enabled(const HttpRequest& request,
                                                  std::string_view name) {
    return admin_set_mcp_server_enabled(config_context(), name, request);
}

HttpResponse AdminHandler::capture_knowledge(Handler& plane, const HttpRequest& request) {
    return admin_capture_knowledge(config_context(), plane, request);
}

HttpResponse AdminHandler::create_knowledge(Handler& plane, const HttpRequest& request) {
    return admin_create_knowledge(config_context(), plane, request);
}

HttpResponse AdminHandler::refine_knowledge(Handler& plane, const HttpRequest& request) {
    return admin_refine_knowledge(config_context(), plane, request);
}

HttpResponse AdminHandler::list_knowledge(Handler& plane, const HttpRequest& request) {
    return admin_list_knowledge(config_context(), plane, request);
}

HttpResponse AdminHandler::reindex_knowledge(Handler& plane, const HttpRequest& request) {
    return admin_reindex_knowledge(config_context(), plane, request);
}

HttpResponse AdminHandler::get_knowledge(const HttpRequest& request, std::string_view id) {
    return admin_get_knowledge(config_context(), id, request);
}

HttpResponse AdminHandler::patch_knowledge(const HttpRequest& request, std::string_view id) {
    return admin_patch_knowledge(config_context(), id, request);
}

HttpResponse AdminHandler::delete_knowledge(const HttpRequest& request, std::string_view id) {
    return admin_delete_knowledge(config_context(), id, request);
}

HttpResponse AdminHandler::list_permissions(const HttpRequest&) {
    return admin_list_permissions(config_context());
}

HttpResponse AdminHandler::put_permission(const HttpRequest& request, std::string_view tool) {
    return admin_put_permission(config_context(), tool, request);
}

HttpResponse AdminHandler::set_default(const HttpRequest& request) {
    return admin_set_role(config_context(), "default", request);
}

HttpResponse AdminHandler::set_default_embedding(const HttpRequest& request) {
    return admin_set_role(config_context(), "default_embedding", request);
}

HttpResponse AdminHandler::set_default_extraction(const HttpRequest& request) {
    return admin_set_role(config_context(), "default_extraction", request);
}

HttpResponse AdminHandler::format_config(const HttpRequest& /*request*/) {
    return admin_format_config(config_context());
}

HttpResponse AdminHandler::list_credentials(const HttpRequest& /*request*/) {
    return admin_list_credentials(
        AdminAuthContext{.config_path = options_.config_path, .env = options_.env});
}

HttpResponse AdminHandler::put_credential(const HttpRequest& request, std::string_view provider) {
    return admin_put_credential(
        AdminAuthContext{.config_path = options_.config_path, .env = options_.env}, provider,
        request);
}

HttpResponse AdminHandler::clear_credential(const HttpRequest& /*request*/,
                                            std::string_view provider) {
    return admin_clear_credential(
        AdminAuthContext{.config_path = options_.config_path, .env = options_.env}, provider);
}

HttpResponse AdminHandler::events_stream(const HttpRequest& /*request*/) {
    return event_stream(*bus_, options_.events);
}

HttpResponse AdminHandler::list_jobs(const HttpRequest& /*request*/) {
    nlohmann::json data = nlohmann::json::array();
    for (const JobRecord& record : jobs_->list()) {
        data.push_back(job_json(record));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse AdminHandler::get_job(const HttpRequest& /*request*/, std::string_view id) {
    const std::optional<JobRecord> record = jobs_->get(std::string{id});
    if (!record.has_value()) {
        return error_response(404, "no job with id '" + std::string{id} + "'", kNotFoundError);
    }
    return json_response(200, job_json(*record));
}

HttpResponse AdminHandler::cancel_job(const HttpRequest& /*request*/, std::string_view id) {
    const std::optional<JobRecord> record = jobs_->cancel(std::string{id});
    if (!record.has_value()) {
        return error_response(404, "no job with id '" + std::string{id} + "'", kNotFoundError);
    }
    return json_response(200, job_json(*record));
}

}  // namespace apogee::httpserver
