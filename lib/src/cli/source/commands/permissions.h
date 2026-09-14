#pragma once

#include <filesystem>
#include <istream>
#include <memory>
#include <set>
#include <string>

#include "agent/tool.h"
#include "ansi/ansi.h"
#include "commands/json_reporter.h"
#include "commands/status_line.h"
#include "harness/config.h"

/// The permission gate's two halves, as every surface wires them.
///
/// The gate itself lives in `agent/tool.h` and knows nothing about config
/// files or terminals: it asks a `PermissionChecker` for a level and, on
/// `Ask`, a `ConfirmFn` for a yes or no. This file is where those two are
/// made -- from the `permissions:` config, the answers remembered for the
/// session, and whichever prompt the surface has: the terminal's
/// `[y]es / [n]o / [a]lways / [s]ession`, machine mode's `question` event, or
/// nothing at all (`serve`, a pipe), which the gate resolves to deny.
namespace apogee::commands {

/// Tools the user answered `session` for. Shared between the checker and the
/// prompt so an answer given once holds for the run and not the next.
using SessionApprovals = std::set<std::string, std::less<>>;

/// The checker: the config's level for the tool, then the session's answers,
/// then `Ask`. `approvals` may be null (nothing is remembered).
[[nodiscard]] agent::PermissionChecker make_permission_checker(
    const harness::Config& config, std::shared_ptr<SessionApprovals> approvals);

/// The terminal prompt, or null when there is no terminal to prompt on -- and
/// a null ConfirmFn is exactly what makes `ask` deny on a pipe.
///
/// `always` is written to `permissions.<tool>: allow` through the config
/// editor (one mutation path) and remembered for the session too; `session`
/// is remembered only; `yes` allows this once; anything else denies.
[[nodiscard]] agent::ConfirmFn terminal_confirm_fn(StatusLine& status, ansi::Style style,
                                                   std::filesystem::path config_path,
                                                   std::shared_ptr<SessionApprovals> approvals);

/// Machine mode's prompt: a `question` event with `"kind": "permission"`,
/// answered by an ordinary `answer` line -- `yes`, `no`, `always`, `session`.
/// A driver that closes stdin with the prompt outstanding fails the turn,
/// the way an unanswered question does.
[[nodiscard]] agent::ConfirmFn make_driver_confirm_fn(JsonReporter& reporter, std::istream& input,
                                                      std::filesystem::path config_path,
                                                      std::shared_ptr<SessionApprovals> approvals);

/// Both halves, so a surface hands them to the loop as one thing.
struct ToolGate {
    agent::PermissionChecker permission;
    agent::ConfirmFn confirm;
};

}  // namespace apogee::commands
