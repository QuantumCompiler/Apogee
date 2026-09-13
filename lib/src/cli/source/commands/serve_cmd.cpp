#include "commands/serve_cmd.h"

#include <CLI/CLI.hpp>

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "backends/factory.h"
#include "commands/helpers.h"
#include "harness/config.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/paths.h"
#include "harness/roles.h"
#include "httpserver/handler.h"
#include "httpserver/mux.h"
#include "httpserver/serve.h"
#include "logger/operational.h"

namespace apogee::commands {
namespace {

struct ServeFlags {
    std::string model;
    bool all_backends = false;
    std::string bind = "127.0.0.1";
    int port = 8080;
    bool allow_remote = false;
    std::string rag;
    int rag_limit = 4;
    /// Kept so an explicit `--rag ""` can be told from no flag at all.
    CLI::Option* rag_option = nullptr;
    std::string retriever;
    std::string rerank;
    bool tools = false;
    bool search = false;
    bool preload = false;
    bool ignore_timeout = false;
    int session_ttl = 60;
    bool verbose = false;
};

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee serve: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

void note(const std::string& message) {
    std::cerr << "apogee serve: " << message << "\n";
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += out.empty() ? "" : ", ";
        out += item;
    }
    return out;
}

}  // namespace

std::string_view ServeCommand::name() const noexcept {
    return "serve";
}

std::string_view ServeCommand::summary() const noexcept {
    return "Serve the OpenAI-compatible HTTP API (server deployments only)";
}

void ServeCommand::bind(CLI::App& root, const RootContext& context) {
    auto flags = std::make_shared<ServeFlags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_option("-m,--model", flags->model,
                    "Serve this backend instead of models.default (a request may name "
                    "only served backends)");
    cmd->add_flag("--all-backends", flags->all_backends,
                  "Serve every configured API-billing and local backend");
    cmd->add_option("--bind", flags->bind,
                    "Host to listen on (default 127.0.0.1; a non-loopback host needs "
                    "--allow-remote)");
    cmd->add_option("--port", flags->port, "Port to listen on (default 8080; 0 picks a free one)");
    cmd->add_flag("--allow-remote", flags->allow_remote,
                  "Permit a non-loopback --bind: this is a server deployment");
    flags->rag_option =
        cmd->add_option("--rag", flags->rag,
                        "Retrieve context from this collection on every request (see 'apogee "
                        "embed'); \"\" switches off the config's auto_rag");
    cmd->add_option("--rag-limit", flags->rag_limit, "How many chunks to inject (default 4)");
    cmd->add_option("--retriever", flags->retriever,
                    "How to search the collection: lexical, vector, hybrid, or auto "
                    "(?retriever= overrides per request)")
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    cmd->add_option("--rerank", flags->rerank,
                    "Backend that reorders retrieved chunks, or off (?rerank= overrides)");
    cmd->add_flag("--tools", flags->tools,
                  "Let the model call tools server-side (fetch_url); clients see only the "
                  "final answer");
    cmd->add_flag("--search", flags->search,
                  "Enable the provider's own server-side web search, where it has one");
    cmd->add_flag("--preload", flags->preload,
                  "Load every served local model now rather than on the first request");
    cmd->add_flag("--ignore-timeout", flags->ignore_timeout,
                  "Keep local models resident: ignore idle_unload_seconds");
    cmd->add_option("--session-ttl", flags->session_ttl,
                    "Minutes a server-side session may sit idle before it is dropped from "
                    "memory (default 60; 0 keeps them)");
    cmd->add_flag("-v,--verbose", flags->verbose, "Log every request to stderr");

    cmd->callback([&context, flags]() {
        harness::Config config;
        try {
            config = harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }

        if (flags->ignore_timeout) {
            // An in-memory override for this process only -- no file is
            // touched. Serve is the one surface allowed to hold a resident
            // model, and a deployment that says so should not lose it to a
            // quiet hour.
            for (auto& [name, entry] : config.backends) {
                entry.idle_unload_seconds.reset();
            }
        }

        if (!flags->model.empty() && !names_a_configured_backend(config, flags->model)) {
            fail_user("no backend named '" + flags->model +
                      "' (configured: " + join(config.backend_names()) + ")");
        }
        if (!flags->rerank.empty() && flags->rerank != agentloop::kRerankOff &&
            config.find_backend(flags->rerank) == nullptr) {
            fail_user("--rerank: no backend named '" + flags->rerank + "'");
        }

        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.web_search = flags->search;
        const backends::BuildResult built = backends::build_providers(harness, build_options);

        // Which backends this server answers for. The default alone unless
        // told otherwise -- adding an entry to the config must not silently
        // expose it -- and never a vendor-CLI backend, by type.
        const std::string default_key = flags->model.empty()
                                            ? harness::resolve_chat_backend(config, {})
                                            : configured_backend_key(config, flags->model);
        httpserver::HandlerOptions options;
        options.default_backend = default_key;
        std::vector<std::string> skipped_cli;
        for (const backends::BackendStatus& status : built.statuses) {
            const harness::BackendConfig* entry = config.find_backend(status.name);
            const bool wanted = flags->all_backends || status.name == default_key;
            if (!wanted) {
                continue;
            }
            if (entry != nullptr && httpserver::is_vendor_cli(entry->type)) {
                skipped_cli.push_back(status.name);
                continue;
            }
            if (!status.constructed) {
                options.unavailable[status.name] = status.reason;
                continue;
            }
            options.served.push_back(status.name);
        }
        for (const std::string& name : skipped_cli) {
            note("skipping " + name +
                 ": a vendor-CLI backend runs on a personal subscription and is not served "
                 "over HTTP");
        }
        for (const auto& [name, reason] : options.unavailable) {
            note("skipping " + name + ": " + reason);
        }
        if (options.served.empty()) {
            std::string message = "no servable backend";
            if (default_key.empty()) {
                message += " -- set models.default, or pass -m";
            } else if (!skipped_cli.empty() && options.unavailable.empty()) {
                message += " -- '" + default_key +
                           "' is a vendor-CLI backend; serve dispatches to API-billing and "
                           "local backends only (pass -m, or --all-backends)";
            } else if (!built.skipped_summary().empty()) {
                message += " -- " + built.skipped_summary();
            }
            fail_user(message);
        }

        // Retrieval is fixed for the server's lifetime: the flag, else the
        // config's auto_rag -- the one shared decision every surface makes.
        const RagChoice rag_choice =
            choose_rag_collection(flags->rag_option->count() > 0, flags->rag, config.auto_rag);
        options.rag_collection = rag_choice.collection;
        options.rag_source = rag_choice.source;
        options.rag_limit = flags->rag_limit;
        options.retriever = flags->retriever == "auto" ? std::string{} : flags->retriever;
        options.rerank = flags->rerank;
        options.session_ttl = std::chrono::minutes{flags->session_ttl};

        agent::ToolRegistry registry;
        if (flags->tools) {
            registry = make_built_in_tools();
        }

        httpserver::Handler handler{harness, options, flags->tools ? &registry : nullptr};
        httpserver::Mux mux{handler};

        if (flags->preload) {
            for (const std::string& name : options.served) {
                try {
                    const bool loaded =
                        harness.preload_model(name, [&](const harness::StatusEvent& event) {
                            if (event.type == harness::StatusEvent::Type::ModelReady) {
                                note("preloaded " + name);
                            }
                        });
                    if (!loaded && flags->verbose) {
                        note(name + " has nothing to preload");
                    }
                } catch (const harness::HarnessError& e) {
                    note("preload failed for " + name + ": " + e.what());
                }
            }
        }

        httpserver::ServeOptions serve_options;
        serve_options.bind.host = flags->bind;
        serve_options.bind.port = flags->port;
        serve_options.bind.allow_remote = flags->allow_remote;
        serve_options.session_ttl = std::chrono::minutes{flags->session_ttl};
        if (const std::string refusal = httpserver::bind_refusal(serve_options.bind);
            !refusal.empty()) {
            fail_user(refusal);
        }
        serve_options.on_listening = [](const std::string& host, int port) {
            std::cerr << "apogee serve: listening on http://" << host << ":" << port << "\n";
        };
        if (flags->verbose) {
            serve_options.on_log = [](std::string_view line) {
                std::cerr << "apogee serve: " << line << "\n";
            };
        }

        std::string summary = "serving " + join(options.served);
        if (!default_key.empty()) {
            summary += " (default " + default_key + ")";
        }
        summary += flags->tools ? " -- tools: fetch_url" : " -- tools: none";
        if (rag_choice.active()) {
            summary += " -- rag: " + rag_choice.collection +
                       (rag_choice.source == RagSource::Config ? " (auto_rag)" : "");
        } else {
            summary += " -- rag: off";
        }
        summary += flags->session_ttl > 0 ? " -- sessions idle out after " +
                                                std::to_string(flags->session_ttl) + " min"
                                          : " -- sessions never idle out";
        note(summary);
        if (!httpserver::is_loopback_host(flags->bind)) {
            note(
                "bound to a non-loopback host: the inference plane is unauthenticated, so "
                "put it behind your own access control");
        }
        logger::log(logger::Level::Info, "serve", summary);

        try {
            httpserver::run_server(mux, handler, serve_options);
        } catch (const std::runtime_error& e) {
            logger::log(logger::Level::Error, "serve", e.what());
            fail_user(e.what());
        }
        logger::log(logger::Level::Info, "serve", "stopped");
        note("stopped");
    });
}

}  // namespace apogee::commands
