#include "cli/tui_runner.h"

#include <cstddef>
#include <exception>
#include <utility>

#include "cli/complete_protocol.h"
#include "cli/complete_sources.h"
#include "cli/line_tokens.h"
#include "cli/root.h"
#include "cli/tui_child.h"
#include "cli/tui_parity.h"
#include "contracts/config.h"
#include "contracts/paths.h"
#include "tui/progress.h"

namespace apogee::commands {

namespace {

/// The root flags that take a value: the word after them is theirs.
[[nodiscard]] bool takes_value(const std::string& flag) {
    return flag == "--config" || flag == "--custom";
}

/// Where the command's own words begin: past the root flags.
[[nodiscard]] std::size_t first_verb(const std::vector<std::string>& words) {
    std::size_t at = 0;
    while (at < words.size() && words.at(at).starts_with("-")) {
        at += takes_value(words.at(at)) && words.at(at).find('=') == std::string::npos ? 2 : 1;
    }
    return at;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        out += (out.empty() ? "" : " ") + word;
    }
    return out;
}

/// The live command tree, read once: the parser every run would build.
[[nodiscard]] const CommandSpec& command_tree() {
    static const CommandSpec tree = []() {
        const RootCommand root;
        return specs_from_app(root.app());
    }();
    return tree;
}

}  // namespace

std::string exec_refusal(const std::vector<std::string>& words) {
    const std::size_t verb = first_verb(words);
    for (const ShellRefusal& refusal : shell_refusals()) {
        const std::vector<std::string> refused = line_tokens(refusal.words).words;
        if (refused.empty() || words.size() - std::min(verb, words.size()) < refused.size()) {
            continue;
        }
        bool matches = true;
        for (std::size_t i = 0; i < refused.size() && matches; ++i) {
            matches = words.at(verb + i) == refused.at(i);
        }
        if (matches) {
            return "refused: '" + std::string{refusal.words} + "' -- " +
                   std::string{refusal.reason};
        }
    }
    return {};
}

ExecWords exec_words(const std::string& scope, const std::string& line) {
    ExecWords out;
    const LineTokens split = line_tokens(line);
    if (!split.error.empty()) {
        out.refusal = "not run: " + split.error;
        return out;
    }
    out.words = line_tokens(scope).words;
    out.words.insert(out.words.end(), split.words.begin(), split.words.end());
    if (split.words.empty()) {
        out.refusal = "not run: nothing was typed";
        return out;
    }
    out.refusal = exec_refusal(out.words);
    return out;
}

std::vector<std::string> exec_candidates(const RootContext& context, const std::string& scope,
                                         const std::string& line) {
    const LineTokens split = line_tokens(scope + (scope.empty() ? "" : " ") + line);
    if (!split.error.empty()) {
        return {};
    }
    CompletionRequest request;
    request.words = split.words;
    if (!split.trailing_space && !request.words.empty()) {
        request.current = request.words.back();
        request.words.pop_back();
    }
    // As `__complete` answers: the config loaded quietly, the live names --
    // from the shell's own root, which is the root a run's child is given. A
    // typed root flag does not re-root the shell: the process is shared with
    // a conversation and a run that read their root as they go.
    harness::Config config;
    try {
        config = harness::load_config(harness::resolve_config_path(context.config_path));
    } catch (const std::exception&) {
        // A config that will not load completes nothing it would name.
    }
    return complete_words(request, config, command_tree(), default_completion_sources()).candidates;
}

tui::ExecLineOptions exec_line_options(const RootContext& context,
                                       std::shared_ptr<tui::Progress> output,
                                       std::filesystem::path binary) {
    tui::ExecLineOptions options;
    options.output = output;
    options.complete = [&context](const std::string& scope, const std::string& line) {
        return exec_candidates(context, scope, line);
    };
    options.run = [&context, output, binary](const std::string& scope, const std::string& line) {
        ExecWords run = exec_words(scope, line);
        if (!run.refusal.empty()) {
            return run.refusal;
        }
        const std::string heading = "apogee " + joined(run.words);
        if (!start_child_run(*output, context, binary, std::move(run.words), heading)) {
            return std::string{"not now: a command is running -- Ctrl-C stops it"};
        }
        return std::string{};
    };
    return options;
}

}  // namespace apogee::commands
