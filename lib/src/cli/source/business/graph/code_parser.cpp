#include "graph/code_parser.h"

#include <tree_sitter/api.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

// The grammars' entry points, one per vendored grammar
// (third_party/tree-sitter/CMakeLists.txt). Declared here and nowhere else:
// this file is the boundary, and a TSLanguage never leaves it.
extern "C" {
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_python();
const TSLanguage* tree_sitter_javascript();
const TSLanguage* tree_sitter_typescript();
const TSLanguage* tree_sitter_tsx();
const TSLanguage* tree_sitter_go();
const TSLanguage* tree_sitter_rust();
const TSLanguage* tree_sitter_java();
const TSLanguage* tree_sitter_c_sharp();
const TSLanguage* tree_sitter_ruby();
const TSLanguage* tree_sitter_bash();
}

namespace apogee::graph {
namespace {

struct ParserDeleter {
    void operator()(TSParser* parser) const noexcept {
        ts_parser_delete(parser);
    }
};

struct TreeDeleter {
    void operator()(TSTree* tree) const noexcept {
        ts_tree_delete(tree);
    }
};

struct QueryDeleter {
    void operator()(TSQuery* query) const noexcept {
        ts_query_delete(query);
    }
};

struct CursorDeleter {
    void operator()(TSQueryCursor* cursor) const noexcept {
        ts_query_cursor_delete(cursor);
    }
};

using ParserPtr = std::unique_ptr<TSParser, ParserDeleter>;
using TreePtr = std::unique_ptr<TSTree, TreeDeleter>;
using QueryPtr = std::unique_ptr<TSQuery, QueryDeleter>;
using CursorPtr = std::unique_ptr<TSQueryCursor, CursorDeleter>;

[[nodiscard]] const TSLanguage* grammar_of(CodeLanguageId id) {
    switch (id) {
        case CodeLanguageId::C:
            return tree_sitter_c();
        case CodeLanguageId::Cpp:
            return tree_sitter_cpp();
        case CodeLanguageId::Python:
            return tree_sitter_python();
        case CodeLanguageId::JavaScript:
            return tree_sitter_javascript();
        case CodeLanguageId::TypeScript:
            return tree_sitter_typescript();
        case CodeLanguageId::Tsx:
            return tree_sitter_tsx();
        case CodeLanguageId::Go:
            return tree_sitter_go();
        case CodeLanguageId::Rust:
            return tree_sitter_rust();
        case CodeLanguageId::Java:
            return tree_sitter_java();
        case CodeLanguageId::CSharp:
            return tree_sitter_c_sharp();
        case CodeLanguageId::Ruby:
            return tree_sitter_ruby();
        case CodeLanguageId::Bash:
            return tree_sitter_bash();
    }
    return nullptr;
}

[[nodiscard]] std::string_view query_error_name(TSQueryError error) {
    switch (error) {
        case TSQueryErrorSyntax:
            return "syntax";
        case TSQueryErrorNodeType:
            return "unknown node type";
        case TSQueryErrorField:
            return "unknown field";
        case TSQueryErrorCapture:
            return "unknown capture";
        case TSQueryErrorStructure:
            return "impossible pattern";
        case TSQueryErrorLanguage:
            return "language";
        case TSQueryErrorNone:
            break;
    }
    return "error";
}

/// One text predicate of one pattern: `#<op> @capture <value>...`, a value
/// being a string or another capture.
struct Predicate {
    enum class Op : std::uint8_t { Eq, NotEq, AnyOf, NotAnyOf };
    Op op = Op::Eq;
    std::uint32_t capture = 0;
    /// The literal values, or empty when compared against `other`.
    std::vector<std::string> values;
    bool against_capture = false;
    std::uint32_t other = 0;
};

/// Whether the tree holds an ERROR node: text the grammar could not place,
/// which the parse skipped. A MISSING node -- a token the parser assumed to
/// recover, zero bytes wide -- loses no text, and is not counted: the
/// pinned C++ grammar inserts one for every `= {}` default argument.
// NOLINTNEXTLINE(misc-no-recursion): bounded by the tree's depth, and only
// along the branches that carry an error.
[[nodiscard]] bool contains_error(TSNode node) {
    if (ts_node_is_error(node)) {
        return true;
    }
    if (!ts_node_has_error(node)) {
        return false;
    }
    const std::uint32_t count = ts_node_child_count(node);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (contains_error(ts_node_child(node, i))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string_view node_text(std::string_view source, TSNode node) {
    const std::uint32_t start = ts_node_start_byte(node);
    const std::uint32_t end = ts_node_end_byte(node);
    if (start >= source.size() || end < start) {
        return {};
    }
    return source.substr(start, std::min<std::size_t>(end, source.size()) - start);
}

}  // namespace

struct CodeParser::Impl {
    CodeLanguageId language = CodeLanguageId::C;
    ParserPtr parser;
    QueryPtr query;
    /// Per capture id: its name, and whether it reaches the extractor (a
    /// name starting with `_` exists only for a predicate).
    std::vector<std::string> capture_names;
    std::vector<bool> capture_public;
    std::vector<std::vector<Predicate>> predicates;

    [[nodiscard]] bool passes(const TSQueryMatch& match, std::string_view source) const {
        if (match.pattern_index >= predicates.size()) {
            return true;
        }
        const auto text_of = [&](std::uint32_t capture) -> std::string_view {
            for (std::uint16_t i = 0; i < match.capture_count; ++i) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API array.
                const TSQueryCapture& found = match.captures[i];
                if (found.index == capture) {
                    return node_text(source, found.node);
                }
            }
            return {};
        };
        for (const Predicate& predicate : predicates[match.pattern_index]) {
            const std::string_view text = text_of(predicate.capture);
            bool hit = false;
            if (predicate.against_capture) {
                hit = text == text_of(predicate.other);
            } else {
                hit = std::ranges::find(predicate.values, text) != predicate.values.end();
            }
            const bool negated =
                predicate.op == Predicate::Op::NotEq || predicate.op == Predicate::Op::NotAnyOf;
            if (hit == negated) {
                return false;
            }
        }
        return true;
    }
};

const CodeCapture* CodeMatch::find(std::string_view name) const noexcept {
    for (const CodeCapture& capture : captures) {
        if (capture.name == name) {
            return &capture;
        }
    }
    return nullptr;
}

CodeParser::CodeParser(CodeLanguageId language) : impl_{std::make_unique<Impl>()} {
    const CodeLanguage& spec = code_language(language);
    impl_->language = language;
    const TSLanguage* grammar = grammar_of(language);
    impl_->parser = ParserPtr{ts_parser_new()};
    if (grammar == nullptr || !ts_parser_set_language(impl_->parser.get(), grammar)) {
        throw std::logic_error("the " + std::string{spec.display} +
                               " grammar is not compatible with this tree-sitter runtime");
    }
    std::uint32_t error_offset = 0;
    TSQueryError error = TSQueryErrorNone;
    const std::string text = std::string{spec.query} + std::string{spec.extra_query};
    impl_->query = QueryPtr{ts_query_new(
        grammar, text.data(), static_cast<std::uint32_t>(text.size()), &error_offset, &error)};
    if (!impl_->query) {
        const std::string_view near =
            std::string_view{text}.substr(std::min<std::size_t>(error_offset, text.size()), 48);
        throw std::logic_error("the " + std::string{spec.display} + " query does not compile (" +
                               std::string{query_error_name(error)} + " at offset " +
                               std::to_string(error_offset) + ": " + std::string{near} + ")");
    }
    const std::uint32_t captures = ts_query_capture_count(impl_->query.get());
    for (std::uint32_t id = 0; id < captures; ++id) {
        std::uint32_t length = 0;
        const char* name = ts_query_capture_name_for_id(impl_->query.get(), id, &length);
        impl_->capture_names.emplace_back(name, length);
        impl_->capture_public.push_back(!impl_->capture_names.back().starts_with('_'));
    }
    const std::uint32_t patterns = ts_query_pattern_count(impl_->query.get());
    impl_->predicates.resize(patterns);
    for (std::uint32_t pattern = 0; pattern < patterns; ++pattern) {
        std::uint32_t count = 0;
        const TSQueryPredicateStep* steps =
            ts_query_predicates_for_pattern(impl_->query.get(), pattern, &count);
        std::vector<TSQueryPredicateStep> current;
        for (std::uint32_t i = 0; i < count; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API array.
            const TSQueryPredicateStep step = steps[i];
            if (step.type != TSQueryPredicateStepTypeDone) {
                current.push_back(step);
                continue;
            }
            if (current.size() < 3 || current[0].type != TSQueryPredicateStepTypeString ||
                current[1].type != TSQueryPredicateStepTypeCapture) {
                throw std::logic_error("the " + std::string{spec.display} +
                                       " query has a predicate this host does not evaluate");
            }
            std::uint32_t length = 0;
            const char* op_text =
                ts_query_string_value_for_id(impl_->query.get(), current[0].value_id, &length);
            const std::string op{op_text, length};
            Predicate predicate;
            if (op == "eq?") {
                predicate.op = Predicate::Op::Eq;
            } else if (op == "not-eq?") {
                predicate.op = Predicate::Op::NotEq;
            } else if (op == "any-of?") {
                predicate.op = Predicate::Op::AnyOf;
            } else if (op == "not-any-of?") {
                predicate.op = Predicate::Op::NotAnyOf;
            } else {
                throw std::logic_error("the " + std::string{spec.display} + " query uses #" + op +
                                       ", which this host does not evaluate");
            }
            predicate.capture = current[1].value_id;
            for (std::size_t j = 2; j < current.size(); ++j) {
                if (current[j].type == TSQueryPredicateStepTypeCapture) {
                    predicate.against_capture = true;
                    predicate.other = current[j].value_id;
                    continue;
                }
                const char* value =
                    ts_query_string_value_for_id(impl_->query.get(), current[j].value_id, &length);
                predicate.values.emplace_back(value, length);
            }
            impl_->predicates[pattern].push_back(std::move(predicate));
            current.clear();
        }
    }
}

CodeParser::~CodeParser() = default;
CodeParser::CodeParser(CodeParser&&) noexcept = default;
CodeParser& CodeParser::operator=(CodeParser&&) noexcept = default;

CodeParseResult CodeParser::parse(std::string_view source) {
    CodeParseResult out;
    if (source.size() > std::numeric_limits<std::uint32_t>::max()) {
        out.error = "too large to parse";
        return out;
    }
    // A fresh parse per file: no tree is kept between calls, so the parser's
    // only state is the language and nothing leaks from one file to the next.
    ts_parser_reset(impl_->parser.get());
    const TreePtr tree{ts_parser_parse_string(impl_->parser.get(), nullptr, source.data(),
                                              static_cast<std::uint32_t>(source.size()))};
    if (!tree) {
        out.error = "the parser produced no tree";
        return out;
    }
    out.parsed = true;
    const TSNode root = ts_tree_root_node(tree.get());
    out.has_errors = contains_error(root);
    const CursorPtr cursor{ts_query_cursor_new()};
    ts_query_cursor_exec(cursor.get(), impl_->query.get(), root);
    TSQueryMatch match{};
    while (ts_query_cursor_next_match(cursor.get(), &match)) {
        if (!impl_->passes(match, source)) {
            continue;
        }
        CodeMatch kept;
        kept.captures.reserve(match.capture_count);
        for (std::uint16_t i = 0; i < match.capture_count; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): C API array.
            const TSQueryCapture& capture = match.captures[i];
            if (capture.index >= impl_->capture_names.size() ||
                !impl_->capture_public[capture.index]) {
                continue;
            }
            CodeCapture item;
            item.name = impl_->capture_names[capture.index];
            item.start_byte = ts_node_start_byte(capture.node);
            item.end_byte = ts_node_end_byte(capture.node);
            const TSPoint start = ts_node_start_point(capture.node);
            const TSPoint end = ts_node_end_point(capture.node);
            item.start_line = start.row + 1;
            // A node ending exactly at a line break ends on the line before.
            item.end_line = end.column == 0 && end.row > start.row ? end.row : end.row + 1;
            kept.captures.push_back(std::move(item));
        }
        if (!kept.captures.empty()) {
            out.matches.push_back(std::move(kept));
        }
    }
    return out;
}

}  // namespace apogee::graph
