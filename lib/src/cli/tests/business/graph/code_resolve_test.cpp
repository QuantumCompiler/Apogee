#include "graph/code_resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "graph/code_extract.h"
#include "graph/code_languages.h"
#include "graph/code_parser.h"

/// The deterministic cross-file pass (27k): what links and what stays a
/// name. A reference resolves only when the language's own lookup finds
/// exactly that qualified name; two candidates are as unresolved as none;
/// the result never depends on the order the files arrive in.
namespace {

using apogee::graph::CodeEdge;
using apogee::graph::CodeGraph;
using apogee::graph::CodeNode;
using apogee::graph::SourceFacts;

/// Every file parsed by the language its extension names, then linked.
[[nodiscard]] std::vector<SourceFacts> facts_of(const std::map<std::string, std::string>& files) {
    std::vector<std::string> paths;
    for (const auto& [path, unused] : files) {
        paths.push_back(path);
    }
    const bool cpp = apogee::graph::tree_has_cpp(paths);
    std::vector<SourceFacts> out;
    for (const auto& [path, source] : files) {
        const apogee::graph::CodeLanguage* language =
            apogee::graph::code_language_for_path(path, cpp);
        REQUIRE(language != nullptr);
        apogee::graph::CodeParser parser{language->id};
        const apogee::graph::CodeParseResult parse = parser.parse(source);
        REQUIRE(parse.parsed);
        out.push_back(SourceFacts{
            .member = "src",
            .facts = apogee::graph::extract_code_facts(*language, path, source, parse)});
    }
    return out;
}

[[nodiscard]] CodeGraph link(const std::map<std::string, std::string>& files) {
    return apogee::graph::resolve_code(facts_of(files));
}

[[nodiscard]] bool has_edge(const CodeGraph& graph, std::string_view source,
                            std::string_view relation, std::string_view target_type,
                            std::string_view target) {
    return std::ranges::any_of(graph.edges, [&](const CodeEdge& edge) {
        return edge.source_name == source && edge.relation == relation &&
               edge.target_type == target_type && edge.target_name == target;
    });
}

[[nodiscard]] bool has_node(const CodeGraph& graph, std::string_view type, std::string_view name) {
    return std::ranges::any_of(
        graph.nodes, [&](const CodeNode& node) { return node.type == type && node.name == name; });
}

}  // namespace

TEST_CASE("a call links across files by qualified name, and the counts say what resolved",
          "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"a.cc", "namespace util { int helper(int x) { return x; } }\n"},
        {"b.cc",
         "namespace util { int helper(int x); }\n"
         "int run() { return util::helper(1) + std::max(1, 2); }\n"},
    });
    CHECK(has_edge(graph, "run", "calls", "function", "util::helper"));
    CHECK(has_edge(graph, "run", "calls", "name", "std::max"));
    CHECK(graph.counts.calls == 2);
    CHECK(graph.counts.calls_resolved == 1);
    CHECK(graph.counts.unresolved() >= 1);
    // The declaration and the definition are one node, stated twice.
    const auto helper = std::ranges::find_if(
        graph.nodes, [](const CodeNode& node) { return node.name == "util::helper"; });
    REQUIRE(helper != graph.nodes.end());
    CHECK(helper->mentions.size() == 2);
}

TEST_CASE("two candidates are as unresolved as none: an ambiguous import is a name",
          "[graph][code][resolve]") {
    // `import util` from a file at the top: neither a/util.py nor b/util.py is
    // the module `util`, and the suffix rule finds two.
    const CodeGraph graph = link({
        {"a/util.py", "def f():\n    return 1\n"},
        {"b/util.py", "def f():\n    return 2\n"},
        {"main.py", "import util\n\n\ndef run():\n    return util.f()\n"},
    });
    CHECK(has_edge(graph, "main.py", "imports", "name", "util"));
    CHECK(has_edge(graph, "main.run", "calls", "name", "util.f"));
    CHECK_FALSE(has_edge(graph, "main.run", "calls", "function", "a.util.f"));
    CHECK_FALSE(has_edge(graph, "main.run", "calls", "function", "b.util.f"));
    CHECK(graph.counts.imports_resolved == 0);
}

TEST_CASE("a reference never resolves by resemblance or by case", "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"store/store.go",
         "package store\n\nfunc Parse() int { return 1 }\n\nfunc parse() int { return Parse() }\n"},
        {"main.go",
         "package main\n\nimport \"example.com/x/store\"\n\nfunc main() { store.parse(); "
         "store.Parsed() }\n"},
    });
    CHECK(has_node(graph, "function", "store.Parse"));
    CHECK(has_node(graph, "function", "store.parse"));
    CHECK(has_edge(graph, "store.parse", "calls", "function", "store.Parse"));
    CHECK(has_edge(graph, "main.main", "calls", "function", "store.parse"));
    // `Parsed` is not `Parse`: no near match is ever taken.
    CHECK(has_edge(graph, "main.main", "calls", "name", "store.Parsed"));
}

TEST_CASE("recursion records no self edge", "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"f.py", "def walk(n):\n    return walk(n - 1) if n else 0\n"},
    });
    CHECK_FALSE(has_edge(graph, "f.walk", "calls", "function", "f.walk"));
    CHECK(graph.counts.calls == 1);
    CHECK(graph.counts.calls_resolved == 1);
}

TEST_CASE("a member call resolves through a declared type, and through a base",
          "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"shape.h",
         "struct Shape { double area() const; };\n"
         "struct Square : Shape { double side() const; };\n"},
        {"use.cpp",
         "#include \"shape.h\"\n"
         "double use(const Square& s, Square* p) { return s.area() + p->side(); }\n"},
    });
    CHECK(has_edge(graph, "use", "calls", "function", "Shape::area"));
    CHECK(has_edge(graph, "use", "calls", "function", "Square::side"));
    CHECK(has_edge(graph, "Square", "inherits", "class", "Shape"));
    CHECK(has_edge(graph, "use", "references", "class", "Square"));
    CHECK(has_edge(graph, "use.cpp", "imports", "file", "shape.h"));
}

TEST_CASE("the graph never depends on the order the files arrive in", "[graph][code][resolve]") {
    std::vector<SourceFacts> facts = facts_of({
        {"pkg/models.py",
         "class Base:\n    def hi(self):\n        return 1\n\n\nclass User(Base):\n"
         "    pass\n"},
        {"pkg/service.py", "from .models import User\n\n\ndef run(u: User):\n    return u.hi()\n"},
        {"main.py", "from pkg import service\n\nservice.run(None)\n"},
    });
    const CodeGraph ordered = apogee::graph::resolve_code(facts);
    std::ranges::reverse(facts);
    const CodeGraph reversed = apogee::graph::resolve_code(facts);
    std::mt19937 shuffle{42};
    std::ranges::shuffle(facts, shuffle);
    const CodeGraph shuffled = apogee::graph::resolve_code(facts);
    const auto same = [](const CodeGraph& a, const CodeGraph& b) {
        if (a.nodes.size() != b.nodes.size() || a.edges.size() != b.edges.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.nodes.size(); ++i) {
            if (a.nodes[i].name != b.nodes[i].name || a.nodes[i].mentions != b.nodes[i].mentions ||
                a.nodes[i].metadata != b.nodes[i].metadata) {
                return false;
            }
        }
        for (std::size_t i = 0; i < a.edges.size(); ++i) {
            if (a.edges[i].source_name != b.edges[i].source_name ||
                a.edges[i].target_name != b.edges[i].target_name ||
                a.edges[i].relation != b.edges[i].relation ||
                a.edges[i].sites != b.edges[i].sites) {
                return false;
            }
        }
        return true;
    };
    CHECK(same(ordered, reversed));
    CHECK(same(ordered, shuffled));

    // Where a lookup could depend on which file came first -- a field a C#
    // partial class declares in two files, with two types -- the pick is by
    // path, whatever the order.
    std::vector<SourceFacts> partial = facts_of({
        {"a.cs", "class A { public void X() {} }\npartial class P { A f; }\n"},
        {"b.cs", "class B { public void X() {} }\npartial class P { B f; void M() { f.X(); } }\n"},
    });
    const CodeGraph forward = apogee::graph::resolve_code(partial);
    std::ranges::reverse(partial);
    const CodeGraph backward = apogee::graph::resolve_code(partial);
    CHECK(same(forward, backward));
    CHECK(has_edge(forward, "P.M", "calls", "function", "A.X"));
    CHECK(has_edge(ordered, "pkg.service.run", "calls", "function", "pkg.models.Base.hi"));
    CHECK(has_edge(ordered, "main.py", "calls", "function", "pkg.service.run"));
}

TEST_CASE("an internal-linkage definition is its file's own", "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"a.c", "static int clamp(int v) { return v; }\nint a(void) { return clamp(1); }\n"},
        {"b.c", "static int clamp(int v) { return -v; }\nint b(void) { return clamp(2); }\n"},
    });
    CHECK(has_edge(graph, "a", "calls", "function", "a.c::clamp"));
    CHECK(has_edge(graph, "b", "calls", "function", "b.c::clamp"));
    CHECK_FALSE(has_edge(graph, "a", "calls", "function", "b.c::clamp"));
}

TEST_CASE("a package's __init__ is the package: what it defines is the package's",
          "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"pkg/__init__.py", "from .util import helper\n\n\ndef version():\n    return helper()\n"},
        {"pkg/util.py", "def helper():\n    return 1\n"},
        // Another `util`, so only the package-relative lookup finds the one.
        {"other/util.py", "def helper():\n    return 2\n"},
        {"main.py", "import pkg\n\n\ndef run():\n    return pkg.version()\n"},
    });
    CHECK(has_node(graph, "function", "pkg.version"));
    CHECK(has_edge(graph, "main.run", "calls", "function", "pkg.version"));
    // A relative import inside the package's own __init__ starts from it.
    CHECK(has_edge(graph, "pkg.version", "calls", "function", "pkg.util.helper"));
    CHECK(has_edge(graph, "main.py", "imports", "file", "pkg/__init__.py"));
}

TEST_CASE("an include found tree-wide must be the only one", "[graph][code][resolve]") {
    const CodeGraph graph = link({
        {"a/util.h", "int util_a();\n"},
        {"b/util.h", "int util_b();\n"},
        {"lib/one.h", "int one();\n"},
        {"src/main.cpp",
         "#include \"util.h\"\n#include \"lib/one.h\"\nint main() { return one(); }\n"},
    });
    CHECK(has_edge(graph, "src/main.cpp", "imports", "name", "util.h"));
    CHECK(has_edge(graph, "src/main.cpp", "imports", "file", "lib/one.h"));
    CHECK(has_edge(graph, "main", "calls", "function", "one"));
}
