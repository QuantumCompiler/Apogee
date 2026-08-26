#include "commands/config_cmd.h"

#include <CLI/CLI.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/paths.h"

namespace apogee::commands {
namespace {

using harness::BackendConfig;
using harness::Config;
using harness::ConfigEditError;
using harness::ConfigError;

/// Prints `message` to stderr and returns the process exit code to use.
///
/// Config failures are user errors, not crashes: a wrong key name or a missing
/// file should read as a sentence, never as a stack trace. SPEC.md -> "fail
/// loud on install/parity, degrade gracefully at runtime".
[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee config: " << message << "\n";
    throw CLI::RuntimeError(1);
}

std::filesystem::path config_path_for(const RootContext& context) {
    try {
        return harness::resolve_config_path(context.config_path);
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

/// Applies a pure transform to the config file, reporting either failure mode
/// in the user's terms.
template <typename Transform>
void apply_edit(const std::filesystem::path& path, Transform&& transform) {
    try {
        harness::edit_config_file(path, std::forward<Transform>(transform));
    } catch (const ConfigEditError& e) {
        fail(e.what());
    } catch (const ConfigError& e) {
        fail(e.what());
    }
}

/// Confirms a role pointer names a real backend before writing it.
///
/// Without this, `set-default typo` writes a config that loads fine and then
/// fails on the next request with a far less obvious message.
void require_backend_exists(const std::filesystem::path& path, const std::string& name) {
    try {
        const Config config = harness::load_config(path);
        if (config.find_backend(name) == nullptr) {
            std::string known;
            for (const std::string& candidate : config.backend_names()) {
                known += known.empty() ? "" : ", ";
                known += candidate;
            }
            fail("no backend named '" + name + "' in this config" +
                 (known.empty() ? " (it has no backends yet -- add one with 'apogee config "
                                  "add-backend')"
                                : " (known backends: " + known + ")"));
        }
    } catch (const ConfigError& e) {
        fail(e.what());
    }
}

/// Renders one value for `config get`.
std::string render(const std::string& value) {
    return value;
}

/// api_key values are redacted unless the user explicitly asks for them.
///
/// `config get` output lands in terminals, screenshots, and shell history.
/// SPEC.md keeps secrets out of logs and off HTTP; printing one on a bare
/// `get` would be the same mistake with a shorter path.
std::string render_secret(const std::string& value, bool reveal) {
    if (value.empty()) {
        return {};
    }
    return reveal ? value : "<redacted -- pass --reveal to print it>";
}

/// Resolves a dotted key against a loaded config.
/// Returns nullopt when the key does not name anything.
std::optional<std::string> lookup(const Config& config, std::string_view key, bool reveal) {
    if (key == "status_mode") {
        return std::string{harness::to_string(config.status_mode)};
    }
    if (key == "color") {
        return config.color ? "true" : "false";
    }
    if (key == "models.default") {
        return render(config.models.default_backend);
    }
    if (key == "models.default_embedding") {
        return render(config.models.default_embedding);
    }
    if (key == "models.default_extraction") {
        return render(config.models.default_extraction);
    }
    if (key == "paths.gguf_dir") {
        return render(config.paths.gguf_dir);
    }
    if (key == "paths.hf_dir") {
        return render(config.paths.hf_dir);
    }
    if (key == "paths.mcp_dir") {
        return render(config.paths.mcp_dir);
    }
    if (key == "paths.embeddings_dir") {
        return render(config.paths.embeddings_dir);
    }
    if (key == "backends") {
        std::string out;
        for (const std::string& name : config.backend_names()) {
            out += out.empty() ? "" : "\n";
            out += name;
        }
        return out;
    }

    constexpr std::string_view kBackendsPrefix = "backends.";
    if (key.substr(0, kBackendsPrefix.size()) != kBackendsPrefix) {
        return std::nullopt;
    }

    const std::string_view rest = key.substr(kBackendsPrefix.size());
    const std::size_t dot = rest.rfind('.');
    if (dot == std::string_view::npos) {
        // `backends.<name>` -- summarize the entry as its type.
        const BackendConfig* backend = config.find_backend(rest);
        return backend == nullptr
                   ? std::nullopt
                   : std::optional<std::string>{std::string{harness::to_string(backend->type)}};
    }

    const std::string_view backend_name = rest.substr(0, dot);
    const std::string_view field = rest.substr(dot + 1);
    const BackendConfig* backend = config.find_backend(backend_name);
    if (backend == nullptr) {
        return std::nullopt;
    }

    if (field == "type") {
        return std::string{harness::to_string(backend->type)};
    }
    if (field == "api_key") {
        return render_secret(backend->api_key, reveal);
    }
    if (field == "model") {
        return render(backend->model);
    }
    if (field == "model_path") {
        return render(backend->model_path);
    }
    if (field == "system_prompt") {
        return render(backend->system_prompt);
    }
    if (field == "context_size") {
        return backend->context_size.has_value() ? std::to_string(*backend->context_size)
                                                 : std::string{};
    }
    if (field == "max_tokens") {
        return backend->max_tokens.has_value() ? std::to_string(*backend->max_tokens)
                                               : std::string{};
    }
    if (field == "temperature") {
        return backend->temperature.has_value() ? std::to_string(*backend->temperature)
                                                : std::string{};
    }
    return std::nullopt;
}

/// Flags for `add-backend`, kept alive for the life of the app so CLI11's
/// callbacks can read them.
struct AddBackendFlags {
    std::string name;
    std::string type;
    std::string api_key;
    std::string model;
    std::string model_path;
    std::string system_prompt;
    std::int64_t context_size = 0;
    std::int64_t max_tokens = 0;
    double temperature = 0.0;
    bool force = false;

    CLI::Option* context_size_option = nullptr;
    CLI::Option* max_tokens_option = nullptr;
    CLI::Option* temperature_option = nullptr;
};

void bind_init(CLI::App& parent, const RootContext& context) {
    auto force = std::make_shared<bool>(false);
    CLI::App* cmd = parent.add_subcommand("init", "Write a starter config file");
    cmd->add_flag("-f,--force", *force, "Overwrite an existing config file");
    cmd->callback([&context, force]() {
        const std::filesystem::path path = config_path_for(context);
        try {
            harness::save_config_template(path, *force);
        } catch (const ConfigEditError& e) {
            fail(e.what());
        }
        std::cout << "wrote " << path.string() << "\n";
    });
}

void bind_path(CLI::App& parent, const RootContext& context) {
    CLI::App* cmd =
        parent.add_subcommand("path", "Print the config file path this invocation would use");
    cmd->callback([&context]() { std::cout << config_path_for(context).string() << "\n"; });
}

void bind_add_backend(CLI::App& parent, const RootContext& context) {
    auto flags = std::make_shared<AddBackendFlags>();

    CLI::App* cmd = parent.add_subcommand("add-backend", "Add a backend entry");
    cmd->add_option("name", flags->name, "Name for the new backend")->required();

    std::vector<std::string> types;
    for (const std::string_view type : harness::backend_type_names()) {
        types.emplace_back(type);
    }
    cmd->add_option("-t,--type", flags->type, "Backend type")
        ->required()
        ->check(CLI::IsMember(types));

    cmd->add_option("--api-key", flags->api_key,
                    "API key. Prefer a ${ENV_VAR} reference, which is stored literally and "
                    "expanded on read");
    cmd->add_option("--model", flags->model, "Model name");
    cmd->add_option("--model-path", flags->model_path, "Path to a local model file");
    cmd->add_option("--system-prompt", flags->system_prompt, "Default system prompt");
    flags->context_size_option =
        cmd->add_option("--context-size", flags->context_size, "Context window, in tokens");
    flags->max_tokens_option =
        cmd->add_option("--max-tokens", flags->max_tokens, "Maximum tokens to generate");
    flags->temperature_option =
        cmd->add_option("--temperature", flags->temperature, "Sampling temperature");
    cmd->add_flag("-f,--force", flags->force, "Replace an existing entry with this name");

    cmd->callback([&context, flags]() {
        const std::filesystem::path path = config_path_for(context);

        BackendConfig backend;
        const std::optional<harness::BackendType> type =
            harness::backend_type_from_string(flags->type);
        if (!type.has_value()) {
            fail("unknown backend type '" + flags->type + "'");
        }
        backend.type = *type;
        backend.api_key = flags->api_key;
        backend.model = flags->model;
        backend.model_path = flags->model_path;
        backend.system_prompt = flags->system_prompt;
        if (flags->context_size_option->count() > 0) {
            backend.context_size = flags->context_size;
        }
        if (flags->max_tokens_option->count() > 0) {
            backend.max_tokens = flags->max_tokens;
        }
        if (flags->temperature_option->count() > 0) {
            backend.temperature = flags->temperature;
        }

        apply_edit(path, [flags, &backend](std::string_view content) {
            return harness::append_backend(content, flags->name, backend, flags->force);
        });
        std::cout << "added backend '" << flags->name << "' to " << path.string() << "\n";
    });
}

void bind_delete_backend(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand("delete-backend", "Remove a backend entry");
    cmd->add_option("name", *name, "Backend to remove")->required();
    cmd->callback([&context, name]() {
        const std::filesystem::path path = config_path_for(context);
        apply_edit(path, [name](std::string_view content) {
            return harness::delete_backend(content, *name);
        });
        std::cout << "removed backend '" << *name << "' from " << path.string() << "\n";
    });
}

/// The three `set-default*` commands differ only in which key they write.
void bind_set_role(CLI::App& parent, const RootContext& context, const std::string& command_name,
                   const std::string& field, const std::string& description) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand(command_name, description);
    cmd->add_option("name", *name, "Backend to point this role at")->required();
    cmd->callback([&context, name, field]() {
        const std::filesystem::path path = config_path_for(context);
        require_backend_exists(path, *name);
        apply_edit(path, [name, field](std::string_view content) {
            return harness::set_models_role(content, field, *name);
        });
        std::cout << "models." << field << " = " << *name << "\n";
    });
}

void bind_get(CLI::App& parent, const RootContext& context) {
    auto key = std::make_shared<std::string>();
    auto reveal = std::make_shared<bool>(false);
    CLI::App* cmd = parent.add_subcommand("get", "Print a config value by dotted key");
    cmd->add_option("key", *key,
                    "Dotted key, e.g. models.default or backends.claude.model. 'backends' "
                    "lists every backend name")
        ->required();
    cmd->add_flag("--reveal", *reveal, "Print api_key values instead of redacting them");
    cmd->callback([&context, key, reveal]() {
        const std::filesystem::path path = config_path_for(context);
        Config config;
        try {
            config = harness::load_config(path);
        } catch (const ConfigError& e) {
            fail(e.what());
        }
        const std::optional<std::string> value = lookup(config, *key, *reveal);
        if (!value.has_value()) {
            fail("no such config key: '" + *key + "'");
        }
        std::cout << *value << "\n";
    });
}

void bind_format(CLI::App& parent, const RootContext& context) {
    CLI::App* cmd = parent.add_subcommand(
        "format", "Tidy whitespace in the config file, preserving all comments and key order");
    cmd->callback([&context]() {
        const std::filesystem::path path = config_path_for(context);
        apply_edit(path, [](std::string_view content) { return harness::format_config(content); });
        std::cout << "formatted " << path.string() << "\n";
    });
}

}  // namespace

std::string_view ConfigCommand::name() const noexcept {
    return "config";
}

std::string_view ConfigCommand::summary() const noexcept {
    return "Inspect and edit the config file";
}

void ConfigCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    bind_init(*cmd, context);
    bind_path(*cmd, context);
    bind_add_backend(*cmd, context);
    bind_delete_backend(*cmd, context);
    bind_set_role(*cmd, context, "set-default", "default", "Set the default backend");
    bind_set_role(*cmd, context, "set-default-embedding", "default_embedding",
                  "Set the backend used for embeddings");
    bind_set_role(*cmd, context, "set-default-extraction", "default_extraction",
                  "Set the backend used for structured extraction");
    bind_get(*cmd, context);
    bind_format(*cmd, context);
}

}  // namespace apogee::commands
