#include "commands/permissions.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <utility>

#include "harness/config_edit.h"
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

/// Applies an answer: remembers it, persists `always`, and says whether the
/// operation may run. Returns what to tell the user about persistence.
bool apply_answer(Answer answer, std::string_view tool, const std::filesystem::path& config_path,
                  const std::shared_ptr<SessionApprovals>& approvals, std::string& note) {
    switch (answer) {
        case Answer::Deny:
            return false;
        case Answer::Once:
            return true;
        case Answer::Session:
            if (approvals != nullptr) {
                approvals->insert(std::string{tool});
            }
            return true;
        case Answer::Always:
            if (approvals != nullptr) {
                approvals->insert(std::string{tool});
            }
            if (!config_path.empty()) {
                try {
                    harness::edit_config_file(config_path, [tool](std::string_view content) {
                        return harness::set_permission(content, tool, "allow");
                    });
                    note = "permissions." + std::string{tool} + " = allow written to " +
                           config_path.string();
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
    // The levels are copied: the checker outlives any one config object, and
    // a mid-session edit to the file takes effect on the next session.
    return [levels = config.permissions, approvals = std::move(approvals)](
               std::string_view tool, std::string_view) -> agent::Permission {
        switch (levels.level(tool)) {
            case harness::PermissionLevel::Allow:
                return agent::Permission::Allow;
            case harness::PermissionLevel::Deny:
                return agent::Permission::Deny;
            case harness::PermissionLevel::Ask:
                break;
        }
        if (approvals != nullptr && approvals->contains(tool)) {
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
    return [&status, style, config_path = std::move(config_path), approvals = std::move(approvals)](
               std::string_view tool, std::string_view target) {
        // Through the status line, so a spinner frame cannot land on top of
        // the prompt -- the same discipline the question prompt keeps.
        status.print_line(style.tag(ansi::Role::Permission) + " " + std::string{tool} +
                          (target.empty() ? "" : " -> " + std::string{target}));
        std::cerr << style.dim("Allow? [y]es / [n]o / [a]lways / [s]ession: ") << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cerr << "\n";
            return false;
        }
        std::string note;
        const bool allowed = apply_answer(parse_answer(line), tool, config_path, approvals, note);
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
            approvals = std::move(approvals)](std::string_view tool, std::string_view target) {
        reporter.emit_permission_question(tool, target);
        std::string line;
        while (std::getline(input, line)) {
            const DriverMessage message = parse_driver_line(line);
            if (message.kind != DriverMessage::Kind::Answer) {
                continue;  // the same tolerance the question path keeps
            }
            std::string note;
            return apply_answer(parse_answer(message.text), tool, config_path, approvals, note);
        }
        throw std::runtime_error("the driver closed stdin with a permission prompt unanswered");
    };
}

}  // namespace apogee::commands
