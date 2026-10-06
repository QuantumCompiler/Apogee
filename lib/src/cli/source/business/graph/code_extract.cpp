#include "graph/code_extract.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace apogee::graph {
namespace {

[[nodiscard]] bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

[[nodiscard]] bool is_ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$';
}

[[nodiscard]] std::string_view slice(std::string_view source, std::uint32_t start,
                                     std::uint32_t end) {
    if (start >= source.size() || end <= start) {
        return {};
    }
    return source.substr(start, std::min<std::size_t>(end, source.size()) - start);
}

[[nodiscard]] std::string_view slice(std::string_view source, const CodeCapture& capture) {
    return slice(source, capture.start_byte, capture.end_byte);
}

/// Whitespace runs collapsed to one space, the ends trimmed.
[[nodiscard]] std::string collapse(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out.push_back(' ');
            space = false;
        }
        out.push_back(c);
    }
    return out;
}

/// The first `limit` codepoints of `text`, with an ellipsis when cut.
[[nodiscard]] std::string clip(const std::string& text, std::size_t limit) {
    std::size_t codepoints = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        }
        if (codepoints == limit) {
            return text.substr(0, i) + "…";
        }
        i += length;
        ++codepoints;
    }
    return text;
}

[[nodiscard]] std::vector<std::string> split(std::string_view text, std::string_view separator) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t at = text.find(separator, start);
        const std::string_view part =
            text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
        if (!part.empty()) {
            out.emplace_back(part);
        }
        if (at == std::string_view::npos) {
            break;
        }
        start = at + separator.size();
    }
    return out;
}

[[nodiscard]] bool generic_args(CodeLanguageId id) {
    return id == CodeLanguageId::Cpp || id == CodeLanguageId::Java ||
           id == CodeLanguageId::CSharp || id == CodeLanguageId::TypeScript ||
           id == CodeLanguageId::Tsx || id == CodeLanguageId::Rust;
}

/// A written name or callee, cleaned: whitespace gone, generic arguments
/// (`<...>`, Go's `[...]`) dropped, `?.`/`!.` read as `.`, a leading `::`
/// dropped, a dangling `::` (a turbofish's) dropped. `->` is kept: through a
/// pointer, a C++ wrapper is dereferenced, and that is the only place it is.
[[nodiscard]] std::string clean_name(const CodeLanguage& language, std::string_view text) {
    std::string out;
    out.reserve(text.size());
    int angle = 0;
    int square = 0;
    const bool angles = generic_args(language.id);
    const bool squares = language.id == CodeLanguageId::Go;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            continue;
        }
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '>' && angle == 0) {
            out += "->";
            ++i;
            continue;
        }
        if (angles && c == '<') {
            ++angle;
            continue;
        }
        if (angles && c == '>' && angle > 0) {
            --angle;
            continue;
        }
        if (squares && c == '[') {
            ++square;
            continue;
        }
        if (squares && c == ']' && square > 0) {
            --square;
            continue;
        }
        if (angle > 0 || square > 0) {
            continue;
        }
        if ((c == '?' || c == '!') && i + 1 < text.size() && text[i + 1] == '.') {
            continue;
        }
        out.push_back(c);
    }
    while (out.starts_with("::")) {
        out.erase(0, 2);
    }
    while (out.ends_with("::")) {
        out.erase(out.size() - 2);
    }
    // A turbofish leaves `name::` before the call's own `::`-free tail;
    // `a::::b` cannot be written, so collapse any doubled separator.
    std::size_t doubled = out.find("::::");
    while (doubled != std::string::npos) {
        out.erase(doubled, 2);
        doubled = out.find("::::");
    }
    return out;
}

/// An import path as written, its quotes and brackets removed.
[[nodiscard]] std::string clean_path(std::string_view text) {
    std::string out = collapse(text);
    while (!out.empty() && (out.front() == '"' || out.front() == '\'' || out.front() == '<' ||
                            out.front() == '`')) {
        out.erase(0, 1);
    }
    while (!out.empty() && (out.back() == '"' || out.back() == '\'' || out.back() == '>' ||
                            out.back() == '`' || out.back() == ';')) {
        out.pop_back();
    }
    return out;
}

/// The part of a definition before its body: the header a reader scans.
[[nodiscard]] std::string signature_of(std::string_view source, const CodeCapture& definition,
                                       const CodeCapture* body) {
    std::string_view text;
    if (body != nullptr && body->start_byte > definition.start_byte) {
        text = slice(source, definition.start_byte, body->start_byte);
    } else {
        text = slice(source, definition);
        if (body == nullptr) {
            // No body capture (a Ruby method, a Rust struct): the first line.
            const std::size_t newline = text.find('\n');
            if (newline != std::string_view::npos) {
                text = text.substr(0, newline);
            }
        }
    }
    std::string out = collapse(text);
    // What opens a body is not part of the header.
    for (const std::string_view tail : {"{", ":", "=>", ";", " do"}) {
        if (out.ends_with(tail)) {
            out.erase(out.size() - tail.size());
            out = collapse(out);
        }
    }
    return clip(out, kMaxSignatureLength);
}

/// The file's own scope, from its path: a Python module, a Go package
/// directory, a Rust module. The package-declaring languages fill theirs
/// from the `@package` capture instead.
[[nodiscard]] std::vector<std::string> path_module(const CodeLanguage& language,
                                                   std::string_view path) {
    std::vector<std::string> parts = split(path, "/");
    if (parts.empty()) {
        return {};
    }
    std::string& last = parts.back();
    const std::size_t dot = last.find_last_of('.');
    if (dot != std::string::npos && dot > 0) {
        last.erase(dot);
    }
    switch (language.qualification) {
        case Qualification::ModulePath:
            if (parts.back() == "__init__") {
                parts.pop_back();
            }
            return parts;
        case Qualification::PackageDirectory: {
            parts.pop_back();
            if (parts.empty()) {
                return {};
            }
            std::string dir;
            for (const std::string& part : parts) {
                dir += (dir.empty() ? "" : "/") + part;
            }
            return {dir};
        }
        case Qualification::CrateModule: {
            std::vector<std::string> module;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (parts[i] == "src") {
                    module.clear();
                    continue;
                }
                module.push_back(parts[i]);
            }
            if (!module.empty() && module.back() == "mod") {
                module.pop_back();
            }
            if (module.size() == 1 && (module.front() == "lib" || module.front() == "main")) {
                module.clear();
            }
            return module;
        }
        case Qualification::Global:
        case Qualification::FileScoped:
        case Qualification::Package:
            break;
    }
    return {};
}

/// A scope-bearing match: a definition, a declaration, or a scope that adds
/// a segment (a Rust impl) or only a linkage (a C++ anonymous namespace).
struct Scope {
    enum class Kind : std::uint8_t { Module, Class, Function, Declaration, Impl, Anonymous };
    Kind kind = Kind::Function;
    CodeCapture node;
    std::string name;
    std::uint32_t name_start = 0;
    std::optional<CodeCapture> body;
    bool is_static = false;
    std::string receiver;
    std::string return_type;
    std::uint32_t return_line = 0;
    int parent = -1;
    /// Its index among the definitions, or -1 for an impl/anonymous scope.
    int definition = -1;
};

/// The innermost scope containing byte `at`, or -1. `scopes` is sorted by
/// start ascending, end descending, with parents resolved.
[[nodiscard]] int innermost(const std::vector<Scope>& scopes, std::uint32_t at, std::uint32_t end) {
    // The last scope starting at or before `at`, then up its parents until one
    // contains [at, end).
    auto it = std::upper_bound(
        scopes.begin(), scopes.end(), at,
        [](std::uint32_t value, const Scope& scope) { return value < scope.node.start_byte; });
    if (it == scopes.begin()) {
        return -1;
    }
    int index = static_cast<int>(std::distance(scopes.begin(), it)) - 1;
    while (index >= 0) {
        const Scope& scope = scopes[static_cast<std::size_t>(index)];
        if (scope.node.start_byte <= at && end <= scope.node.end_byte) {
            return index;
        }
        index = scope.parent;
    }
    return -1;
}

/// The definition a scope index stands for, walking out past impl and
/// anonymous scopes; -1 at file level.
[[nodiscard]] int owner_definition(const std::vector<Scope>& scopes, int scope) {
    while (scope >= 0) {
        const Scope& item = scopes[static_cast<std::size_t>(scope)];
        if (item.definition >= 0) {
            return item.definition;
        }
        scope = item.parent;
    }
    return -1;
}

/// The text between the first `{` at depth 0 and its match, split at depth-0
/// commas, each item trimmed.
[[nodiscard]] std::vector<std::string> use_list_items(std::string_view inner) {
    std::vector<std::string> out;
    int depth = 0;
    std::string item;
    const auto flush = [&] {
        std::string trimmed = collapse(item);
        if (!trimmed.empty()) {
            out.push_back(std::move(trimmed));
        }
        item.clear();
    };
    for (const char c : inner) {
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
        }
        if (c == ',' && depth == 0) {
            flush();
            continue;
        }
        item.push_back(c);
    }
    flush();
    return out;
}

/// A Rust use tree's leaves: `a::{b, c::d as e}` -> (`a::b`, ""), (`a::c::d`, "e").
// NOLINTNEXTLINE(misc-no-recursion): bounded by the use tree's nesting.
void expand_use_tree(std::string_view tree, const std::string& prefix,
                     std::vector<std::pair<std::string, std::string>>& out) {
    const std::string text = collapse(tree);
    const std::size_t brace = text.find('{');
    const auto joined = [&](std::string_view tail) {
        std::string head{tail};
        std::erase(head, ' ');
        while (head.ends_with("::")) {
            head.erase(head.size() - 2);
        }
        if (prefix.empty()) {
            return head;
        }
        return head.empty() ? prefix : prefix + "::" + head;
    };
    if (brace == std::string::npos) {
        std::string alias;
        std::string path = text;
        if (const std::size_t as = text.find(" as "); as != std::string::npos) {
            alias = collapse(text.substr(as + 4));
            path = text.substr(0, as);
        }
        std::string full = joined(path);
        if (full == "self") {
            full = prefix;
        } else if (full.ends_with("::self")) {
            full.erase(full.size() - 6);
        }
        if (!full.empty()) {
            out.emplace_back(full, alias);
        }
        return;
    }
    const std::string next = joined(text.substr(0, brace));
    const std::size_t close = text.rfind('}');
    const std::string_view inner = std::string_view{text}.substr(
        brace + 1,
        close == std::string::npos || close < brace ? std::string_view::npos : close - brace - 1);
    for (const std::string& item : use_list_items(inner)) {
        expand_use_tree(item, next, out);
    }
}

}  // namespace

std::string join_segments(const std::vector<std::string>& segments, std::string_view separator) {
    std::string out;
    for (const std::string& segment : segments) {
        if (!out.empty()) {
            out += separator;
        }
        out += segment;
    }
    return out;
}

std::vector<std::string> type_names(const CodeLanguage& language, std::string_view type) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::size_t i = 0;
    while (i < type.size()) {
        if (type[i] == '\'' && language.id == CodeLanguageId::Rust) {
            // A lifetime: skip its name.
            ++i;
            while (i < type.size() && is_ident(type[i])) {
                ++i;
            }
            continue;
        }
        if (!is_ident_start(type[i]) || (i > 0 && is_ident(type[i - 1]))) {
            ++i;
            continue;
        }
        std::string path;
        while (i < type.size() && is_ident_start(type[i])) {
            const std::size_t start = i;
            while (i < type.size() && is_ident(type[i])) {
                ++i;
            }
            path += type.substr(start, i - start);
            if (i + 1 < type.size() && type[i] == ':' && type[i + 1] == ':' &&
                i + 2 < type.size() && is_ident_start(type[i + 2])) {
                path += "::";
                i += 2;
                continue;
            }
            if (i < type.size() && type[i] == '.' && i + 1 < type.size() &&
                is_ident_start(type[i + 1])) {
                path += ".";
                ++i;
                continue;
            }
            break;
        }
        const std::size_t cut = path.find_last_of(":.");
        const std::string last = cut == std::string::npos ? path : path.substr(cut + 1);
        const bool builtin =
            std::ranges::find(language.builtin_types, path) != language.builtin_types.end() ||
            (cut == std::string::npos &&
             std::ranges::find(language.builtin_types, last) != language.builtin_types.end());
        if (!builtin && !path.empty() && seen.insert(path).second) {
            out.push_back(path);
        }
    }
    return out;
}

std::string receiver_type(const CodeLanguage& language, std::string_view type,
                          bool through_pointer) {
    const std::vector<std::string> names = type_names(language, type);
    if (names.empty()) {
        return {};
    }
    const std::string& first = names.front();
    const std::size_t cut = first.find_last_of(":.");
    const std::string last = cut == std::string::npos ? first : first.substr(cut + 1);
    const bool wrapper =
        std::ranges::find(language.wrapper_types, last) != language.wrapper_types.end();
    // C++ reaches through a wrapper only with `->`; Rust's smart pointers
    // and Python's Optional are read through implicitly.
    const bool implicit = language.id != CodeLanguageId::Cpp;
    if (wrapper && names.size() > 1 && (through_pointer || implicit)) {
        return names[1];
    }
    return first;
}

FileFacts extract_code_facts(const CodeLanguage& language, std::string_view path,
                             std::string_view source, const CodeParseResult& parse) {
    FileFacts facts;
    facts.path = std::string{path};
    facts.language = std::string{language.name};
    facts.has_errors = parse.has_errors;
    facts.lines = source.empty() ? 0
                                 : static_cast<std::uint32_t>(std::ranges::count(source, '\n') +
                                                              (source.ends_with('\n') ? 0 : 1));
    const std::string_view sep = language.separator;
    if (language.qualification == Qualification::FileScoped) {
        facts.prefix = std::string{path} + "::";
    }
    facts.module = path_module(language, path);

    // ---- Pass 1: classify every match ------------------------------------
    std::vector<Scope> scopes;

    struct PendingCall {
        CodeCapture node;
        std::string text;
    };

    std::vector<PendingCall> calls;

    struct PendingImport {
        CodeCapture node;
        std::string kind;
        std::string path;
        std::string name;
        std::string alias;
    };

    std::vector<PendingImport> imports;

    struct PendingInherit {
        CodeCapture child;
        std::string base;
    };

    std::vector<PendingInherit> inherits;

    struct PendingTyped {
        std::string kind;
        CodeCapture name;
        std::string type;
    };

    std::vector<PendingTyped> typed;
    std::optional<std::pair<std::string, std::uint32_t>> package;

    for (const CodeMatch& match : parse.matches) {
        const CodeCapture* name = match.find("name");
        const auto scope_of = [&](const CodeCapture& node, Scope::Kind kind) {
            Scope scope;
            scope.kind = kind;
            scope.node = node;
            if (name != nullptr) {
                scope.name = clean_name(language, slice(source, *name));
                scope.name_start = name->start_byte;
            }
            if (const CodeCapture* body = match.find("body"); body != nullptr) {
                scope.body = *body;
            }
            if (const CodeCapture* is_static = match.find("static"); is_static != nullptr) {
                scope.is_static = slice(source, *is_static) == "static";
            }
            if (const CodeCapture* receiver = match.find("receiver"); receiver != nullptr) {
                scope.receiver = receiver_type(language, slice(source, *receiver), false);
            }
            if (const CodeCapture* returns = match.find("return.type"); returns != nullptr) {
                scope.return_type = std::string{slice(source, *returns)};
                scope.return_line = returns->start_line;
            }
            scopes.push_back(std::move(scope));
        };
        static constexpr std::array<std::pair<std::string_view, Scope::Kind>, 6> kScopeCaptures{{
            {"definition.module", Scope::Kind::Module},
            {"definition.class", Scope::Kind::Class},
            {"definition.function", Scope::Kind::Function},
            {"declaration.function", Scope::Kind::Declaration},
            {"scope.impl", Scope::Kind::Impl},
            {"scope.anonymous", Scope::Kind::Anonymous},
        }};
        bool classified = false;
        for (const auto& [capture, kind] : kScopeCaptures) {
            if (const CodeCapture* scope_node = match.find(capture); scope_node != nullptr) {
                scope_of(*scope_node, kind);
                classified = true;
                break;
            }
        }
        if (classified) {
            continue;
        }
        if (const CodeCapture* package_node = match.find("package"); package_node != nullptr) {
            if (!package.has_value() && name != nullptr) {
                package.emplace(clean_name(language, slice(source, *name)),
                                package_node->start_line);
            }
            continue;
        }
        if (const CodeCapture* call_node = match.find("call"); call_node != nullptr) {
            std::string text;
            if (const CodeCapture* target = match.find("call.target"); target != nullptr) {
                text = clean_name(language, slice(source, *target));
            } else if (const CodeCapture* called = match.find("call.name"); called != nullptr) {
                text = clean_name(language, slice(source, *called));
                if (const CodeCapture* receiver = match.find("call.receiver");
                    receiver != nullptr) {
                    text = clean_name(language, slice(source, *receiver)) + "." + text;
                }
            }
            // A C/C++ cast or operator the grammar reads as a call is not one.
            static constexpr std::array<std::string_view, 11> kNotCalls{
                "static_cast", "dynamic_cast",  "reinterpret_cast", "const_cast",
                "sizeof",      "alignof",       "decltype",         "typeid",
                "noexcept",    "static_assert", "offsetof"};
            const bool c_family =
                language.id == CodeLanguageId::C || language.id == CodeLanguageId::Cpp;
            // A JSX element renders a component when its name is one; a
            // lower-case name (`<div>`, `<svg:rect>`) is the host's element.
            const bool host_element = slice(source, *call_node).starts_with('<') && !text.empty() &&
                                      std::islower(static_cast<unsigned char>(text.front())) != 0;
            if (!text.empty() && !host_element &&
                !(c_family && std::ranges::find(kNotCalls, text) != kNotCalls.end())) {
                calls.push_back(PendingCall{.node = *call_node, .text = std::move(text)});
            }
            continue;
        }
        if (const CodeCapture* path_node = match.find("import.path"); path_node != nullptr) {
            static constexpr std::array<std::pair<std::string_view, std::string_view>, 4>
                kImportCaptures{{{"import", "module"},
                                 {"import.relative", "relative"},
                                 {"import.namespace", "namespace"},
                                 {"import.symbol", "symbol"}}};
            PendingImport item;
            for (const auto& [capture, kind] : kImportCaptures) {
                if (const CodeCapture* import_node = match.find(capture); import_node != nullptr) {
                    item.node = *import_node;
                    item.kind = std::string{kind};
                    break;
                }
            }
            if (item.kind.empty()) {
                continue;
            }
            item.path = clean_path(slice(source, *path_node));
            if (const CodeCapture* imported = match.find("import.name"); imported != nullptr) {
                item.name = collapse(slice(source, *imported));
            }
            if (const CodeCapture* alias = match.find("import.alias"); alias != nullptr) {
                item.alias = collapse(slice(source, *alias));
            }
            imports.push_back(std::move(item));
            continue;
        }
        if (const CodeCapture* child = match.find("inherit.child"); child != nullptr) {
            if (const CodeCapture* base = match.find("inherit.base"); base != nullptr) {
                inherits.push_back(PendingInherit{
                    .child = *child, .base = clean_name(language, slice(source, *base))});
            }
            continue;
        }
        {
            for (const std::string_view kind : {"param", "local", "field"}) {
                const CodeCapture* named = match.find(std::string{kind} + ".name");
                const CodeCapture* type = match.find(std::string{kind} + ".type");
                if (named != nullptr && type != nullptr) {
                    typed.push_back(PendingTyped{.kind = std::string{kind},
                                                 .name = *named,
                                                 .type = collapse(slice(source, *type))});
                    break;
                }
            }
        }
    }

    // ---- Pass 2: the scope tree --------------------------------------------
    // One scope per node and kind: a node two patterns both matched counts
    // once (the first pattern's captures win).
    {
        std::set<std::tuple<std::uint32_t, std::uint32_t, int>> seen;
        std::vector<Scope> unique;
        for (Scope& scope : scopes) {
            if (seen.emplace(scope.node.start_byte, scope.node.end_byte,
                             static_cast<int>(scope.kind))
                    .second) {
                unique.push_back(std::move(scope));
            }
        }
        scopes = std::move(unique);
    }
    std::ranges::stable_sort(scopes, [](const Scope& a, const Scope& b) {
        if (a.node.start_byte != b.node.start_byte) {
            return a.node.start_byte < b.node.start_byte;
        }
        return a.node.end_byte > b.node.end_byte;
    });
    {
        std::vector<int> stack;
        for (std::size_t i = 0; i < scopes.size(); ++i) {
            while (!stack.empty() && scopes[static_cast<std::size_t>(stack.back())].node.end_byte <=
                                         scopes[i].node.start_byte) {
                stack.pop_back();
            }
            // A scope never contains itself, and an equal-range pair nests by
            // sort order (the outer one first).
            scopes[i].parent = stack.empty() ? -1 : stack.back();
            stack.push_back(static_cast<int>(i));
        }
    }
    // The file's own module: a package declaration for the package-declaring
    // languages, the path for the rest.
    if (package.has_value()) {
        if (language.qualification == Qualification::PackageDirectory) {
            if (facts.module.empty()) {
                facts.module = {package->first};
            }
        } else {
            facts.module = split(package->first, sep);
        }
    }

    // ---- Pass 3: definitions -------------------------------------------------
    // A module node for the file's own module when the language names one:
    // a package (Java, C#, Go) or a Rust file's module path.
    const bool file_module =
        !facts.module.empty() &&
        (package.has_value() || language.qualification == Qualification::CrateModule);
    if (file_module) {
        CodeDefinition module;
        module.kind = "module";
        module.segments = facts.module;
        module.qualified = join_segments(module.segments, sep);
        module.line = package.has_value() ? package->second : 1;
        module.end_line = module.line;
        module.signature = std::string{language.module_noun} + " " + module.qualified;
        facts.definitions.push_back(std::move(module));
    }
    for (std::size_t i = 0; i < scopes.size(); ++i) {
        Scope& scope = scopes[i];
        if (scope.kind == Scope::Kind::Impl || scope.kind == Scope::Kind::Anonymous ||
            scope.name.empty()) {
            continue;
        }
        std::vector<std::string> enclosing;
        bool internal = false;
        bool in_class = false;
        for (int up = scope.parent; up >= 0; up = scopes[static_cast<std::size_t>(up)].parent) {
            const Scope& outer = scopes[static_cast<std::size_t>(up)];
            if (outer.kind == Scope::Kind::Anonymous) {
                internal = true;
                continue;
            }
            if (outer.kind == Scope::Kind::Declaration) {
                continue;
            }
            if (outer.kind == Scope::Kind::Class || outer.kind == Scope::Kind::Impl) {
                in_class = true;
            }
            std::vector<std::string> parts = split(outer.name, sep);
            enclosing.insert(enclosing.begin(), parts.begin(), parts.end());
        }
        if (scope.is_static && !in_class &&
            (language.id == CodeLanguageId::C || language.id == CodeLanguageId::Cpp)) {
            internal = true;
        }
        CodeDefinition definition;
        switch (scope.kind) {
            case Scope::Kind::Module:
                definition.kind = "module";
                break;
            case Scope::Kind::Class:
                definition.kind = "class";
                break;
            case Scope::Kind::Function:
                definition.kind = "function";
                break;
            case Scope::Kind::Declaration:
                definition.kind = "function";
                definition.declaration = true;
                break;
            case Scope::Kind::Impl:
            case Scope::Kind::Anonymous:
                break;
        }
        definition.segments = facts.module;
        definition.segments.insert(definition.segments.end(), enclosing.begin(), enclosing.end());
        if (!scope.receiver.empty()) {
            std::vector<std::string> parts = split(scope.receiver, sep);
            definition.segments.insert(definition.segments.end(), parts.begin(), parts.end());
        }
        std::vector<std::string> own = split(scope.name, sep);
        definition.segments.insert(definition.segments.end(), own.begin(), own.end());
        definition.prefix =
            !facts.prefix.empty() ? facts.prefix : (internal ? std::string{path} + "::" : "");
        definition.qualified = definition.prefix + join_segments(definition.segments, sep);
        definition.line = scope.node.start_line;
        definition.end_line = scope.node.end_line;
        definition.signature =
            signature_of(source, scope.node, scope.body.has_value() ? &*scope.body : nullptr);
        scope.definition = static_cast<int>(facts.definitions.size());
        facts.definitions.push_back(std::move(definition));
    }
    // Parents, as definition indexes.
    for (const Scope& scope : scopes) {
        if (scope.definition < 0) {
            continue;
        }
        facts.definitions[static_cast<std::size_t>(scope.definition)].parent =
            owner_definition(scopes, scope.parent);
    }

    // ---- Pass 4: what the definitions refer to --------------------------------
    // A call within a node that matched as an import is the import, never
    // also a call (`require 'x'`, `source lib.sh`, `const fs = require('fs')`).
    const auto inside_import = [&](const CodeCapture& node) {
        return std::ranges::any_of(imports, [&](const PendingImport& item) {
            return item.node.start_byte <= node.start_byte && node.end_byte <= item.node.end_byte;
        });
    };
    for (const PendingCall& call : calls) {
        if (inside_import(call.node)) {
            continue;
        }
        const int scope = innermost(scopes, call.node.start_byte, call.node.end_byte);
        facts.references.push_back(CodeReference{.kind = "call",
                                                 .text = call.text,
                                                 .line = call.node.start_line,
                                                 .owner = owner_definition(scopes, scope)});
    }
    std::set<std::tuple<int, std::string, std::uint32_t>> type_seen;
    const auto add_type_refs = [&](int owner, std::string_view type, std::uint32_t line) {
        for (const std::string& name : type_names(language, type)) {
            const std::size_t cut = name.find_last_of(":.");
            const std::string last = cut == std::string::npos ? name : name.substr(cut + 1);
            if (std::ranges::find(language.wrapper_types, last) != language.wrapper_types.end()) {
                continue;
            }
            if (type_seen.emplace(owner, name, line).second) {
                facts.references.push_back(
                    CodeReference{.kind = "type", .text = name, .line = line, .owner = owner});
            }
        }
    };
    for (const Scope& scope : scopes) {
        if (scope.definition >= 0 && !scope.return_type.empty()) {
            add_type_refs(scope.definition, scope.return_type, scope.return_line);
        }
    }
    std::set<std::tuple<std::string, std::string, int>> typed_seen;
    for (const PendingTyped& item : typed) {
        const int scope = innermost(scopes, item.name.start_byte, item.name.end_byte);
        const int owner = owner_definition(scopes, scope);
        const std::string name = collapse(slice(source, item.name));
        if (!typed_seen.emplace(item.kind, name, owner).second) {
            continue;
        }
        facts.typed.push_back(
            CodeTyped{.kind = item.kind, .name = name, .type = item.type, .owner = owner});
        if (item.kind != "local") {
            add_type_refs(owner, item.type, item.name.start_line);
        }
    }
    for (const PendingInherit& item : inherits) {
        CodeInherit inherit;
        inherit.child = clean_name(language, slice(source, item.child));
        inherit.base = item.base;
        inherit.line = item.child.start_line;
        for (const Scope& scope : scopes) {
            if (scope.definition >= 0 && scope.kind == Scope::Kind::Class &&
                scope.name_start == item.child.start_byte) {
                inherit.child_definition = scope.definition;
                break;
            }
        }
        const int scope = innermost(scopes, item.child.start_byte, item.child.end_byte);
        inherit.owner = inherit.child_definition >= 0 ? inherit.child_definition
                                                      : owner_definition(scopes, scope);
        facts.inherits.push_back(std::move(inherit));
    }

    // ---- Pass 5: imports ----------------------------------------------------------
    // A node captured as a namespace import is not also a symbol import (the
    // C++ and Java patterns overlap on `using namespace` / `import x.*`).
    std::set<std::pair<std::uint32_t, std::uint32_t>> namespace_nodes;
    for (const PendingImport& item : imports) {
        if (item.kind == "namespace") {
            namespace_nodes.emplace(item.node.start_byte, item.node.end_byte);
        }
    }
    std::set<std::tuple<std::string, std::string, std::string, std::string, std::uint32_t>> seen;
    const auto add_import = [&](CodeImport item) {
        if (item.path.empty()) {
            return;
        }
        if (seen.emplace(item.kind, item.path, item.name, item.alias, item.line).second) {
            facts.imports.push_back(std::move(item));
        }
    };
    for (const PendingImport& item : imports) {
        if (item.kind == "symbol" &&
            namespace_nodes.contains({item.node.start_byte, item.node.end_byte})) {
            continue;
        }
        if (language.id == CodeLanguageId::Rust) {
            std::vector<std::pair<std::string, std::string>> leaves;
            expand_use_tree(item.path, "", leaves);
            for (auto& [leaf, alias] : leaves) {
                const bool glob = leaf.ends_with("::*");
                std::string target = glob ? leaf.substr(0, leaf.size() - 3) : leaf;
                add_import(CodeImport{.kind = glob ? "namespace" : "symbol",
                                      .path = std::move(target),
                                      .name = "",
                                      .alias = alias,
                                      .line = item.node.start_line});
            }
            continue;
        }
        add_import(CodeImport{.kind = item.kind,
                              .path = item.path,
                              .name = item.name,
                              .alias = item.alias,
                              .line = item.node.start_line});
    }
    // Deterministic order: by line, then what was written.
    std::ranges::stable_sort(facts.imports, [](const CodeImport& a, const CodeImport& b) {
        return std::tie(a.line, a.kind, a.path, a.name, a.alias) <
               std::tie(b.line, b.kind, b.path, b.name, b.alias);
    });
    std::ranges::stable_sort(facts.references, [](const CodeReference& a, const CodeReference& b) {
        return std::tie(a.line, a.kind, a.text, a.owner) <
               std::tie(b.line, b.kind, b.text, b.owner);
    });
    return facts;
}

// ---- JSON -----------------------------------------------------------------------

std::string facts_to_json(const FileFacts& facts) {
    nlohmann::json definitions = nlohmann::json::array();
    for (const CodeDefinition& item : facts.definitions) {
        definitions.push_back({{"k", item.kind},
                               {"s", item.segments},
                               {"p", item.prefix},
                               {"q", item.qualified},
                               {"l", item.line},
                               {"e", item.end_line},
                               {"g", item.signature},
                               {"d", item.declaration},
                               {"u", item.parent}});
    }
    nlohmann::json imports = nlohmann::json::array();
    for (const CodeImport& item : facts.imports) {
        imports.push_back({{"k", item.kind},
                           {"p", item.path},
                           {"n", item.name},
                           {"a", item.alias},
                           {"l", item.line}});
    }
    nlohmann::json references = nlohmann::json::array();
    for (const CodeReference& item : facts.references) {
        references.push_back(
            {{"k", item.kind}, {"t", item.text}, {"l", item.line}, {"o", item.owner}});
    }
    nlohmann::json inherits = nlohmann::json::array();
    for (const CodeInherit& item : facts.inherits) {
        inherits.push_back({{"c", item.child},
                            {"b", item.base},
                            {"l", item.line},
                            {"o", item.owner},
                            {"d", item.child_definition}});
    }
    nlohmann::json typed = nlohmann::json::array();
    for (const CodeTyped& item : facts.typed) {
        typed.push_back({{"k", item.kind}, {"n", item.name}, {"t", item.type}, {"o", item.owner}});
    }
    const nlohmann::json out{{"path", facts.path},
                             {"language", facts.language},
                             {"lines", facts.lines},
                             {"errors", facts.has_errors},
                             {"module", facts.module},
                             {"prefix", facts.prefix},
                             {"definitions", std::move(definitions)},
                             {"imports", std::move(imports)},
                             {"references", std::move(references)},
                             {"inherits", std::move(inherits)},
                             {"typed", std::move(typed)}};
    return out.dump();
}

FileFacts facts_from_json(std::string_view json) {
    const nlohmann::json in = nlohmann::json::parse(json, nullptr, false);
    if (in.is_discarded() || !in.is_object()) {
        throw std::runtime_error("cached code facts are not a facts document");
    }
    FileFacts facts;
    try {
        facts.path = in.at("path").get<std::string>();
        facts.language = in.at("language").get<std::string>();
        facts.lines = in.at("lines").get<std::uint32_t>();
        facts.has_errors = in.at("errors").get<bool>();
        facts.module = in.at("module").get<std::vector<std::string>>();
        facts.prefix = in.at("prefix").get<std::string>();
        for (const nlohmann::json& item : in.at("definitions")) {
            facts.definitions.push_back(
                CodeDefinition{.kind = item.at("k").get<std::string>(),
                               .segments = item.at("s").get<std::vector<std::string>>(),
                               .prefix = item.at("p").get<std::string>(),
                               .qualified = item.at("q").get<std::string>(),
                               .line = item.at("l").get<std::uint32_t>(),
                               .end_line = item.at("e").get<std::uint32_t>(),
                               .signature = item.at("g").get<std::string>(),
                               .declaration = item.at("d").get<bool>(),
                               .parent = item.at("u").get<int>()});
        }
        for (const nlohmann::json& item : in.at("imports")) {
            facts.imports.push_back(CodeImport{.kind = item.at("k").get<std::string>(),
                                               .path = item.at("p").get<std::string>(),
                                               .name = item.at("n").get<std::string>(),
                                               .alias = item.at("a").get<std::string>(),
                                               .line = item.at("l").get<std::uint32_t>()});
        }
        for (const nlohmann::json& item : in.at("references")) {
            facts.references.push_back(CodeReference{.kind = item.at("k").get<std::string>(),
                                                     .text = item.at("t").get<std::string>(),
                                                     .line = item.at("l").get<std::uint32_t>(),
                                                     .owner = item.at("o").get<int>()});
        }
        for (const nlohmann::json& item : in.at("inherits")) {
            facts.inherits.push_back(CodeInherit{.child = item.at("c").get<std::string>(),
                                                 .base = item.at("b").get<std::string>(),
                                                 .line = item.at("l").get<std::uint32_t>(),
                                                 .owner = item.at("o").get<int>(),
                                                 .child_definition = item.at("d").get<int>()});
        }
        for (const nlohmann::json& item : in.at("typed")) {
            facts.typed.push_back(CodeTyped{.kind = item.at("k").get<std::string>(),
                                            .name = item.at("n").get<std::string>(),
                                            .type = item.at("t").get<std::string>(),
                                            .owner = item.at("o").get<int>()});
        }
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string{"cached code facts are malformed: "} + e.what());
    }
    return facts;
}

}  // namespace apogee::graph
