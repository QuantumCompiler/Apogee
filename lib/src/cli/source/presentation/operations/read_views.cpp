#include "operations/read_views.h"

#include <nlohmann/json.hpp>

#include <string>

namespace apogee::operations {

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

nlohmann::json agents_document(const harness::Config& config) {
    nlohmann::json data = nlohmann::json::array();
    for (const harness::NamedAgent& agent : harness::all_agents(config)) {
        data.push_back(agent_view(agent));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}};
}

nlohmann::json mcp_server_view(std::string_view name, const harness::McpServerConfig& server) {
    return nlohmann::json{{"name", std::string{name}},
                          {"command", server.command},
                          {"args", server.args},
                          {"enabled", server.enabled},
                          {"env_set", !server.env.empty()}};
}

nlohmann::json mcp_servers_document(const harness::Config& config) {
    nlohmann::json data = nlohmann::json::array();
    for (const auto& [name, server] : config.mcp_servers) {
        data.push_back(mcp_server_view(name, server));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}};
}

}  // namespace apogee::operations
