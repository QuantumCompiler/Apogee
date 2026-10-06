#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "graph/code_languages.h"

/// The one boundary class over tree-sitter's C API (27k).
///
/// tree-sitter hands out raw handles -- a parser, a tree, a compiled query, a
/// query cursor -- each freed by its own function. They are wrapped in
/// `std::unique_ptr`s with custom deleters inside `CodeParser`'s
/// implementation, and **no raw handle, and no tree-sitter type, ever leaves
/// it**: what crosses is plain data -- the captures a language's query
/// produced, each with its byte range and lines. Nothing else in the tree
/// includes `tree_sitter/api.h` (`harness.layering` holds that), so every
/// other piece of the code graph is ordinary C++ over strings and offsets,
/// testable without a parse.
///
/// Text predicates (`#eq?`, `#not-eq?`, `#any-of?`, `#not-any-of?`) are
/// evaluated here: the C library leaves them to its host.
namespace apogee::graph {

/// One captured node: the capture's name and where the node lies. Lines are
/// 1-based; `end_line` is the line the node's last byte is on.
struct CodeCapture {
    std::string name;
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    std::uint32_t start_line = 0;
    std::uint32_t end_line = 0;
};

/// One match of one query pattern: its captures, in pattern order.
struct CodeMatch {
    std::vector<CodeCapture> captures;

    /// The first capture named `name`, or null.
    [[nodiscard]] const CodeCapture* find(std::string_view name) const noexcept;
};

struct CodeParseResult {
    /// False when the grammar produced no tree at all (the parse was refused).
    bool parsed = false;
    /// The tree carries an ERROR node -- text the grammar could not place,
    /// skipped by the parse's recovery -- so what the matches hold is
    /// partial. A MISSING node (a token assumed, no text lost) is not one.
    bool has_errors = false;
    std::vector<CodeMatch> matches;
    std::string error;
};

/// A parser and its language's compiled query, reused across files.
class CodeParser {
public:
    /// Binds the grammar and compiles the language's query. Throws
    /// `std::logic_error` naming the language and the offset when the query
    /// does not compile against the pinned grammar -- a bug in the roster,
    /// which the suite's per-language goldens catch first.
    explicit CodeParser(CodeLanguageId language);
    ~CodeParser();

    CodeParser(const CodeParser&) = delete;
    CodeParser& operator=(const CodeParser&) = delete;
    CodeParser(CodeParser&&) noexcept;
    CodeParser& operator=(CodeParser&&) noexcept;

    /// Parses `source` (UTF-8) and runs the query over the whole tree.
    [[nodiscard]] CodeParseResult parse(std::string_view source);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace apogee::graph
