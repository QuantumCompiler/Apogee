#include "cli/chat_completer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "ansi/text_width.h"
#include "backends/model_roster.h"
#include "knowledge/record.h"

namespace apogee::commands {
namespace {

/// What follows `/attach`'s path (27p): the method, for this attach alone.
constexpr std::array kAttachFlags{
    ChatFlagSpec{"graph", "Whether a folder's code graph is built: code or off",
                 ArgumentValues::GraphMethods},
};

/// What follows `/suite`'s name (27e): the switches `parse_suite_argument`
/// reads.
constexpr std::array kSuiteFlags{
    ChatFlagSpec{"force", "Switch even when what the suite takes is over this machine's memory"},
    ChatFlagSpec{"warm", "Load the suite's members now, rather than at first use"},
};

constexpr std::array kCommands{
    ChatCommandSpec{"help", ChatVerb::Help, "", "List these commands"},
    ChatCommandSpec{"model", ChatVerb::Model, "[backend|model]",
                    "Show the backend answering, or switch: a backend, a roster model, or "
                    "backend:model",
                    ArgumentValues::Models},
    ChatCommandSpec{"models", ChatVerb::Models, "", "List the configured backends"},
    ChatCommandSpec{"suite", ChatVerb::Suite, "[name|off]",
                    "Show the suite and what its members hold, or switch to another (then "
                    "--force, --warm) or off",
                    ArgumentValues::Suites, kSuiteFlags, SessionRows::ChatOnly},
    // An execute session always runs under a suite (27s): its `/suite`
    // switches and never turns it off.
    ChatCommandSpec{"suite", ChatVerb::Suite, "[name]",
                    "Show the suite and what its members hold, or switch to another (then "
                    "--force, --warm)",
                    ArgumentValues::NamedSuites, kSuiteFlags, SessionRows::ExecuteOnly},
    ChatCommandSpec{"symphonies",
                    ChatVerb::Symphonies,
                    "",
                    "List the symphonies /play can play",
                    ArgumentValues::None,
                    {},
                    SessionRows::ExecuteOnly},
    ChatCommandSpec{"play",
                    ChatVerb::Play,
                    "<symphony> [input]",
                    "Play a symphony on the input: its stages on the suite's members, its "
                    "output this session's answer",
                    ArgumentValues::Symphonies,
                    {},
                    SessionRows::ExecuteOnly},
    ChatCommandSpec{"system", ChatVerb::System, "<text>", "Replace the system prompt"},
    ChatCommandSpec{"temperature", ChatVerb::Temperature, "<number>",
                    "Set the sampling temperature"},
    ChatCommandSpec{"think", ChatVerb::Think, "[on|off|auto]",
                    "Show or set whether the model thinks first: on, off, or auto per question",
                    ArgumentValues::ThinkingModes},
    ChatCommandSpec{"max-tokens", ChatVerb::MaxTokens, "<number>",
                    "Cap each answer's length in tokens"},
    ChatCommandSpec{"compact", ChatVerb::Compact, "",
                    "Summarise the conversation so far, to free context"},
    ChatCommandSpec{"title", ChatVerb::Title, "<name>", "Rename this chat"},
    ChatCommandSpec{"retriever", ChatVerb::Retriever, "[mode]",
                    "Show or set how documents are searched: auto, lexical, vector or hybrid",
                    ArgumentValues::Retrievers},
    ChatCommandSpec{"rerank", ChatVerb::Rerank, "[backend]",
                    "Show or set the model that reranks search results, or off, or auto",
                    ArgumentValues::RerankTargets},
    ChatCommandSpec{"branch", ChatVerb::Branch, "[ref]",
                    "Show or set the branch under review: head, base..head, or off"},
    ChatCommandSpec{"capture", ChatVerb::Capture, "[status|link]",
                    "Save this conversation as a knowledge record",
                    ArgumentValues::CaptureStatuses},
    ChatCommandSpec{"check", ChatVerb::Check, "",
                    "Have the suite's verifier check the last answer, once"},
    ChatCommandSpec{"attach", ChatVerb::Attach, "<path>",
                    "Attach a file, folder or glob: inlined when it fits, retrieved when not "
                    "(then --graph=code|off)",
                    ArgumentValues::Paths, kAttachFlags},
    ChatCommandSpec{"attachments", ChatVerb::Attachments, "", "List what is attached"},
    ChatCommandSpec{"detach", ChatVerb::Detach, "<name>", "Detach an attachment",
                    ArgumentValues::AttachmentNames},
    ChatCommandSpec{"recall", ChatVerb::Recall, "[on|off]",
                    "Show or set whether turns recall earlier chats", ArgumentValues::OnOff},
    ChatCommandSpec{"private", ChatVerb::Private, "", "Never summarise this chat for recall"},
    ChatCommandSpec{"allow", ChatVerb::Allow, "[tool|website]",
                    "Allow a tool or website for this chat; alone, list each answer",
                    ArgumentValues::GatedTools},
    ChatCommandSpec{"deny", ChatVerb::Deny, "<tool|website>",
                    "Refuse a tool or website for this chat, without asking",
                    ArgumentValues::GatedTools},
    ChatCommandSpec{"revoke", ChatVerb::Revoke, "<tool|website>",
                    "Forget this chat's answer for a tool or website",
                    ArgumentValues::SessionPermissions},
    ChatCommandSpec{"permissions", ChatVerb::Permissions, "",
                    "List each tool's answer and where it comes from"},
    ChatCommandSpec{"exit", ChatVerb::Exit, "", "Save and leave"},
    ChatCommandSpec{"quit", ChatVerb::Exit, "", "Save and leave"},
};

/// What each retriever value means, for the rows. A value agentloop adds
/// without a line here still completes, just undescribed.
std::string_view retriever_description(std::string_view name) {
    if (name == "lexical") {
        return "Keyword search";
    }
    if (name == "vector") {
        return "Search by meaning, with the embedding model";
    }
    if (name == "hybrid") {
        return "Both, merged";
    }
    if (name == "auto") {
        return "Vector when the collection's vectors match, else lexical";
    }
    return {};
}

/// Greedy word wrap by display width. A word wider than a row keeps its own
/// row -- /help is printed once, not repainted, so a row the terminal wraps
/// costs nothing but looks.
std::vector<std::string> wrap_words(std::string_view text, std::size_t width) {
    std::vector<std::string> rows(1);
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t end = std::min(text.find(' ', at), text.size());
        const std::string_view word = text.substr(at, end - at);
        at = end + 1;
        if (word.empty()) {
            continue;
        }
        if (!rows.back().empty() &&
            ansi::display_width(rows.back()) + 1 + ansi::display_width(word) > width) {
            rows.emplace_back();
        }
        if (!rows.back().empty()) {
            rows.back() += ' ';
        }
        rows.back() += word;
    }
    return rows;
}

bool is_space(char c) {
    return c == ' ' || c == '\t';
}

/// Where the word the cursor is in begins, or npos when the cursor sits just
/// after whitespace. An `@"` quote runs across spaces to its closing quote.
std::size_t current_word(std::string_view text) {
    std::size_t at = 0;
    while (at < text.size()) {
        if (is_space(text[at])) {
            ++at;
            continue;
        }
        const std::size_t start = at;
        if (text.substr(at).starts_with("@\"")) {
            const std::size_t close = text.find('"', at + 2);
            if (close == std::string_view::npos) {
                return start;
            }
            at = close + 1;
        }
        while (at < text.size() && !is_space(text[at])) {
            ++at;
        }
        if (at == text.size()) {
            return start;
        }
    }
    return std::string_view::npos;
}

char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/// Smart case: a prefix with a capital in it must match exactly; one without
/// matches either case, because on a case-insensitive disk `@readme` not
/// finding `README.md` reads as broken.
bool name_matches(std::string_view name, std::string_view prefix) {
    const bool exact = std::any_of(prefix.begin(), prefix.end(), [](char c) {
        return std::isupper(static_cast<unsigned char>(c)) != 0;
    });
    if (exact) {
        return name.starts_with(prefix);
    }
    if (name.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lower(name[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

Suggestions complete_path(std::string_view before, std::size_t from,
                          const ChatCompletionSources& sources) {
    Suggestions out;
    out.from = from;
    if (!sources.list) {
        return out;
    }
    const std::string_view token = before.substr(from);
    const bool quoted = token.starts_with("@\"");
    const std::string_view typed = token.substr(quoted ? 2 : 1);
    const std::size_t slash = typed.rfind('/');
    const std::string folder{slash == std::string_view::npos ? std::string_view{}
                                                             : typed.substr(0, slash + 1)};
    const std::string_view prefix =
        slash == std::string_view::npos ? typed : typed.substr(slash + 1);

    std::vector<DirectoryEntry> entries =
        sources.list(folder.empty() ? sources.working_directory
                                    : sources.working_directory / std::filesystem::path{folder});
    std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        const auto folded = [](std::string_view s) {
            std::string out{s};
            std::transform(out.begin(), out.end(), out.begin(), lower);
            return out;
        };
        const std::string fa = folded(a.name);
        const std::string fb = folded(b.name);
        return fa != fb ? fa < fb : a.name < b.name;
    });

    const bool show_hidden = prefix.starts_with('.');
    for (const DirectoryEntry& entry : entries) {
        if (entry.name.empty() || entry.name == "." || entry.name == "..") {
            continue;
        }
        if (!show_hidden && entry.name.front() == '.') {
            continue;
        }
        if (!name_matches(entry.name, prefix)) {
            continue;
        }
        const std::string path = folder + entry.name + (entry.directory ? "/" : "");
        Suggestion suggestion;
        if (quoted || path.find_first_of(" \t") != std::string::npos) {
            // A folder's quote stays open so the path can go on into it.
            suggestion.text = "@\"" + path + (entry.directory ? "" : "\"");
            suggestion.label = (quoted ? "@\"" : "@") + path;
        } else {
            suggestion.text = "@" + path;
        }
        out.candidates.push_back(std::move(suggestion));
    }
    return out;
}

std::vector<NamedChoice> argument_choices(ArgumentValues values,
                                          const ChatCompletionSources& sources) {
    std::vector<NamedChoice> choices;
    switch (values) {
        case ArgumentValues::None:
            break;
        case ArgumentValues::Backends:
            choices = sources.backends;
            break;
        case ArgumentValues::Models:
            choices = sources.backends;
            for (const NamedChoice& model : sources.roster_models) {
                if (std::ranges::none_of(choices, [&model](const NamedChoice& named) {
                        return named.name == model.name;
                    })) {
                    choices.push_back(model);
                }
            }
            break;
        case ArgumentValues::Retrievers:
            for (const std::string_view name : agentloop::retriever_names()) {
                choices.push_back({std::string{name}, std::string{retriever_description(name)}});
            }
            break;
        case ArgumentValues::RerankTargets:
            choices.push_back({std::string{agentloop::kRerankOff}, "No reranking"});
            choices.push_back({std::string{agentloop::kRerankOn},
                               "The utility model judges, else the chat's own"});
            choices.push_back({"auto", "Follow each collection's rerank: pin"});
            choices.insert(choices.end(), sources.backends.begin(), sources.backends.end());
            break;
        case ArgumentValues::CaptureStatuses:
            for (const std::string_view status : knowledge::valid_statuses()) {
                choices.push_back({std::string{status}, {}});
            }
            break;
        case ArgumentValues::Paths:
            break;  // completed as paths, below
        case ArgumentValues::OnOff:
            choices.push_back({"on", "Recall earlier chats that bear on each question"});
            choices.push_back({"off", "Recall nothing for the rest of this chat"});
            break;
        case ArgumentValues::ThinkingModes:
            choices.push_back({"on", "Think before every answer"});
            choices.push_back({"off", "Answer without thinking first"});
            choices.push_back({"auto", "Think when the question needs it"});
            break;
        case ArgumentValues::AttachmentNames:
            if (sources.attachment_names) {
                for (std::string& name : sources.attachment_names()) {
                    choices.push_back({std::move(name), {}});
                }
            }
            break;
        case ArgumentValues::GatedTools:
            if (sources.gated_tools) {
                for (std::string& name : sources.gated_tools()) {
                    choices.push_back({std::move(name), {}});
                }
            }
            break;
        case ArgumentValues::SessionPermissions:
            if (sources.session_permissions) {
                for (std::string& name : sources.session_permissions()) {
                    choices.push_back({std::move(name), {}});
                }
            }
            break;
        case ArgumentValues::Suites:
            choices = sources.suites;
            choices.push_back({std::string{harness::kSuiteOff}, "No suite: the global pointers"});
            break;
        case ArgumentValues::NamedSuites:
            choices = sources.suites;
            break;
        case ArgumentValues::Symphonies:
            if (sources.symphonies) {
                choices = sources.symphonies();
            }
            break;
        case ArgumentValues::GraphMethods:
            for (const std::string_view name : harness::attachment_graph_method_names()) {
                choices.push_back(
                    {std::string{name},
                     harness::attachment_graph_method_from_string(name) ==
                             harness::AttachmentGraphMethod::Code
                         ? "Parse a folder of code into the chat's code graph, with no model"
                         : "Index its chunks alone, with no code graph"});
            }
            break;
    }
    return choices;
}

/// Where the flags begin in a path argument (27p), when the cursor is past
/// the path: after its closing quote and a space; in an open quote, at the
/// first word starting `-`; unquoted, at the first word after a space that
/// starts `-`, or -- with none -- at the cursor when a space is just behind
/// it. Nullopt while the cursor is still in the path, so an unquoted path
/// with a space completes as it always has.
std::optional<std::size_t> flags_begin(std::string_view typed) {
    const auto first_dash_word = [&typed]() -> std::optional<std::size_t> {
        for (std::size_t at = 1; at < typed.size(); ++at) {
            if (typed[at] == '-' && is_space(typed[at - 1])) {
                return at;
            }
        }
        return std::nullopt;
    };
    if (typed.starts_with('"')) {
        const std::size_t close = typed.find('"', 1);
        if (close == std::string_view::npos) {
            return first_dash_word();
        }
        if (close + 1 < typed.size() && is_space(typed[close + 1])) {
            return close + 1;
        }
        return std::nullopt;
    }
    if (typed.find_first_of(" \t") == std::string_view::npos) {
        return std::nullopt;
    }
    if (const std::optional<std::size_t> dash = first_dash_word(); dash.has_value()) {
        return dash;
    }
    if (is_space(typed.back())) {
        return typed.size();
    }
    return std::nullopt;
}

/// The words of `text`, split at spaces and tabs.
std::vector<std::string_view> words_in(std::string_view text) {
    std::vector<std::string_view> out;
    for (std::size_t at = 0; at < text.size();) {
        while (at < text.size() && is_space(text[at])) {
            ++at;
        }
        const std::size_t end = std::min(text.find_first_of(" \t", at), text.size());
        if (end > at) {
            out.push_back(text.substr(at, end - at));
        }
        at = end;
    }
    return out;
}

/// The flag of `spec` named `name` (without its `--`), or null.
const ChatFlagSpec* find_flag(const ChatCommandSpec& spec, std::string_view name) {
    for (const ChatFlagSpec& flag : spec.flags) {
        if (flag.name == name) {
            return &flag;
        }
    }
    return nullptr;
}

/// `flag`'s values that start with `typed`, each written after `lead` --
/// `--graph=` when the value follows the flag's own `=`, nothing when it is
/// a word of its own.
std::vector<Suggestion> value_candidates(const ChatFlagSpec* flag, std::string_view typed,
                                         const std::string& lead,
                                         const ChatCompletionSources& sources) {
    std::vector<Suggestion> out;
    if (flag == nullptr) {
        return out;
    }
    for (NamedChoice& choice : argument_choices(flag->values, sources)) {
        if (choice.name.starts_with(typed)) {
            out.push_back({lead + choice.name, {}, std::move(choice.description)});
        }
    }
    return out;
}

/// The flags of `spec` for the text from `begin` to the cursor (27p): a
/// flag's values after `--name ` or `--name=`, else the flags not yet given
/// that start with the word at the cursor.
Suggestions complete_flags(std::string_view before, std::size_t begin, const ChatCommandSpec& spec,
                           const ChatCompletionSources& sources) {
    Suggestions out;
    const std::string_view part = before.substr(begin);
    const std::size_t space = part.find_last_of(" \t");
    const std::size_t word = space == std::string_view::npos ? 0 : space + 1;
    const std::string_view current = part.substr(word);
    const std::vector<std::string_view> earlier = words_in(part.substr(0, word));
    out.from = begin + word;
    // `--graph off`: the value, as its own word -- unless the flag is a
    // switch, which takes none (`--warm`).
    if (!earlier.empty() && earlier.back().starts_with("--") &&
        earlier.back().find('=') == std::string_view::npos) {
        const ChatFlagSpec* flag = find_flag(spec, earlier.back().substr(2));
        if (flag == nullptr || flag->values != ArgumentValues::None) {
            out.candidates = value_candidates(flag, current, {}, sources);
            return out;
        }
    }
    // `--graph=of`: the value, after the flag's own `=`.
    if (const std::size_t equals = current.find('=');
        current.starts_with("--") && equals != std::string_view::npos) {
        out.candidates = value_candidates(find_flag(spec, current.substr(2, equals - 2)),
                                          current.substr(equals + 1),
                                          std::string{current.substr(0, equals + 1)}, sources);
        return out;
    }
    // The flags not yet given -- on an empty word, or the start of one: a
    // word that is not a flag's start matches none.
    for (const ChatFlagSpec& flag : spec.flags) {
        const std::string name = "--" + std::string{flag.name};
        const std::string text = flag.values == ArgumentValues::None ? name : name + "=";
        const bool given = std::ranges::any_of(earlier, [&](std::string_view earlier_word) {
            return earlier_word == name || earlier_word.starts_with(name + "=");
        });
        if (!given && std::string_view{text}.starts_with(current)) {
            out.candidates.push_back({text, {}, std::string{flag.description}});
        }
    }
    return out;
}

}  // namespace

bool offered_in(SessionRows rows, SessionMode mode) noexcept {
    switch (rows) {
        case SessionRows::Every:
            return true;
        case SessionRows::ChatOnly:
            return mode == SessionMode::Chat;
        case SessionRows::ExecuteOnly:
            return mode == SessionMode::Execute;
    }
    return false;
}

std::vector<std::reference_wrapper<const ChatCommandSpec>> chat_commands(SessionMode mode) {
    std::vector<std::reference_wrapper<const ChatCommandSpec>> rows;
    for (const ChatCommandSpec& spec : kCommands) {
        if (offered_in(spec.sessions, mode)) {
            rows.emplace_back(spec);
        }
    }
    return rows;
}

const ChatCommandSpec* find_chat_command(std::string_view verb, SessionMode mode) noexcept {
    for (const ChatCommandSpec& spec : kCommands) {
        if (spec.verb == verb && offered_in(spec.sessions, mode)) {
            return &spec;
        }
    }
    return nullptr;
}

std::vector<std::string> chat_help_lines(std::size_t width, SessionMode mode) {
    const auto usage = [](const ChatCommandSpec& spec) {
        std::string out = "/" + std::string{spec.verb};
        if (!spec.argument.empty()) {
            out += " " + std::string{spec.argument};
        }
        return out;
    };
    const std::vector<std::reference_wrapper<const ChatCommandSpec>> offered = chat_commands(mode);
    std::size_t widest = 0;
    for (const ChatCommandSpec& spec : offered) {
        widest = std::max(widest, ansi::display_width(usage(spec)));
    }
    // Two in, the usages, two more, then the descriptions -- wrapped under
    // their own column, or on a line of their own when the terminal is too
    // narrow for a column worth reading. Never the last column.
    constexpr std::size_t kIndent = 2;
    constexpr std::size_t kGap = 2;
    constexpr std::size_t kNarrowest = 20;
    const std::size_t column = kIndent + widest + kGap;
    const bool unbounded = width == 0;
    const bool beside = unbounded || width > column + kNarrowest;
    const std::size_t indent = beside ? column : (2 * kIndent);
    const std::size_t text_width = unbounded ? std::string::npos : width - 1 - indent;

    std::vector<std::string> lines;
    for (const ChatCommandSpec& spec : offered) {
        const std::string head = std::string(kIndent, ' ') + usage(spec);
        const std::vector<std::string> rows = wrap_words(spec.description, text_width);
        if (beside) {
            for (std::size_t i = 0; i < rows.size(); ++i) {
                std::string line = i == 0 ? head : std::string{};
                line.append(column - ansi::display_width(line), ' ');
                lines.push_back(line + rows[i]);
            }
        } else {
            lines.push_back(head);
            for (const std::string& row : rows) {
                lines.push_back(std::string(2 * kIndent, ' ') + row);
            }
        }
    }
    return lines;
}

std::vector<DirectoryEntry> list_directory(const std::filesystem::path& directory) {
    std::vector<DirectoryEntry> entries;
    std::error_code error;
    std::filesystem::directory_iterator it{
        directory, std::filesystem::directory_options::skip_permission_denied, error};
    for (; !error && it != std::filesystem::directory_iterator{}; it.increment(error)) {
        if (entries.size() >= kMaxListing) {
            break;
        }
        // Follows a link, so a link to a folder completes as a folder.
        std::error_code type_error;
        const bool directory_entry = it->is_directory(type_error) && !type_error;
        entries.push_back({it->path().filename().string(), directory_entry});
    }
    return entries;
}

ChatCompletionSources chat_completion_sources(const harness::Config& config,
                                              std::filesystem::path working_directory) {
    ChatCompletionSources sources;
    for (const std::string& name : config.backend_names()) {
        const harness::BackendConfig* backend = config.find_backend(name);
        if (backend == nullptr) {
            continue;
        }
        std::string description{harness::to_string(backend->type)};
        const std::string model =
            !backend->model.empty()
                ? backend->model
                : std::filesystem::path{backend->model_path}.filename().string();
        if (!model.empty()) {
            description += " · " + model;
        }
        sources.backends.push_back({name, std::move(description)});
    }
    // The cached rosters of the configured provider types (M13): what
    // `/model` can switch to beside the backends (33).
    for (const auto& [type, roster] : backends::known_rosters().rosters) {
        if (std::ranges::none_of(config.backends, [&wanted = type](const auto& entry) {
                return harness::to_string(entry.second.type) == wanted;
            })) {
            continue;
        }
        for (const backends::RosterModel& model : roster.models) {
            sources.roster_models.push_back({model.id, type + "'s roster"});
        }
        // Each entry of the type, for `<backend>:` (34).
        for (const auto& [name, entry] : config.backends) {
            if (harness::to_string(entry.type) != type) {
                continue;
            }
            std::vector<NamedChoice>& models = sources.backend_rosters[name];
            for (const backends::RosterModel& model : roster.models) {
                models.push_back({model.id, type + "'s roster"});
            }
        }
    }
    for (const std::string& name : config.suite_names()) {
        const harness::SuiteConfig* suite = config.find_suite(name);
        sources.suites.push_back({name, suite != nullptr ? suite->description : std::string{}});
    }
    sources.working_directory = std::move(working_directory);
    sources.list = list_directory;
    return sources;
}

Suggestions suggest_chat_input(std::string_view before_cursor,
                               const ChatCompletionSources& sources) {
    Suggestions out;
    const bool slash = before_cursor.starts_with('/');
    const std::size_t first_space = before_cursor.find_first_of(" \t");

    // A command, while the cursor is still in its name. A name with a '/' in
    // it is a path, as parse_slash has it.
    if (slash && first_space == std::string_view::npos) {
        const std::string_view typed = before_cursor.substr(1);
        if (typed.find('/') != std::string_view::npos) {
            return out;
        }
        for (const ChatCommandSpec& spec : chat_commands(sources.mode)) {
            if (spec.verb.starts_with(typed)) {
                const std::string name = "/" + std::string{spec.verb};
                // A trailing space when it takes an argument, so the next
                // rows are already that argument's values.
                out.candidates.push_back({spec.argument.empty() ? name : name + " ", name,
                                          std::string{spec.description}});
            }
        }
        return out;
    }

    // A path, wherever an `@` starts the word.
    const std::size_t word = current_word(before_cursor);
    if (word != std::string_view::npos && before_cursor[word] == '@') {
        return complete_path(before_cursor, word, sources);
    }

    // A command's own values.
    if (!slash) {
        return out;
    }
    const ChatCommandSpec* spec =
        find_chat_command(before_cursor.substr(1, first_space - 1), sources.mode);
    if (spec == nullptr || spec->values == ArgumentValues::None) {
        return out;
    }
    std::size_t from = first_space;
    while (from < before_cursor.size() && is_space(before_cursor[from])) {
        ++from;
    }
    const std::string_view typed = before_cursor.substr(from);
    // Past a path argument, its flags (27p).
    if (spec->values == ArgumentValues::Paths && !spec->flags.empty()) {
        if (const std::optional<std::size_t> begin = flags_begin(typed); begin.has_value()) {
            return complete_flags(before_cursor, from + *begin, *spec, sources);
        }
    }
    // Past any other argument and a space, its flags (`/suite fast --warm`).
    if (!spec->flags.empty() && spec->values != ArgumentValues::Paths) {
        if (const std::size_t space = typed.find_first_of(" \t");
            space != std::string_view::npos && !typed.starts_with('-')) {
            // `off` loads and admits nothing: no flag follows it.
            if (spec->id == ChatVerb::Suite && typed.substr(0, space) == harness::kSuiteOff) {
                return out;
            }
            return complete_flags(before_cursor, from + space, *spec, sources);
        }
    }
    if (spec->values == ArgumentValues::Paths) {
        // The `@` completer's own logic, on the argument as if `@` led it.
        const std::string as_mention = "@" + std::string{typed};
        Suggestions paths = complete_path(as_mention, 0, sources);
        for (Suggestion& candidate : paths.candidates) {
            candidate.text.erase(0, 1);
            if (!candidate.label.empty()) {
                candidate.label.erase(0, 1);
            }
        }
        paths.from = from;
        return paths;
    }
    if (typed.find_first_of(" \t") != std::string_view::npos) {
        return out;
    }
    out.from = from;
    // Past a backend's colon, its roster's models as `<backend>:<model>`
    // (34). A colon after anything else is the name's own: a roster id may
    // hold one.
    if (spec->values == ArgumentValues::Models) {
        if (const std::size_t colon = typed.find(':'); colon != std::string_view::npos) {
            if (const auto listed = sources.backend_rosters.find(typed.substr(0, colon));
                listed != sources.backend_rosters.end()) {
                for (const NamedChoice& model : listed->second) {
                    std::string pinned = listed->first + ":" + model.name;
                    if (pinned.starts_with(typed)) {
                        out.candidates.push_back({std::move(pinned), {}, model.description});
                    }
                }
                return out;
            }
        }
    }
    for (NamedChoice& choice : argument_choices(spec->values, sources)) {
        if (choice.name.starts_with(typed)) {
            out.candidates.push_back({choice.name, {}, std::move(choice.description)});
        }
    }
    return out;
}

}  // namespace apogee::commands
