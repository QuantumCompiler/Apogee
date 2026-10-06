#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The code graph's language roster (27k): which grammars are vendored, the
/// file extensions each claims, how a language qualifies a name, and the
/// tree-sitter query each is read with -- **data, beside the extractor**.
///
/// A query names what the extractor wants out of a parse with a small, fixed
/// vocabulary of capture names, the same in every language, so
/// `code_extract` is one function over every grammar:
///
///   @definition.module | .class | .function, @declaration.function
///       with @name, and optionally @body (where the signature ends),
///       @return.type, @static (C/C++ internal linkage), @receiver (a Go
///       method's receiver type)
///   @scope.anonymous   a C++ anonymous namespace: internal linkage
///   @scope.impl        a Rust impl block: a scope segment, no node
///   @package           with @name: the file's package or namespace
///   @call              with @call.target, or @call.name and @call.receiver
///                      (a JSX element rendering a component is a call)
///   @import | .relative | .namespace | .symbol
///                      with @import.path, optionally @import.name and
///                      @import.alias
///   @inherit.child + @inherit.base
///   @param.name + @param.type, @local.name + @local.type,
///   @field.name + @field.type
///
/// Captures whose name starts with `_` exist for a predicate (`#eq?`,
/// `#any-of?`) and carry nothing to the extractor.
///
/// Nothing here includes tree-sitter: the grammars' entry points are bound
/// in `code_parser.cpp`, the one boundary class over the C API.
namespace apogee::graph {

enum class CodeLanguageId : std::uint8_t {
    C,
    Cpp,
    Python,
    JavaScript,
    TypeScript,
    Tsx,
    Go,
    Rust,
    Java,
    CSharp,
    Ruby,
    Bash,
};

/// How a language turns a definition's place into its qualified name.
enum class Qualification : std::uint8_t {
    /// C, C++, Ruby: one global namespace; scopes nest by name. A C/C++
    /// definition with internal linkage is qualified by its file.
    Global,
    /// Python: the module is the file's path, dotted.
    ModulePath,
    /// JavaScript, TypeScript, Bash: a file is its own scope; names are
    /// qualified `<path>::<name>`.
    FileScoped,
    /// Java, C#: the declared package or namespace.
    Package,
    /// Go: the package is the file's directory.
    PackageDirectory,
    /// Rust: the module is the file's path under its crate's `src/`.
    CrateModule,
};

struct CodeLanguage {
    CodeLanguageId id = CodeLanguageId::C;
    /// The name `--lang` takes and every report prints: `cpp`, `python`.
    std::string_view name;
    std::string_view display;
    /// The pinned grammar, as third_party/tree-sitter/CMakeLists.txt pins it.
    std::string_view grammar;
    /// Between a name's segments: `::` or `.`.
    std::string_view separator;
    Qualification qualification = Qualification::Global;
    /// What its scope-defining construct is called, for a module node's
    /// description: `namespace`, `package`, `module`.
    std::string_view module_noun;
    std::span<const std::string_view> extensions;
    /// Type names that are the language's own (`int`, `str`, `void`): never
    /// a reference, never a receiver type.
    std::span<const std::string_view> builtin_types;
    /// Wrapper types whose first type argument is what a member access
    /// reaches (`std::unique_ptr<T>`, `Option<T>`).
    std::span<const std::string_view> wrapper_types;
    std::string_view query;
    /// Patterns appended to `query` for this grammar alone -- TSX's JSX,
    /// which the TypeScript grammar it shares a query with does not have.
    std::string_view extra_query;
};

/// Bumped whenever the queries or the extraction rules change what a parse
/// yields: part of every file's fingerprint, so the next `graph update`
/// re-parses everything rather than mixing two extractors' facts.
inline constexpr int kCodeExtractorVersion = 1;

/// The roster, in a fixed order.
[[nodiscard]] std::span<const CodeLanguage> code_languages() noexcept;

[[nodiscard]] const CodeLanguage& code_language(CodeLanguageId id) noexcept;

/// By `--lang` name (`cpp`, `python`, ...) or a common alias (`c++`, `py`,
/// `js`, `ts`, `cs`, `rb`, `sh`); null when unknown.
[[nodiscard]] const CodeLanguage* code_language_by_name(std::string_view name);

/// The language a file is parsed as, by extension (case-insensitive); null
/// when no vendored grammar claims it. A `.h` header is C++ when
/// `headers_are_cpp` -- the tree holds C++ sources -- and C otherwise.
[[nodiscard]] const CodeLanguage* code_language_for_path(std::string_view path,
                                                         bool headers_are_cpp);

/// Whether `paths` holds any file only C++ claims -- the `.h` rule's input.
[[nodiscard]] bool tree_has_cpp(const std::vector<std::string>& paths);

/// The extractor a file's facts came from, recorded with them: the language,
/// the grammar pin and `kCodeExtractorVersion` --
/// `cpp:tree-sitter-cpp v0.23.4:extractor-1`. A different value means
/// re-parse; the language before the first `:` is what `graph stats` counts.
[[nodiscard]] std::string code_extractor_id(const CodeLanguage& language);

/// Every `--lang` name, comma-separated -- for help text and refusals.
[[nodiscard]] std::string code_language_names();

}  // namespace apogee::graph
