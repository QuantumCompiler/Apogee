#include "commands/complete_protocol.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>

#include "commands/complete_sources.h"
#include "harness/paths.h"

namespace apogee::commands {
namespace {

[[nodiscard]] bool contains(const std::vector<std::string>& list, std::string_view word) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

/// What `option` takes, from what it declared: its type name, and the
/// validators CLI11 appends to it (`TEXT:{a,b}`, `TEXT:FILE`). `display` is how
/// the hint names it -- the spelling typed, or the positional's name.
[[nodiscard]] ValueSpec value_of(const CLI::Option& option, const std::string& display) {
    ValueSpec value;
    const std::string type = option.get_type_name();
    const std::size_t colon = type.find(':');
    const std::string base = type.substr(0, colon);
    value.type = base;
    if (base == kBackendValue) {
        value.kind = ValueKind::Backend;
    } else if (base == kPathValue) {
        value.kind = ValueKind::Path;
    } else if (std::ranges::find(kNameValues, base) != kNameValues.end()) {
        value.kind = ValueKind::Names;
        value.source = base;
    }
    if (colon != std::string::npos) {
        std::stringstream validators{type.substr(colon + 1)};
        for (std::string part; std::getline(validators, part, ':');) {
            if (part.size() >= 2 && part.front() == '{' && part.back() == '}') {
                // `CLI::IsMember` -- the set the parser will hold the word to.
                value.kind = ValueKind::Choice;
                std::stringstream members{part.substr(1, part.size() - 2)};
                for (std::string member; std::getline(members, member, ',');) {
                    value.choices.push_back(member);
                }
            } else if (part == "FILE" || part == "DIR" || part.starts_with("PATH(")) {
                value.kind = ValueKind::Path;  // CLI::ExistingFile and its siblings
            }
        }
    }
    value.hint = display + " " + base;
    if (!option.get_description().empty()) {
        value.hint += ": " + option.get_description();
    }
    return value;
}

/// The names `value` offers for `current`: a list's words after the last
/// comma, carrying what precedes it and skipping what is already there.
[[nodiscard]] Completion offer_names(const ValueSpec& value, const CompletionSources& sources,
                                     const CompletionContext& context, std::string_view current) {
    Completion completion;
    const bool list = value.source == kCollectionListValue;
    std::string head;
    std::string_view word = current;
    if (const std::size_t comma = current.rfind(','); list && comma != std::string_view::npos) {
        head = std::string{current.substr(0, comma + 1)};
        word = current.substr(comma + 1);
    }
    NameList found;
    if (sources) {
        try {
            found = sources(list ? std::string_view{kCollectionValue} : value.source, context);
        } catch (const std::exception&) {
            found = {};  // silent, as every completion failure is
        }
    }
    std::ranges::sort(found.names);
    const auto [duplicates, end] = std::ranges::unique(found.names);
    found.names.erase(duplicates, end);
    for (const std::string& name : filter_prefix(found.names, word)) {
        if (list && (',' + head).find(',' + name + ',') != std::string::npos) {
            continue;  // already in the list
        }
        completion.candidates.push_back(head + name);
    }
    if (!completion.candidates.empty()) {
        return completion;
    }
    if (found.paths) {
        completion.files = true;
    } else {
        completion.hint = found.names.empty() && !found.none.empty()
                              ? value.hint + " -- " + found.none
                              : value.hint;
    }
    return completion;
}

/// What to offer for a word `value` describes.
[[nodiscard]] Completion offer(const ValueSpec& value, const harness::Config& config,
                               const CompletionSources& sources, const CompletionContext& context,
                               std::string_view current) {
    Completion completion;
    switch (value.kind) {
        case ValueKind::Names:
            return offer_names(value, sources, context, current);
        case ValueKind::Backend: {
            const std::vector<std::string> names = config.backend_names();
            if (names.empty()) {
                completion.hint =
                    value.hint + " -- no backends configured yet ('apogee config add-backend')";
            }
            completion.candidates = filter_prefix(names, current);
            break;
        }
        case ValueKind::Choice:
            completion.candidates = filter_prefix(value.choices, current);
            break;
        case ValueKind::Path:
            completion.files = true;
            break;
        case ValueKind::Text:
            completion.hint = value.hint;
            break;
    }
    return completion;
}

/// The child of `node` that `word` names, by name or alias; nullptr if none.
[[nodiscard]] const CommandSpec* child_named(const CommandSpec& node, std::string_view word) {
    for (const CommandSpec& child : node.subcommands) {
        if (child.name == word || contains(child.aliases, word)) {
            return &child;
        }
    }
    return nullptr;
}

[[nodiscard]] CommandSpec spec_of(const CLI::App& app) {
    CommandSpec spec;
    spec.name = app.get_name();
    spec.aliases = app.get_aliases();

    for (const CLI::Option* option : app.get_options()) {
        if (!option->nonpositional()) {
            spec.positionals.push_back(value_of(*option, option->get_name(true)));
            spec.last_positional_repeats = option->get_items_expected_max() > 1;
            continue;
        }
        std::vector<std::string> spellings;
        for (const std::string& name : option->get_snames()) {
            spellings.push_back("-" + name);
        }
        for (const std::string& name : option->get_lnames()) {
            spellings.push_back("--" + name);
        }
        for (const std::string& spelling : spellings) {
            spec.flags.push_back(spelling);
            // A flag has no value to take; an option has at least one.
            if (option->get_type_size_max() > 0) {
                spec.values.emplace(spelling, value_of(*option, spelling));
            }
        }
    }
    std::sort(spec.flags.begin(), spec.flags.end());

    for (const CLI::App* sub : app.get_subcommands({})) {
        if (sub->get_group().empty()) {
            continue;  // hidden from --help, so hidden from completion too
        }
        spec.subcommands.push_back(spec_of(*sub));
    }
    return spec;
}

}  // namespace

std::vector<std::string> filter_prefix(const std::vector<std::string>& candidates,
                                       std::string_view prefix) {
    if (prefix.empty()) {
        return candidates;
    }
    std::vector<std::string> matched;
    for (const std::string& candidate : candidates) {
        if (candidate.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), candidate.begin())) {
            matched.push_back(candidate);
        }
    }
    return matched;
}

CommandSpec specs_from_app(const CLI::App& app) {
    // The root's own flags live on the root node, so a bare `apogee --<TAB>`
    // needs no special case.
    CommandSpec root = spec_of(app);
    root.name.clear();
    return root;
}

Completion complete_words(const CompletionRequest& request, const harness::Config& config,
                          const CommandSpec& root, const CompletionSources& sources) {
    // Walk the words the way the parser will: descend on a subcommand's name,
    // step over a flag and the value it consumes, count everything else as a
    // positional of the command in play -- keeping what each was, for a list
    // that depends on it.
    const CommandSpec* node = &root;
    CompletionContext context;
    context.config = &config;
    const ValueSpec* pending = nullptr;
    std::string pending_flag;
    for (const std::string& word : request.words) {
        if (pending != nullptr) {
            context.flags[pending_flag] = word;  // this word was that flag's value
            pending = nullptr;
            continue;
        }
        if (word.size() > 1 && word.front() == '-') {
            // `--model=x` carries its own value; `--model x` takes the next word.
            if (const std::size_t equals = word.find('='); equals != std::string::npos) {
                context.flags[word.substr(0, equals)] = word.substr(equals + 1);
            } else if (const auto value = node->values.find(word); value != node->values.end()) {
                pending = &value->second;
                pending_flag = word;
            }
            continue;
        }
        if (const CommandSpec* child = child_named(*node, word)) {
            node = child;
            context.positionals.clear();
            context.flags.clear();
            continue;
        }
        context.positionals.push_back(word);
    }
    const std::size_t positionals = context.positionals.size();

    // The cursor is on a flag's value.
    if (pending != nullptr) {
        return offer(*pending, config, sources, context, request.current);
    }

    // A word starting with a dash is a flag being typed: the flags of whichever
    // command is in play -- the root's when none is. Only its own: CLI11 does
    // not accept a parent's flag after a subcommand.
    if (request.current.starts_with('-')) {
        return {.candidates = filter_prefix(node->flags, request.current)};
    }

    // A command with verbs, none chosen yet: offer them.
    if (!node->subcommands.empty() && positionals == 0) {
        std::vector<std::string> names;
        names.reserve(node->subcommands.size());
        for (const CommandSpec& child : node->subcommands) {
            names.push_back(child.name);
        }
        return {.candidates = filter_prefix(names, request.current)};
    }

    // The next positional, if the command takes one.
    if (positionals < node->positionals.size()) {
        return offer(node->positionals[positionals], config, sources, context, request.current);
    }
    if (!node->positionals.empty() && node->last_positional_repeats) {
        return offer(node->positionals.back(), config, sources, context, request.current);
    }

    // Every positional is given: only flags can follow, so offer those rather
    // than nothing.
    return {.candidates = filter_prefix(node->flags, request.current)};
}

std::vector<std::string> completion_candidates(const CompletionRequest& request,
                                               const harness::Config& config,
                                               const CommandSpec& root,
                                               const CompletionSources& sources) {
    return complete_words(request, config, root, sources).candidates;
}

std::string render_completion(const Completion& completion, bool directives) {
    std::string out;
    if (directives) {
        if (!completion.candidates.empty() || (!completion.files && completion.hint.empty())) {
            out += ":values\n";
        } else if (completion.files) {
            out += ":files\n";
        } else {
            // One line, whatever the description held: the stubs read lines.
            std::string hint = completion.hint;
            std::replace(hint.begin(), hint.end(), '\n', ' ');
            out += ":hint " + hint + "\n";
        }
    }
    for (const std::string& candidate : completion.candidates) {
        out += candidate + "\n";
    }
    return out;
}

std::string_view CompleteProtocolCommand::name() const noexcept {
    return "__complete";
}

std::string_view CompleteProtocolCommand::summary() const noexcept {
    return "Shell completion protocol (internal)";
}

void CompleteProtocolCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->group("");  // hidden from --help: it is a protocol, not a feature

    // Everything after `__complete` is DATA, not arguments. Without this the
    // parser claims them: a line ending `--model ""` -- the exact case this
    // protocol exists for -- has `--model` eaten as an unknown option, and the
    // completion silently returns nothing. `prefix_command` stops parsing and
    // hands the rest over verbatim.
    cmd->prefix_command();

    cmd->callback([&context, cmd, &root]() {
        const std::vector<std::string> raw = cmd->remaining(true);

        CompletionRequest request;
        const std::vector<std::string>* words = &raw;
        if (!words->empty()) {
            // The last word is what the user is typing; everything before it is
            // context. A trailing empty argument (the shell's way of saying
            // "the cursor is at a fresh word") lands here as an empty `current`,
            // which is exactly right.
            request.current = words->back();
            request.words.assign(words->begin(), words->end() - 1);
        }

        harness::Config config;
        try {
            config = harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const std::exception&) {
            // Silent. A broken or absent config makes completion unhelpful; it
            // must never print a diagnostic into the user's command line.
        }

        // Read out of the live parser, so a flag or command added anywhere, at
        // any depth, completes without this file being told about it. The
        // protocol verb itself is hidden, so it is never offered.
        const CommandSpec tree = specs_from_app(root);

        // Directives only for a stub that asks: an older stub would offer the
        // directive line itself as a candidate.
        const char* protocol = std::getenv(kCompletionProtocolVar);
        const bool directives = protocol != nullptr && std::string_view{protocol} == "2";
        std::cout << render_completion(
            complete_words(request, config, tree, default_completion_sources()), directives);
    });
}

}  // namespace apogee::commands
