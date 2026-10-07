#include "graph/code_parser.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "graph/code_extract.h"
#include "graph/code_languages.h"

/// The one boundary over tree-sitter's C API (27k), and the roster it reads:
/// every vendored grammar loads into this runtime and compiles its query;
/// the text predicates the C library leaves to its host are evaluated here;
/// an ERROR node marks a parse partial and a MISSING one does not; and the
/// roster maps names and extensions the way every report prints them.
namespace {

using apogee::graph::CodeLanguage;
using apogee::graph::CodeLanguageId;
using apogee::graph::CodeParser;
using apogee::graph::CodeParseResult;

[[nodiscard]] const CodeLanguage& language(CodeLanguageId id) {
    return apogee::graph::code_language(id);
}

}  // namespace

TEST_CASE("every vendored grammar loads into the runtime and compiles its query",
          "[graph][code][parser]") {
    for (const CodeLanguage& entry : apogee::graph::code_languages()) {
        DYNAMIC_SECTION(entry.name) {
            CodeParser parser{entry.id};
            const CodeParseResult empty = parser.parse("");
            CHECK(empty.parsed);
            CHECK_FALSE(empty.has_errors);
            CHECK(empty.matches.empty());
        }
    }
}

TEST_CASE("captures cross the boundary as plain data, with 1-based lines",
          "[graph][code][parser]") {
    CodeParser parser{CodeLanguageId::Python};
    const CodeParseResult result = parser.parse("\n\ndef walk(n):\n    return n\n");
    REQUIRE(result.parsed);
    bool found = false;
    for (const apogee::graph::CodeMatch& match : result.matches) {
        if (const apogee::graph::CodeCapture* definition = match.find("definition.function")) {
            found = true;
            CHECK(definition->start_line == 3);
            CHECK(definition->end_line == 4);
            const apogee::graph::CodeCapture* name = match.find("name");
            REQUIRE(name != nullptr);
            CHECK(name->end_byte - name->start_byte == 4);
        }
    }
    CHECK(found);
}

TEST_CASE("the host evaluates the text predicates, and a `_` capture never reaches the extractor",
          "[graph][code][parser]") {
    // `#eq? @_require "require"`: only a call to `require` is an import.
    CodeParser parser{CodeLanguageId::JavaScript};
    const CodeParseResult result =
        parser.parse("const fs = require('fs');\nconst x = load('y');\n");
    int imports = 0;
    for (const apogee::graph::CodeMatch& match : result.matches) {
        if (match.find("import") != nullptr) {
            ++imports;
        }
        for (const apogee::graph::CodeCapture& capture : match.captures) {
            CHECK_FALSE(capture.name.starts_with('_'));
        }
    }
    CHECK(imports == 1);
    // `#any-of?`: `source` and `.` both read a script in.
    CodeParser bash{CodeLanguageId::Bash};
    const apogee::graph::FileFacts facts = apogee::graph::extract_code_facts(
        language(CodeLanguageId::Bash), "main.sh", "source a.sh\n. b.sh\nrun c.sh\n",
        bash.parse("source a.sh\n. b.sh\nrun c.sh\n"));
    REQUIRE(facts.imports.size() == 2);
    CHECK(facts.imports[0].path == "a.sh");
    CHECK(facts.imports[1].path == "b.sh");
}

TEST_CASE("an ERROR node makes a parse partial; a MISSING token does not",
          "[graph][code][parser]") {
    CodeParser python{CodeLanguageId::Python};
    CHECK(python.parse("def broken():\n    return )))\n").has_errors);
    CHECK_FALSE(python.parse("def fine():\n    pass\n").has_errors);
    // The pinned C++ grammar assumes a token for every `= {}` default
    // argument -- no text lost, so not partial.
    CodeParser cpp{CodeLanguageId::Cpp};
    CHECK_FALSE(cpp.parse("void f(Options options = {});\n").has_errors);
}

TEST_CASE("the roster: names, aliases, extensions and the header rule",
          "[graph][code][languages]") {
    CHECK(apogee::graph::code_languages().size() == 12);
    CHECK(apogee::graph::code_language_names() ==
          "c, cpp, python, javascript, typescript, tsx, go, rust, java, csharp, ruby, bash");
    CHECK(apogee::graph::code_language_by_name("C++")->id == CodeLanguageId::Cpp);
    CHECK(apogee::graph::code_language_by_name("py")->id == CodeLanguageId::Python);
    CHECK(apogee::graph::code_language_by_name("c#")->id == CodeLanguageId::CSharp);
    CHECK(apogee::graph::code_language_by_name("cobol") == nullptr);

    using apogee::graph::code_language_for_path;
    CHECK(code_language_for_path("src/a.TSX", false)->id == CodeLanguageId::Tsx);
    CHECK(code_language_for_path("a/b.rs", false)->id == CodeLanguageId::Rust);
    CHECK(code_language_for_path("x.d.ts", false)->id == CodeLanguageId::TypeScript);
    CHECK(code_language_for_path("Rakefile.rake", false)->id == CodeLanguageId::Ruby);
    CHECK(code_language_for_path("notes.txt", false) == nullptr);
    CHECK(code_language_for_path("Makefile", false) == nullptr);
    CHECK(code_language_for_path(".bashrc", false) == nullptr);
    // A `.h` is C unless the tree holds C++.
    CHECK(code_language_for_path("a.h", false)->id == CodeLanguageId::C);
    CHECK(code_language_for_path("a.h", true)->id == CodeLanguageId::Cpp);
    CHECK(apogee::graph::tree_has_cpp({"a.c", "a.h", "b.cc"}));
    CHECK_FALSE(apogee::graph::tree_has_cpp({"a.c", "a.h"}));

    // The extractor a file's facts came from: language, grammar pin, version.
    CHECK(apogee::graph::code_extractor_id(language(CodeLanguageId::Go)) ==
          "go:tree-sitter-go v0.25.0:extractor-" +
              std::to_string(apogee::graph::kCodeExtractorVersion));
}

TEST_CASE("type expressions name their identifiers, builtins dropped and wrappers read through",
          "[graph][code][extract]") {
    const CodeLanguage& cpp = language(CodeLanguageId::Cpp);
    CHECK(apogee::graph::type_names(cpp, "const std::vector<Chunk>&") ==
          std::vector<std::string>{"std::vector", "Chunk"});
    CHECK(apogee::graph::type_names(cpp, "unsigned long long").empty());
    // C++ reaches through a smart pointer only with `->`.
    CHECK(apogee::graph::receiver_type(cpp, "std::unique_ptr<geo::Shape>", true) == "geo::Shape");
    CHECK(apogee::graph::receiver_type(cpp, "std::unique_ptr<geo::Shape>", false) ==
          "std::unique_ptr");
    CHECK(apogee::graph::receiver_type(cpp, "int", false).empty());
    // Rust reads through its smart pointers implicitly, and skips lifetimes.
    const CodeLanguage& rust = language(CodeLanguageId::Rust);
    CHECK(apogee::graph::receiver_type(rust, "&'a Box<dyn Shape>", false) == "Shape");
    CHECK(apogee::graph::type_names(rust, "&'a mut Vec<u8>") == std::vector<std::string>{"Vec"});
    // java.lang's own types are the language's.
    CHECK(apogee::graph::type_names(language(CodeLanguageId::Java), "Map<String, Dog>") ==
          std::vector<std::string>{"Map", "Dog"});
}

TEST_CASE("a file's facts survive their JSON cache whole", "[graph][code][extract]") {
    const CodeLanguage& cpp = language(CodeLanguageId::Cpp);
    const std::string source =
        "#include \"a.h\"\nnamespace n {\nclass B : public A {\n  int f(Store& s) { return "
        "s.add(1); }\n};\n}\n";
    CodeParser parser{cpp.id};
    const apogee::graph::FileFacts facts =
        apogee::graph::extract_code_facts(cpp, "x.cpp", source, parser.parse(source));
    REQUIRE_FALSE(facts.definitions.empty());
    REQUIRE_FALSE(facts.imports.empty());
    REQUIRE_FALSE(facts.references.empty());
    REQUIRE_FALSE(facts.inherits.empty());
    REQUIRE_FALSE(facts.typed.empty());
    const std::string json = apogee::graph::facts_to_json(facts);
    CHECK(apogee::graph::facts_to_json(apogee::graph::facts_from_json(json)) == json);
    CHECK_THROWS(apogee::graph::facts_from_json("{not json"));
    CHECK_THROWS(apogee::graph::facts_from_json(R"({"path": "x"})"));
}

TEST_CASE("a signature is the header before the body, whitespace collapsed and clipped",
          "[graph][code][extract]") {
    const CodeLanguage& python = language(CodeLanguageId::Python);
    const std::string long_name(300, 'a');
    const std::string source =
        "def f(\n    x,\n    y,\n):\n    return x\n\n\ndef " + long_name + "():\n    pass\n";
    CodeParser parser{python.id};
    const apogee::graph::FileFacts facts =
        apogee::graph::extract_code_facts(python, "m.py", source, parser.parse(source));
    REQUIRE(facts.definitions.size() == 2);
    CHECK(facts.definitions[0].signature == "def f( x, y, )");
    CHECK(facts.definitions[0].qualified == "m.f");
    // Clipped at 200 codepoints, with an ellipsis.
    CHECK(facts.definitions[1].signature.size() ==
          apogee::graph::kMaxSignatureLength + std::string{"…"}.size());
    CHECK(facts.lines == 9);
}
