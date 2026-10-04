#include "cli/permissions.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "agent/web_search.h"
#include "contracts/config_edit.h"
#include "contracts/host.h"
#include "platform/platform.h"

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

agent::PermissionChecker make_permission_checker(const harness::Config& config,
                                                 std::shared_ptr<SessionApprovals> approvals) {
    // The levels and hosts are copied: the checker outlives any one config
    // object, and a mid-session edit to the file takes effect on the next
    // session.
    //
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
    return
        [levels = config.permissions, hosts = std::move(hosts),
         approvals = std::move(approvals)](const agent::GateRequest& request) -> agent::Permission {
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
                return agent::Permission::Ask;
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
            if (level == harness::PermissionLevel::Allow) {
                return agent::Permission::Allow;
            }
            if (approvals != nullptr && approvals->tools.contains(tool)) {
                return agent::Permission::Allow;
            }
            return agent::Permission::Ask;
        };
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
        status.print_line(style.tag(ansi::Role::Permission) + " " + std::string{request.tool} +
                          (request.target.empty() ? "" : " -> " + std::string{request.target}));
        if (!request.detail.empty()) {
            // The whole URL: what would leave the machine is in it.
            status.print_line(style.dim("  " + std::string{request.detail}));
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
        const bool allowed =
            apply_answer(parse_answer(line), request, config_path, approvals, note);
        if (!note.empty()) {
            status.print_line(style.dim("  " + note));
        }
        if (!allowed) {
            status.print_line(style.dim("  denied"));
        }
        return allowed;
    };
}

agent::ConfirmFn make_driver_confirm_fn(JsonReporter& reporter, std::istream& input,
                                        std::filesystem::path config_path,
                                        std::shared_ptr<SessionApprovals> approvals) {
    return [&reporter, &input, config_path = std::move(config_path),
            approvals = std::move(approvals)](const agent::GateRequest& request) {
        reporter.emit_permission_question(request);
        std::string line;
        while (std::getline(input, line)) {
            const DriverMessage message = parse_driver_line(line);
            if (message.kind != DriverMessage::Kind::Answer) {
                continue;  // the same tolerance the question path keeps
            }
            std::string note;
            return apply_answer(parse_answer(message.text), request, config_path, approvals, note);
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

GatedName name_gated(std::string_view name, const std::vector<std::string>& gated, bool websites) {
    GatedName out;
    if (std::ranges::find(gated, name) != gated.end()) {
        out.key = std::string{name};
        return out;
    }
    // A website, by the host it names -- as a session's own answer keeps it.
    if (websites && name.find('.') != std::string_view::npos) {
        if (const std::optional<std::string> host = harness::canonical_host(name);
            host.has_value()) {
            out.key = *host;
            out.host = true;
            return out;
        }
    }
    std::string listed;
    for (const std::string& tool : gated) {
        listed += (listed.empty() ? "" : ", ") + tool;
    }
    out.error = "'" + std::string{name} + "' is not a tool this chat asks about" +
                (gated.empty() ? std::string{" -- it has none (--tools)"}
                               : " -- the ones it asks about: " + listed) +
                (websites ? "; or name a website, like docs.python.org" : "");
    return out;
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

std::string seed_approvals(SessionApprovals& approvals, const PermissionPresets& presets,
                           const std::vector<std::string>& gated) {
    for (const std::string& tool : presets.allow) {
        // `--allow` is for tools; a website has `--allow-host`.
        const GatedName name = name_gated(tool, gated, false);
        if (!name.error.empty()) {
            return "--allow: " + name.error + " (a website takes --allow-host)";
        }
        allow_for_session(approvals, name);
    }
    for (const std::string& host : presets.allow_hosts) {
        const std::optional<std::string> canonical = harness::canonical_host(host);
        if (!canonical.has_value()) {
            return "--allow-host: '" + host + "' is not a website's name";
        }
        allow_for_session(approvals, GatedName{.key = *canonical, .host = true});
    }
    // Denials last: when a run says both, no wins.
    for (const std::string& entry : presets.deny) {
        const GatedName name = name_gated(entry, gated);
        if (!name.error.empty()) {
            return "--deny: " + name.error;
        }
        deny_for_session(approvals, name);
    }
    return {};
}

std::vector<std::string> describe_permissions(const harness::Config& config,
                                              const SessionApprovals& approvals,
                                              const std::vector<std::string>& gated) {
    std::vector<std::string> lines;
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
        } else if (level == harness::PermissionLevel::Allow) {
            answer = "allow  (config)";
        } else if (approvals.tools.contains(tool)) {
            answer = "allow  (this session)";
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
    if (lines.empty()) {
        lines.emplace_back("no tool in this chat asks permission (--tools)");
    }
    return lines;
}

}  // namespace apogee::commands
