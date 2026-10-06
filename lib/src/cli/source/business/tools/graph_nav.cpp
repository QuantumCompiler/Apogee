#include "tools/graph_nav.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tools/args.h"

namespace apogee::tools {
namespace {

constexpr std::array<std::string_view, 4> kGraphToolNames{
    kGraphQueryToolName, kGraphPathToolName, kGraphExplainToolName, kGraphNeighborsToolName};

/// When to reach for a scoped set (27o). Offered and never mentioned, the
/// tools went uncalled on real weights: Qwen3-VL-8B and Llama 3.1 8B answered
/// "where is retrieval implemented?" from the turn's excerpts alone -- the
/// passive injection the attachment-representation spike found failing. A
/// capability and why, never an order: the model still decides.
constexpr std::string_view kScopedGraphPolicy =
    "How to use the code graph: when a question turns on the code attached to this chat -- "
    "where something is defined or implemented, what calls what, how one part reaches another "
    "-- look it up with the graph tools before you answer: graph_query to find what the question "
    "names, graph_explain for one function, class or file, graph_path for how one reaches "
    "another. The excerpts show a few passages; the graph shows the whole structure, each entity "
    "at its file and line. Name only files and lines the graph or the excerpts show, never a "
    "guessed path.";

/// What a call is given beside its own arguments, shared by the four.
struct Context {
    std::shared_ptr<const harness::Config> fallback;
    const harness::Config* config = nullptr;
    std::optional<graph::GraphTarget> scope;
    std::string scope_note;

    [[nodiscard]] const harness::Config& settings() const {
        return config != nullptr ? *config : *fallback;
    }
};

/// The `graph`/`collection` properties an unscoped tool takes.
constexpr std::string_view kSelectionProperties =
    R"JSON("graph":{"type":"string","description":"The graph to read: a named graph or a collection's own graph. Omit when only one graph is built; an error lists them otherwise"},"collection":{"type":"string","description":"Read the graph covering this collection instead: a named graph listing it, else its own"})JSON";

constexpr std::string_view kAddressing =
    " Address a node by name, kind:name (function:pkg.mod.run) or path:line (src/app.py:12); a "
    "code node also by its unqualified name when only one qualified name ends in it. A name "
    "several nodes share is never guessed: the error lists each by the address that names it "
    "alone.";

[[nodiscard]] std::string schema(const Context& context, std::string_view own,
                                 std::string_view required) {
    std::string properties{own};
    if (!context.scope.has_value()) {
        properties += ",";
        properties += kSelectionProperties;
    }
    return R"({"type":"object","properties":{)" + properties + R"(},"required":[)" +
           std::string{required} + "]}";
}

[[nodiscard]] std::string described(const Context& context, std::string text) {
    if (context.scope.has_value()) {
        text += context.scope_note.empty() ? " Reads the graph '" + context.scope->name + "'."
                                           : context.scope_note;
    }
    return text;
}

/// The graph a call reads: the scope, else the call's selection.
[[nodiscard]] graph::GraphTarget target_for(const Context& context, const Arguments& args) {
    if (context.scope.has_value()) {
        return *context.scope;
    }
    return graph::resolve_graph_target(
        context.settings(), graph::GraphSelection{.graph = args.string("graph"),
                                                  .collection = args.string("collection")});
}

/// Parses, opens, runs, serialises -- every failure an error result the
/// model can act on, never an exception out of the tool.
[[nodiscard]] agent::ToolOutcome run_on_graph(
    const Context& context, std::string_view arguments, std::string_view example,
    const std::function<nlohmann::json(const graph::OpenGraph&, const Arguments&)>& body) {
    agent::ToolOutcome failure;
    const std::optional<Arguments> args = parse_arguments(arguments, example, failure);
    if (!args.has_value()) {
        return failure;
    }
    try {
        const graph::OpenGraph open{target_for(context, *args)};
        return ok(body(open, *args).dump());
    } catch (const graph::NavigationError& e) {
        return error(e.what());
    } catch (const std::invalid_argument& e) {
        return error(e.what());
    } catch (const std::exception& e) {
        return error(std::string{"could not read the graph: "} + e.what());
    }
}

/// An integer argument, or `fallback` when absent; a non-number is refused.
[[nodiscard]] int integer_or(const Arguments& args, std::string_view key, int fallback) {
    if (!args.has(key)) {
        return fallback;
    }
    const std::optional<std::int64_t> value = args.integer(key);
    if (!value.has_value()) {
        throw std::invalid_argument(std::string{key} + " must be an integer");
    }
    // The core refuses anything outside its range by name; clamping here
    // only keeps a huge number from wrapping on the way there.
    constexpr std::int64_t bound = 1'000'000;
    return static_cast<int>(std::clamp<std::int64_t>(*value, -bound, bound));
}

/// A boolean argument, or `fallback` when absent. A local model's template
/// may send every value as a string -- `"True"` as often as `true` (Llama 3.1
/// 8B, 2026-10-04) -- so the usual spellings are read, case aside; anything
/// else is refused rather than guessed.
[[nodiscard]] bool boolean_or(const Arguments& args, std::string_view key, bool fallback) {
    if (!args.has(key)) {
        return fallback;
    }
    const nlohmann::json& value = args.object.at(std::string{key});
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    std::string text = args.string(key);
    std::ranges::transform(text, text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text == "true" || text == "yes" || text == "1") {
        return true;
    }
    if (text == "false" || text == "no" || text == "0") {
        return false;
    }
    throw std::invalid_argument(std::string{key} + " must be true or false");
}

/// `word` without the quotes around it.
[[nodiscard]] std::string unquote(std::string word) {
    while (!word.empty() && (word.front() == '\'' || word.front() == '"')) {
        word.erase(word.begin());
    }
    while (!word.empty() && (word.back() == '\'' || word.back() == '"')) {
        word.pop_back();
    }
    return word;
}

/// The words of one text, comma-separated -- or an array written out as text
/// (`['calls']`), read as its items.
void split_words(std::string_view text, std::vector<std::string>& out) {
    std::string_view inner = text;
    if (const std::string trimmed = trim(text);
        trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
        const std::size_t open = text.find('[');
        const std::size_t close = text.rfind(']');
        inner = text.substr(open + 1, close - open - 1);
    }
    std::size_t start = 0;
    while (start <= inner.size()) {
        const std::size_t comma = inner.find(',', start);
        std::string word = unquote(trim(inner.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start)));
        if (!word.empty()) {
            out.push_back(std::move(word));
        }
        if (comma == std::string_view::npos) {
            return;
        }
        start = comma + 1;
    }
}

/// A list of words: a JSON array of strings, or one string -- comma-separated,
/// or an array written out as text (`"['calls']"`, which a local model's
/// template sends for an array as readily as the array itself).
[[nodiscard]] std::vector<std::string> words(const Arguments& args, std::string_view key) {
    std::vector<std::string> out;
    if (!args.has(key)) {
        return out;
    }
    const nlohmann::json& value = args.object.at(std::string{key});
    if (value.is_string()) {
        split_words(value.get<std::string>(), out);
        return out;
    }
    if (!value.is_array()) {
        throw std::invalid_argument(std::string{key} + " must be a list of strings");
    }
    for (const nlohmann::json& item : value) {
        if (!item.is_string()) {
            throw std::invalid_argument(std::string{key} + " must be a list of strings");
        }
        split_words(item.get<std::string>(), out);
    }
    return out;
}

[[nodiscard]] std::string required_string(const Arguments& args, std::string_view key) {
    std::string value = args.string(key);
    if (value.empty()) {
        throw std::invalid_argument(std::string{key} + " is required");
    }
    return value;
}

}  // namespace

std::span<const std::string_view> graph_tool_names() noexcept {
    return kGraphToolNames;
}

void register_graph_tools(agent::ToolRegistry& registry, const GraphToolsOptions& options) {
    auto context = std::make_shared<Context>();
    context->fallback = std::make_shared<const harness::Config>();
    context->config = options.config;
    context->scope = options.scope;
    context->scope_note = options.scope_note;

    agent::Tool query;
    query.name = std::string{kGraphQueryToolName};
    query.description = described(
        *context,
        "Search a knowledge graph by a question: the entities whose names it matches, and the "
        "bounded neighbourhood around them with the relations among them -- what is connected "
        "to what. For a code graph: functions, classes, files and modules, with where each is "
        "defined. Returns JSON.");
    query.parameters_schema = schema(
        *context,
        R"JSON("question":{"type":"string","description":"An entity's name, or words naming the entities to start from"},"hops":{"type":"integer","description":"How far to walk from them: 1 (default) or 2"},"max_entities":{"type":"integer","description":"Entities returned at most (1-50; default the graph's own, 8 unless configured)"})JSON",
        R"("question")");
    query.run = [context](std::string_view arguments) {
        return run_on_graph(
            *context, arguments, R"({"question": "build_source"})",
            [](const graph::OpenGraph& open, const Arguments& args) {
                return graph::to_json(graph::run_query(
                    open,
                    graph::QueryRequest{.question = required_string(args, "question"),
                                        .hops = integer_or(args, "hops", 0),
                                        .max_entities = integer_or(args, "max_entities", 0)}));
            });
    };
    registry.add(std::move(query));

    agent::Tool path;
    path.name = std::string{kGraphPathToolName};
    path.description = described(
        *context,
        "Find how one entity in a knowledge graph reaches another: the shortest path between "
        "them, each hop with its relation (calls, imports, inherits, references, defined_in, or "
        "a document's own) and whether it was parsed from source (extracted) or asserted by a "
        "model (inferred), with the call site where the source states it. For how code reaches "
        "other code, pass relations [\"calls\"] and directed true. Undirected by default, at "
        "most 8 hops." +
            std::string{kAddressing} + " Returns JSON.");
    path.parameters_schema = schema(
        *context,
        R"JSON("from":{"type":"string","description":"Where the path starts: a name, kind:name or path:line"},"to":{"type":"string","description":"Where it ends"},"max_hops":{"type":"integer","description":"The longest path looked for (1-32; default 8)"},"directed":{"type":"boolean","description":"Follow edges only the way they point (from calls to); default false"},"relations":{"type":"array","items":{"type":"string"},"description":"Walk only these relations, such as [\"calls\"]; default every one"})JSON",
        R"("from","to")");
    path.run = [context](std::string_view arguments) {
        return run_on_graph(
            *context, arguments, R"({"from": "main", "to": "parse_config"})",
            [](const graph::OpenGraph& open, const Arguments& args) {
                return graph::to_json(graph::find_path(
                    open, graph::PathRequest{
                              .from = required_string(args, "from"),
                              .to = required_string(args, "to"),
                              .max_hops = integer_or(args, "max_hops", graph::kDefaultPathHops),
                              .directed = boolean_or(args, "directed", false),
                              .relations = words(args, "relations")}));
            });
    };
    registry.add(std::move(path));

    agent::Tool explain;
    explain.name = std::string{kGraphExplainToolName};
    explain.description = described(
        *context,
        "Explain one entity in a knowledge graph: its kind, where it is defined (file:line) or "
        "which documents mention it, how many edges it has, its neighbours grouped by relation "
        "(what it calls and what calls it, what it imports, what it is defined in), its "
        "community, and the decision records attached to it." +
            std::string{kAddressing} + " Returns JSON.");
    explain.parameters_schema = schema(
        *context,
        R"JSON("node":{"type":"string","description":"The entity: a name, kind:name or path:line"},"max_neighbors":{"type":"integer","description":"Neighbours listed per relation (1-100; default 12)"})JSON",
        R"("node")");
    explain.run = [context](std::string_view arguments) {
        return run_on_graph(
            *context, arguments, R"({"node": "make_user"})",
            [](const graph::OpenGraph& open, const Arguments& args) {
                return graph::to_json(graph::explain_node(
                    open, graph::CardRequest{
                              .node = required_string(args, "node"),
                              .max_per_relation = integer_or(
                                  args, "max_neighbors", graph::kDefaultNeighborsPerRelation)}));
            });
    };
    registry.add(std::move(explain));

    agent::Tool neighbors;
    neighbors.name = std::string{kGraphNeighborsToolName};
    neighbors.description = described(
        *context,
        "List an entity's neighbours in a knowledge graph, grouped by relation, optionally only "
        "one relation (such as calls) and one direction (out: what it calls or imports; in: "
        "what calls or imports it), capped per relation with the total said." +
            std::string{kAddressing} + " Returns JSON.");
    neighbors.parameters_schema = schema(
        *context,
        R"JSON("node":{"type":"string","description":"The entity: a name, kind:name or path:line"},"relation":{"type":"string","description":"Only this relation, such as calls"},"direction":{"type":"string","enum":["both","out","in"],"description":"out: edges from the entity; in: edges to it; both (default)"},"max_neighbors":{"type":"integer","description":"Neighbours listed per relation (1-100; default 12)"})JSON",
        R"("node")");
    neighbors.run = [context](std::string_view arguments) {
        return run_on_graph(
            *context, arguments, R"({"node": "make_user", "relation": "calls", "direction": "in"})",
            [](const graph::OpenGraph& open, const Arguments& args) {
                embedstore::EdgeDirection direction = embedstore::EdgeDirection::Both;
                if (!graph::direction_from_string(args.string("direction"), direction)) {
                    throw std::invalid_argument("direction must be both, out or in");
                }
                return graph::to_json(graph::find_neighbors(
                    open, graph::NeighborsRequest{
                              .node = required_string(args, "node"),
                              .relation = args.string("relation"),
                              .direction = direction,
                              .max_per_relation = integer_or(
                                  args, "max_neighbors", graph::kDefaultNeighborsPerRelation)}));
            });
    };
    registry.add(std::move(neighbors));
}

std::string_view scoped_graph_policy() noexcept {
    return kScopedGraphPolicy;
}

agent::ToolRegistry with_graph_scope(const agent::ToolRegistry& registry,
                                     const GraphToolsOptions& options) {
    const bool registered = std::ranges::any_of(
        kGraphToolNames, [&](std::string_view name) { return registry.find(name) != nullptr; });
    if (!registered || !options.scope.has_value()) {
        return registry;
    }
    agent::ToolRegistry scoped;
    // The note says when to reach for them (26p's mechanism), read off the
    // registry asking: one narrowed to a toolset without them says nothing.
    scoped.set_environment(
        [inner = registry.environment_source()](const agent::ToolRegistry& tools) {
            std::string note = inner ? inner(tools) : std::string{};
            if (std::ranges::any_of(kGraphToolNames, [&](std::string_view name) {
                    return tools.find(name) != nullptr;
                })) {
                note += (note.empty() ? "" : "\n\n") + std::string{kScopedGraphPolicy};
            }
            return note;
        });
    for (const std::string& name : registry.names()) {
        if (std::ranges::find(kGraphToolNames, name) == kGraphToolNames.end()) {
            scoped.add(*registry.find(name));
        }
    }
    register_graph_tools(scoped, options);
    return scoped;
}

}  // namespace apogee::tools
