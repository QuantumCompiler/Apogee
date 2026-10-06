#include "graph/code_resolve.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace apogee::graph {
namespace {

/// A definition as the symbol table holds it.
struct Symbol {
    std::string kind;
    std::size_t file = 0;
    int definition = -1;
};

/// What an import made visible under a local name.
struct Binding {
    std::string target;
    /// `target` is a file's prefix (`<path>::`): the rest of a name is
    /// appended to it directly.
    bool file_prefix = false;
};

struct FileContext {
    const CodeLanguage* language = nullptr;
    std::map<std::string, Binding> bindings;
    std::vector<std::string> roots;
};

/// A declared type and where it was declared, so it is looked up from there.
struct TypedAt {
    std::string type;
    std::size_t file = 0;
    int owner = -1;
};

/// One written name, tokenized: identifiers and the separators between them.
struct Tokens {
    std::vector<std::string> idents;
    /// `seps[i]` precedes `idents[i]`; `seps[0]` is empty.
    std::vector<std::string> seps;
    /// Something other than identifiers and separators was written (a call
    /// on a call's result, an index, a cast): no lookup can name it.
    bool complex = false;
};

[[nodiscard]] Tokens tokenize(std::string_view text) {
    Tokens out;
    std::string pending_sep;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$' || c == '~') {
            const std::size_t start = i;
            ++i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 ||
                                       text[i] == '_' || text[i] == '$')) {
                ++i;
            }
            out.seps.push_back(pending_sep);
            out.idents.emplace_back(text.substr(start, i - start));
            pending_sep.clear();
            continue;
        }
        if (text.substr(i).starts_with("::") || text.substr(i).starts_with("->")) {
            pending_sep = std::string{text.substr(i, 2)};
            i += 2;
            continue;
        }
        if (c == '.') {
            pending_sep = ".";
            ++i;
            continue;
        }
        out.complex = true;
        ++i;
    }
    if (out.idents.empty()) {
        out.complex = true;
    }
    return out;
}

[[nodiscard]] std::vector<std::string> split_on(std::string_view text, std::string_view sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t at = text.find(sep, start);
        const std::string_view part =
            text.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
        if (!part.empty()) {
            out.emplace_back(part);
        }
        if (at == std::string_view::npos) {
            break;
        }
        start = at + sep.size();
    }
    return out;
}

[[nodiscard]] std::string_view self_word(CodeLanguageId id) {
    switch (id) {
        case CodeLanguageId::Cpp:
        case CodeLanguageId::Java:
        case CodeLanguageId::CSharp:
        case CodeLanguageId::JavaScript:
        case CodeLanguageId::TypeScript:
        case CodeLanguageId::Tsx:
            return "this";
        case CodeLanguageId::Python:
        case CodeLanguageId::Rust:
        case CodeLanguageId::Ruby:
            return "self";
        case CodeLanguageId::C:
        case CodeLanguageId::Go:
        case CodeLanguageId::Bash:
            break;
    }
    return {};
}

/// Whether a bare name inside a method may name a member of its class (and
/// its bases): C++, Java, C#, Ruby. Python, JavaScript, TypeScript, Rust and
/// Go need the receiver spelled out.
[[nodiscard]] bool members_in_scope(CodeLanguageId id) {
    return id == CodeLanguageId::C || id == CodeLanguageId::Cpp || id == CodeLanguageId::Java ||
           id == CodeLanguageId::CSharp || id == CodeLanguageId::Ruby;
}

[[nodiscard]] std::string parent_dir(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? std::string{} : std::string{path.substr(0, slash)};
}

/// `dir` + `relative`, with `.` and `..` folded; nullopt when it climbs out.
[[nodiscard]] std::optional<std::string> join_path(std::string_view dir,
                                                   std::string_view relative) {
    std::vector<std::string> parts = split_on(dir, "/");
    for (const std::string& part : split_on(relative, "/")) {
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            if (parts.empty()) {
                return std::nullopt;
            }
            parts.pop_back();
            continue;
        }
        parts.push_back(part);
    }
    std::string out;
    for (const std::string& part : parts) {
        out += (out.empty() ? "" : "/") + part;
    }
    return out;
}

using NodeKey = std::pair<std::string, std::string>;
using EdgeKey = std::tuple<NodeKey, NodeKey, std::string>;

class Resolver {
public:
    explicit Resolver(std::vector<SourceFacts> sources) : sources_{std::move(sources)} {
        std::ranges::sort(sources_, [](const SourceFacts& a, const SourceFacts& b) {
            return std::tie(a.member, a.facts.path) < std::tie(b.member, b.facts.path);
        });
        contexts_.resize(sources_.size());
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            const CodeLanguage* language = code_language_by_name(sources_[file].facts.language);
            contexts_[file].language =
                language != nullptr ? language : &code_language(CodeLanguageId::C);
        }
    }

    [[nodiscard]] CodeGraph run() {
        index_symbols();
        index_files();
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            add_definitions(file);
        }
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            bind_imports(file);
        }
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            resolve_inherits(file);
        }
        index_fields();
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            resolve_references(file);
        }
        return finish();
    }

private:
    // ---- Tables ---------------------------------------------------------------

    void index_symbols() {
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            const FileFacts& facts = sources_[file].facts;
            for (std::size_t i = 0; i < facts.definitions.size(); ++i) {
                const CodeDefinition& definition = facts.definitions[i];
                symbols_[definition.qualified].push_back(Symbol{
                    .kind = definition.kind, .file = file, .definition = static_cast<int>(i)});
            }
        }
    }

    void index_files() {
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            files_by_path_[sources_[file].facts.path].push_back(file);
        }
    }

    void index_fields() {
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            const FileFacts& facts = sources_[file].facts;
            for (const CodeTyped& typed : facts.typed) {
                if (typed.kind != "field" || typed.owner < 0) {
                    continue;
                }
                const CodeDefinition& owner =
                    facts.definitions[static_cast<std::size_t>(typed.owner)];
                if (owner.kind != kCodeKindClass) {
                    continue;
                }
                fields_[owner.qualified].try_emplace(
                    typed.name, TypedAt{.type = typed.type, .file = file, .owner = typed.owner});
            }
        }
    }

    [[nodiscard]] bool has(const std::string& qualified,
                           std::initializer_list<std::string_view> kinds) const {
        const auto it = symbols_.find(qualified);
        if (it == symbols_.end()) {
            return false;
        }
        return std::ranges::any_of(it->second, [&](const Symbol& symbol) {
            return std::ranges::find(kinds, std::string_view{symbol.kind}) != kinds.end();
        });
    }

    [[nodiscard]] const CodeLanguage& language_of(std::size_t file) const {
        return *contexts_[file].language;
    }

    [[nodiscard]] const CodeDefinition* definition(std::size_t file, int index) const {
        if (index < 0) {
            return nullptr;
        }
        return &sources_[file].facts.definitions[static_cast<std::size_t>(index)];
    }

    /// The lookup scope a reference is made from: its owner's path, or the
    /// file's own module.
    [[nodiscard]] std::pair<std::vector<std::string>, std::string> scope_of(std::size_t file,
                                                                            int owner) const {
        if (const CodeDefinition* def = definition(file, owner); def != nullptr) {
            return {def->segments, def->prefix};
        }
        return {sources_[file].facts.module, sources_[file].facts.prefix};
    }

    // ---- Nodes and edges ------------------------------------------------------

    [[nodiscard]] static NodeKey key(std::string_view type, std::string_view name) {
        return {std::string{type}, embedstore::code_identity(name)};
    }

    NodeKey node(std::string_view type, std::string_view name) {
        NodeKey k = key(type, name);
        auto [it, inserted] = nodes_.try_emplace(k);
        if (inserted) {
            it->second.type = std::string{type};
            it->second.name = std::string{name};
        }
        return k;
    }

    void mention(const NodeKey& k, CodeLocation location) {
        mentions_[k].insert(std::move(location));
    }

    void edge(const NodeKey& source, const NodeKey& target, std::string_view relation,
              std::size_t file, std::uint32_t line) {
        if (source == target) {
            return;  // recursion and self-reference are not recorded
        }
        // A site is a place, not a mention: no extent, no role.
        edges_[EdgeKey{source, target, std::string{relation}}].insert(
            CodeLocation{.member = sources_[file].member,
                         .file = sources_[file].facts.path,
                         .line = line,
                         .end_line = 0,
                         .role = {}});
    }

    NodeKey file_node(std::size_t file) {
        return key(kCodeKindFile, sources_[file].facts.path);
    }

    /// The node a reference is made from: its owner function or class, else
    /// the file (a module's scope reads as the file's).
    NodeKey source_node(std::size_t file, int owner) {
        const CodeDefinition* def = definition(file, owner);
        if (def != nullptr && (def->kind == kCodeKindFunction || def->kind == kCodeKindClass)) {
            return key(def->kind, def->qualified);
        }
        return file_node(file);
    }

    NodeKey unresolved(std::string_view text, std::size_t file, std::uint32_t line) {
        const NodeKey k = node(kCodeKindName, text);
        mention(k, CodeLocation{.member = sources_[file].member,
                                .file = sources_[file].facts.path,
                                .line = line,
                                .end_line = line,
                                .role = "reference"});
        return k;
    }

    void add_definitions(std::size_t file) {
        const SourceFacts& source = sources_[file];
        const FileFacts& facts = source.facts;
        const CodeLanguage& language = language_of(file);
        const NodeKey file_key = node(kCodeKindFile, facts.path);
        primary_[file_key] = Primary{.description = std::string{language.display} + " file",
                                     .location = CodeLocation{.member = source.member,
                                                              .file = facts.path,
                                                              .line = 1,
                                                              .end_line = facts.lines,
                                                              .role = "definition"},
                                     .language = std::string{language.name}};
        mention(file_key, primary_[file_key].location);
        for (std::size_t i = 0; i < facts.definitions.size(); ++i) {
            const CodeDefinition& def = facts.definitions[i];
            const NodeKey k = node(def.kind, def.qualified);
            CodeLocation location{.member = source.member,
                                  .file = facts.path,
                                  .line = def.line,
                                  .end_line = def.end_line,
                                  .role = def.declaration ? "declaration" : "definition"};
            mention(k, location);
            // The primary location: a definition before a declaration, then
            // the first in (member, file, line) order -- deterministic.
            const auto rank = [](const CodeLocation& at) {
                return std::make_tuple(at.role == "declaration" ? 1 : 0, at.member, at.file,
                                       at.line);
            };
            auto [it, inserted] =
                primary_.try_emplace(k, Primary{.description = def.signature,
                                                .location = location,
                                                .language = std::string{language.name}});
            if (!inserted && rank(location) < rank(it->second.location)) {
                it->second = Primary{.description = def.signature,
                                     .location = location,
                                     .language = std::string{language.name}};
            }
            // Containment: nested in a class or function, else in the file.
            const CodeDefinition* parent = definition(file, def.parent);
            if (def.kind == kCodeKindModule) {
                const std::vector<std::string> outer(
                    def.segments.begin(), def.segments.end() - (def.segments.empty() ? 0 : 1));
                const std::string outer_q = def.prefix + join_segments(outer, language.separator);
                if (!outer.empty() && has(outer_q, {kCodeKindModule})) {
                    edge(k, key(kCodeKindModule, outer_q), kCodeRelationDefinedIn, file, def.line);
                }
                // The file's contents live in its innermost modules.
                const bool has_inner_module =
                    std::ranges::any_of(facts.definitions, [&](const CodeDefinition& other) {
                        return other.kind == kCodeKindModule && other.parent == static_cast<int>(i);
                    });
                if (!has_inner_module) {
                    edge(file_key, k, kCodeRelationDefinedIn, file, def.line);
                }
                continue;
            }
            if (parent != nullptr &&
                (parent->kind == kCodeKindClass || parent->kind == kCodeKindFunction)) {
                edge(k, key(parent->kind, parent->qualified), kCodeRelationDefinedIn, file,
                     def.line);
                continue;
            }
            edge(k, file_key, kCodeRelationDefinedIn, file, def.line);
            // Defined outside the class its name places it in -- a C++
            // out-of-line member, a Go method by its receiver, a Rust impl's
            // function: it belongs to that class too, by qualification.
            if (def.segments.size() > 1) {
                const std::vector<std::string> outer(def.segments.begin(), def.segments.end() - 1);
                const std::string outer_q = def.prefix + join_segments(outer, language.separator);
                if (has(outer_q, {kCodeKindClass})) {
                    edge(k, key(kCodeKindClass, outer_q), kCodeRelationDefinedIn, file, def.line);
                }
            }
        }
    }

    // ---- Lookup -----------------------------------------------------------------

    /// The bases of a class, resolved, transitively, without cycles -- in
    /// breadth order.
    [[nodiscard]] std::vector<std::string> bases_of(const std::string& klass) const {
        std::vector<std::string> out;
        std::set<std::string> seen{klass};
        std::vector<std::string> frontier{klass};
        for (int depth = 0; depth < 8 && !frontier.empty(); ++depth) {
            std::vector<std::string> next;
            for (const std::string& current : frontier) {
                const auto it = bases_.find(current);
                if (it == bases_.end()) {
                    continue;
                }
                for (const std::string& base : it->second) {
                    if (seen.insert(base).second) {
                        out.push_back(base);
                        next.push_back(base);
                    }
                }
            }
            frontier = std::move(next);
        }
        return out;
    }

    /// A member of `klass` or of one of its bases.
    [[nodiscard]] std::optional<std::string> member_of(
        const std::string& klass, const std::vector<std::string>& path, std::string_view sep,
        std::initializer_list<std::string_view> kinds) const {
        const std::string tail = join_segments(path, sep);
        if (const std::string direct = klass + std::string{sep} + tail; has(direct, kinds)) {
            return direct;
        }
        for (const std::string& base : bases_of(klass)) {
            if (const std::string inherited = base + std::string{sep} + tail;
                has(inherited, kinds)) {
                return inherited;
            }
        }
        return std::nullopt;
    }

    /// An import binding covering the longest leading part of `path`: the
    /// name it stands for, the rest appended.
    [[nodiscard]] std::optional<std::string> rebind(std::size_t file,
                                                    const std::vector<std::string>& path) const {
        const FileContext& context = contexts_[file];
        const std::string_view sep = context.language->separator;
        for (std::size_t k = path.size(); k > 0; --k) {
            const std::vector<std::string> head(path.begin(),
                                                path.begin() + static_cast<std::ptrdiff_t>(k));
            const auto it = context.bindings.find(join_segments(head, sep));
            if (it == context.bindings.end()) {
                continue;
            }
            const std::vector<std::string> rest(path.begin() + static_cast<std::ptrdiff_t>(k),
                                                path.end());
            if (rest.empty()) {
                return it->second.file_prefix
                           ? it->second.target.substr(0, it->second.target.size() - 2)
                           : it->second.target;
            }
            const std::string target_sep =
                it->second.file_prefix ? std::string{} : std::string{sep};
            return it->second.target + target_sep + join_segments(rest, sep);
        }
        return std::nullopt;
    }

    /// Where `path` names a definition of one of `kinds`, looked up from
    /// `owner` the way the language looks a name up.
    [[nodiscard]] std::optional<std::string> lookup(std::size_t file, int owner,
                                                    std::vector<std::string> path,
                                                    std::initializer_list<std::string_view> kinds,
                                                    bool class_scopes) const {
        if (path.empty()) {
            return std::nullopt;
        }
        const CodeLanguage& language = language_of(file);
        const std::string sep{language.separator};
        auto [segments, prefix] = scope_of(file, owner);

        // Rust's path roots: crate, self, super, Self.
        if (language.id == CodeLanguageId::Rust) {
            std::vector<std::string> module = sources_[file].facts.module;
            if (path.front() == "crate") {
                path.erase(path.begin());
                if (const std::string q = join_segments(path, sep);
                    !path.empty() && has(q, kinds)) {
                    return q;
                }
                return std::nullopt;
            }
            bool relative = false;
            while (!path.empty() && (path.front() == "self" || path.front() == "super")) {
                if (path.front() == "super" && !module.empty()) {
                    module.pop_back();
                }
                path.erase(path.begin());
                relative = true;
            }
            if (relative) {
                module.insert(module.end(), path.begin(), path.end());
                if (const std::string q = join_segments(module, sep); has(q, kinds)) {
                    return q;
                }
                return std::nullopt;
            }
            if (path.front() == "Self") {
                if (const std::optional<std::string> klass = self_class(file, owner)) {
                    path.erase(path.begin());
                    return path.empty() ? klass : member_of(*klass, path, sep, kinds);
                }
                return std::nullopt;
            }
        }

        // An import binding.
        if (const std::optional<std::string> bound = rebind(file, path)) {
            if (has(*bound, kinds)) {
                return bound;
            }
            // `Imported.method`: a member of the class the binding names.
            const std::vector<std::string> parts = split_on(*bound, sep);
            if (parts.size() > 1) {
                const std::vector<std::string> head(parts.begin(), parts.end() - 1);
                const std::string klass = join_segments(head, sep);
                if (has(klass, {kCodeKindClass})) {
                    return member_of(klass, {parts.back()}, sep, kinds);
                }
            }
        }

        // The prefixes a name in this file may carry: a file-scoped
        // language's own, and in C/C++ the file's internal-linkage prefix --
        // tried first, from anywhere in the file, since a definition with
        // external linkage calls its file's `static` helpers too -- then none.
        std::vector<std::string> prefixes;
        if (!prefix.empty()) {
            prefixes.push_back(prefix);
        }
        if (language.id == CodeLanguageId::C || language.id == CodeLanguageId::Cpp) {
            const std::string internal = sources_[file].facts.path + "::";
            if (internal != prefix) {
                prefixes.push_back(internal);
            }
        }
        prefixes.emplace_back();
        // The enclosing scopes, innermost first.
        for (std::size_t i = segments.size() + 1; i-- > 0;) {
            const std::vector<std::string> scope(segments.begin(),
                                                 segments.begin() + static_cast<std::ptrdiff_t>(i));
            const std::string scope_q = join_segments(scope, sep);
            std::optional<std::string> klass;
            if (i > 0) {
                for (const std::string& p : prefixes) {
                    if (has(p + scope_q, {kCodeKindClass})) {
                        klass = p + scope_q;
                        break;
                    }
                }
            }
            if (klass.has_value() && !class_scopes) {
                continue;
            }
            std::vector<std::string> candidate = scope;
            candidate.insert(candidate.end(), path.begin(), path.end());
            const std::string joined = join_segments(candidate, sep);
            for (const std::string& p : prefixes) {
                if (has(p + joined, kinds)) {
                    return p + joined;
                }
            }
            if (klass.has_value()) {
                if (std::optional<std::string> inherited = member_of(*klass, path, sep, kinds)) {
                    return inherited;
                }
            }
        }

        // What a namespace import opened.
        for (const std::string& root : contexts_[file].roots) {
            if (const std::string q = root + sep + join_segments(path, sep); has(q, kinds)) {
                return q;
            }
        }
        return std::nullopt;
    }

    /// The class a method's `this`/`self` is: the nearest enclosing scope
    /// that is a class.
    [[nodiscard]] std::optional<std::string> self_class(std::size_t file, int owner) const {
        const CodeDefinition* def = definition(file, owner);
        if (def == nullptr) {
            return std::nullopt;
        }
        const CodeLanguage& language = language_of(file);
        const std::string sep{language.separator};
        // A method of a class with internal linkage carries no prefix of its
        // own when defined out of line; its class does.
        std::vector<std::string> prefixes{def->prefix};
        if ((language.id == CodeLanguageId::C || language.id == CodeLanguageId::Cpp) &&
            def->prefix.empty()) {
            prefixes.push_back(sources_[file].facts.path + "::");
        }
        if (!def->prefix.empty()) {
            prefixes.emplace_back();
        }
        for (std::size_t i = def->segments.size() + 1; i-- > 1;) {
            const std::vector<std::string> scope(
                def->segments.begin(), def->segments.begin() + static_cast<std::ptrdiff_t>(i));
            const std::string q = join_segments(scope, sep);
            for (const std::string& p : prefixes) {
                if (has(p + q, {kCodeKindClass})) {
                    return p + q;
                }
            }
        }
        return std::nullopt;
    }

    /// The class a type expression names, looked up from where it was written.
    [[nodiscard]] std::optional<std::string> class_of_type(std::size_t file, int owner,
                                                           std::string_view type,
                                                           bool through_pointer) const {
        const CodeLanguage& language = language_of(file);
        const std::string named = receiver_type(language, type, through_pointer);
        if (named.empty()) {
            return std::nullopt;
        }
        std::vector<std::string> path = split_on(named, language.separator);
        if (language.separator != "." && named.find('.') != std::string::npos) {
            path = split_on(named, ".");
        }
        return lookup(file, owner, path, {kCodeKindClass}, true);
    }

    /// The declared type of a variable a reference names: a parameter or
    /// local of its function, else a field of its class.
    [[nodiscard]] std::optional<TypedAt> variable(std::size_t file, int owner,
                                                  const std::string& name, bool fields) const {
        const FileFacts& facts = sources_[file].facts;
        for (const CodeTyped& typed : facts.typed) {
            if (typed.owner == owner && typed.name == name &&
                (typed.kind == "param" || typed.kind == "local")) {
                return TypedAt{.type = typed.type, .file = file, .owner = owner};
            }
        }
        if (fields) {
            if (const std::optional<std::string> klass = self_class(file, owner)) {
                return field(*klass, name);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<TypedAt> field(const std::string& klass,
                                               const std::string& name) const {
        std::vector<std::string> chain{klass};
        const std::vector<std::string> bases = bases_of(klass);
        chain.insert(chain.end(), bases.begin(), bases.end());
        for (const std::string& current : chain) {
            const auto it = fields_.find(current);
            if (it == fields_.end()) {
                continue;
            }
            if (const auto found = it->second.find(name); found != it->second.end()) {
                return found->second;
            }
        }
        return std::nullopt;
    }

    /// Follows `idents[1..]` from a starting class: every step but the last a
    /// field, the last a method.
    [[nodiscard]] std::optional<std::string> through_fields(std::string klass,
                                                            const std::vector<std::string>& idents,
                                                            const std::vector<std::string>& seps,
                                                            std::size_t from,
                                                            std::string_view sep) const {
        for (std::size_t i = from; i + 1 < idents.size(); ++i) {
            const std::optional<TypedAt> next = field(klass, idents[i]);
            if (!next.has_value()) {
                return std::nullopt;
            }
            const std::optional<std::string> next_class = class_of_type(
                next->file, next->owner, next->type, i + 1 < seps.size() && seps[i + 1] == "->");
            if (!next_class.has_value()) {
                return std::nullopt;
            }
            klass = *next_class;
        }
        return member_of(klass, {idents.back()}, sep, {kCodeKindFunction});
    }

    // ---- Imports ------------------------------------------------------------------

    /// A file a path names: relative to `dir` first, then the one file whose
    /// path ends with it. Two candidates are none.
    [[nodiscard]] std::optional<std::size_t> find_file(std::size_t from, std::string_view dir,
                                                       std::string_view path,
                                                       const std::vector<std::string>& suffixes,
                                                       bool tree_wide) const {
        const std::string& member = sources_[from].member;
        const auto in_member = [&](const std::string& candidate) -> std::optional<std::size_t> {
            const auto it = files_by_path_.find(candidate);
            if (it == files_by_path_.end()) {
                return std::nullopt;
            }
            for (const std::size_t file : it->second) {
                if (sources_[file].member == member) {
                    return file;
                }
            }
            return std::nullopt;
        };
        for (const std::string& suffix : suffixes) {
            const std::string written = std::string{path} + suffix;
            if (const std::optional<std::string> joined = join_path(dir, written)) {
                if (const std::optional<std::size_t> found = in_member(*joined)) {
                    return found;
                }
            }
        }
        if (!tree_wide) {
            return std::nullopt;
        }
        for (const std::string& suffix : suffixes) {
            const std::string written = std::string{path} + suffix;
            std::optional<std::size_t> only;
            int count = 0;
            for (std::size_t file = 0; file < sources_.size(); ++file) {
                const std::string& candidate = sources_[file].facts.path;
                if (sources_[file].member != member) {
                    continue;
                }
                if (candidate == written || candidate.ends_with("/" + written)) {
                    only = file;
                    ++count;
                }
            }
            if (count == 1) {
                return only;
            }
            if (count > 1) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    /// A Python module (dotted segments) to its file: exactly, else the one
    /// file whose module ends with it.
    [[nodiscard]] std::optional<std::size_t> find_python_module(
        std::size_t from, const std::vector<std::string>& module) const {
        if (module.empty()) {
            return std::nullopt;
        }
        std::optional<std::size_t> exact;
        std::optional<std::size_t> suffix;
        int suffixes = 0;
        for (std::size_t file = 0; file < sources_.size(); ++file) {
            const FileFacts& facts = sources_[file].facts;
            if (facts.language != "python" || sources_[file].member != sources_[from].member) {
                continue;
            }
            if (facts.module == module) {
                exact = file;
                break;
            }
            if (facts.module.size() > module.size() &&
                std::equal(module.rbegin(), module.rend(), facts.module.rbegin())) {
                suffix = file;
                ++suffixes;
            }
        }
        if (exact.has_value()) {
            return exact;
        }
        return suffixes == 1 ? suffix : std::nullopt;
    }

    void import_edge(std::size_t file, const NodeKey& target, std::uint32_t line, bool resolved) {
        ++counts_.imports;
        if (resolved) {
            ++counts_.imports_resolved;
        }
        edge(file_node(file), target, kCodeRelationImports, file, line);
    }

    void bind_imports(std::size_t file) {
        const FileFacts& facts = sources_[file].facts;
        FileContext& context = contexts_[file];
        const CodeLanguage& language = *context.language;
        const std::string dir = parent_dir(facts.path);
        for (const CodeImport& item : facts.imports) {
            switch (language.id) {
                case CodeLanguageId::C:
                case CodeLanguageId::Cpp: {
                    if (item.kind == "symbol") {
                        const std::vector<std::string> parts = split_on(item.path, "::");
                        if (!parts.empty()) {
                            context.bindings[parts.back()] = Binding{.target = item.path};
                        }
                        break;
                    }
                    if (item.kind == "namespace") {
                        context.roots.push_back(namespace_root(item.path, "::"));
                        break;
                    }
                    if (const std::optional<std::size_t> target =
                            find_file(file, dir, item.path, {""}, true)) {
                        import_edge(file, file_node(*target), item.line, true);
                    } else {
                        import_edge(file, unresolved(item.path, file, item.line), item.line, false);
                    }
                    break;
                }
                case CodeLanguageId::Python:
                    bind_python(file, item);
                    break;
                case CodeLanguageId::JavaScript:
                case CodeLanguageId::TypeScript:
                case CodeLanguageId::Tsx:
                    bind_javascript(file, item, dir);
                    break;
                case CodeLanguageId::Go: {
                    std::optional<std::string> package;
                    for (const auto& [qualified, symbols] : symbols_) {
                        if (std::ranges::any_of(symbols,
                                                [&](const Symbol& symbol) {
                                                    return symbol.kind == kCodeKindModule &&
                                                           sources_[symbol.file].facts.language ==
                                                               "go";
                                                }) &&
                            (item.path == qualified || item.path.ends_with("/" + qualified))) {
                            package = qualified;
                            break;
                        }
                    }
                    const std::vector<std::string> parts = split_on(item.path, "/");
                    const std::string local = !item.alias.empty()
                                                  ? item.alias
                                                  : (parts.empty() ? item.path : parts.back());
                    context.bindings[local] = Binding{.target = package.value_or(item.path)};
                    if (package.has_value()) {
                        import_edge(file, key(kCodeKindModule, *package), item.line, true);
                    } else {
                        import_edge(file, unresolved(item.path, file, item.line), item.line, false);
                    }
                    break;
                }
                case CodeLanguageId::Rust: {
                    std::vector<std::string> path = split_on(item.path, "::");
                    if (item.kind == "namespace") {
                        if (const std::optional<std::string> module =
                                lookup(file, -1, path, {kCodeKindModule, kCodeKindClass}, true)) {
                            context.roots.push_back(*module);
                            import_edge(file, key(kind_of(*module), *module), item.line, true);
                        } else {
                            import_edge(file, unresolved(item.path, file, item.line), item.line,
                                        false);
                        }
                        break;
                    }
                    const std::string local =
                        !item.alias.empty() ? item.alias : (path.empty() ? item.path : path.back());
                    const std::optional<std::string> target = lookup(
                        file, -1, path, {kCodeKindModule, kCodeKindClass, kCodeKindFunction}, true);
                    context.bindings[local] = Binding{.target = target.value_or(item.path)};
                    if (target.has_value()) {
                        import_edge(file, key(kind_of(*target), *target), item.line, true);
                    } else {
                        import_edge(file, unresolved(item.path, file, item.line), item.line, false);
                    }
                    break;
                }
                case CodeLanguageId::Java:
                case CodeLanguageId::CSharp: {
                    if (item.kind == "namespace") {
                        context.roots.push_back(item.path);
                        if (has(item.path, {kCodeKindModule})) {
                            import_edge(file, key(kCodeKindModule, item.path), item.line, true);
                        } else {
                            import_edge(file, unresolved(item.path, file, item.line), item.line,
                                        false);
                        }
                        break;
                    }
                    const std::vector<std::string> parts = split_on(item.path, ".");
                    const std::string local = !item.alias.empty()
                                                  ? item.alias
                                                  : (parts.empty() ? item.path : parts.back());
                    context.bindings[local] = Binding{.target = item.path};
                    if (has(item.path, {kCodeKindClass, kCodeKindModule, kCodeKindFunction})) {
                        import_edge(file, key(kind_of(item.path), item.path), item.line, true);
                    } else {
                        import_edge(file, unresolved(item.path, file, item.line), item.line, false);
                    }
                    break;
                }
                case CodeLanguageId::Ruby: {
                    const bool relative = item.kind == "relative";
                    const std::optional<std::size_t> target = find_file(
                        file, relative ? dir : std::string{}, item.path, {".rb", ""}, !relative);
                    if (target.has_value()) {
                        import_edge(file, file_node(*target), item.line, true);
                    } else {
                        import_edge(file, unresolved(item.path, file, item.line), item.line, false);
                    }
                    break;
                }
                case CodeLanguageId::Bash: {
                    const std::optional<std::size_t> target =
                        find_file(file, dir, item.path, {""}, false);
                    if (target.has_value()) {
                        context.roots.push_back(sources_[*target].facts.path);
                        import_edge(file, file_node(*target), item.line, true);
                    } else {
                        import_edge(file, unresolved(item.path, file, item.line), item.line, false);
                    }
                    break;
                }
            }
        }
    }

    [[nodiscard]] std::string kind_of(const std::string& qualified) const {
        const auto it = symbols_.find(qualified);
        if (it == symbols_.end() || it->second.empty()) {
            return std::string{kCodeKindName};
        }
        // A deterministic pick when a name is two kinds: module, class, function.
        for (const std::string_view kind : {kCodeKindModule, kCodeKindClass, kCodeKindFunction}) {
            if (std::ranges::any_of(it->second,
                                    [&](const Symbol& symbol) { return symbol.kind == kind; })) {
                return std::string{kind};
            }
        }
        return it->second.front().kind;
    }

    /// A `using namespace` / wildcard target: as written when it is a
    /// module, else the one module whose name ends with it.
    [[nodiscard]] std::string namespace_root(const std::string& path, std::string_view sep) const {
        if (has(path, {kCodeKindModule})) {
            return path;
        }
        std::optional<std::string> only;
        int count = 0;
        for (const auto& [qualified, symbols] : symbols_) {
            if (qualified.ends_with(std::string{sep} + path) &&
                std::ranges::any_of(
                    symbols, [](const Symbol& symbol) { return symbol.kind == kCodeKindModule; })) {
                only = qualified;
                ++count;
            }
        }
        return count == 1 ? only.value_or(path) : path;
    }

    void bind_python(std::size_t file, const CodeImport& item) {
        const FileFacts& facts = sources_[file].facts;
        FileContext& context = contexts_[file];
        // The module the import names: relative ones from this file's package.
        std::vector<std::string> module;
        std::string written = item.path;
        if (written.starts_with('.')) {
            std::size_t dots = 0;
            while (dots < written.size() && written[dots] == '.') {
                ++dots;
            }
            std::vector<std::string> package = facts.module;
            const bool is_package = facts.path.ends_with("__init__.py");
            if (!is_package && !package.empty()) {
                package.pop_back();
            }
            for (std::size_t up = 1; up < dots && !package.empty(); ++up) {
                package.pop_back();
            }
            module = package;
            for (const std::string& part : split_on(written.substr(dots), ".")) {
                module.push_back(part);
            }
        } else {
            module = split_on(written, ".");
        }
        const auto dotted = [](const std::vector<std::string>& parts) {
            return join_segments(parts, ".");
        };
        if (item.kind == "namespace") {
            if (const std::optional<std::size_t> target = find_python_module(file, module)) {
                context.roots.push_back(dotted(sources_[*target].facts.module));
                import_edge(file, file_node(*target), item.line, true);
            } else {
                import_edge(file, unresolved(item.path, file, item.line), item.line, false);
            }
            return;
        }
        if (!item.name.empty()) {
            // `from m import n`: a submodule n, else a name n in m.
            std::vector<std::string> submodule = module;
            submodule.push_back(item.name);
            const std::string local = item.alias.empty() ? item.name : item.alias;
            if (const std::optional<std::size_t> target = find_python_module(file, submodule)) {
                context.bindings[local] = Binding{.target = dotted(sources_[*target].facts.module)};
                import_edge(file, file_node(*target), item.line, true);
                return;
            }
            if (const std::optional<std::size_t> target = find_python_module(file, module)) {
                context.bindings[local] =
                    Binding{.target = dotted(sources_[*target].facts.module) + "." + item.name};
                import_edge(file, file_node(*target), item.line, true);
                return;
            }
            context.bindings[local] = Binding{.target = item.path + "." + item.name};
            import_edge(file, unresolved(item.path, file, item.line), item.line, false);
            return;
        }
        const std::string local = item.alias.empty() ? written : item.alias;
        if (const std::optional<std::size_t> target = find_python_module(file, module)) {
            context.bindings[local] = Binding{.target = dotted(sources_[*target].facts.module)};
            import_edge(file, file_node(*target), item.line, true);
        } else {
            context.bindings[local] = Binding{.target = written};
            import_edge(file, unresolved(item.path, file, item.line), item.line, false);
        }
    }

    void bind_javascript(std::size_t file, const CodeImport& item, const std::string& dir) {
        FileContext& context = contexts_[file];
        std::optional<std::size_t> target;
        if (item.path.starts_with('.')) {
            target = find_file(file, dir, item.path,
                               {"", ".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs", ".mts", ".cts",
                                ".d.ts", "/index.ts", "/index.tsx", "/index.js", "/index.jsx"},
                               false);
        }
        const std::string prefix =
            target.has_value() ? sources_[*target].facts.path + "::" : std::string{};
        if (!item.name.empty()) {
            const std::string local = item.alias.empty() ? item.name : item.alias;
            context.bindings[local] = target.has_value()
                                          ? Binding{.target = prefix + item.name}
                                          : Binding{.target = item.path + "." + item.name};
        } else if (!item.alias.empty()) {
            context.bindings[item.alias] = target.has_value()
                                               ? Binding{.target = prefix, .file_prefix = true}
                                               : Binding{.target = item.path};
        }
        // One edge per import statement, however many names it binds.
        if (!imported_.emplace(file, item.line, item.path).second) {
            return;
        }
        if (target.has_value()) {
            import_edge(file, file_node(*target), item.line, true);
        } else {
            import_edge(file, unresolved(item.path, file, item.line), item.line, false);
        }
    }

    // ---- Inherits ------------------------------------------------------------------

    void resolve_inherits(std::size_t file) {
        const FileFacts& facts = sources_[file].facts;
        const CodeLanguage& language = language_of(file);
        for (const CodeInherit& item : facts.inherits) {
            ++counts_.inherits;
            std::optional<std::string> child;
            if (const CodeDefinition* def = definition(file, item.child_definition)) {
                child = def->qualified;
            } else {
                child = lookup(file, item.owner, split_on(item.child, language.separator),
                               {kCodeKindClass}, true);
            }
            if (!child.has_value()) {
                continue;  // a type outside the tree implementing a trait: no node to hang it on
            }
            const std::optional<std::string> base = class_of_written(file, item.owner, item.base);
            const NodeKey child_key = key(kCodeKindClass, *child);
            if (base.has_value()) {
                ++counts_.inherits_resolved;
                bases_[*child].push_back(*base);
                edge(child_key, key(kCodeKindClass, *base), kCodeRelationInherits, file, item.line);
            } else {
                edge(child_key, unresolved(item.base, file, item.line), kCodeRelationInherits, file,
                     item.line);
            }
        }
    }

    /// A class named as written (`ns::Base`, `mod.Other`), from `owner`.
    [[nodiscard]] std::optional<std::string> class_of_written(std::size_t file, int owner,
                                                              const std::string& written) const {
        const CodeLanguage& language = language_of(file);
        std::vector<std::string> path = split_on(written, language.separator);
        if (language.separator != "." && written.find('.') != std::string::npos &&
            written.find("::") == std::string::npos) {
            path = split_on(written, ".");
        }
        return lookup(file, owner, path, {kCodeKindClass}, true);
    }

    // ---- References -----------------------------------------------------------------

    void resolve_references(std::size_t file) {
        const FileFacts& facts = sources_[file].facts;
        for (const CodeReference& ref : facts.references) {
            if (ref.kind == "type") {
                ++counts_.types;
                const NodeKey source = source_node(file, ref.owner);
                if (const std::optional<std::string> klass =
                        class_of_written(file, ref.owner, ref.text)) {
                    ++counts_.types_resolved;
                    edge(source, key(kCodeKindClass, *klass), kCodeRelationReferences, file,
                         ref.line);
                } else {
                    edge(source, unresolved(ref.text, file, ref.line), kCodeRelationReferences,
                         file, ref.line);
                }
                continue;
            }
            ++counts_.calls;
            const NodeKey source = source_node(file, ref.owner);
            std::string unresolved_text;
            if (const std::optional<std::string> target =
                    resolve_call(file, ref.owner, ref.text, unresolved_text)) {
                ++counts_.calls_resolved;
                edge(source, key(kind_of(*target), *target), kCodeRelationCalls, file, ref.line);
            } else {
                edge(source, unresolved(unresolved_text, file, ref.line), kCodeRelationCalls, file,
                     ref.line);
            }
        }
    }

    /// A call's callee, or nullopt with `unresolved_text` set to the name its
    /// `name` node carries.
    [[nodiscard]] std::optional<std::string> resolve_call(std::size_t file, int owner,
                                                          const std::string& text,
                                                          std::string& unresolved_text) const {
        const CodeLanguage& language = language_of(file);
        const std::string sep{language.separator};
        unresolved_text = text;
        if (language.id == CodeLanguageId::Bash) {
            // A command: a function in this script or one it sources.
            if (const std::string here = sources_[file].facts.prefix + text;
                has(here, {kCodeKindFunction})) {
                return here;
            }
            for (const std::string& root : contexts_[file].roots) {
                if (const std::string there = root + "::" + text; has(there, {kCodeKindFunction})) {
                    return there;
                }
            }
            return std::nullopt;
        }
        const Tokens tokens = tokenize(text);
        if (tokens.complex) {
            // A call on an expression: only a trailing member name is
            // nameable.
            if (tokens.idents.size() > 1 &&
                (tokens.seps.back() == "." || tokens.seps.back() == "->")) {
                unresolved_text = "." + tokens.idents.back();
            }
            return std::nullopt;
        }
        const std::initializer_list<std::string_view> callables{kCodeKindFunction, kCodeKindClass};
        const bool scopes = members_in_scope(language.id);
        const std::string_view self = self_word(language.id);

        if (sep == "::") {
            // `::` qualifies; `.` and `->` reach a member.
            std::size_t member = 0;
            for (std::size_t i = 1; i < tokens.seps.size(); ++i) {
                if (tokens.seps[i] == "." || tokens.seps[i] == "->") {
                    member = i;
                }
            }
            if (member == 0) {
                return lookup(file, owner, tokens.idents, callables, scopes);
            }
            unresolved_text = "." + tokens.idents.back();
            // The receiver: `this`/`self`, a typed variable, or a class path.
            const std::vector<std::string> receiver(
                tokens.idents.begin(), tokens.idents.begin() + static_cast<std::ptrdiff_t>(member));
            bool receiver_is_path = true;
            for (std::size_t i = 1; i < member; ++i) {
                if (tokens.seps[i] != "::") {
                    receiver_is_path = false;
                }
            }
            if (receiver.size() == 1 && !self.empty() && receiver.front() == self) {
                if (const std::optional<std::string> klass = self_class(file, owner)) {
                    return through_fields(*klass, tokens.idents, tokens.seps, 1, sep);
                }
                return std::nullopt;
            }
            if (const std::optional<TypedAt> typed =
                    variable(file, owner, tokens.idents.front(), scopes)) {
                const std::optional<std::string> klass =
                    class_of_type(typed->file, typed->owner, typed->type,
                                  tokens.seps.size() > 1 && tokens.seps[1] == "->");
                if (klass.has_value()) {
                    return through_fields(*klass, tokens.idents, tokens.seps, 1, sep);
                }
                return std::nullopt;
            }
            if (receiver_is_path && member + 1 == tokens.idents.size()) {
                if (const std::optional<std::string> klass =
                        lookup(file, owner, receiver, {kCodeKindClass}, true)) {
                    return member_of(*klass, {tokens.idents.back()}, sep, {kCodeKindFunction});
                }
            }
            return std::nullopt;
        }

        // `.` languages: a dot qualifies and reaches a member alike.
        const std::vector<std::string>& idents = tokens.idents;
        if (idents.size() == 1) {
            return lookup(file, owner, idents, callables, scopes);
        }
        if (!self.empty() && idents.front() == self) {
            unresolved_text = "." + idents.back();
            if (const std::optional<std::string> klass = self_class(file, owner)) {
                return through_fields(*klass, idents, tokens.seps, 1, sep);
            }
            return std::nullopt;
        }
        if (const std::optional<TypedAt> typed = variable(file, owner, idents.front(), scopes)) {
            unresolved_text = "." + idents.back();
            if (const std::optional<std::string> klass =
                    class_of_type(typed->file, typed->owner, typed->type, false)) {
                return through_fields(*klass, idents, tokens.seps, 1, sep);
            }
            return std::nullopt;
        }
        if (const std::optional<std::string> found =
                lookup(file, owner, idents, callables, scopes)) {
            return found;
        }
        // `Class.method` through a class the receiver path names.
        const std::vector<std::string> receiver(idents.begin(), idents.end() - 1);
        if (const std::optional<std::string> klass =
                lookup(file, owner, receiver, {kCodeKindClass}, true)) {
            if (std::optional<std::string> method =
                    member_of(*klass, {idents.back()}, sep, {kCodeKindFunction})) {
                return method;
            }
        }
        // Unresolved: through an import it keeps the name it reached;
        // through anything else, only the member's.
        if (const std::optional<std::string> bound = rebind(file, idents)) {
            unresolved_text = *bound;
        } else {
            unresolved_text = "." + idents.back();
        }
        return std::nullopt;
    }

    // ---- Output -------------------------------------------------------------------

    [[nodiscard]] CodeGraph finish() const {
        CodeGraph out;
        out.counts = counts_;
        for (const auto& [k, original] : nodes_) {
            CodeNode node = original;
            if (const auto mentioned = mentions_.find(k); mentioned != mentions_.end()) {
                node.mentions.assign(mentioned->second.begin(), mentioned->second.end());
            }
            embedstore::CodeNodeMetadata metadata;
            metadata.code = true;
            if (const auto primary = primary_.find(k); primary != primary_.end()) {
                node.description = primary->second.description;
                metadata.language = primary->second.language;
                metadata.member = primary->second.location.member;
                metadata.file = primary->second.location.file;
                metadata.line = primary->second.location.line;
                metadata.end_line = primary->second.location.end_line;
            } else {
                metadata.unresolved = true;
            }
            node.metadata = embedstore::code_node_metadata_json(metadata);
            out.nodes.push_back(std::move(node));
        }
        for (const auto& [k, sites] : edges_) {
            const auto& [source, target, relation] = k;
            const auto from = nodes_.find(source);
            const auto to = nodes_.find(target);
            if (from == nodes_.end() || to == nodes_.end()) {
                continue;  // every endpoint is made before its edge; guarded anyway
            }
            CodeEdge edge;
            edge.source_type = source.first;
            edge.source_name = from->second.name;
            edge.target_type = target.first;
            edge.target_name = to->second.name;
            edge.relation = relation;
            edge.sites.assign(sites.begin(), sites.end());
            out.edges.push_back(std::move(edge));
        }
        return out;
    }

    struct Primary {
        std::string description;
        CodeLocation location;
        std::string language;
    };

    std::vector<SourceFacts> sources_;
    std::vector<FileContext> contexts_;
    std::map<std::string, std::vector<std::size_t>> files_by_path_;
    std::map<std::string, std::vector<Symbol>> symbols_;
    std::map<std::string, std::vector<std::string>> bases_;
    std::map<std::string, std::map<std::string, TypedAt>> fields_;
    std::map<NodeKey, CodeNode> nodes_;
    std::map<NodeKey, std::set<CodeLocation>> mentions_;
    std::map<NodeKey, Primary> primary_;
    std::map<EdgeKey, std::set<CodeLocation>> edges_;
    std::set<std::tuple<std::size_t, std::uint32_t, std::string>> imported_;
    ResolveCounts counts_;
};

}  // namespace

CodeGraph resolve_code(std::vector<SourceFacts> sources) {
    Resolver resolver{std::move(sources)};
    return resolver.run();
}

}  // namespace apogee::graph
