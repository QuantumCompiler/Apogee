#include "cli/complete_protocol.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string_view>

#include "backends/model_roster.h"
#include "cli/complete_sources.h"
#include "contracts/paths.h"

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
    value.list = base.ends_with(",...") || option.get_delimiter() == ',';
    if (base == kBackendValue) {
        value.kind = ValueKind::Backend;
    } else if (base == kBackendOrModelValue) {
        value.kind = ValueKind::Model;
    } else if (base == kPathValue) {
        value.kind = ValueKind::Path;
    } else if (std::ranges::find(kNameValues, base) != kNameValues.end()) {
        value.kind = ValueKind::Names;
        value.source = base;
    }
    if (colon != std::string::npos) {
        // Colon-separated -- a `{...}` set read whole, since a word in it may
        // hold a colon itself (`--on-question`'s `answer:`, 27i).
        const std::string validators = type.substr(colon + 1);
        for (std::size_t at = 0; at < validators.size();) {
            std::size_t end = validators.find(validators[at] == '{' ? '}' : ':', at);
            if (end == std::string::npos) {
                end = validators.size();
            } else if (validators[at] == '{') {
                ++end;  // the set's closing brace is its own
            }
            const std::string part = validators.substr(at, end - at);
            at = end + 1;
            if (part.size() >= 2 && part.front() == '{' && part.back() == '}') {
                std::vector<std::string> words;
                std::stringstream members{part.substr(1, part.size() - 2)};
                for (std::string member; std::getline(members, member, ',');) {
                    words.push_back(member);
                }
                const bool keyed = value.kind == ValueKind::Choice && !value.choices.empty() &&
                                   std::ranges::all_of(value.choices, [](const std::string& key) {
                                       return key.ends_with('=');
                                   });
                if (value.kind == ValueKind::Names) {
                    // A name kind keeps its source: the set is every word the
                    // parser takes, the source the ones the line can
                    // (`models convert --mlx --type`).
                } else if (keyed) {
                    value.key_values = std::move(words);  // `keyed_words_value`
                } else {
                    // `CLI::IsMember` -- the set the parser will hold the word to.
                    value.kind = ValueKind::Choice;
                    value.choices.insert(value.choices.end(), words.begin(), words.end());
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

/// The `items` that complete `current`. For a list, the word after the last
/// comma: what precedes it is carried, and what it already holds is skipped.
[[nodiscard]] std::vector<std::string> offer_items(const std::vector<std::string>& items,
                                                   const ValueSpec& value,
                                                   std::string_view current) {
    std::string head;
    std::string_view word = current;
    if (const std::size_t comma = current.rfind(',');
        value.list && comma != std::string_view::npos) {
        head = std::string{current.substr(0, comma + 1)};
        word = current.substr(comma + 1);
    }
    std::vector<std::string> offered;
    for (const std::string& item : filter_prefix(items, word)) {
        if (value.list && (',' + head).find(',' + item + ',') != std::string::npos) {
            continue;  // already in the list
        }
        offered.push_back(head + item);
    }
    return offered;
}

/// The names `value` offers for `current`.
[[nodiscard]] Completion offer_names(const ValueSpec& value, const CompletionSources& sources,
                                     const CompletionContext& context, std::string_view current) {
    Completion completion;
    const std::string_view kind = value.source == kCollectionListValue
                                      ? std::string_view{kCollectionValue}
                                      : std::string_view{value.source};
    NameList found;
    if (sources) {
        try {
            found = sources(kind, context);
        } catch (const std::exception&) {
            found = {};  // silent, as every completion failure is
        }
    }
    // The one order (45a), unless the source's own is the order.
    if (found.as_listed) {
        drop_repeats(found.names);
    } else {
        order_names(found.names);
    }
    completion.candidates = offer_items(found.names, value, current);
    // Each candidate's note for a menu (45b), when the source gave any.
    if (!found.notes.empty()) {
        for (const std::string& candidate : completion.candidates) {
            const auto note = found.notes.find(candidate);
            completion.notes.push_back(note == found.notes.end() ? CandidateNote{} : note->second);
        }
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
            // A backend's name alone: the configured keys, as the BACKEND
            // column shows them -- never a shelf or a roster id, which name
            // a model to run and no entry.
            std::vector<std::string> keys = config.backend_names();
            if (keys.empty()) {
                completion.hint =
                    value.hint + " -- no backends configured yet ('apogee config add-backend')";
            }
            order_names(keys);
            completion.candidates = filter_prefix(keys, current);
            completion.notes = model_notes(config, completion.candidates);
            break;
        }
        case ValueKind::Model: {
            if (config.backend_names().empty()) {
                completion.hint =
                    value.hint + " -- no backends configured yet ('apogee config add-backend')";
            }
            // The namespaces (45a): the configured keys and each roster's
            // `<key>:` first, a shelf behind its colon, a typed word
            // matching everything -- the cached rosters read (M13), never
            // fetched here.
            completion.candidates = model_candidates(model_shelves(config), current);
            completion.notes = model_notes(config, completion.candidates);
            break;
        }
        case ValueKind::Choice: {
            // Past a key's `=`, that key's values: `chat=fs,g` -> `chat=fs,git`.
            const std::size_t equals = current.find('=');
            if (!value.key_values.empty() && equals != std::string_view::npos &&
                contains(value.choices, current.substr(0, equals + 1))) {
                const std::string key{current.substr(0, equals + 1)};
                const ValueSpec values{.kind = ValueKind::Choice, .list = true};
                for (const std::string& item :
                     offer_items(value.key_values, values, current.substr(equals + 1))) {
                    completion.candidates.push_back(key + item);
                }
                break;
            }
            completion.candidates = offer_items(value.choices, value, current);
            break;
        }
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
        if (option->get_group().empty()) {
            continue;  // hidden from --help, so hidden from completion too
        }
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
            // A flag has no value to take; an option has at least one. Asked
            // the way the parser asks: CLI11 gives every option a type size
            // of one, flags included, and a flag none of its items -- read the
            // size and `--safetensors <TAB>` was taken for its value.
            if (option->get_items_expected_max() > 0) {
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

void order_names(std::vector<std::string>& names) {
    const auto folded = [](const std::string& name) {
        std::string out = name;
        std::ranges::transform(out, out.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    std::ranges::sort(names, [&folded](const std::string& a, const std::string& b) {
        const std::string left = folded(a);
        const std::string right = folded(b);
        return left != right ? left < right : a < b;
    });
    const auto [duplicates, end] = std::ranges::unique(names);
    names.erase(duplicates, end);
}

void drop_repeats(std::vector<std::string>& names) {
    std::vector<std::string> kept;
    kept.reserve(names.size());
    for (std::string& name : names) {
        if (std::ranges::find(kept, name) == kept.end()) {
            kept.push_back(std::move(name));
        }
    }
    names = std::move(kept);
}

ModelShelves model_shelves(const harness::Config& config) {
    ModelShelves out;
    out.keys = config.backend_names();
    // How many configured types list each id: a bare id runs only on a sole
    // owner (33), so only those are offered bare.
    std::map<std::string, int> owners;
    for (const auto& [type, roster] : backends::known_rosters().rosters) {
        bool configured = false;
        for (const auto& [name, entry] : config.backends) {
            if (harness::to_string(entry.type) != type) {
                continue;
            }
            configured = true;
            if (harness::names_its_model(entry.type) && !roster.models.empty()) {
                std::vector<std::string>& shelf = out.shelves[name];
                for (const backends::RosterModel& model : roster.models) {
                    shelf.push_back(model.id);
                }
            }
        }
        if (configured) {
            for (const backends::RosterModel& model : roster.models) {
                ++owners[model.id];
            }
        }
    }
    for (const auto& [id, count] : owners) {
        if (count == 1 && std::ranges::find(out.keys, id) == out.keys.end()) {
            out.roster_ids.push_back(id);
        }
    }
    return out;
}

std::vector<std::string> model_bands(const ModelShelves& shelves) {
    std::vector<std::string> keys = shelves.keys;
    order_names(keys);
    std::vector<std::string> prefixes;
    for (const auto& [key, models] : shelves.shelves) {
        prefixes.push_back(key + ":");
    }
    order_names(prefixes);
    std::vector<std::string> ids = shelves.roster_ids;
    order_names(ids);
    std::vector<std::string> out = std::move(keys);
    out.insert(out.end(), prefixes.begin(), prefixes.end());
    out.insert(out.end(), ids.begin(), ids.end());
    drop_repeats(out);
    return out;
}

std::vector<std::string> model_candidates(const ModelShelves& shelves, std::string_view typed) {
    // Past a shelf's colon: that shelf, as `<key>:<model>` (34).
    if (const std::size_t colon = typed.find(':'); colon != std::string_view::npos) {
        if (const auto shelf = shelves.shelves.find(std::string{typed.substr(0, colon)});
            shelf != shelves.shelves.end()) {
            std::vector<std::string> pinned;
            for (const std::string& model : shelf->second) {
                pinned.push_back(shelf->first + ":" + model);
            }
            order_names(pinned);
            return filter_prefix(pinned, typed);
        }
    }
    std::vector<std::string> bands = model_bands(shelves);
    if (typed.empty()) {
        // The map: the keys and the shelves, the territory behind them.
        std::erase_if(bands, [&shelves](const std::string& name) {
            return std::ranges::find(shelves.roster_ids, name) != shelves.roster_ids.end() &&
                   std::ranges::find(shelves.keys, name) == shelves.keys.end();
        });
        return bands;
    }
    return filter_prefix(bands, typed);
}

namespace {

/// A roster's heading: whose it is, and the day it was fetched -- or, for a
/// list carried in the binary, the day it was checked (ADR 0009).
[[nodiscard]] std::string roster_heading(std::string_view type,
                                         const backends::ProviderRoster& roster) {
    return std::string{type} + "'s roster · " + (roster.built_in ? "reviewed " : "fetched ") +
           roster.fetched_at;
}

/// The vendor's name for `id` on `roster`, else the id.
[[nodiscard]] std::string roster_name(const backends::ProviderRoster& roster, std::string_view id) {
    const auto found = std::ranges::find(roster.models, id, &backends::RosterModel::id);
    return found == roster.models.end() ? std::string{id} : found->name;
}

}  // namespace

std::vector<CandidateNote> model_notes(const harness::Config& config,
                                       const std::vector<std::string>& candidates) {
    const backends::RosterCache rosters = backends::known_rosters();
    std::vector<CandidateNote> notes;
    notes.reserve(candidates.size());
    for (const std::string& candidate : candidates) {
        CandidateNote note;
        if (const harness::BackendConfig* entry = config.find_backend(candidate)) {
            // A key: its type and its model or file, as `config list` shows.
            const bool local = entry->type == harness::BackendType::LlamaCpp ||
                               entry->type == harness::BackendType::Mlx;
            note.group = local ? "local" : "configured";
            note.description = std::string{harness::to_string(entry->type)};
            const std::string model =
                !entry->model.empty()
                    ? entry->model
                    : std::filesystem::path{entry->model_path}.filename().string();
            if (!model.empty()) {
                note.description += " · " + model;
            }
        } else if (const std::size_t colon = candidate.find(':');
                   colon != std::string::npos &&
                   config.find_backend(candidate.substr(0, colon)) != nullptr) {
            // A shelf, or a pin on it: under its roster.
            const harness::BackendConfig* owner = config.find_backend(candidate.substr(0, colon));
            const std::string type{harness::to_string(owner->type)};
            if (const backends::ProviderRoster* roster = rosters.roster_for(type)) {
                note.group = roster_heading(type, *roster);
                note.description =
                    colon + 1 == candidate.size()
                        ? std::to_string(roster->models.size()) + " models"
                        : roster_name(*roster, std::string_view{candidate}.substr(colon + 1));
            }
        } else {
            // A bare roster id: under the one configured roster that lists it.
            for (const auto& [type, roster] : rosters.rosters) {
                const bool configured =
                    std::ranges::any_of(config.backends, [&roster_type = type](const auto& keyed) {
                        return harness::to_string(keyed.second.type) == roster_type;
                    });
                if (configured && std::ranges::any_of(roster.models, [&candidate](const auto& m) {
                        return m.id == candidate;
                    })) {
                    note.group = roster_heading(type, roster);
                    note.description = roster_name(roster, candidate);
                    break;
                }
            }
        }
        notes.push_back(std::move(note));
    }
    return notes;
}

std::vector<std::size_t> menu_order(const std::vector<std::string>& groups) {
    const auto rank = [](const std::string& group) {
        if (group == "configured") {
            return 0;
        }
        if (group.empty()) {
            return 3;
        }
        return group == "local" ? 2 : 1;
    };
    // The groups in the order each first appears, ranked.
    std::vector<std::string> order;
    for (const std::string& group : groups) {
        if (std::ranges::find(order, group) == order.end()) {
            order.push_back(group);
        }
    }
    std::ranges::stable_sort(
        order, [&rank](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
    std::vector<std::size_t> out;
    out.reserve(groups.size());
    for (const std::string& group : order) {
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (groups.at(i) == group) {
                out.push_back(i);
            }
        }
    }
    return out;
}

CommandSpec specs_from_app(const CLI::App& app) {
    // The root's own flags live on the root node, so a bare `apogee --<TAB>`
    // needs no special case.
    CommandSpec root = spec_of(app);
    root.name.clear();
    return root;
}

std::optional<harness::RootFlag> typed_root_flag(const std::vector<std::string>& words,
                                                 const CommandSpec& root) {
    const std::string custom_equals = std::string{harness::kCustomFlag} + "=";
    std::optional<harness::RootFlag> flag;
    int given = 0;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& word = words[i];
        if (!word.starts_with('-')) {
            break;  // the verb: the root's flags come before it
        }
        for (const harness::Channel channel : harness::kChannels) {
            if (word == harness::channel_flag(channel)) {
                flag = harness::RootFlag{.channel = channel, .custom_config = {}};
                ++given;
            }
        }
        if (word.starts_with(custom_equals)) {
            flag = harness::RootFlag{.channel = std::nullopt,
                                     .custom_config = word.substr(custom_equals.size())};
            ++given;
        } else if (root.values.contains(word) && i + 1 < words.size()) {
            ++i;  // the flag's value, never the verb
            if (word == harness::kCustomFlag) {
                flag = harness::RootFlag{.channel = std::nullopt, .custom_config = words[i]};
                ++given;
            }
        }
    }
    if (given != 1) {
        return std::nullopt;
    }
    harness::RootInputs inputs = harness::current_root_inputs();
    inputs.flag = flag;
    if (!harness::resolve_root(inputs).ok()) {
        return std::nullopt;
    }
    return flag;
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
            } else if (contains(node->flags, word)) {
                context.flags.try_emplace(word);  // given, with no value to take
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

        // Read out of the live parser, so a flag or command added anywhere, at
        // any depth, completes without this file being told about it. The
        // protocol verb itself is hidden, so it is never offered.
        const CommandSpec tree = specs_from_app(root);

        // A root flag on the line steers which install the candidates come
        // from, for this answer only (M10).
        const harness::RootFlagScope typed_root{typed_root_flag(request.words, tree)};

        harness::Config config;
        try {
            config = harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const std::exception&) {
            // Silent. A broken or absent config makes completion unhelpful; it
            // must never print a diagnostic into the user's command line.
        }

        // Directives only for a stub that asks: an older stub would offer the
        // directive line itself as a candidate.
        const char* protocol = std::getenv(kCompletionProtocolVar);
        const bool directives = protocol != nullptr && std::string_view{protocol} == "2";
        std::cout << render_completion(
            complete_words(request, config, tree, default_completion_sources()), directives);
    });
}

}  // namespace apogee::commands
