#include "httpserver/admin_graph_navigate.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "contracts/config.h"
#include "graph/navigate.h"

namespace apogee::httpserver {
namespace {

/// A query parameter's integer, or `fallback` when absent; nullopt for one
/// that is not a number.
[[nodiscard]] std::optional<int> integer_param(const HttpRequest& request, std::string_view key,
                                               int fallback) {
    const std::string text = request.query_value(key);
    if (text.empty()) {
        return fallback;
    }
    int value = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, code] = std::from_chars(text.data(), end, value);
    if (code != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] HttpResponse bad_integer(std::string_view key) {
    return error_response(400, "the " + std::string{key} + " query parameter must be an integer");
}

/// The words of a comma-separated parameter.
[[nodiscard]] std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        std::string word =
            text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!word.empty()) {
            out.push_back(std::move(word));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

/// A navigation failure in the error envelope: the CLI's message, the status
/// its kind means, and an ambiguous name's candidates beside it.
[[nodiscard]] HttpResponse navigation_error(const graph::NavigationError& e) {
    HttpError error;
    error.message = e.what();
    switch (e.kind()) {
        case graph::NavigationError::Kind::NotFound:
        case graph::NavigationError::Kind::NotBuilt:
            error.status = 404;
            error.type = std::string{kNotFoundError};
            break;
        case graph::NavigationError::Kind::Ambiguous:
        case graph::NavigationError::Kind::InvalidArgument:
            error.status = 400;
            break;
    }
    if (!e.candidates().empty()) {
        nlohmann::json candidates = nlohmann::json::array();
        for (const graph::NodeRef& node : e.candidates()) {
            candidates.push_back(graph::to_json(node));
        }
        error.extra["candidates"] = std::move(candidates);
    }
    return to_response(error);
}

/// Loads the config as it is now, opens `{name}` graphs-first, and answers
/// with what `body` returns.
[[nodiscard]] HttpResponse with_graph(
    const AdminConfigContext& context, std::string_view name,
    const std::function<nlohmann::json(const graph::OpenGraph&)>& body) {
    harness::Config config;
    try {
        config = harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        return error_response(500, std::string{"the config cannot be read: "} + e.what(),
                              "config_error");
    }
    try {
        const graph::OpenGraph open{graph::resolve_graph_target(
            config, graph::GraphSelection{.graph = std::string{name}, .collection = {}})};
        return json_response(200, body(open));
    } catch (const graph::NavigationError& e) {
        return navigation_error(e);
    } catch (const std::exception& e) {
        return error_response(500, std::string{"could not read the graph: "} + e.what());
    }
}

}  // namespace

HttpResponse admin_graph_path(const AdminConfigContext& context, std::string_view name,
                              const HttpRequest& request) {
    graph::PathRequest path;
    path.from = request.query_value("from");
    path.to = request.query_value("to");
    if (path.from.empty() || path.to.empty()) {
        return error_response(400, "the from and to query parameters are required");
    }
    const std::optional<int> hops = integer_param(request, "max_hops", graph::kDefaultPathHops);
    if (!hops.has_value()) {
        return bad_integer("max_hops");
    }
    path.max_hops = *hops;
    const std::string directed = request.query_value("directed");
    if (!directed.empty() && directed != "true" && directed != "false") {
        return error_response(400, "the directed query parameter must be true or false");
    }
    path.directed = directed == "true";
    path.relations = words(request.query_value("relations"));
    return with_graph(context, name, [&path](const graph::OpenGraph& open) {
        return graph::to_json(graph::find_path(open, path));
    });
}

HttpResponse admin_graph_explain(const AdminConfigContext& context, std::string_view name,
                                 const HttpRequest& request) {
    graph::CardRequest card;
    card.node = request.query_value("node");
    if (card.node.empty()) {
        return error_response(400, "the node query parameter is required");
    }
    const std::optional<int> cap =
        integer_param(request, "max_neighbors", graph::kDefaultNeighborsPerRelation);
    if (!cap.has_value()) {
        return bad_integer("max_neighbors");
    }
    card.max_per_relation = *cap;
    return with_graph(context, name, [&card](const graph::OpenGraph& open) {
        return graph::to_json(graph::explain_node(open, card));
    });
}

HttpResponse admin_graph_neighbors(const AdminConfigContext& context, std::string_view name,
                                   const HttpRequest& request) {
    graph::NeighborsRequest neighbors;
    neighbors.node = request.query_value("node");
    if (neighbors.node.empty()) {
        return error_response(400, "the node query parameter is required");
    }
    neighbors.relation = request.query_value("relation");
    if (!graph::direction_from_string(request.query_value("direction"), neighbors.direction)) {
        return error_response(400, "the direction query parameter must be both, out or in");
    }
    const std::optional<int> cap =
        integer_param(request, "max_neighbors", graph::kDefaultNeighborsPerRelation);
    if (!cap.has_value()) {
        return bad_integer("max_neighbors");
    }
    neighbors.max_per_relation = *cap;
    return with_graph(context, name, [&neighbors](const graph::OpenGraph& open) {
        return graph::to_json(graph::find_neighbors(open, neighbors));
    });
}

HttpResponse admin_graph_query(const AdminConfigContext& context, std::string_view name,
                               const HttpRequest& request) {
    graph::QueryRequest query;
    query.question = request.query_value("q");
    if (query.question.empty()) {
        return error_response(400, "the q query parameter is required");
    }
    const std::optional<int> hops = integer_param(request, "hops", 0);
    if (!hops.has_value()) {
        return bad_integer("hops");
    }
    const std::optional<int> entities = integer_param(request, "max_entities", 0);
    if (!entities.has_value()) {
        return bad_integer("max_entities");
    }
    query.hops = *hops;
    query.max_entities = *entities;
    return with_graph(context, name, [&query](const graph::OpenGraph& open) {
        return graph::to_json(graph::run_query(open, query));
    });
}

}  // namespace apogee::httpserver
