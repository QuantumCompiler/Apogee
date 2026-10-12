#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
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

/// How much a session may do without asking (39b): Claude Code's permission
/// modes, one layer over the gate. A config `deny` and a session `/deny`
/// refuse in every mode.
enum class PermissionMode : std::uint8_t {
    /// The gate as configured: the config's level, the session's answers,
    /// else ask.
    Manual,
    /// A tool that edits a file or a note (`Tool::edits`) runs without
    /// asking; a command, an MCP tool and a website still ask.
    AcceptEdits,
    /// Nothing that writes runs, and the model is told to plan.
    Plan,
    /// Whatever would ask is refused. Never in the Shift+Tab cycle.
    DontAsk,
    /// Nothing asks. Reachable only when the session was started allowing it.
    Bypass,
};

/// Every mode, in the order `/permission-mode` offers them.
inline constexpr std::array kPermissionModes{PermissionMode::Manual, PermissionMode::AcceptEdits,
                                             PermissionMode::Plan, PermissionMode::DontAsk,
                                             PermissionMode::Bypass};

/// `manual`, `accept-edits`, `plan`, `dont-ask`, `bypass`.
[[nodiscard]] std::string_view to_string(PermissionMode mode) noexcept;
/// The mode a word names, or unset.
[[nodiscard]] std::optional<PermissionMode> permission_mode_from_string(
    std::string_view text) noexcept;
/// The modes' words, in order: what `--permission-mode` accepts.
[[nodiscard]] std::vector<std::string> permission_mode_words();
/// What a mode does, in a phrase: `/permission-mode`'s reply and completion.
[[nodiscard]] std::string_view permission_mode_description(PermissionMode mode) noexcept;
/// Claude Code's status words for a mode -- `⏵⏵ accept edits on`, `⏸ plan
/// mode on` -- coloured by `style`; empty for manual, which shows nothing.
[[nodiscard]] std::string permission_mode_label(PermissionMode mode, const ansi::Style& style);
/// Shift+Tab's next: manual → accept-edits → plan → (bypass, when allowed) →
/// manual; from dont-ask, outside the cycle, manual.
[[nodiscard]] PermissionMode next_permission_mode(PermissionMode mode,
                                                  bool bypass_allowed) noexcept;

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
    /// The session's permission mode (39b). Atomic: the shell's Shift+Tab
    /// moves it from its own thread while a turn may be running, and the
    /// checker reads it at each call.
    std::atomic<PermissionMode> mode{PermissionMode::Manual};
    /// Whether bypass may be chosen: set at launch by `--allow-bypass` or
    /// `--permission-mode bypass`, never mid-session.
    bool bypass_allowed = false;
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
/// Those of them that edit a file or a note (`Tool::edits`, 39b).
[[nodiscard]] std::vector<std::string> edit_tools(const agent::ToolRegistry& registry);

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
/// from -- the config, this session, the mode, or the default -- then the
/// websites the session answered and the ones the config lists. Outside
/// manual the mode leads (39b); `edits` are the gated tools that edit.
[[nodiscard]] std::vector<std::string> describe_permissions(
    const harness::Config& config, const SessionApprovals& approvals,
    const std::vector<std::string>& gated, const std::vector<std::string>& edits = {});

/// The checker. For a tool that writes: the config's level for the tool, then
/// the session's answers, then the session's mode (39b: plan refuses every
/// writer, accept-edits allows an edit, bypass allows, dont-ask refuses),
/// then `Ask`. For an outbound one: the host in
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

/// The answer `text` gives a permission prompt, applied: `yes`/`y` allows
/// once, `session`/`s` remembers it for the run, `always`/`a` remembers it
/// and writes it to the config through the one editor (`permissions.<tool>:
/// allow`, or the host into `tools.allowed_hosts`); anything else denies.
/// Returns whether the call may run; `note` says what was written, or why
/// writing failed. The one path every prompt -- the terminal's, machine
/// mode's, the full-screen shell's (32c) -- answers through.
[[nodiscard]] bool answer_permission(std::string_view text, const agent::GateRequest& request,
                                     const std::filesystem::path& config_path,
                                     const std::shared_ptr<SessionApprovals>& approvals,
                                     std::string& note);

/// Machine mode's prompt: a `question` event with `"kind": "permission"`,
/// answered by an ordinary `answer` line -- `yes`, `no`, `always`, `session`.
/// A driver that closes stdin with the prompt outstanding fails the turn,
/// the way an unanswered question does.
[[nodiscard]] agent::ConfirmFn make_driver_confirm_fn(JsonReporter& reporter, DriverInput& input,
                                                      std::filesystem::path config_path,
                                                      std::shared_ptr<SessionApprovals> approvals);

/// Plan mode's note for a turn's system prompt (39b), or empty in any other
/// mode: it rides the turn, never the transcript.
[[nodiscard]] std::string plan_note(const SessionApprovals& approvals);

/// The refusal's reason a mode gives the model (39b): plan mode's and
/// dont-ask's, read from the mode at the call; empty otherwise.
[[nodiscard]] agent::RefusalNote mode_refusal(std::shared_ptr<const SessionApprovals> approvals);

/// Both halves, so a surface hands them to the loop as one thing.
struct ToolGate {
    agent::PermissionChecker permission;
    agent::ConfirmFn confirm;
    /// The session's answers and mode (39b), for plan mode's note and a
    /// refusal's reason; null where no session holds any.
    std::shared_ptr<const SessionApprovals> approvals;
};

}  // namespace apogee::commands
