#include "httpserver/mux.h"

#include <array>
#include <string>

namespace apogee::httpserver {
namespace {

using RouteFn = HttpResponse (*)(Handler&, const HttpRequest&, const std::string&);

struct Route {
    const char* method;
    const char* pattern;
    RouteFn call;
};

/// The table. `tests/http_api_conformance.cmake` reads the method/pattern
/// pairs off these lines and requires each to be documented in
/// `documentation/reference/http-api.md`, and each documented route to be
/// here -- so the reference a client author trusts cannot drift from the
/// routes that exist.
constexpr std::array<Route, 8> kRoutes{{
    {"POST", "/v1/chat/completions",
     +[](Handler& h, const HttpRequest& r, const std::string&) { return h.chat_completions(r); }},
    {"POST", "/v1/completions",
     +[](Handler& h, const HttpRequest& r, const std::string&) { return h.completions(r); }},
    {"GET", "/v1/models",
     +[](Handler& h, const HttpRequest& r, const std::string&) { return h.list_models(r); }},
    {"GET", "/v1/model/status",
     +[](Handler& h, const HttpRequest& r, const std::string&) { return h.model_status(r); }},
    {"GET", "/health",
     +[](Handler& h, const HttpRequest& r, const std::string&) { return h.health(r); }},
    {"GET", "/v1/sessions",
     +[](Handler& h, const HttpRequest& r, const std::string&) { return h.list_sessions(r); }},
    {"GET", "/v1/sessions/{id}",
     +[](Handler& h, const HttpRequest& r, const std::string& id) { return h.get_session(r, id); }},
    {"DELETE", "/v1/sessions/{id}",
     +[](Handler& h, const HttpRequest& r, const std::string& id) {
         return h.delete_session(r, id);
     }},
}};

}  // namespace

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

std::vector<RouteSpec> Mux::routes() {
    std::vector<RouteSpec> out;
    out.reserve(kRoutes.size());
    for (const Route& route : kRoutes) {
        out.push_back(RouteSpec{route.method, route.pattern});
    }
    return out;
}

HttpResponse Mux::dispatch(const HttpRequest& request) const {
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
        return route.call(*handler_, request, *captured);
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
