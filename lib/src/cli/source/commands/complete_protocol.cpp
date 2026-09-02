#include "commands/complete_protocol.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <memory>

#include "harness/paths.h"

namespace apogee::commands {
namespace {

/// Flags whose value is a backend name. Completing these is the whole reason
/// the protocol is dynamic rather than a generated file.
[[nodiscard]] bool takes_a_backend(std::string_view word) noexcept {
    return word == "-m" || word == "--model";
}

/// Subcommands whose first positional argument is a backend name.
[[nodiscard]] bool positional_is_backend(const std::vector<std::string>& words) {
    if (words.size() < 2) {
        return false;
    }
    if (words[0] != "config") {
        return false;
    }
    return words[1] == "set-default" || words[1] == "set-default-embedding" ||
           words[1] == "set-default-extraction" || words[1] == "delete-backend";
}

}  // namespace

std::vector<std::string> filter_prefix(const std::vector<std::string>& candidates,
                                       std::string_view prefix) {
    if (prefix.empty()) {
        return candidates;
    }
    std::vector<std::string> matched;
    for (const std::string& candidate : candidates) {
        if (candidate.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), candidate.begin())) {
            matched.push_back(candidate);
        }
    }
    return matched;
}

std::vector<std::string> completion_candidates(const CompletionRequest& request,
                                               const harness::Config& config,
                                               const std::vector<std::string>& commands) {
    // A flag expecting a backend wins over everything: the word before the
    // cursor decides, not the subcommand.
    if (!request.words.empty() && takes_a_backend(request.words.back())) {
        return filter_prefix(config.backend_names(), request.current);
    }

    if (positional_is_backend(request.words)) {
        return filter_prefix(config.backend_names(), request.current);
    }

    // Nothing typed yet, or a partial subcommand: offer the subcommands.
    if (request.words.empty()) {
        return filter_prefix(commands, request.current);
    }

    // A `config` subcommand's own verbs.
    if (request.words.size() == 1 && request.words[0] == "config") {
        const std::vector<std::string> verbs{
            "init",   "path",        "add-backend",           "delete-backend",
            "get",    "set-default", "set-default-embedding", "set-default-extraction",
            "format",
        };
        return filter_prefix(verbs, request.current);
    }

    return {};
}

std::string_view CompleteProtocolCommand::name() const noexcept {
    return "__complete";
}

std::string_view CompleteProtocolCommand::summary() const noexcept {
    return "Shell completion protocol (internal)";
}

void CompleteProtocolCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->group("");  // hidden from --help: it is a protocol, not a feature

    // Everything after `__complete` is DATA, not arguments. Without this the
    // parser claims them: a line ending `--model ""` -- the exact case this
    // protocol exists for -- has `--model` eaten as an unknown option, and the
    // completion silently returns nothing. `prefix_command` stops parsing and
    // hands the rest over verbatim.
    cmd->prefix_command();

    cmd->callback([&context, cmd]() {
        const std::vector<std::string> raw = cmd->remaining(true);

        CompletionRequest request;
        const std::vector<std::string>* words = &raw;
        if (!words->empty()) {
            // The last word is what the user is typing; everything before it is
            // context. A trailing empty argument (the shell's way of saying
            // "the cursor is at a fresh word") lands here as an empty `current`,
            // which is exactly right.
            request.current = words->back();
            request.words.assign(words->begin(), words->end() - 1);
        }

        harness::Config config;
        try {
            config = harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const std::exception&) {
            // Silent. A broken or absent config makes completion unhelpful; it
            // must never print a diagnostic into the user's command line.
        }

        std::vector<std::string> commands;
        for (const std::string& name : context.command_names) {
            if (name.rfind("__", 0) == 0) {
                continue;  // never offer the protocol verb itself
            }
            commands.push_back(name);
        }

        for (const std::string& candidate : completion_candidates(request, config, commands)) {
            std::cout << candidate << "\n";
        }
    });
}

}  // namespace apogee::commands
