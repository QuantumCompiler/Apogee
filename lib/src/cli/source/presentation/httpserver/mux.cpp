#include "httpserver/mux.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#include "contracts/utf8.h"
#include "httpserver/admin_auth.h"

namespace apogee::httpserver {
namespace {

using RouteFn = HttpResponse (*)(Handler&, AdminHandler*, const HttpRequest&, const std::string&);

struct Route {
    const char* method;
    const char* pattern;
    bool admin;
    RouteFn call;
};

constexpr std::string_view kAdminPrefix = "/v1/admin";

/// The table. `tests/scripts/cmake/http_api_conformance.cmake` reads the method/pattern
/// pairs off these lines and requires each to be documented in
/// `documentation/reference/cli/http-api.md`, and each documented route to be
/// here -- so the reference a client author trusts cannot drift from the
/// routes that exist. Admin rows are only reachable through the gate.
constexpr std::array<Route, 97> kRoutes{{
    {"POST", "/v1/chat/completions", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.chat_completions(r);
     }},
    {"POST", "/v1/completions", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.completions(r);
     }},
    {"GET", "/v1/models", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.list_models(r);
     }},
    {"GET", "/v1/model/status", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.model_status(r);
     }},
    {"GET", "/health", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.health(r);
     }},
    {"GET", "/v1/sessions", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string&) {
         return h.list_sessions(r);
     }},
    {"GET", "/v1/sessions/{id}", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string& id) {
         return h.get_session(r, id);
     }},
    {"DELETE", "/v1/sessions/{id}", false,
     +[](Handler& h, AdminHandler*, const HttpRequest& r, const std::string& id) {
         return h.delete_session(r, id);
     }},
    // --- the control plane ------------------------------------------------
    {"GET", "/v1/admin/backends", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_backends(r);
     }},
    {"POST", "/v1/admin/backends", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_backend(r);
     }},
    {"POST", "/v1/admin/backends/default", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default(r);
     }},
    {"POST", "/v1/admin/backends/default-embedding", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_embedding(r);
     }},
    {"POST", "/v1/admin/backends/default-extraction", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_extraction(r);
     }},
    {"POST", "/v1/admin/backends/default-vision", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_vision(r);
     }},
    {"POST", "/v1/admin/backends/default-transcription", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_transcription(r);
     }},
    {"POST", "/v1/admin/backends/default-utility", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_utility(r);
     }},
    {"GET", "/v1/admin/backends/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_backend(r, id);
     }},
    {"DELETE", "/v1/admin/backends/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_backend(r, id);
     }},
    {"POST", "/v1/admin/backends/{id}/rename", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->rename_backend(r, id);
     }},
    {"PATCH", "/v1/admin/backends/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_backend_fields(r, id);
     }},
    {"PUT", "/v1/admin/config/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_config_value(r, id);
     }},
    {"POST", "/v1/admin/config/format", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->format_config(r);
     }},
    {"GET", "/v1/admin/mcp-servers", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_mcp_servers(r);
     }},
    {"POST", "/v1/admin/mcp-servers", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_mcp_server(r);
     }},
    {"GET", "/v1/admin/mcp-servers/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_mcp_server(r, id);
     }},
    {"DELETE", "/v1/admin/mcp-servers/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_mcp_server(r, id);
     }},
    {"PUT", "/v1/admin/mcp-servers/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_mcp_server_enabled(r, id);
     }},
    {"GET", "/v1/admin/agents", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_agents(r);
     }},
    {"POST", "/v1/admin/agents", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_agent(r);
     }},
    {"GET", "/v1/admin/agents/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_agent(r, id);
     }},
    {"PUT", "/v1/admin/agents/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_agent(r, id);
     }},
    {"DELETE", "/v1/admin/agents/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_agent(r, id);
     }},
    // The symphonies slice (27q): the twins of create, edit and delete, and
    // the reads list and show serve; play stays on the command line.
    {"GET", "/v1/admin/symphonies", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_symphonies(r);
     }},
    {"POST", "/v1/admin/symphonies", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_symphony(r);
     }},
    {"GET", "/v1/admin/symphonies/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_symphony(r, id);
     }},
    {"PUT", "/v1/admin/symphonies/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_symphony(r, id);
     }},
    {"DELETE", "/v1/admin/symphonies/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_symphony(r, id);
     }},
    {"POST", "/v1/admin/knowledge", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_knowledge(h, r);
     }},
    {"POST", "/v1/admin/knowledge/capture", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->capture_knowledge(h, r);
     }},
    {"POST", "/v1/admin/knowledge/refine", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->refine_knowledge(h, r);
     }},
    {"POST", "/v1/admin/knowledge/reindex", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->reindex_knowledge(h, r);
     }},
    {"GET", "/v1/admin/knowledge", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_knowledge(h, r);
     }},
    // The literal knowledge paths above are matched before the `{id}` rows,
    // so `capture`, `refine` and `reindex` are never read as record ids.
    {"GET", "/v1/admin/knowledge/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_knowledge(r, id);
     }},
    {"PATCH", "/v1/admin/knowledge/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->patch_knowledge(r, id);
     }},
    {"DELETE", "/v1/admin/knowledge/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_knowledge(r, id);
     }},
    // The knowledge graph over a collection or a named graph -- `{id}` is
    // resolved graphs-first. It sits mid-path, which the matcher handles
    // segment by segment; the fixed tails keep `build`, `stats`, `entity`,
    // `communities` and `dedupe` distinct from the bare `DELETE`.
    {"POST", "/v1/admin/graph/{id}/build", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->build_graph(h, r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/stats", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_stats(r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/entity", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_entity(r, id);
     }},
    // Navigation (27l): reads only, the `graph` verbs' JSON documents.
    {"GET", "/v1/admin/graph/{id}/path", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_path(r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/explain", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_explain(r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/neighbors", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_neighbors(r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/query", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->graph_query(r, id);
     }},
    {"POST", "/v1/admin/graph/{id}/communities", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->build_communities(h, r, id);
     }},
    {"GET", "/v1/admin/graph/{id}/communities", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->list_communities(r, id);
     }},
    {"POST", "/v1/admin/graph/{id}/dedupe", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->dedupe_graph(r, id);
     }},
    {"DELETE", "/v1/admin/graph/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_graph(r, id);
     }},
    {"PUT", "/v1/admin/embeddings/{id}/graph", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_graph_enabled(r, id);
     }},
    // The graphs: config slice -- named multi-collection graphs' entries;
    // the data lives behind /v1/admin/graph/{id}/* above.
    {"GET", "/v1/admin/graphs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_graphs(r);
     }},
    {"POST", "/v1/admin/graphs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_graph(r);
     }},
    {"GET", "/v1/admin/graphs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_graph(r, id);
     }},
    {"PUT", "/v1/admin/graphs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_graph(r, id);
     }},
    {"DELETE", "/v1/admin/graphs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_graph_config(r, id);
     }},
    // The suites: config slice (27d) -- the literal `default` first, so it
    // is never read as a suite's name on a POST.
    {"GET", "/v1/admin/suites", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_suites(r);
     }},
    {"POST", "/v1/admin/suites", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_suite(r);
     }},
    {"POST", "/v1/admin/suites/default", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->set_default_suite(r);
     }},
    {"GET", "/v1/admin/suites/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_suite(r, id);
     }},
    {"PUT", "/v1/admin/suites/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_suite(r, id);
     }},
    {"DELETE", "/v1/admin/suites/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_suite(r, id);
     }},
    {"PUT", "/v1/admin/suites/{id}/members", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_suite_member(r, id);
     }},
    {"PUT", "/v1/admin/suites/{id}/consult", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_suite_consult(r, id);
     }},
    {"PUT", "/v1/admin/suites/{id}/validate", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_suite_validate(r, id);
     }},
    {"PUT", "/v1/admin/suites/{id}/orchestrate", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->set_suite_orchestrate(r, id);
     }},
    // The datasets slice: the literal paths first, so `synth` and `kits`
    // are never read as dataset names.
    {"GET", "/v1/admin/datasets", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_datasets(r);
     }},
    {"POST", "/v1/admin/datasets", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->create_dataset(r);
     }},
    {"POST", "/v1/admin/datasets/synth", true,
     +[](Handler& h, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->synth_dataset(h, r);
     }},
    {"GET", "/v1/admin/datasets/kits", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_kits(r);
     }},
    {"GET", "/v1/admin/datasets/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_dataset(r, id);
     }},
    {"DELETE", "/v1/admin/datasets/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_dataset(r, id);
     }},
    // The training slice: reads only. `train run|eval|promote|rollback|setup`,
    // `pipeline run|resume`, `regime run` and `cycle run|halt|resume` have
    // no route by the track's constraint -- control is CLI-only.
    {"GET", "/v1/admin/training/status", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->training_status(r);
     }},
    {"GET", "/v1/admin/training/runs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_training_runs(r);
     }},
    {"GET", "/v1/admin/training/runs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_training_run(r, id);
     }},
    {"GET", "/v1/admin/training/versions", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_training_versions(r);
     }},
    {"GET", "/v1/admin/training/cycle", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->training_cycle(r);
     }},
    // The tasks slice (27j): reads only, the training split. `task run`,
    // `resume`, `halt` and `cancel` have no route -- task control is
    // CLI-only.
    {"GET", "/v1/admin/tasks", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_tasks(r);
     }},
    {"GET", "/v1/admin/tasks/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_task(r, id);
     }},
    {"GET", "/v1/admin/permissions", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_permissions(r);
     }},
    {"PUT", "/v1/admin/permissions/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_permission(r, id);
     }},
    {"GET", "/v1/admin/allowed-hosts", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_allowed_hosts(r);
     }},
    {"PUT", "/v1/admin/allowed-hosts/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_allowed_host(r, id);
     }},
    {"DELETE", "/v1/admin/allowed-hosts/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->delete_allowed_host(r, id);
     }},
    {"GET", "/v1/admin/auth", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_credentials(r);
     }},
    {"PUT", "/v1/admin/auth/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->put_credential(r, id);
     }},
    {"DELETE", "/v1/admin/auth/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->clear_credential(r, id);
     }},
    {"GET", "/v1/admin/events", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->events_stream(r);
     }},
    {"GET", "/v1/admin/jobs", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string&) {
         return a->list_jobs(r);
     }},
    {"GET", "/v1/admin/jobs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->get_job(r, id);
     }},
    {"DELETE", "/v1/admin/jobs/{id}", true,
     +[](Handler&, AdminHandler* a, const HttpRequest& r, const std::string& id) {
         return a->cancel_job(r, id);
     }},
}};

constexpr std::string_view kForbidden = "forbidden";

/// The most of a refused value a refusal echoes back.
constexpr std::size_t kEchoLimit = 200;

std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool all_digits(std::string_view text) noexcept {
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return !text.empty();
}

/// The host of an authority -- `name`, `name:port`, `[v6]`, `[v6]:port` --
/// lower-cased, brackets kept; empty when it is not one.
std::string host_of_authority(std::string_view authority) {
    std::string_view host;
    std::string_view rest;
    if (authority.starts_with('[')) {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos) {
            return {};
        }
        host = authority.substr(0, close + 1);
        rest = authority.substr(close + 1);
    } else {
        const std::size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        rest = colon == std::string_view::npos ? std::string_view{} : authority.substr(colon);
    }
    if (!rest.empty()) {
        // One port, of digits only: `127.0.0.1:8080:9` is no address.
        if (rest.front() != ':') {
            return {};
        }
        rest.remove_prefix(1);
        if (!all_digits(rest) || rest.size() > 5) {
            return {};
        }
    }
    if (host.empty() || host == "[]") {
        return {};
    }
    return lower(host);
}

/// The host of a serialized origin -- `scheme://authority` and nothing after
/// it -- or empty: `null` names no host.
std::string host_of_origin(std::string_view origin) {
    const std::size_t separator = origin.find("://");
    if (separator == std::string_view::npos || separator == 0) {
        return {};
    }
    const std::string_view authority = origin.substr(separator + 3);
    if (authority.find_first_of("/?#@") != std::string_view::npos) {
        return {};
    }
    return host_of_authority(authority);
}

/// `127.a.b.c`, written as four decimal numbers each at most 255.
bool is_dotted_loopback(std::string_view host) noexcept {
    int octets = 0;
    bool first_is_127 = false;
    while (true) {
        const std::size_t dot = host.find('.');
        const std::string_view octet = host.substr(0, dot);
        if (!all_digits(octet) || octet.size() > 3) {
            return false;
        }
        int value = 0;
        for (const char c : octet) {
            value = (value * 10) + (c - '0');
        }
        if (value > 255) {
            return false;
        }
        if (octets == 0) {
            first_is_127 = value == 127;
        }
        ++octets;
        if (dot == std::string_view::npos) {
            break;
        }
        host.remove_prefix(dot + 1);
    }
    return octets == 4 && first_is_127;
}

/// A name only this machine answers to, as a `Host` or an `Origin` writes
/// it -- strictly: no prefix or suffix of one is one.
bool is_loopback_name(std::string_view host) noexcept {
    return host == "localhost" || host == "[::1]" || is_dotted_loopback(host);
}

/// A value the client set, safe to put back in a JSON body.
std::string echo(std::string_view value) {
    return harness::valid_utf8(value.substr(0, kEchoLimit));
}

}  // namespace

std::string request_refusal(const HttpRequest& request, bool check_host) {
    std::string host;
    const auto host_header = request.headers.find("host");
    if (host_header != request.headers.end()) {
        host = host_of_authority(host_header->second);
        if (check_host && !is_loopback_name(host)) {
            return "request refused: this server answers only requests addressed to it on "
                   "loopback (Host " +
                   echo(host_header->second) + ")";
        }
    }
    const auto origin_header = request.headers.find("origin");
    if (origin_header != request.headers.end()) {
        const std::string origin = host_of_origin(origin_header->second);
        const bool trusted =
            !origin.empty() && (is_loopback_name(origin) || (!host.empty() && origin == host));
        if (!trusted) {
            return "request refused: this server answers no request from a page on another "
                   "site (Origin " +
                   echo(origin_header->second) + ")";
        }
    }
    return {};
}

bool is_admin_path(std::string_view path) noexcept {
    return path == kAdminPrefix || path.starts_with("/v1/admin/");
}

std::optional<std::string> match_route(std::string_view pattern, std::string_view path) {
    std::string captured;
    while (true) {
        const std::size_t pattern_slash = pattern.find('/');
        const std::size_t path_slash = path.find('/');
        const std::string_view pattern_segment = pattern.substr(0, pattern_slash);
        const std::string_view path_segment = path.substr(0, path_slash);

        if (pattern_segment == "{id}") {
            if (path_segment.empty()) {
                return std::nullopt;
            }
            captured = std::string{path_segment};
        } else if (pattern_segment != path_segment) {
            return std::nullopt;
        }

        const bool pattern_done = pattern_slash == std::string_view::npos;
        const bool path_done = path_slash == std::string_view::npos;
        if (pattern_done || path_done) {
            return pattern_done && path_done ? std::optional<std::string>{captured} : std::nullopt;
        }
        pattern.remove_prefix(pattern_slash + 1);
        path.remove_prefix(path_slash + 1);
    }
}

Mux::Mux(Handler& handler) : handler_{&handler} {}

Mux::Mux(Handler& handler, AdminHandler& admin, std::string admin_token)
    : handler_{&handler}, admin_{&admin}, admin_token_{std::move(admin_token)} {}

std::vector<RouteSpec> Mux::routes() {
    std::vector<RouteSpec> out;
    out.reserve(kRoutes.size());
    for (const Route& route : kRoutes) {
        out.push_back(RouteSpec{route.method, route.pattern, route.admin});
    }
    return out;
}

HttpResponse Mux::dispatch(const HttpRequest& request) const {
    // Who may reach the server at all, ahead of every route and the admin
    // gate (57n): a page's forged request learns nothing about either.
    if (const std::string refusal = request_refusal(request, host_check_); !refusal.empty()) {
        return error_response(403, refusal, kForbidden);
    }
    if (is_admin_path(request.path)) {
        if (admin_ == nullptr) {
            return error_response(404, "the admin plane is not mounted on this server",
                                  kNotFoundError);
        }
        // The gate runs BEFORE routing: an unauthenticated probe learns
        // nothing about which routes exist.
        if (!bearer_valid(request, admin_token_)) {
            return unauthorized_response();
        }
    }

    std::string allowed;
    for (const Route& route : kRoutes) {
        const std::optional<std::string> captured = match_route(route.pattern, request.path);
        if (!captured.has_value()) {
            continue;
        }
        if (request.method != route.method) {
            allowed += allowed.empty() ? "" : ", ";
            allowed += route.method;
            continue;
        }
        return route.call(*handler_, admin_, request, *captured);
    }
    if (!allowed.empty()) {
        HttpResponse out = error_response(405, request.method + " is not allowed on " +
                                                   request.path + " (allowed: " + allowed + ")");
        out.headers["Allow"] = allowed;
        return out;
    }
    return error_response(404, "no route for " + request.method + " " + request.path,
                          kNotFoundError);
}

}  // namespace apogee::httpserver
