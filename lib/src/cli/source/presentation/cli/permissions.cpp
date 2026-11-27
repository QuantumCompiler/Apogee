#include "cli/permissions.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "agent/web_search.h"
#include "agentloop/member_call.h"
#include "agentloop/question.h"
#include "agentloop/tool_selection.h"
#include "ansi/inert.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"
#include "contracts/host.h"
#include "contracts/text_diff.h"
#include "contracts/todo.h"
#include "machine/driver_input.h"
#include "mcp/types.h"
#include "platform/platform.h"
#include "tools/toolsets.h"

namespace apogee::commands {
namespace {

enum class Answer { Once, Always, Session, Deny };

Answer parse_answer(std::string_view text) {
    std::string word;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!word.empty()) {
                break;
            }
            continue;
        }
        word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (word == "y" || word == "yes" || word == "allow" || word == "once") {
        return Answer::Once;
    }
    if (word == "a" || word == "always") {
        return Answer::Always;
    }
    if (word == "s" || word == "session") {
        return Answer::Session;
    }
    return Answer::Deny;
}

/// Remembers a `session` or `always` answer for the rest of the run: the
/// host for an outbound tool, the tool otherwise.
void remember(const agent::GateRequest& request,
              const std::shared_ptr<SessionApprovals>& approvals) {
    if (approvals == nullptr) {
        return;
    }
    if (request.outbound) {
        if (const std::optional<std::string> host = harness::canonical_host(request.target);
            host.has_value()) {
            approvals->hosts.insert(*host);
        }
        return;
    }
    approvals->tools.insert(std::string{request.tool});
}

/// Applies an answer: remembers it, persists `always`, and says whether the
/// operation may run. Returns what to tell the user about persistence.
bool apply_answer(Answer answer, const agent::GateRequest& request,
                  const std::filesystem::path& config_path,
                  const std::shared_ptr<SessionApprovals>& approvals, std::string& note) {
    switch (answer) {
        case Answer::Deny:
            return false;
        case Answer::Once:
            return true;
        case Answer::Session:
            remember(request, approvals);
            return true;
        case Answer::Always:
            remember(request, approvals);
            if (!config_path.empty()) {
                try {
                    if (request.outbound) {
                        harness::edit_config_file(
                            config_path, [host = request.target](std::string_view content) {
                                return harness::add_allowed_host(content, host);
                            });
                        note = std::string{request.target} + " added to tools.allowed_hosts in " +
                               config_path.string();
                    } else {
                        harness::edit_config_file(
                            config_path, [tool = request.tool](std::string_view content) {
                                return harness::set_permission(content, tool, "allow");
                            });
                        note = "permissions." + std::string{request.tool} + " = allow written to " +
                               config_path.string();
                    }
                } catch (const std::exception& e) {
                    // The answer still allows this run; only the memory failed.
                    note = std::string{"could not write the config: "} + e.what() +
                           " -- allowed for this session only";
                }
            }
            return true;
    }
    return false;
}

}  // namespace

std::string_view to_string(PermissionMode mode) noexcept {
    switch (mode) {
        case PermissionMode::Manual:
            return "manual";
        case PermissionMode::AcceptEdits:
            return "accept-edits";
        case PermissionMode::Plan:
            return "plan";
        case PermissionMode::DontAsk:
            return "dont-ask";
        case PermissionMode::Bypass:
            return "bypass";
    }
    return "manual";
}

std::optional<PermissionMode> permission_mode_from_string(std::string_view text) noexcept {
    for (const PermissionMode mode : kPermissionModes) {
        if (text == to_string(mode)) {
            return mode;
        }
    }
    return std::nullopt;
}

std::vector<std::string> permission_mode_words() {
    std::vector<std::string> words;
    for (const PermissionMode mode : kPermissionModes) {
        words.emplace_back(to_string(mode));
    }
    return words;
}

std::string_view permission_mode_description(PermissionMode mode) noexcept {
    switch (mode) {
        case PermissionMode::Manual:
            return "each gated tool asks, as configured";
        case PermissionMode::AcceptEdits:
            return "file and note edits run without asking; commands and websites still ask";
        case PermissionMode::Plan:
            return "nothing that writes runs; the model plans";
        case PermissionMode::DontAsk:
            return "whatever would ask is refused";
        case PermissionMode::Bypass:
            return "nothing asks; a deny still refuses";
    }
    return {};
}

std::string permission_mode_label(PermissionMode mode, const ansi::Style& style) {
    // Claude Code's status bar words (39b), each in its own colour.
    switch (mode) {
        case PermissionMode::Manual:
            return {};
        case PermissionMode::AcceptEdits:
            return style.colorize("⏵⏵ accept edits on", ansi::Color::Magenta);
        case PermissionMode::Plan:
            return style.colorize("⏸ plan mode on", ansi::Color::Cyan);
        case PermissionMode::DontAsk:
            return style.colorize("⏵⏵ don't ask on", ansi::Color::Yellow);
        case PermissionMode::Bypass:
            return style.colorize("⏵⏵ bypass permissions on", ansi::Color::Red);
    }
    return {};
}

PermissionMode next_permission_mode(PermissionMode mode, bool bypass_allowed) noexcept {
    switch (mode) {
        case PermissionMode::Manual:
            return PermissionMode::AcceptEdits;
        case PermissionMode::AcceptEdits:
            return PermissionMode::Plan;
        case PermissionMode::Plan:
            return bypass_allowed ? PermissionMode::Bypass : PermissionMode::Manual;
        case PermissionMode::DontAsk:
        case PermissionMode::Bypass:
            return PermissionMode::Manual;
    }
    return PermissionMode::Manual;
}

std::string plan_note(const SessionApprovals& approvals) {
    if (approvals.mode.load() != PermissionMode::Plan) {
        return {};
    }
    return "Plan mode is on. Research the request and propose a plan: read what you need, then "
           "say what you would change and how. Do not change files, write notes or run "
           "commands -- tools that would change anything are refused until the person leaves "
           "plan mode.";
}

agent::RefusalNote mode_refusal(std::shared_ptr<const SessionApprovals> approvals) {
    if (approvals == nullptr) {
        return {};
    }
    return [approvals = std::move(approvals)](const agent::GateRequest& request) -> std::string {
        switch (approvals->mode.load()) {
            case PermissionMode::Plan:
                if (!request.outbound) {
                    return "plan mode is on, and nothing that changes files or runs commands "
                           "runs until the person leaves it; propose the change instead.";
                }
                return {};
            case PermissionMode::DontAsk:
                return "this session refuses whatever it would ask permission for (dont-ask "
                       "mode).";
            case PermissionMode::Manual:
            case PermissionMode::AcceptEdits:
            case PermissionMode::Bypass:
                return {};
        }
        return {};
    };
}

std::vector<std::string> listed_hosts(const harness::Config& config) {
    // The search instance's host counts as listed: the user named it in
    // tools.search, which is the consent (25e). The pages a search finds are
    // ordinary fetches, asked per website.
    std::vector<std::string> hosts = config.tools.allowed_hosts;
    std::string unused;
    if (const std::optional<agent::SearchInstance> search =
            agent::search_instance(config.tools.search, unused);
        search.has_value()) {
        hosts.push_back(search->base.host);
    }
    return hosts;
}

agent::PermissionChecker make_permission_checker(const harness::Config& config,
                                                 std::shared_ptr<SessionApprovals> approvals) {
    // The levels and hosts are copied: the checker outlives any one config
    // object, and a mid-session edit to the file takes effect on the next
    // session.
    std::vector<std::string> hosts = listed_hosts(config);
    return
        [levels = config.permissions, hosts = std::move(hosts),
         approvals = std::move(approvals)](const agent::GateRequest& request) -> agent::Permission {
            // The session's mode (39b), read at each call: the shell's
            // Shift+Tab may have moved it since the last.
            const PermissionMode mode =
                approvals != nullptr ? approvals->mode.load() : PermissionMode::Manual;
            // What would ask, in the mode: bypass allows it, dont-ask refuses
            // it -- only after every deny has had its say.
            const auto by_mode = [mode](agent::Permission ask) {
                switch (mode) {
                    case PermissionMode::Bypass:
                        return agent::Permission::Allow;
                    case PermissionMode::DontAsk:
                        return agent::Permission::Deny;
                    case PermissionMode::Manual:
                    case PermissionMode::AcceptEdits:
                    case PermissionMode::Plan:
                        break;
                }
                return ask;
            };
            if (request.outbound) {
                // By host, never by tool: `permissions.fetch_url` is not a key.
                const std::optional<std::string> host = harness::canonical_host(request.target);
                if (!host.has_value()) {
                    return agent::Permission::Deny;
                }
                // A session's no comes first: it only ever tightens (26o).
                if (approvals != nullptr && approvals->denied_hosts.contains(*host)) {
                    return agent::Permission::Deny;
                }
                if (harness::host_listed(hosts, *host)) {
                    return agent::Permission::Allow;
                }
                if (approvals != nullptr && approvals->hosts.contains(*host)) {
                    return agent::Permission::Allow;
                }
                return by_mode(agent::Permission::Ask);
            }
            const std::string_view tool = request.tool;
            const harness::PermissionLevel level = levels.level(tool);
            // The config's no, then the session's no -- which tightens even
            // the config's allow -- then the allows (26o).
            if (level == harness::PermissionLevel::Deny) {
                return agent::Permission::Deny;
            }
            if (approvals != nullptr && approvals->denied_tools.contains(tool)) {
                return agent::Permission::Deny;
            }
            // Plan mode refuses every writer, over any allow (39b).
            if (mode == PermissionMode::Plan) {
                return agent::Permission::Deny;
            }
            if (level == harness::PermissionLevel::Allow) {
                return agent::Permission::Allow;
            }
            if (approvals != nullptr && approvals->tools.contains(tool)) {
                return agent::Permission::Allow;
            }
            if (mode == PermissionMode::AcceptEdits && request.edits) {
                return agent::Permission::Allow;
            }
            return by_mode(agent::Permission::Ask);
        };
}

bool answer_permission(std::string_view text, const agent::GateRequest& request,
                       const std::filesystem::path& config_path,
                       const std::shared_ptr<SessionApprovals>& approvals, std::string& note) {
    return apply_answer(parse_answer(text), request, config_path, approvals, note);
}

std::string permission_prompt_line(const agent::GateRequest& request, const ansi::Style& style) {
    // Each a line (57p): a CR and an erase in a command would otherwise draw a
    // harmless one over the prompt, and the person approve what they never
    // saw.
    return style.tag(ansi::Role::Permission) + " " + ansi::inert_line(request.tool) +
           (request.target.empty() ? "" : " -> " + ansi::inert_line(request.target));
}

std::vector<std::string> permission_prompt_lines(const agent::GateRequest& request,
                                                 const ansi::Style& style) {
    std::vector<std::string> lines{permission_prompt_line(request, style)};
    if (request.detail.empty() || request.detail == request.target) {
        return lines;  // a target already whole is not said twice
    }
    if (request.edits) {
        // A file writer's diff is said by its counts (52a) -- the change's
        // size at a glance; machine mode carries the diff itself.
        lines.push_back(
            style.dim("  " + harness::describe_diff_counts(harness::diff_counts(request.detail))));
        return lines;
    }
    // The whole URL, the whole command and where it runs, the whole
    // arguments: what this call does is in it. Shown as text, every line
    // under the prompt, so nothing in it starts where a prompt line starts.
    const std::string shown = ansi::inert_text(request.detail);
    std::size_t start = 0;
    while (start < shown.size()) {
        const std::size_t newline = shown.find('\n', start);
        const std::size_t end = newline == std::string::npos ? shown.size() : newline;
        lines.push_back(style.dim("  " + shown.substr(start, end - start)));
        start = end + 1;
    }
    return lines;
}

agent::ConfirmFn terminal_confirm_fn(StatusLine& status, ansi::Style style,
                                     std::filesystem::path config_path,
                                     std::shared_ptr<SessionApprovals> approvals) {
    if (!platform::is_terminal(platform::StandardStream::In) ||
        !platform::is_terminal(platform::StandardStream::Err)) {
        return nullptr;
    }
    return [&status, style, config_path = std::move(config_path),
            approvals = std::move(approvals)](const agent::GateRequest& request) {
        // Through the status line, so a spinner frame cannot land on top of
        // the prompt -- the same discipline the question prompt keeps.
        for (const std::string& line : permission_prompt_lines(request, style)) {
            status.print_line(line);
        }
        std::cerr << style.dim(request.outbound
                                   ? "Allow reaching this website? [y]es / [n]o / [a]lways / "
                                     "[s]ession: "
                                   : "Allow? [y]es / [n]o / [a]lways / [s]ession: ")
                  << std::flush;
        std::string line;
        {
            // A turn keeps typing hidden (platform::TypeaheadGuard); the answer
            // to this question must be seen as it is typed.
            const platform::EchoPause visible;
            if (!std::getline(std::cin, line)) {
                std::cerr << "\n";
                return false;
            }
        }
        std::string note;
        const bool allowed = answer_permission(line, request, config_path, approvals, note);
        if (!note.empty()) {
            status.print_line(style.dim("  " + note));
        }
        if (!allowed) {
            status.print_line(style.dim("  denied"));
        }
        return allowed;
    };
}

agent::ConfirmFn make_driver_confirm_fn(JsonReporter& reporter, DriverInput& input,
                                        std::filesystem::path config_path,
                                        std::shared_ptr<SessionApprovals> approvals) {
    return [&reporter, &input, config_path = std::move(config_path),
            approvals = std::move(approvals)](const agent::GateRequest& request) {
        reporter.emit_permission_question(request);
        while (true) {
            bool cancelled = false;
            const std::optional<DriverLine> line = input.next_line_in_turn(cancelled);
            if (cancelled) {
                // Cancelled while asked (28f): the turn fails as a closed
                // stdin fails it, and the session goes on.
                throw harness::CancelledError();
            }
            if (!line.has_value()) {
                break;
            }
            const DriverMessage message = parse_driver_line(line->text);
            if (message.kind != DriverMessage::Kind::Answer) {
                continue;  // the same tolerance the question path keeps
            }
            std::string note;
            return answer_permission(message.text, request, config_path, approvals, note);
        }
        throw std::runtime_error("the driver closed stdin with a permission prompt unanswered");
    };
}

std::vector<std::string> gated_tools(const agent::ToolRegistry& registry) {
    std::vector<std::string> out;
    for (const std::string& name : registry.names()) {
        if (const agent::Tool* tool = registry.find(name); tool != nullptr && tool->writes) {
            out.push_back(name);
        }
    }
    std::ranges::sort(out);
    return out;
}

std::vector<std::string> edit_tools(const agent::ToolRegistry& registry) {
    std::vector<std::string> out;
    for (const std::string& name : registry.names()) {
        if (const agent::Tool* tool = registry.find(name); tool != nullptr && tool->edits) {
            out.push_back(name);
        }
    }
    std::ranges::sort(out);
    return out;
}

bool website_shaped(std::string_view name) noexcept {
    const bool dotted = name.find('.') != std::string_view::npos;
    const bool bracketed = name.starts_with('[');
    const bool ipv6 = name.find(':') != std::string_view::npos;
    const bool single_label = !dotted && name.find('_') == std::string_view::npos;
    return dotted || bracketed || ipv6 || single_label;
}

namespace {

/// Whether `word` is one a suite member's `toolset:` takes (`fs` ... `mcp`).
bool toolset_word(std::string_view word) {
    return std::ranges::find(harness::suite_toolset_names(), word) !=
           harness::suite_toolset_names().end();
}

/// The tools a toolset holds that ask: `destructive_tool_names` filed under
/// it, in that list's order.
std::vector<std::string> toolset_writers(std::string_view toolset) {
    std::vector<std::string> out;
    for (const std::string_view tool : tools::destructive_tool_names()) {
        if (tools::toolset_of(tool) == toolset) {
            out.emplace_back(tool);
        }
    }
    return out;
}

/// Whether `name` is a tool's own name: a native toolset's, `fetch_url`'s or
/// `web_search`'s, an MCP tool's (`toolset_of`), or one the loop registers
/// beside them -- never a website a typo's refusal should point to.
bool names_a_tool(std::string_view name) {
    return !tools::toolset_of(name).empty() || name == agentloop::kQuestionToolName ||
           name == agentloop::kConsultToolName || name == agentloop::kDelegateToolName ||
           name == harness::kTodoToolName;
}

/// `a`, `a and b`, `a, b and c`.
std::string and_list(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += i + 1 == items.size() ? " and " : ", ";
        }
        out += items[i];
    }
    return out;
}

/// `--deny a`, `--deny a and --deny b`, `--deny a, --deny b and --deny c`.
std::string each_with(std::string_view verb, const std::vector<std::string>& tools) {
    std::vector<std::string> said;
    said.reserve(tools.size());
    for (const std::string& tool : tools) {
        said.push_back(std::string{verb} + " " + tool);
    }
    return and_list(said);
}

}  // namespace

GatedName name_gated(std::string_view name, const std::vector<std::string>& gated,
                     const std::vector<std::string>& known, bool websites, bool child_processes,
                     std::string_view unstarted) {
    GatedName out;
    if (std::ranges::find(gated, name) != gated.end()) {
        out.key = std::string{name};
        return out;
    }
    // A native tool this build never offers is no typo: say why it is not
    // here (57k), not that the name is no tool.
    if (std::string why = tools::why_not_offered(name, child_processes); !why.empty()) {
        out.error = std::move(why);
        out.unoffered = true;
        return out;
    }
    const std::optional<std::string> host = harness::canonical_host(name);
    // A host the run already knows -- the config's list, the session's own
    // answers -- is that website whatever its shape, matched as the checker
    // matches it (CLI-M39): a listed `my_host` is reached unasked, so a no
    // must reach it too. The person's own host says nothing more, even one
    // spelled like a toolset.
    if (websites && host.has_value() && harness::host_listed(known, *host)) {
        out.key = *host;
        out.host = true;
        return out;
    }
    // A website, by the host it names -- as a session's own answer keeps it,
    // and as `--allow-host` takes it (57o): any name `canonical_host` accepts,
    // `localhost`, an intranet name and an IPv6 literal included, except a
    // dotless one with a `_`, which is shaped like a tool -- so a mistyped
    // `write_flie` is still caught as one. One named like a toolset or a
    // gated tool stays a website, by the user's call (CLI-M39: an intranet
    // `git` is denied so today), and carries the word so the surface says so.
    if (websites && host.has_value() && website_shaped(name)) {
        out.key = *host;
        out.host = true;
        if (toolset_word(*host) || std::ranges::find(gated, *host) != gated.end()) {
            out.shares = *host;
        }
        return out;
    }
    std::string listed;
    for (const std::string& tool : gated) {
        listed += (listed.empty() ? "" : ", ") + tool;
    }
    std::string asked;
    if (!gated.empty()) {
        asked = " -- the ones it asks about: " + listed;
    } else if (!unstarted.empty()) {
        asked = " -- none of its built-in tools does";
    } else {
        asked = " -- it has none (--tools)";
    }
    // A name shaped like a tool that is a website's name too: the spelling
    // that takes it as one (CLI-M39) -- never for a tool's own name, in any
    // case, which that spelling would deny as a website and leave running.
    std::string spelling;
    if (websites && host.has_value() && !names_a_tool(*host) &&
        std::ranges::find(gated, *host) == gated.end()) {
        spelling = " -- '" + *host + ".' is the website " + *host;
    }
    out.error = "'" + std::string{name} + "' is not a tool this chat asks about" + asked +
                std::string{unstarted} +
                (websites ? "; or name a website, like docs.python.org" + spelling : "");
    return out;
}

std::vector<std::string> known_hosts(const std::vector<std::string>& listed,
                                     const SessionApprovals& approvals) {
    std::vector<std::string> known = listed;
    known.insert(known.end(), approvals.hosts.begin(), approvals.hosts.end());
    known.insert(known.end(), approvals.denied_hosts.begin(), approvals.denied_hosts.end());
    return known;
}

std::string shared_name_pointer(std::string_view shares, PointerVerb verb) {
    if (shares.empty()) {
        return {};
    }
    const bool allow = verb == PointerVerb::ChatAllow;
    std::string_view spelled = "/allow";
    if (verb == PointerVerb::LaunchDeny) {
        spelled = "--deny";
    } else if (verb == PointerVerb::ChatDeny) {
        spelled = "/deny";
    }
    const std::string word{shares};
    const std::string to = allow ? "to allow" : "to refuse";
    if (word == "web") {
        return "fetch_url and web_search are asked about per website -- name the website, like "
               "docs.python.org";
    }
    if (word == "mcp") {
        return std::string{allow ? "allow" : "refuse"} +
               " an MCP tool by its name, mcp__<server>__<tool>";
    }
    if (!toolset_word(word)) {
        // A gated tool's own name, taken as a website.
        return to + " the tool, " + std::string{spelled} + " " + word;
    }
    // To refuse, the toolset can be left out too; nothing allows a read.
    const std::string disabled = "tools.disabled: [" + word + "]";
    const std::vector<std::string> writers = toolset_writers(word);
    if (writers.empty()) {
        return word + "'s tools only read and never ask" +
               (allow ? "" : " -- leave them out with " + disabled);
    }
    return to + " the " + word + (writers.size() == 1 ? "'s tool, " : " tools, ") +
           each_with(spelled, writers) +
           (allow ? "" : ", or leave the toolset out with " + disabled);
}

std::string shared_name_note(std::string_view given, const GatedName& name) {
    if (name.shares.empty()) {
        return {};
    }
    return "'" + std::string{given} + "' is taken as " +
           (given == name.key ? std::string{"a website"} : "the website " + name.key) + "; " +
           shared_name_pointer(name.shares, PointerVerb::LaunchDeny);
}

std::string unstarted_servers_clause(const std::vector<std::string>& servers) {
    if (servers.empty()) {
        return {};
    }
    std::vector<std::string> prefixes;
    prefixes.reserve(servers.size());
    for (const std::string& server : servers) {
        prefixes.push_back(mcp::namespaced_name(server, "*"));
    }
    return "; and " + and_list(prefixes) + ", whose " +
           (servers.size() == 1 ? "server" : "servers") + " a refused run does not start";
}

void allow_for_session(SessionApprovals& approvals, const GatedName& name) {
    if (name.host) {
        approvals.denied_hosts.erase(name.key);
        approvals.hosts.insert(name.key);
    } else {
        approvals.denied_tools.erase(name.key);
        approvals.tools.insert(name.key);
    }
}

void deny_for_session(SessionApprovals& approvals, const GatedName& name) {
    if (name.host) {
        approvals.hosts.erase(name.key);
        approvals.denied_hosts.insert(name.key);
    } else {
        approvals.tools.erase(name.key);
        approvals.denied_tools.insert(name.key);
    }
}

bool revoke_for_session(SessionApprovals& approvals, const GatedName& name) {
    if (name.host) {
        return (approvals.hosts.erase(name.key) + approvals.denied_hosts.erase(name.key)) > 0;
    }
    return (approvals.tools.erase(name.key) + approvals.denied_tools.erase(name.key)) > 0;
}

SeededPresets seed_approvals(SessionApprovals& approvals, const PermissionPresets& presets,
                             const std::vector<std::string>& gated,
                             const std::vector<std::string>& listed, std::string_view unstarted) {
    const bool child_processes = platform::supports_child_processes();
    SeededPresets seeded;
    for (const std::string& tool : presets.allow) {
        // `--allow` is for tools; a website has `--allow-host`, so no host
        // is known to it.
        const GatedName name = name_gated(tool, gated, {}, false, child_processes, unstarted);
        if (!name.error.empty()) {
            seeded.refused = "--allow: " + name.error +
                             (name.unoffered ? "" : " (a website takes --allow-host)");
            return seeded;
        }
        allow_for_session(approvals, name);
    }
    for (const std::string& host : presets.allow_hosts) {
        const std::optional<std::string> canonical = harness::canonical_host(host);
        if (!canonical.has_value()) {
            seeded.refused = "--allow-host: '" + host + "' is not a website's name";
            return seeded;
        }
        allow_for_session(approvals, GatedName{.key = *canonical, .host = true});
    }
    // Denials last: when a run says both, no wins.
    for (const std::string& entry : presets.deny) {
        const GatedName name = name_gated(entry, gated, known_hosts(listed, approvals), true,
                                          child_processes, unstarted);
        if (!name.error.empty()) {
            seeded.refused = "--deny: " + name.error;
            return seeded;
        }
        deny_for_session(approvals, name);
        if (std::string note = shared_name_note(entry, name); !note.empty()) {
            seeded.notes.push_back(std::move(note));
        }
    }
    return seeded;
}

std::string preflight_presets(const PermissionPresets& presets,
                              const std::vector<std::string>& gated_native,
                              const std::vector<std::string>& servers,
                              const std::vector<std::string>& listed) {
    // An MCP tool's name waits for its server, the only one that knows
    // whether it exists and writes, and whether the config's level binds it
    // (57o). Every other name is the flags' alone.
    const auto native = [](const std::vector<std::string>& names) {
        std::vector<std::string> out;
        std::ranges::copy_if(names, std::back_inserter(out),
                             [](const std::string& name) { return !mcp::is_namespaced(name); });
        return out;
    };
    SessionApprovals scratch;
    return seed_approvals(scratch,
                          PermissionPresets{.allow = native(presets.allow),
                                            .deny = native(presets.deny),
                                            .allow_hosts = presets.allow_hosts},
                          gated_native, listed, unstarted_servers_clause(servers))
        .refused;
}

std::vector<std::string> describe_permissions(const harness::Config& config,
                                              const SessionApprovals& approvals,
                                              const std::vector<std::string>& gated,
                                              const std::vector<std::string>& edits) {
    std::vector<std::string> lines;
    const PermissionMode mode = approvals.mode.load();
    if (mode != PermissionMode::Manual) {
        lines.push_back("mode: " + std::string{to_string(mode)} + " -- " +
                        std::string{permission_mode_description(mode)});
    }
    std::size_t width = 0;
    for (const std::string& tool : gated) {
        width = std::max(width, tool.size());
    }
    for (const std::string& tool : gated) {
        const harness::PermissionLevel level = config.permissions.level(tool);
        const bool configured = config.permissions.levels.contains(tool);
        std::string answer;
        if (level == harness::PermissionLevel::Deny) {
            answer = "deny   (config)";
        } else if (approvals.denied_tools.contains(tool)) {
            answer = "deny   (this session)";
        } else if (mode == PermissionMode::Plan) {
            answer = "deny   (plan mode)";
        } else if (level == harness::PermissionLevel::Allow) {
            answer = "allow  (config)";
        } else if (approvals.tools.contains(tool)) {
            answer = "allow  (this session)";
        } else if (mode == PermissionMode::Bypass) {
            answer = "allow  (bypass mode)";
        } else if (mode == PermissionMode::AcceptEdits &&
                   std::ranges::find(edits, tool) != edits.end()) {
            answer = "allow  (accept-edits mode)";
        } else if (mode == PermissionMode::DontAsk) {
            answer = "deny   (dont-ask mode)";
        } else {
            answer = std::string{"ask    "} + (configured ? "(config)" : "(default)");
        }
        std::string line = tool;
        line.append(width - tool.size() + 2, ' ');
        lines.push_back(line + answer);
    }
    for (const std::string& host : approvals.hosts) {
        lines.push_back("website " + host + ": allow (this session)");
    }
    for (const std::string& host : approvals.denied_hosts) {
        lines.push_back("website " + host + ": deny (this session)");
    }
    for (const std::string& host : config.tools.allowed_hosts) {
        lines.push_back("website " + host + ": allow (config)");
    }
    if (lines.size() == (mode == PermissionMode::Manual ? 0U : 1U)) {
        lines.emplace_back("no tool in this chat asks permission (--tools)");
    }
    return lines;
}

}  // namespace apogee::commands
