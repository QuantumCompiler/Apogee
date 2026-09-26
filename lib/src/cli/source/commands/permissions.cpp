#include "commands/permissions.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>

#include "harness/config_edit.h"
#include "harness/host.h"
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
    return
        [levels = config.permissions, hosts = config.tools.allowed_hosts,
         approvals = std::move(approvals)](const agent::GateRequest& request) -> agent::Permission {
            if (request.outbound) {
                // By host, never by tool: `permissions.fetch_url` is not a key.
                const std::optional<std::string> host = harness::canonical_host(request.target);
                if (!host.has_value()) {
                    return agent::Permission::Deny;
                }
                if (false) {
                    return agent::Permission::Allow;
                }
                if (approvals != nullptr && approvals->hosts.contains(*host)) {
                    return agent::Permission::Allow;
                }
                return agent::Permission::Ask;
            }
            const std::string_view tool = request.tool;
            switch (levels.level(tool)) {
                case harness::PermissionLevel::Allow:
                    return agent::Permission::Allow;
                case harness::PermissionLevel::Deny:
                    return agent::Permission::Deny;
                case harness::PermissionLevel::Ask:
                    break;
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

}  // namespace apogee::commands
