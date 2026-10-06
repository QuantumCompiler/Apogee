#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "graph/code_languages.h"
#include "graph/code_parser.h"

/// One source file's **facts** (27k): what a parse states about it, before
/// anything is linked across files -- the definitions it holds and the
/// qualified name each one has, the imports it makes, the calls and type
/// references it writes (as written), the bases its classes name, and the
/// declared types of its parameters, locals and fields.
///
/// Deterministic and pure: the same bytes at the same path through the same
/// grammar and extractor always yield the same facts, which is what lets
/// `graph update` keep a file's facts (cached as JSON in its `kg_state` row)
/// until its content hash moves, and re-link the whole tree from the cache
/// without re-parsing a file it did not change. Cross-file linking is
/// `code_resolve`'s; nothing here looks past the one file.
///
/// No model, no network, no harness: generation never arrives here, because
/// this extractor takes none.
namespace apogee::graph {

/// A module, class or function the file defines -- or declares, for a
/// function prototype or an interface method.
struct CodeDefinition {
    /// `module`, `class` or `function`.
    std::string kind;
    /// The lookup path: the file's module, then every enclosing scope, then
    /// the definition's own name, split at the language's separator.
    std::vector<std::string> segments;
    /// Set for a name scoped to its file -- `<path>::` -- for every
    /// definition in a file-scoped language and for a C/C++ definition with
    /// internal linkage (`static`, or inside an anonymous namespace).
    std::string prefix;
    /// The identity: `prefix` then `segments` joined with the separator.
    std::string qualified;
    std::uint32_t line = 0;
    std::uint32_t end_line = 0;
    /// The header up to the body, whitespace collapsed and clipped.
    std::string signature;
    bool declaration = false;
    /// The syntactically enclosing definition (index), or -1 at file level.
    int parent = -1;
};

/// One import the file makes.
struct CodeImport {
    /// `module` (a module or file named by path), `relative` (a path relative
    /// to the importing file), `namespace` (every name under a prefix becomes
    /// visible: `using namespace`, `import x.*`, `from x import *`) or
    /// `symbol` (one qualified name bound locally: a C++ using-declaration,
    /// a Java single-type import, a C# alias).
    std::string kind;
    /// The path as written, quotes and brackets removed.
    std::string path;
    /// The symbol imported from `path` (`from x import name`,
    /// `import { name } from 'x'`), when one is.
    std::string name;
    /// The local name it is bound to, when renamed or when the language
    /// binds the module itself (`import x as y`, `import * as y`).
    std::string alias;
    std::uint32_t line = 0;
};

/// A name written in the file: a call's callee or a type in a signature.
struct CodeReference {
    /// `call` or `type`.
    std::string kind;
    /// As written, cleaned: generic arguments and whitespace removed, a
    /// member access kept as `.` or `->`.
    std::string text;
    std::uint32_t line = 0;
    /// The definition the reference is made from (index), or -1: the file.
    int owner = -1;
};

/// A base a class (or a Rust type, by `impl Trait for Type`) names.
struct CodeInherit {
    std::string child;
    std::string base;
    std::uint32_t line = 0;
    /// The definition whose scope both names are looked up from, or -1.
    int owner = -1;
    /// The class definition `child` names, when the pattern captured its
    /// own name node; -1 when it is a name to look up (a Rust impl).
    int child_definition = -1;
};

/// A declared type: a parameter's, a local's or a field's -- what lets a
/// call through `store.add(...)` reach `Store::add`.
struct CodeTyped {
    /// `param`, `local` or `field`.
    std::string kind;
    std::string name;
    std::string type;
    /// The function a parameter or local belongs to, the class a field
    /// belongs to (index), or -1.
    int owner = -1;
};

struct FileFacts {
    /// Relative to the source root, `/`-separated.
    std::string path;
    /// The roster name: `cpp`, `python`.
    std::string language;
    std::uint32_t lines = 0;
    /// The parse recovered from an error: the facts are partial.
    bool has_errors = false;
    /// The file's own scope: its package, module path or namespace.
    std::vector<std::string> module;
    /// `<path>::` for a file-scoped language, else empty.
    std::string prefix;
    std::vector<CodeDefinition> definitions;
    std::vector<CodeImport> imports;
    std::vector<CodeReference> references;
    std::vector<CodeInherit> inherits;
    std::vector<CodeTyped> typed;
};

/// The longest signature kept (codepoints).
inline constexpr std::size_t kMaxSignatureLength = 200;

/// The facts of one parsed file. `path` is relative to the source root,
/// `/`-separated; it is part of the facts (file-scoped names, a Python or
/// Rust module path, a Go package directory).
[[nodiscard]] FileFacts extract_code_facts(const CodeLanguage& language, std::string_view path,
                                           std::string_view source, const CodeParseResult& parse);

/// Joins segments with the language's separator.
[[nodiscard]] std::string join_segments(const std::vector<std::string>& segments,
                                        std::string_view separator);

/// The identifier paths a type expression names (`const std::vector<Chunk>&`
/// -> `std::vector`, `Chunk`), builtins dropped and wrappers kept. In order,
/// without duplicates.
[[nodiscard]] std::vector<std::string> type_names(const CodeLanguage& language,
                                                  std::string_view type);

/// The type a member access through a variable of `type` reaches:
/// the first named type, or -- through a wrapper, when `through_pointer` or
/// the language dereferences it implicitly -- its first argument. Empty when
/// the type is a builtin or names nothing.
[[nodiscard]] std::string receiver_type(const CodeLanguage& language, std::string_view type,
                                        bool through_pointer);

/// The facts as compact JSON, and back -- the cache a file's `kg_state` row
/// carries. `facts_from_json` throws on a document that is not one.
[[nodiscard]] std::string facts_to_json(const FileFacts& facts);
[[nodiscard]] FileFacts facts_from_json(std::string_view json);

}  // namespace apogee::graph
