#include "cli/acp_cmd.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "acp/host_servers.h"
#include "acp/server.h"
#include "cli/chat_session.h"
#include "cli/helpers.h"
#include "cli/tui_child.h"
#include "contracts/paths.h"
#include "harness/roles.h"
#include "modelstore/mlx_info.h"
#include "platform/child_process.h"
#include "platform/platform.h"

namespace apogee::commands {

namespace {

constexpr std::array<std::string_view, 7> kProtocolFlags{
    "--output-format", "--input-format", "--resume", "--continue",
    "--private",       "--no-color",     "--raw"};

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee acp: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

/// Why the protocol owns `flag`, in the command's words.
[[nodiscard]] std::string protocol_reason(std::string_view flag) {
    if (flag == "--output-format" || flag == "--input-format") {
        return "the protocol owns the streams: the host speaks the Agent Client Protocol on "
               "stdio, and each session's apogee runs in machine mode";
    }
    if (flag == "--resume" || flag == "--continue") {
        return "each ACP session is a new one; saving and resuming them is a later Apogee's "
               "(56c)";
    }
    if (flag == "--private") {
        return "every ACP session is private already: nothing of it is written";
    }
    return "the host draws the session: there is no terminal to colour or render for";
}

/// Whether `backend` chats: every entry does, but an MLX one over a
/// directory whose kind no chat runs (53e) -- the macOS app's `chats`.
[[nodiscard]] bool chats(const harness::BackendConfig& backend) {
    if (backend.type != harness::BackendType::Mlx || backend.model_path.empty()) {
        return true;
    }
    const std::filesystem::path dir{harness::expand_env_and_home(backend.model_path)};
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        return true;
    }
    return models::read_mlx_kind(dir).backend();
}

void run_acp(const RootContext& context, const SessionFlags& flags, const CLI::App& command) {
    for (const std::string_view flag : kProtocolFlags) {
        if (const CLI::Option* option = command.get_option_no_throw(std::string{flag});
            option != nullptr && option->count() > 0) {
            fail(std::string{flag} + " is refused: " + protocol_reason(flag));
        }
    }
    if (!platform::supports_child_processes()) {
        fail(
            "each ACP session runs as a child of this binary, which this platform cannot start "
            "yet");
    }
    harness::Config config;
    try {
        config = harness::load_config(harness::resolve_config_path(context.config_path));
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }

    acp::AgentSetup setup;
    setup.models = acp_models(config, flags.model);
    if (setup.models.empty()) {
        fail(
            "no backend is configured to chat with: add one (apogee config add-backend), or "
            "register what this machine has (apogee providers scan --register)");
    }
    setup.model = setup.models.front().value;
    setup.mode = flags.permission_mode.empty() ? std::string{"manual"} : flags.permission_mode;
    setup.bypass_allowed = flags.allow_bypass || setup.mode == "bypass";
    setup.verbose = flags.verbose;
    setup.home = platform::home_directory().value_or(std::string{});

    // Execute's own session, in machine mode, with what this line carries.
    std::vector<std::string> words{"execute",         "--input-format", "stream-json",
                                   "--output-format", "stream-json",    "--tools",
                                   "--private"};
    for (std::string& word : forwarded_flags(command)) {
        words.push_back(std::move(word));
    }
    setup.child = self_command(context, platform::executable_path(), words);

    const int code = acp::serve_acp(std::move(setup), std::cin, std::cout, std::cerr);
    if (code != 0) {
        throw CLI::RuntimeError(code);
    }
}

}  // namespace

std::span<const std::string_view> acp_protocol_flags() noexcept {
    return kProtocolFlags;
}

std::vector<acp::ModelChoice> acp_models(const harness::Config& config,
                                         std::string_view override_model) {
    std::vector<acp::ModelChoice> models;
    for (const auto& [name, backend] : config.backends) {
        if (!chats(backend)) {
            continue;
        }
        std::string description{harness::to_string(backend.type)};
        if (!backend.model.empty()) {
            description += " · " + backend.model;
        }
        models.push_back(acp::ModelChoice{.value = name, .name = name, .description = description});
    }
    // The roles chain's answer first: `-m`, else the default.
    const std::string first = harness::resolve_chat_backend(config, override_model);
    if (!first.empty()) {
        const auto found = std::ranges::find(models, first, &acp::ModelChoice::value);
        if (found != models.end()) {
            std::rotate(models.begin(), found, found + 1);
        } else {
            // A model pinned on a backend (`backend:model`), or a roster id:
            // what the session will run, offered first as it is named.
            models.insert(models.begin(), acp::ModelChoice{.value = first, .name = first});
        }
    }
    return models;
}

std::vector<std::string> forwarded_flags(const CLI::App& command) {
    std::vector<std::string> words;
    for (const CLI::Option* option : command.get_options()) {
        if (option->count() == 0 ||
            (option->get_lnames().empty() && option->get_snames().empty())) {
            continue;
        }
        const std::string name = option->get_lnames().empty() ? "-" + option->get_snames().front()
                                                              : "--" + option->get_lnames().front();
        if (name == "--help" || name == "--tools" ||
            std::ranges::find(kProtocolFlags, std::string_view{name}) != kProtocolFlags.end()) {
            continue;
        }
        if (option->get_expected_max() == 0) {
            words.push_back(name);  // a flag: present
            continue;
        }
        for (const std::string& value : option->results()) {
            words.push_back(name);
            words.push_back(value);
        }
    }
    return words;
}

std::vector<mcp::ServerSpec> host_mcp_servers(const std::filesystem::path& handover,
                                              std::vector<std::string>& notes) {
    std::string error;
    acp::HostServers read = acp::read_handover(handover, error);
    if (!error.empty()) {
        notes.push_back("the host's MCP servers could not be read: " + error);
        return {};
    }
    notes.insert(notes.end(), read.notes.begin(), read.notes.end());
    std::vector<mcp::ServerSpec> specs;
    for (acp::HostServer& server : read.servers) {
        specs.push_back(mcp::ServerSpec{.name = std::move(server.name),
                                        .command = std::move(server.command),
                                        .args = std::move(server.args),
                                        .env = std::move(server.env),
                                        .enabled = true});
    }
    return specs;
}

std::string_view AcpCommand::name() const noexcept {
    return "acp";
}

std::string_view AcpCommand::summary() const noexcept {
    return "Be the agent of an ACP host such as Xcode: the Agent Client Protocol on stdio";
}

void AcpCommand::bind(CLI::App& root, const RootContext& context) {
    auto flags = std::make_shared<SessionFlags>();
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    // Execute's own flag table, by reference (ADR 0007: they complete as
    // execute's do).
    bind_session_flags(*cmd, flags, SessionMode::Execute);
    // The protocol's: still parsed, so each is refused in this command's
    // words, but hidden from help and completion -- no host ever passes them.
    for (const std::string_view flag : kProtocolFlags) {
        if (CLI::Option* option = cmd->get_option_no_throw(std::string{flag}); option != nullptr) {
            option->group("");
        }
    }
    cmd->footer(
        "Speaks the Agent Client Protocol -- JSON-RPC, one message a line -- on stdin and stdout "
        "until stdin ends, for the host that started it; no port is opened. Each session is an "
        "apogee execute session in the host's project folder: its AGENTS.md or CLAUDE.md read, "
        "the folder attached, the coder persona, Apogee's tools -- each one that writes or "
        "reaches out asked about through the host's permission sheet. Every configured chat "
        "backend is offered as a model, the subscription CLIs included, the default first; "
        "manual, accept-edits and plan as modes (bypass with --allow-bypass). Every session is "
        "private: nothing of it is saved. The flags here reach every session, as execute's "
        "own, run in the project folder: a relative --attach or --image is found there. In "
        "Xcode 26.6 or later: Settings > Intelligence > Agents > Add an Agent, this "
        "binary's absolute path, and the argument acp.");
    cmd->callback([&context, flags, cmd]() { run_acp(context, *flags, *cmd); });
}

}  // namespace apogee::commands
