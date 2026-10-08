#include "cli/helpers.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "agent/fetch_url.h"
#include "agent/web_search.h"
#include "agentloop/embed_func.h"
#include "agentloop/graph_context.h"
#include "agentloop/media.h"
#include "cli/command.h"
#include "cli/embed.h"
#include "cli/tool_vectors.h"
#include "contracts/utf8.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "platform/platform.h"
#include "transport/http_client.h"
#include "version/version.h"

namespace apogee::commands {

agent::ToolRegistry apply_tool_policy(const agent::ToolRegistry& registry,
                                      harness::AgentToolPolicy policy) {
    if (policy == harness::AgentToolPolicy::All) {
        return registry;
    }
    agent::ToolRegistry filtered;
    if (policy == harness::AgentToolPolicy::None) {
        return filtered;
    }
    filtered.set_environment(registry.environment_source());
    for (const std::string& name : registry.names()) {
        const agent::Tool* tool = registry.find(name);
        if (tool != nullptr && !tool->writes) {
            filtered.add(*tool);
        }
    }
    return filtered;
}

std::vector<std::string> activate_suite(harness::Harness& harness, harness::Config& config,
                                        const std::string& suite,
                                        const backends::BuildOptions& options) {
    const std::vector<std::string> names = config.backend_names();
    std::vector<std::optional<std::int64_t>> windows;
    windows.reserve(names.size());
    for (const std::string& name : names) {
        windows.push_back(harness::suite_pins(config, name).context_size);
    }
    config.models.default_suite = suite;
    harness.set_active_suite(suite);
    std::vector<std::string> repinned;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (harness::suite_pins(config, names[i]).context_size != windows[i]) {
            repinned.push_back(names[i]);
        }
    }
    std::vector<std::string> said;
    if (repinned.empty()) {
        return said;
    }
    for (const backends::BackendStatus& status :
         backends::rebuild_providers(harness, repinned, options).statuses) {
        if (!status.constructed) {
            said.push_back(status.name +
                           " keeps its old window -- it could not be rebuilt: " + status.reason);
        }
    }
    return said;
}

std::string known_suites(const harness::Config& config) {
    std::string out;
    for (const std::string& name : config.suite_names()) {
        out += out.empty() ? "" : ", ";
        out += name;
    }
    return out.empty() ? "none is configured -- 'apogee config add-suite'" : "configured: " + out;
}

std::string select_suite(harness::Config& config, std::string_view suite) {
    if (suite == harness::kSuiteOff) {
        config.models.default_suite.clear();
        return {};
    }
    const auto it = config.suites.find(suite);
    if (it == config.suites.end()) {
        return "no suite named '" + std::string{suite} + "' (" + known_suites(config) + ")";
    }
    // The name as the file spells it, so every line that names the suite
    // afterwards spells it one way.
    config.models.default_suite = it->first;
    return {};
}

agent::ToolRegistry apply_toolset(const agent::ToolRegistry& registry,
                                  const std::vector<std::string>& toolset) {
    agent::ToolRegistry narrowed;
    narrowed.set_environment(registry.environment_source());
    for (const std::string& name : registry.names()) {
        const agent::Tool* tool = registry.find(name);
        if (tool != nullptr &&
            std::ranges::find(toolset, tools::toolset_of(name)) != toolset.end()) {
            narrowed.add(*tool);
        }
    }
    return narrowed;
}

agent::ToolRegistry pin_toolset(const agent::ToolRegistry& registry, const harness::Config& config,
                                std::string_view backend) {
    const std::optional<std::vector<std::string>> toolset =
        harness::suite_pins(config, backend).toolset;
    return toolset.has_value() ? apply_toolset(registry, *toolset) : registry;
}

MeteredProbe provider_metered_probe(std::filesystem::path config_path) {
    return [config_path = std::move(config_path)](const harness::Config& config,
                                                  std::string_view backend) {
        MeteredAnswer answer;
        const auto entry = config.backends.find(backend);
        if (entry == config.backends.end()) {
            answer.unknown = "no backend of that name is configured";
            return answer;
        }
        backends::BuildOptions options;
        options.config_path = config_path;
        std::string reason;
        const std::shared_ptr<harness::LLMProvider> provider = backends::make_provider(
            entry->first, harness::backend_as_run(config, entry->first), reason, options);
        if (provider == nullptr) {
            answer.unknown = reason.empty() ? "it could not be built here" : reason;
            return answer;
        }
        answer.metered = provider->generation_is_metered();
        return answer;
    };
}

agent::UrlFetcher make_http_fetcher(std::shared_ptr<backends::HttpClient> client) {
    return [client = std::move(client)](std::string_view url) {
        agent::FetchResult result;
        backends::HttpRequest request;
        request.method = "GET";
        request.url = std::string{url};
        request.timeout = std::chrono::seconds{30};
        // One hop at a time: the tool follows a redirect only once its host
        // has been through the gate.
        request.follow_redirects = false;
        request.max_body_bytes = kFetchMaxBodyBytes;
        try {
            const backends::HttpResponse response = client->send(request, {}, {});
            if (response.body_limit_exceeded) {
                result.error = "the response is larger than " +
                               std::to_string(kFetchMaxBodyBytes / (1024 * 1024)) +
                               " MB, the most fetch_url reads; nothing of it was kept";
                return result;
            }
            result.status = response.status;
            result.body = response.body;
            result.location = response.location;
            result.content_type = response.content_type;
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        return result;
    };
}

agent::ToolRegistry make_built_in_tools(const BuiltInToolOptions& options) {
    agent::ToolRegistry registry;
    if (options.policy == harness::AgentToolPolicy::None) {
        // Nothing to register and no server to dial: a `none` agent costs
        // no child process either.
        return registry;
    }

    const auto client =
        std::make_shared<backends::HttpClient>(std::make_unique<backends::CurlTransport>());
    registry.add(agent::make_fetch_url_tool(make_http_fetcher(client)));

    // Web search only where the config names an instance (25e): a model is
    // never offered a tool that can only fail. A section that cannot be used
    // registers nothing, and `check` says why.
    if (options.config != nullptr) {
        std::string problem;
        if (const std::optional<agent::SearchInstance> search =
                agent::search_instance(options.config->tools.search, problem);
            search.has_value()) {
            registry.add(agent::make_web_search_tool(
                agent::make_searxng_provider(search->base, make_http_fetcher(client)),
                search->base.host, search->results));
        }
    }

    tools::ToolsetOptions toolsets;
    toolsets.harness = options.harness;
    toolsets.config = options.config;
    toolsets.review = options.review;
    toolsets.live_review = options.live_review;
    if (options.config != nullptr) {
        toolsets.fs_root = harness::expand_env(options.config->tools.fs_root);
        toolsets.disabled = options.config->tools.disabled;
    }
    tools::register_native_toolsets(registry, toolsets);

    // Third-party servers last, so a namespaced name can never shadow a
    // native one -- the registry refuses duplicates either way.
    if (options.mcp != nullptr && options.config != nullptr &&
        !options.config->mcp_servers.empty()) {
        std::vector<mcp::ServerSpec> specs;
        if (options.mcp_servers.has_value()) {
            // Only what the agent named, in the config's own spelling.
            for (const std::string& wanted : *options.mcp_servers) {
                const auto it = options.config->mcp_servers.find(wanted);
                if (it == options.config->mcp_servers.end()) {
                    if (options.mcp_status) {
                        options.mcp_status("[mcp] warning: no server named '" + wanted +
                                           "' in mcp_servers (skipped)");
                    }
                    continue;
                }
                specs.push_back(mcp::ServerSpec{it->first, it->second.command, it->second.args,
                                                it->second.env, it->second.enabled});
            }
        } else {
            for (const auto& [name, server] : options.config->mcp_servers) {
                specs.push_back(
                    mcp::ServerSpec{name, server.command, server.args, server.env, server.enabled});
            }
        }
        mcp::RegistryOptions mcp_options;
        mcp_options.status = options.mcp_status;
        mcp_options.server_log = options.mcp_server_log;
        mcp_options.spawn = options.mcp_spawn;
        mcp_options.client_version = std::string{version::semantic()};
        if (!specs.empty()) {
            options.mcp->connect_all(specs, mcp_options);
            options.mcp->register_into(registry);
        }
    }

    // The policy last, over everything registered -- native, fetch_url and
    // MCP alike -- so what the loop advertises IS the policy.
    return apply_tool_policy(registry, options.policy);
}

std::shared_ptr<const agentloop::ToolRanker> make_tool_ranker(
    const harness::Harness& harness, const harness::Config& config,
    const agent::ToolRegistry& registry, const std::filesystem::path& cache_file,
    std::string& ranked_by) {
    std::optional<agentloop::ToolEmbedding> embedding;
    std::string reason;
    const std::optional<agentloop::Embedder> embedder =
        agentloop::resolve_embedder(harness, config, {}, reason);
    if (!embedder.has_value()) {
        ranked_by = "words (no embedding model" + (reason.empty() ? "" : ": " + reason) + ")";
    } else if (embedder->metered) {
        // One call a turn, on Apogee's initiative: never on a billed one.
        ranked_by = "words (" + embedder->backend + " bills each call)";
    } else {
        auto cache = std::make_shared<ToolVectorCache>(cache_file);
        embedding = agentloop::ToolEmbedding{
            .embed = embedder->embed,
            .model = embedder->backend + "/" + embedder->model,
            .load = [cache](const std::string& key) { return cache->load(key); },
            .store = [cache](const agentloop::ToolVectors& made) { cache->store(made); }};
        ranked_by = "meaning, by " + embedder->backend;
    }
    return std::make_shared<const agentloop::ToolRanker>(
        registry.definitions(), registry.definition_hashes(), std::move(embedding));
}

std::unique_ptr<agentloop::ToolSelection> make_tool_selection(
    const harness::Harness& harness, const harness::Config& config,
    const agent::ToolRegistry& registry, const std::filesystem::path& config_path,
    std::string& ranked_by) {
    if (registry.size() <= agentloop::kToolSelectionThreshold) {
        // Offered whole: no ranker, no embedder resolved, nothing loaded.
        return nullptr;
    }
    return std::make_unique<agentloop::ToolSelection>(
        make_tool_ranker(harness, config, registry, tool_vector_cache_path(config_path), ranked_by),
        registry.size());
}

bool is_base_model(const harness::Harness& harness, std::string_view model) {
    return harness.model_behavior_for(model).base_model;
}

std::string base_model_tools_note(std::string_view model) {
    return "tools off: " + std::string{model} +
           " is a base model, with no tool format to call them in -- it answers without them";
}

std::function<void(std::string_view)> mcp_status_line(StatusLine& status) {
    return [&status](std::string_view line) {
        if (line.find("warning") != std::string_view::npos) {
            status.print_line(line);
        } else {
            status.set(line);
        }
    };
}

std::string read_stdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

void add_read_format(CLI::App* command, const std::shared_ptr<ReadFormat>& format) {
    command
        ->add_option_function<std::string>(
            "--output-format",
            [format](const std::string& value) {
                const std::optional<ReadFormat> parsed = read_format_from_string(value);
                if (!parsed.has_value()) {
                    throw CLI::ValidationError("--output-format", "expected 'text' or 'json'");
                }
                *format = *parsed;
            },
            "Output format: text (default) or json -- one JSON document of the same facts")
        ->type_name(words_value(read_format_names()));
}

bool stdin_is_piped() {
    // Everything here reads stdin through std::cin, and a std::cin given
    // another buffer is not the terminal, whatever descriptor 0 says: the
    // platform asks both (2026-09-19, the rule first written here).
    return !platform::is_terminal(platform::StandardStream::In);
}

std::string base64_encode(std::string_view bytes) {
    return agentloop::base64_encode(bytes);
}

std::string image_media_type(const std::filesystem::path& path) {
    return agentloop::image_media_type(path);
}

harness::ContentPart load_image_part(const std::filesystem::path& path) {
    const std::string media_type = image_media_type(path);
    if (media_type.empty()) {
        throw std::runtime_error(path.string() +
                                 ": unsupported image type (accepted: png, jpg, jpeg, gif, "
                                 "webp, bmp)");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(path.string() + ": cannot open file");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw std::runtime_error(path.string() + ": error reading file");
    }

    const std::string encoded = base64_encode(buffer.str());
    if (encoded.empty()) {
        throw std::runtime_error(path.string() + ": file is empty");
    }
    return harness::ContentPart::from_image_url("data:" + media_type + ";base64," + encoded);
}

std::string attachment_refusal(const harness::Harness& harness, const std::string& model,
                               harness::Medium medium) {
    const agentloop::MediaReaders readers = agentloop::media_readers(harness, model, medium);
    if (readers.native || !readers.describer.empty() || !readers.transcriber.empty()) {
        return {};
    }
    // The role that would read it, when it is set: then it is that model
    // that cannot either, and saying "no vision model is set" would be wrong.
    const harness::Resolution helper = harness::resolve_backend(
        harness.config(), harness::RoleRequest{.role = medium == harness::Medium::Audio
                                                           ? harness::ModelRole::Transcription
                                                           : harness::ModelRole::Vision,
                                               .conversation = model});
    return media_refusal_message(model, medium,
                                 harness::is_named(helper.from) ? helper.key : std::string{});
}

std::string media_refusal_message(const std::string& model, harness::Medium medium,
                                  std::string_view helper) {
    // Names what cannot read it and the role that would. A message that only
    // says "cannot accept images" leaves a user guessing between the wrong
    // backend, a missing mmproj_path, and a build without llama.cpp.
    const std::string local =
        " -- a local model reads one with an mmproj_path on its backend, in a build with "
        "-DAPOGEE_ENABLE_LLAMA=ON, or as an mlx backend over a vision model with mlx-vlm "
        "installed";
    const auto role = [&](std::string_view noun, std::string_view command) {
        if (!helper.empty()) {
            return "and neither can the " + std::string{noun} + " model, '" + std::string{helper} +
                   "' -- point 'apogee config " + std::string{command} + "' at one that can";
        }
        return "and no " + std::string{noun} + " model is set -- set one with 'apogee config " +
               std::string{command} + " <backend>'";
    };
    switch (medium) {
        case harness::Medium::Image:
            return "backend '" + model + "' cannot read images, " +
                   role("vision", "set-default-vision") + local;
        case harness::Medium::Audio:
            return "backend '" + model + "' cannot hear audio, " +
                   role("transcription", "set-default-transcription") +
                   ", a local model whose mmproj has an audio encoder";
        case harness::Medium::Video:
            return "backend '" + model + "' cannot read a video's frames or hear its sound, " +
                   role("vision", "set-default-vision") + local;
    }
    return {};
}

std::vector<harness::ChatMessage> build_messages(
    const std::string& system_prompt, const std::string& context, const std::string& prompt,
    const std::vector<harness::ContentPart>& attachments) {
    std::vector<harness::ChatMessage> messages;

    // What was typed, piped or named arrives as the bytes it was read as --
    // a terminal in another encoding, a Latin-1 file on stdin -- and enters
    // the conversation here, whose history and requests are strict JSON
    // dumps: made text first. Valid text is unchanged.
    if (!system_prompt.empty()) {
        messages.push_back(harness::ChatMessage::system(harness::valid_utf8(system_prompt)));
    }
    if (!context.empty()) {
        // Before the prompt: a model weights the last message most, and the
        // prompt is what it should be answering, not the reference material.
        messages.push_back(harness::ChatMessage::system(harness::valid_utf8(context)));
    }

    if (attachments.empty()) {
        messages.push_back(harness::ChatMessage::user(harness::valid_utf8(prompt)));
        return messages;
    }

    // With attachments the content becomes multi-part: the text first, so the
    // instruction is read before the images it refers to.
    std::vector<harness::ContentPart> parts;
    parts.reserve(attachments.size() + 1);
    if (!prompt.empty()) {
        parts.push_back(harness::ContentPart::from_text(harness::valid_utf8(prompt)));
    }
    for (const harness::ContentPart& attachment : attachments) {
        parts.push_back(attachment);
    }
    messages.push_back(harness::ChatMessage::user(harness::MessageContent::from_parts(parts)));
    return messages;
}

}  // namespace apogee::commands
