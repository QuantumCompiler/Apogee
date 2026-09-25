#include "commands/auth_cmd.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "commands/helpers.h"
#include "harness/paths.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee auth: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && text[start] == ' ') {
        ++start;
    }
    return text.substr(start);
}

std::string accepted_slots() {
    std::string out;
    for (const std::string_view type : harness::backend_type_names()) {
        if (const std::optional<harness::BackendType> parsed =
                harness::backend_type_from_string(type);
            parsed.has_value() && secrets::takes_api_key(*parsed)) {
            out += out.empty() ? "" : ", ";
            out += type;
        }
    }
    return out;
}

/// The slot type for `name`, or the refusal that says why not -- naming the
/// principle for a vendor-CLI type, and the accepted names for the rest.
harness::BackendType require_slot(const std::string& name) {
    if (const std::optional<harness::BackendType> type = secrets::slot_type(name)) {
        return *type;
    }
    if (const std::optional<harness::BackendType> any = harness::backend_type_from_string(name);
        any.has_value()) {
        fail("'" + name +
             "' takes no stored key: a vendor CLI authenticates itself and Apogee never "
             "stores, reads, or proxies its credentials; a local model needs none. Keys are "
             "kept for " +
             accepted_slots());
    }
    fail("unknown provider '" + name + "' (keys are kept for " + accepted_slots() + ")");
}

std::filesystem::path config_path_for(const RootContext& context) {
    try {
        return harness::resolve_config_path(context.config_path);
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

}  // namespace

AuthListing gather_auth_listing(const harness::Config& config,
                                const secrets::CredentialStore& store,
                                const secrets::EnvSnapshot& env) {
    AuthListing listing;
    listing.stored = store.list();
    listing.warning = store.warning();
    for (const auto& [name, entry] : config.backends) {
        if (!secrets::takes_api_key(entry.type)) {
            continue;
        }
        const secrets::KeyResolution resolution = secrets::resolve_api_key(entry, &store, env);
        AuthListing::BackendKey key;
        key.name = name;
        key.type = std::string{harness::to_string(entry.type)};
        key.source = resolution.source;
        key.variable = resolution.variable;
        listing.backends.push_back(std::move(key));
    }
    return listing;
}

std::string render_auth_listing(const AuthListing& listing) {
    std::ostringstream out;
    if (!listing.warning.empty()) {
        out << "warning: " << listing.warning << "\n";
    }
    if (listing.stored.empty()) {
        out << "no stored keys (add one with 'apogee auth add <provider>')\n";
    } else {
        out << "stored keys:\n";
        for (const secrets::CredentialMetadata& entry : listing.stored) {
            out << "  " << entry.provider << "  stored " << entry.stored_at << "\n";
        }
    }
    if (!listing.backends.empty()) {
        out << "configured backends:\n";
        for (const AuthListing::BackendKey& key : listing.backends) {
            out << "  " << key.name << " (" << key.type << ")  key from ";
            switch (key.source) {
                case secrets::KeySource::Config:
                    out << "config";
                    break;
                case secrets::KeySource::Store:
                    out << "the store";
                    break;
                case secrets::KeySource::Environment:
                    out << key.variable;
                    break;
                case secrets::KeySource::None:
                    out << "nowhere -- no key resolves";
                    break;
            }
            out << "\n";
        }
    }
    return out.str();
}

std::string_view AuthCommand::name() const noexcept {
    return "auth";
}

std::string_view AuthCommand::summary() const noexcept {
    return "Store, list, and clear provider API keys";
}

void AuthCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // --- add ---------------------------------------------------------------
    struct AddFlags {
        std::string provider;
        bool from_stdin = false;
        bool from_env = false;
    };

    auto add_flags = std::make_shared<AddFlags>();
    CLI::App* add =
        cmd->add_subcommand("add", "Store a provider's API key (never on the command line)");
    add->add_option("provider", add_flags->provider, "anthropic, openai, or google")
        ->type_name(words_value(secrets::slot_names()))
        ->required();
    add->add_flag("--stdin", add_flags->from_stdin, "Read the key from standard input");
    add->add_flag("--from-env", add_flags->from_env,
                  "Copy the key from the provider's conventional environment variable");
    add->callback([&context, add_flags]() {
        const harness::BackendType type = require_slot(add_flags->provider);
        const std::string slot = *secrets::slot_name(type);
        if (add_flags->from_stdin && add_flags->from_env) {
            fail("--stdin and --from-env are exclusive");
        }

        std::string key;
        if (add_flags->from_env) {
            const secrets::EnvSnapshot& env = secrets::EnvSnapshot::process();
            std::string variable;
            for (const std::string_view candidate : secrets::conventional_variables(type)) {
                if (!env.get(candidate).empty()) {
                    key = env.get(candidate);
                    variable = std::string{candidate};
                    break;
                }
            }
            if (key.empty()) {
                std::string names;
                for (const std::string_view candidate : secrets::conventional_variables(type)) {
                    names += names.empty() ? "" : " or ";
                    names += candidate;
                }
                fail(names + " is not set in this environment");
            }
            std::cerr << "apogee auth: copying " << variable << " into the store\n";
        } else if (add_flags->from_stdin) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                fail("no key on standard input");
            }
            key = trim(line);
        } else {
            const std::optional<std::string> line =
                platform::read_hidden_line("API key for " + slot + ": ");
            if (!line.has_value()) {
                fail("no key entered");
            }
            key = trim(*line);
        }
        if (key.empty()) {
            fail("the key was empty");
        }

        secrets::CredentialStore store{secrets::credentials_path(config_path_for(context))};
        try {
            store.put(slot, key);
        } catch (const std::runtime_error& e) {
            fail(e.what());
        }
        std::cout << "stored a key for " << slot << " in " << store.path().string() << "\n";
    });

    // --- list --------------------------------------------------------------
    CLI::App* list = cmd->add_subcommand("list",
                                         "Show stored keys (metadata only) and which "
                                         "source each configured backend uses");
    list->callback([&context]() {
        const std::filesystem::path config_path = config_path_for(context);
        harness::Config config;
        try {
            config = harness::load_config(config_path);
        } catch (const harness::ConfigError& e) {
            // The store is still listable without a config; the backends
            // column simply has nothing to say.
            std::cerr << "apogee auth: " << e.what() << "\n";
        }
        const secrets::CredentialStore store{secrets::credentials_path(config_path)};
        std::cout << render_auth_listing(
            gather_auth_listing(config, store, secrets::EnvSnapshot::process()));
    });

    // --- clear -------------------------------------------------------------
    auto clear_provider = std::make_shared<std::string>();
    CLI::App* clear = cmd->add_subcommand("clear", "Remove a stored key");
    clear->add_option("provider", *clear_provider, "anthropic, openai, or google")
        ->type_name(words_value(secrets::slot_names()))
        ->required();
    clear->callback([&context, clear_provider]() {
        const harness::BackendType type = require_slot(*clear_provider);
        const std::string slot = *secrets::slot_name(type);
        secrets::CredentialStore store{secrets::credentials_path(config_path_for(context))};
        try {
            if (!store.clear(slot)) {
                fail("no stored key for " + slot);
            }
        } catch (const std::runtime_error& e) {
            fail(e.what());
        }
        std::cout << "cleared the stored key for " << slot << "\n";
    });
}

}  // namespace apogee::commands
