#include "cli/helpers.h"

#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "agent/fetch_url.h"
#include "agent/web_search.h"
#include "agentloop/embed_func.h"
#include "agentloop/graph_context.h"
#include "agentloop/media.h"
#include "cli/embed.h"
#include "cli/tool_vectors.h"
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

namespace {
// The stream buffer std::cin was born with. Everything here reads stdin
// through std::cin, so a caller that has swapped that buffer -- the command
// test fixtures feed their "piped" input this way -- has made stdin something
// other than the terminal for this process, whatever descriptor 0 says.
// Asking only the descriptor made two tests pass under ctest and fail under a
// developer's terminal (2026-09-19). Captured at static initialization, before
// anything can have swapped it, through a noexcept function: rdbuf() is a
// plain accessor the standard merely forgot to mark so, and a static
// initializer must not be able to throw.
[[nodiscard]] std::streambuf* initial_stdin_buffer() noexcept {
    return std::cin.rdbuf();
}

std::streambuf* const kOriginalStdinBuffer = initial_stdin_buffer();
}  // namespace

bool stdin_is_piped() {
    return std::cin.rdbuf() != kOriginalStdinBuffer ||
           !platform::is_terminal(platform::StandardStream::In);
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
    return media_refusal_message(
        model, medium,
        helper.from == harness::ResolvedFrom::RolePointer ? helper.key : std::string{});
}

std::string media_refusal_message(const std::string& model, harness::Medium medium,
                                  std::string_view helper) {
    // Names what cannot read it and the role that would. A message that only
    // says "cannot accept images" leaves a user guessing between the wrong
    // backend, a missing mmproj_path, and a build without llama.cpp.
    const std::string local =
        " -- a local model reads one with an mmproj_path on its backend, in a build with "
        "-DAPOGEE_ENABLE_LLAMA=ON";
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

    if (!system_prompt.empty()) {
        messages.push_back(harness::ChatMessage::system(system_prompt));
    }
    if (!context.empty()) {
        // Before the prompt: a model weights the last message most, and the
        // prompt is what it should be answering, not the reference material.
        messages.push_back(harness::ChatMessage::system(context));
    }

    if (attachments.empty()) {
        messages.push_back(harness::ChatMessage::user(prompt));
        return messages;
    }

    // With attachments the content becomes multi-part: the text first, so the
    // instruction is read before the images it refers to.
    std::vector<harness::ContentPart> parts;
    parts.reserve(attachments.size() + 1);
    if (!prompt.empty()) {
        parts.push_back(harness::ContentPart::from_text(prompt));
    }
    for (const harness::ContentPart& attachment : attachments) {
        parts.push_back(attachment);
    }
    messages.push_back(harness::ChatMessage::user(harness::MessageContent::from_parts(parts)));
    return messages;
}

}  // namespace apogee::commands
