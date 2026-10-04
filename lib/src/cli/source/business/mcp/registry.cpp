#include "mcp/registry.h"

#include <utility>

#include "events/bus.h"

namespace apogee::mcp {

Registry::~Registry() {
    close_all();
}

void Registry::connect_all(const std::vector<ServerSpec>& servers, const RegistryOptions& options) {
    const auto say = [&options](const std::string& line) {
        if (options.status) {
            options.status(line);
        }
    };
    for (const ServerSpec& server : servers) {
        ServerStatus status;
        status.name = server.name;
        status.enabled = server.enabled;
        if (!server.enabled) {
            statuses_.push_back(std::move(status));
            continue;  // listed, never dialled
        }
        // Before the dial, not after: without this the line sits on the
        // previous server's result for up to the connect timeout.
        say("[mcp] connecting: " + server.name + "...");

        std::string error;
        std::unique_ptr<Transport> transport;
        if (server.command.empty()) {
            error = "no command set";
        } else if (options.spawn) {
            transport = options.spawn(server, options.server_log, error);
        } else {
            transport =
                spawn_stdio(server.command, server.args, server.env, options.server_log, error);
        }
        if (transport == nullptr) {
            status.error = error.empty() ? "could not start" : error;
            say("[mcp] warning: " + server.name + ": connect failed (skipped): " + status.error);
            statuses_.push_back(std::move(status));
            continue;
        }

        ClientOptions client_options;
        client_options.connect_timeout = options.connect_timeout;
        client_options.call_timeout = options.call_timeout;
        client_options.client_version = options.client_version;
        // Diagnostics ride the progress writer during construction only:
        // after startup that writer may be a status line a live turn owns.
        client_options.log = options.status;
        std::unique_ptr<Client> client =
            Client::connect(server.name, std::move(transport), client_options, error);
        if (client == nullptr) {
            status.error = error;
            const bool timed_out = error.find("timed out") != std::string::npos;
            say("[mcp] warning: " + server.name +
                (timed_out ? ": connect timed out after " +
                                 std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                    options.connect_timeout)
                                                    .count()) +
                                 "s (skipped)"
                           : ": connect failed (skipped): " + error));
            statuses_.push_back(std::move(status));
            continue;
        }
        // Once connected, the client logs nowhere: the writer may become a
        // live status line, and a background thread writing there would
        // stop the spinner.
        status.connected = true;
        status.protocol_version = client->protocol_version();
        status.server_name = client->server_name();
        for (const ToolInfo& tool : client->tools()) {
            status.tools.push_back(tool.name);
        }
        say("[mcp] connected: " + server.name + " (" + std::to_string(status.tools.size()) +
            " tools)");
        events::emit("mcp.server.connected",
                     {{"name", server.name}, {"tools", status.tools.size()}});
        clients_[server.name] = std::shared_ptr<Client>{std::move(client)};
        statuses_.push_back(std::move(status));
    }
}

std::vector<ServerStatus> Registry::status() const {
    return statuses_;
}

std::size_t Registry::connected_count() const noexcept {
    return clients_.size();
}

void Registry::register_into(agent::ToolRegistry& registry) const {
    for (const auto& [server, client] : clients_) {
        for (const ToolInfo& info : client->tools()) {
            agent::Tool tool;
            tool.name = namespaced_name(server, info.name);
            tool.description =
                info.description.empty() ? "(" + server + ") " + info.name : info.description;
            tool.parameters_schema = info.input_schema.dump();
            tool.writes = !(info.read_only_hint.has_value() && *info.read_only_hint);
            tool.run = [client, name = info.name](std::string_view arguments) {
                const ToolCallResult result =
                    client->call_tool(name, arguments, harness::CancellationToken{});
                return agent::ToolOutcome{result.text, result.is_error};
            };
            tool.describe_target = [name = info.name](std::string_view arguments) {
                std::string text{arguments};
                if (text.size() > 80) {
                    text = text.substr(0, 77) + "...";
                }
                return text;
            };
            registry.add(tool);
        }
    }
}

ToolCallResult Registry::call(std::string_view namespaced, std::string_view arguments,
                              std::string& error) const {
    std::string server;
    std::string tool;
    if (!split_namespaced(namespaced, server, tool)) {
        error = "'" + std::string{namespaced} + "' is not an MCP tool name (mcp__<server>__<tool>)";
        return {};
    }
    const auto it = clients_.find(server);
    if (it == clients_.end()) {
        error = "no connected server '" + server + "'";
        return {};
    }
    return it->second->call_tool(tool, arguments, harness::CancellationToken{});
}

void Registry::close_all() {
    for (auto& [name, client] : clients_) {
        client->close();
        events::emit("mcp.server.disconnected", {{"name", name}});
    }
    clients_.clear();
}

}  // namespace apogee::mcp
