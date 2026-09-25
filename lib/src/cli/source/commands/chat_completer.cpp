#include "commands/chat_completer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <utility>

#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "ansi/text_width.h"
#include "knowledge/record.h"

namespace apogee::commands {
namespace {

constexpr std::array kCommands{
    ChatCommandSpec{"help", ChatVerb::Help, "", "List these commands"},
    ChatCommandSpec{"model", ChatVerb::Model, "[backend]",
                    "Show the backend answering, or switch to another", ArgumentValues::Backends},
    ChatCommandSpec{"models", ChatVerb::Models, "", "List the configured backends"},
    ChatCommandSpec{"system", ChatVerb::System, "<text>", "Replace the system prompt"},
    ChatCommandSpec{"temperature", ChatVerb::Temperature, "<number>",
                    "Set the sampling temperature"},
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
        case ArgumentValues::Retrievers:
            for (const std::string_view name : agentloop::retriever_names()) {
                choices.push_back({std::string{name}, std::string{retriever_description(name)}});
            }
            break;
        case ArgumentValues::RerankTargets:
            choices.push_back({std::string{agentloop::kRerankOff}, "No reranking"});
            choices.push_back({"auto", "Follow each collection's rerank: pin"});
            choices.insert(choices.end(), sources.backends.begin(), sources.backends.end());
            break;
        case ArgumentValues::CaptureStatuses:
            for (const std::string_view status : knowledge::valid_statuses()) {
                choices.push_back({std::string{status}, {}});
            }
            break;
    }
    return choices;
}

}  // namespace

std::span<const ChatCommandSpec> chat_commands() noexcept {
    return kCommands;
}

const ChatCommandSpec* find_chat_command(std::string_view verb) noexcept {
    for (const ChatCommandSpec& spec : kCommands) {
        if (spec.verb == verb) {
            return &spec;
        }
    }
    return nullptr;
}

std::vector<std::string> chat_help_lines(std::size_t width) {
    const auto usage = [](const ChatCommandSpec& spec) {
        std::string out = "/" + std::string{spec.verb};
        if (!spec.argument.empty()) {
            out += " " + std::string{spec.argument};
        }
        return out;
    };
    std::size_t widest = 0;
    for (const ChatCommandSpec& spec : kCommands) {
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
    for (const ChatCommandSpec& spec : kCommands) {
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
        for (const ChatCommandSpec& spec : kCommands) {
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
    const ChatCommandSpec* spec = find_chat_command(before_cursor.substr(1, first_space - 1));
    if (spec == nullptr || spec->values == ArgumentValues::None) {
        return out;
    }
    std::size_t from = first_space;
    while (from < before_cursor.size() && is_space(before_cursor[from])) {
        ++from;
    }
    const std::string_view typed = before_cursor.substr(from);
    if (typed.find_first_of(" \t") != std::string_view::npos) {
        return out;
    }
    out.from = from;
    for (NamedChoice& choice : argument_choices(spec->values, sources)) {
        if (choice.name.starts_with(typed)) {
            out.candidates.push_back({choice.name, {}, std::move(choice.description)});
        }
    }
    return out;
}

}  // namespace apogee::commands
