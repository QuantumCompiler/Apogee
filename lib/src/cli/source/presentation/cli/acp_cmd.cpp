#include "cli/acp_cmd.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "acp/host_servers.h"
#include "acp/server.h"
#include "cli/chat_history.h"
#include "cli/chat_session.h"
#include "cli/helpers.h"
#include "cli/tui_child.h"
#include "contracts/paths.h"
#include "harness/roles.h"
#include "modelstore/mlx_info.h"
#include "operations/acp_registration.h"
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
        return "the host reopens a saved chat itself, with session/load or session/resume, by "
               "the id chats list gives it";
    }
    if (flag == "--private") {
        return "every ACP session is saved as a chat, which chats delete removes; a private "
               "mode is not offered here";
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

/// The flags the protocol owns, refused in the command's words when given.
void refuse_protocol_flags(const CLI::App& command) {
    for (const std::string_view flag : kProtocolFlags) {
        if (const CLI::Option* option = command.get_option_no_throw(std::string{flag});
            option != nullptr && option->count() > 0) {
            fail(std::string{flag} + " is refused: " + protocol_reason(flag));
        }
    }
}

/// This variable's value, or empty when it is unset.
[[nodiscard]] std::string environment_value(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
}

/// `acp registration` (56d): the fields Xcode's Add an Agent sheet takes,
/// for this binary and the root it was run under -- with the flags `acp`
/// itself was given, which every session then has.
void print_registration(const RootContext& context, const CLI::App& acp, ReadFormat format) {
    refuse_protocol_flags(acp);
    operations::AcpRegistrationInputs inputs;
    inputs.executable = platform::executable_path();
    inputs.channel = harness::baked_channel();
    inputs.root_flag = harness::root_flag();
    inputs.apogee_home = environment_value(harness::kHomeEnvVar);
    inputs.apogee_config = environment_value("APOGEE_CONFIG");
    // `--config` as typed: the variable's value is Environment's.
    if (!context.config_path.empty() && context.config_path != inputs.apogee_config) {
        inputs.config_flag = context.config_path;
    }
    inputs.path = environment_value("PATH");
    inputs.acp_flags = forwarded_flags(acp);
    operations::AcpRegistration registration;
    try {
        registration = operations::acp_registration(inputs);
    } catch (const std::exception& e) {
        fail(e.what());
    }
    if (format == ReadFormat::Json) {
        write_document(std::cout, operations::acp_registration_document(registration));
        return;
    }
    std::cout << operations::render_acp_registration(registration);
}

void run_acp(const RootContext& context, const SessionFlags& flags, const CLI::App& command) {
    refuse_protocol_flags(command);
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
    // The one session store (56c): what the host lists and reopens.
    setup.chats = acp_saved_chats();

    // Execute's own session, in machine mode, with what this line carries --
    // saved as an ordinary chat (56c), never --private.
    std::vector<std::string> words{"execute",         "--input-format", "stream-json",
                                   "--output-format", "stream-json",    "--tools"};
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

std::vector<acp::SavedEntry> replay_entries(const logger::Session& session) {
    std::vector<acp::SavedEntry> entries;
    for (const logger::SavedTurn& turn : logger::saved_turns(session)) {
        acp::SavedEntry entry;
        if (turn.compacted.has_value()) {
            entry.compacted = *turn.compacted;
        } else {
            // `chats show`'s `You:` line, and its `attached:` line under it.
            entry.user = turn.user;
            if (!turn.attachments.empty()) {
                entry.user += (entry.user.empty() ? "" : "\n") + std::string{"attached: "} +
                              attachments_line(turn.attachments);
            }
        }
        for (const logger::SavedTool& tool : turn.tools) {
            entry.tools.push_back(
                acp::SavedEntry::Tool{.name = tool.name, .summary = tool.summary});
        }
        entry.answer = turn.answer;
        entries.push_back(std::move(entry));
    }
    return entries;
}

acp::SavedChats acp_saved_chats() {
    return acp::SavedChats{
        .list =
            [] {
                // `chats list`'s own read, newest first.
                std::vector<acp::ListedChat> chats;
                for (const logger::Session& session : logger::list_sessions()) {
                    chats.push_back(acp::ListedChat{.id = session.chat_id,
                                                    .name = session.display_name(),
                                                    .updated_at = session.updated_at,
                                                    .origin = session.origin,
                                                    .cwd = session.cwd});
                }
                return chats;
            },
        .read = [](const std::string& id, std::string& error) -> std::optional<acp::SavedChat> {
            // Read as `chats show` reads it: the loader a resume reads with.
            try {
                const logger::Session session = logger::load(id, {}).session;
                return acp::SavedChat{.id = session.chat_id, .entries = replay_entries(session)};
            } catch (const std::exception& e) {
                error = e.what();
                return std::nullopt;
            }
        }};
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
        "saved as a chat that names the host that began it (chats list, chats show), and the "
        "host can list, reopen and resume it. The flags here reach every session, as execute's "
        "own, run in the project folder: a relative --attach or --image is found there. In "
        "Xcode 26.6 or later: Settings > Intelligence > Agents > Add an Agent, this "
        "binary's absolute path, and the argument acp -- apogee acp registration prints the "
        "fields.");

    // The fields Xcode's sheet takes (56d): a subcommand, so a read's
    // `--output-format` stays off the protocol verb.
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    CLI::App* registration = cmd->add_subcommand(
        "registration",
        "Print the fields an ACP host's Add an Agent takes -- Xcode 26.6 or later's Settings > "
        "Intelligence > Agents -- for this binary and this install");
    add_read_format(registration, format);
    registration->footer(
        "Reads nothing of Xcode's and writes nothing: you paste the fields into Xcode's own "
        "sheet. The Executable is this binary's absolute path; a root flag or --config given "
        "here goes into Arguments ahead of acp, and APOGEE_HOME, APOGEE_CONFIG and PATH, when "
        "set, into Environment -- so the agent Xcode starts opens this install and finds the "
        "subscription CLIs. Flags given to acp before registration follow acp in Arguments.");
    registration->callback(
        [&context, cmd, format]() { print_registration(context, *cmd, *format); });
    cmd->callback([&context, flags, cmd, registration]() {
        if (registration->parsed()) {
            return;  // `acp registration` answered
        }
        run_acp(context, *flags, *cmd);
    });
}

}  // namespace apogee::commands
