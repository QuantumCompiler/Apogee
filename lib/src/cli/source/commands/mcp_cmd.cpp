#include "commands/mcp_cmd.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "commands/helpers.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/paths.h"
#include "mcp/registry.h"
#include "mcp/serve_stdio.h"
#include "scaffold/mcp_server.h"
#include "tools/toolsets.h"
#include "version/version.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee mcp: " << message << "\n";
    throw CLI::RuntimeError(1);
}

std::filesystem::path config_path_for(const RootContext& context) {
    return harness::resolve_config_path(context.config_path);
}

harness::Config load(const std::filesystem::path& path) {
    try {
        return harness::load_config(path);
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
}

std::vector<mcp::ServerSpec> specs_of(const harness::Config& config) {
    std::vector<mcp::ServerSpec> specs;
    for (const auto& [name, server] : config.mcp_servers) {
        specs.push_back(
            mcp::ServerSpec{name, server.command, server.args, server.env, server.enabled});
    }
    return specs;
}

mcp::RegistryOptions quiet_registry_options() {
    mcp::RegistryOptions options;
    options.client_version = std::string{version::semantic()};
    return options;
}

std::string pad(std::string text, std::size_t width) {
    while (text.size() < width) {
        text += ' ';
    }
    return text;
}

void bind_create(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    auto command = std::make_shared<std::string>();
    auto args = std::make_shared<std::vector<std::string>>();
    auto force = std::make_shared<bool>(false);
    CLI::App* cmd = parent.add_subcommand(
        "create", "Scaffold a runnable Python MCP server, or register an existing executable");
    cmd->add_option("name", *name, "The server's name (letters, digits, _ and -)")->required();
    cmd->add_option("--command", *command,
                    "Register this executable instead of scaffolding a server")
        ->type_name(kPathValue);
    cmd->add_option("--args", *args, "Arguments for --command")->needs("--command");
    cmd->add_flag("--force", *force, "Replace an existing server of the same name");
    cmd->callback([&context, name, command, args, force]() {
        const std::filesystem::path path = config_path_for(context);
        scaffold::McpServerSpec spec;
        spec.name = *name;
        spec.command = *command;
        spec.args = *args;
        spec.force = *force;
        scaffold::McpServerResult result;
        try {
            result = scaffold::create_mcp_server(path, spec);
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::cout << (result.directory.empty() ? "registered" : "created") << " MCP server '"
                  << result.name << "'\n";
        if (!result.directory.empty()) {
            std::cout << "  dir:     " << result.directory.string() << "\n";
        }
        std::cout << "  command: " << result.command << "\n"
                  << "  config:  " << result.config_path.string() << "\n\n"
                  << "Test it:  apogee mcp test " << result.name << " echo '{\"text\":\"hi\"}'\n"
                  << "It connects at the start of the next tool-using run.\n";
    });
}

void bind_list(CLI::App& parent, const RootContext& context) {
    CLI::App* cmd =
        parent.add_subcommand("list", "List configured MCP servers and connect to each");
    cmd->callback([&context]() {
        const harness::Config config = load(config_path_for(context));
        if (config.mcp_servers.empty()) {
            std::cout << "No MCP servers configured. Create one with: apogee mcp create <name>\n";
            return;
        }
        mcp::Registry registry;
        registry.connect_all(specs_of(config), quiet_registry_options());
        std::cout << pad("NAME", 20) << pad("STATE", 16) << pad("PROTOCOL", 12) << "TOOLS\n";
        for (const mcp::ServerStatus& status : registry.status()) {
            std::string state = !status.enabled    ? "disabled"
                                : status.connected ? "connected"
                                                   : "not connected";
            std::string tools;
            for (const std::string& tool : status.tools) {
                tools += tools.empty() ? "" : ", ";
                tools += tool;
            }
            std::cout << pad(status.name, 20) << pad(state, 16)
                      << pad(status.protocol_version.empty() ? "-" : status.protocol_version, 12)
                      << (tools.empty() ? "-" : tools) << "\n";
            if (!status.connected && status.enabled && !status.error.empty()) {
                std::cout << "  " << status.error << "\n";
            }
        }
    });
}

void bind_test(CLI::App& parent, const RootContext& context) {
    auto server = std::make_shared<std::string>();
    auto tool = std::make_shared<std::string>();
    auto arguments = std::make_shared<std::string>("{}");
    CLI::App* cmd =
        parent.add_subcommand("test", "Invoke one tool on one server, as the loop would");
    cmd->add_option("server", *server, "The server's name")->type_name(kServerValue)->required();
    cmd->add_option("tool", *tool, "The tool's name, un-namespaced")->required();
    cmd->add_option("arguments", *arguments, "JSON arguments (default {})");
    cmd->callback([&context, server, tool, arguments]() {
        const nlohmann::json parsed = nlohmann::json::parse(*arguments, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            fail("arguments must be a JSON object");
        }
        const harness::Config config = load(config_path_for(context));
        const harness::McpServerConfig* entry = config.find_mcp_server(*server);
        if (entry == nullptr) {
            fail("no MCP server named '" + *server + "' (see: apogee mcp list)");
        }
        // Only the named server, force-enabled: a disabled one is testable.
        mcp::ServerSpec spec{*server, entry->command, entry->args, entry->env, true};
        mcp::Registry registry;
        mcp::RegistryOptions options = quiet_registry_options();
        options.status = [](std::string_view line) { std::cerr << line << "\n"; };
        registry.connect_all({spec}, options);
        if (registry.connected_count() == 0) {
            fail("could not connect to '" + *server + "'");
        }
        std::string error;
        const mcp::ToolCallResult result =
            registry.call(mcp::namespaced_name(*server, *tool), *arguments, error);
        if (!error.empty()) {
            fail(error);
        }
        std::cout << result.text << "\n";
        if (result.is_error) {
            throw CLI::RuntimeError(1);
        }
    });
}

void bind_enable(CLI::App& parent, const RootContext& context, bool enabled) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand(
        enabled ? "enable" : "disable",
        enabled ? "Connect to this server on the next run" : "Stop connecting to this server");
    cmd->add_option("name", *name, "The server's name")->type_name(kServerValue)->required();
    cmd->callback([&context, name, enabled]() {
        const std::filesystem::path path = config_path_for(context);
        try {
            harness::edit_config_file(path, [name, enabled](std::string_view content) {
                return harness::set_mcp_server_enabled(content, *name, enabled);
            });
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::cout << "MCP server '" << *name << "' " << (enabled ? "enabled" : "disabled")
                  << ". It takes effect at the start of the next tool-using run.\n";
    });
}

}  // namespace

std::string_view McpCommand::name() const noexcept {
    return "mcp";
}

std::string_view McpCommand::summary() const noexcept {
    return "Manage MCP tool servers";
}

void McpCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);
    bind_create(*cmd, context);
    bind_list(*cmd, context);
    bind_test(*cmd, context);
    bind_enable(*cmd, context, true);
    bind_enable(*cmd, context, false);
}

std::string_view McpToolsServerCommand::name() const noexcept {
    return "__mcp-tools";
}

std::string_view McpToolsServerCommand::summary() const noexcept {
    return "Serve the read-only native toolsets as an MCP server on stdio (internal)";
}

void McpToolsServerCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->group("");  // hidden from --help: another program spawns it
    cmd->callback([&context]() {
        // The native toolsets only, never the MCP registry: a config that
        // listed this very command as a server would otherwise spawn itself
        // without end.
        harness::Config config;
        try {
            config = harness::load_config(config_path_for(context));
        } catch (const std::exception&) {
            // No config is fine: the defaults sandbox to the launch folder.
        }
        agent::ToolRegistry registry;
        tools::ToolsetOptions options;
        options.config = &config;
        options.fs_root = harness::expand_env(config.tools.fs_root);
        options.disabled = config.tools.disabled;
        tools::register_native_toolsets(registry, options);
        const int status =
            mcp::serve_stdio(std::cin, std::cout, "apogee-tools", std::string{version::semantic()},
                             mcp::read_only_tools(registry), mcp::dispatch_through(registry));
        if (status != 0) {
            throw CLI::RuntimeError(status);
        }
    });
}

}  // namespace apogee::commands
