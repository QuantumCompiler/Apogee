#pragma once

#include <filesystem>
#include <istream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "ansi/ansi.h"
#include "contracts/config.h"
#include "machine/json_reporter.h"
#include "views/status_line.h"

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

class DriverInput;

/// What the user answered `session` for. Shared between the checker and the
/// prompt so an answer given once holds for the run and not the next.
struct SessionApprovals {
    /// Tools, for the ones that write.
    std::set<std::string, std::less<>> tools;
    /// Canonical hosts (`harness/host.h`), for the ones that reach out:
    /// allowing one website is not allowing the next.
    std::set<std::string, std::less<>> hosts;
    /// Said no to for the session, ahead of the prompt (26o): never asked and
    /// never allowed -- over the config's own allow too, since the person at
    /// the keyboard saying no in advance is the safest answer the gate gets.
    std::set<std::string, std::less<>> denied_tools;
    std::set<std::string, std::less<>> denied_hosts;
};

/// What a run was started with (26o): `--allow` and `--deny` take a gated
/// tool's name -- `--deny` a website's too -- and `--allow-host` a website.
/// Each is the `session` answer, given before the prompt would ask.
struct PermissionPresets {
    std::vector<std::string> allow;
    std::vector<std::string> deny;
    std::vector<std::string> allow_hosts;

    [[nodiscard]] bool empty() const noexcept {
        return allow.empty() && deny.empty() && allow_hosts.empty();
    }
};

/// The tools a session can be asked about: those in `registry` that write.
[[nodiscard]] std::vector<std::string> gated_tools(const agent::ToolRegistry& registry);

/// A name a preset or a slash verb was given: a gated tool, or a website by
/// its canonical host. `error` names the gated set when it is neither -- a
/// typo must not quietly grant nothing.
struct GatedName {
    std::string key;
    bool host = false;
    std::string error;
};

[[nodiscard]] GatedName name_gated(std::string_view name, const std::vector<std::string>& gated,
                                   bool websites = true);

/// `--allow`, `--allow-host` and `/allow`: allowed for the session, a
/// denial of it lifted. `--deny` and `/deny`: denied, an allowance lifted.
void allow_for_session(SessionApprovals& approvals, const GatedName& name);
void deny_for_session(SessionApprovals& approvals, const GatedName& name);
/// `/revoke`: the session's own answer forgotten; false when it had none.
bool revoke_for_session(SessionApprovals& approvals, const GatedName& name);

/// Seeds a run's answers from its flags. The first refusal, or empty.
[[nodiscard]] std::string seed_approvals(SessionApprovals& approvals,
                                         const PermissionPresets& presets,
                                         const std::vector<std::string>& gated);

/// `/permissions`: each gated tool's effective answer and where it comes
/// from -- the config, this session, or the default -- then the websites
/// the session answered and the ones the config lists.
[[nodiscard]] std::vector<std::string> describe_permissions(const harness::Config& config,
                                                            const SessionApprovals& approvals,
                                                            const std::vector<std::string>& gated);

/// The checker. For a tool that writes: the config's level for the tool, then
/// the session's answers, then `Ask`. For an outbound one: the host in
/// `tools.allowed_hosts`, then the session's hosts, then `Ask` -- so a
/// surface with nobody to ask reaches only the listed hosts. `approvals` may
/// be null (nothing is remembered).
[[nodiscard]] agent::PermissionChecker make_permission_checker(
    const harness::Config& config, std::shared_ptr<SessionApprovals> approvals);

/// The terminal prompt, or null when there is no terminal to prompt on -- and
/// a null ConfirmFn is exactly what makes `ask` deny on a pipe.
///
/// `always` is written through the config editor (one mutation path) and
/// remembered for the session too -- `permissions.<tool>: allow` for a tool
/// that writes, the host added to `tools.allowed_hosts` for an outbound one;
/// `session` is remembered only; `yes` allows this once; anything else denies.
[[nodiscard]] agent::ConfirmFn terminal_confirm_fn(StatusLine& status, ansi::Style style,
                                                   std::filesystem::path config_path,
                                                   std::shared_ptr<SessionApprovals> approvals);

/// Machine mode's prompt: a `question` event with `"kind": "permission"`,
/// answered by an ordinary `answer` line -- `yes`, `no`, `always`, `session`.
/// A driver that closes stdin with the prompt outstanding fails the turn,
/// the way an unanswered question does.
[[nodiscard]] agent::ConfirmFn make_driver_confirm_fn(JsonReporter& reporter, DriverInput& input,
                                                      std::filesystem::path config_path,
                                                      std::shared_ptr<SessionApprovals> approvals);

/// Both halves, so a surface hands them to the loop as one thing.
struct ToolGate {
    agent::PermissionChecker permission;
    agent::ConfirmFn confirm;
};

}  // namespace apogee::commands
