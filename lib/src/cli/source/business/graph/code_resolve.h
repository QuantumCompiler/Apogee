#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/graph.h"
#include "graph/code_extract.h"

/// The deterministic cross-file pass of the code graph (27k): every file's
/// facts in, one graph of code nodes and `extracted` edges out.
///
/// **Resolution is by qualified name and the import edges -- never by
/// resemblance.** A call, a type in a signature, a base class or an import
/// links to a definition only when a lookup the language's own rules would
/// make finds exactly that qualified name: the enclosing scopes outward (a
/// class's bases among them where the language lets a member be named
/// bare), what an import bound or opened, `this`/`self`, and a variable
/// whose declared type is known. Anything else stays an **unresolved**
/// reference: an edge to a `name` node carrying the text as written
/// (`std::max`, `.push_back`, `os.path.join`), counted, never guessed at --
/// two candidates are as unresolved as none.
///
/// Nodes: `file` (its path), `module` (a namespace, package or module),
/// `class`, `function`, and `name` (an unresolved reference's target). Each
/// definition is mentioned where it is defined or declared, `file:line`; a
/// name node where it is referenced. Edges: `defined_in`, `imports`,
/// `calls`, `inherits`, `references`, each with the sites (`file:line`)
/// that state it.
namespace apogee::graph {

// The node kinds and relations are the store's (embedstore/graph.h), so the
// layers that only read the graph -- expansion, communities, dedupe -- can
// tell a code node without reaching up into this package.
using embedstore::kCodeKindClass;
using embedstore::kCodeKindFile;
using embedstore::kCodeKindFunction;
using embedstore::kCodeKindModule;
using embedstore::kCodeKindName;
using embedstore::kCodeRelationCalls;
using embedstore::kCodeRelationDefinedIn;
using embedstore::kCodeRelationImports;
using embedstore::kCodeRelationInherits;
using embedstore::kCodeRelationReferences;

/// One file's facts and the source member it belongs to.
struct SourceFacts {
    /// The member's label: what every mention and site from it is recorded
    /// under.
    std::string member;
    FileFacts facts;
};

/// A place in a member's tree. `role` is `definition`, `declaration` or
/// `reference` for a mention, empty for an edge's site.
struct CodeLocation {
    std::string member;
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t end_line = 0;
    std::string role;

    [[nodiscard]] bool operator==(const CodeLocation&) const = default;
    [[nodiscard]] auto operator<=>(const CodeLocation&) const = default;
};

struct CodeNode {
    std::string type;
    std::string name;
    std::string description;
    /// The node's JSON metadata: its primary location and language, or the
    /// unresolved marker.
    std::string metadata;
    /// Sorted, unique.
    std::vector<CodeLocation> mentions;
};

struct CodeEdge {
    std::string source_type;
    std::string source_name;
    std::string target_type;
    std::string target_name;
    std::string relation;
    /// Sorted, unique; the edge's weight is their count.
    std::vector<CodeLocation> sites;
};

/// What resolution made of every reference.
struct ResolveCounts {
    int calls = 0;
    int calls_resolved = 0;
    int types = 0;
    int types_resolved = 0;
    int imports = 0;
    int imports_resolved = 0;
    int inherits = 0;
    int inherits_resolved = 0;

    [[nodiscard]] int total() const noexcept {
        return calls + types + imports + inherits;
    }

    [[nodiscard]] int resolved() const noexcept {
        return calls_resolved + types_resolved + imports_resolved + inherits_resolved;
    }

    [[nodiscard]] int unresolved() const noexcept {
        return total() - resolved();
    }
};

struct CodeGraph {
    /// Sorted by (type, name).
    std::vector<CodeNode> nodes;
    /// Sorted by (source, target, relation).
    std::vector<CodeEdge> edges;
    ResolveCounts counts;
};

/// Links every file's facts into one graph. Deterministic: the result
/// depends only on the set of (member, facts), never on their order.
[[nodiscard]] CodeGraph resolve_code(std::vector<SourceFacts> sources);

}  // namespace apogee::graph
