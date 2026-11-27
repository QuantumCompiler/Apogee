#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/loop.h"
#include "cli/command.h"
#include "contracts/config.h"

namespace apogee::commands {

/// `apogee complete <prompt>` — one-shot prompt in, answer out.
///
/// The walking skeleton: config → harness → backend → terminal, the shortest
/// path that proves the architecture end to end. It is deliberately the first
/// user-visible command for that reason — a falsified design bet is cheapest to
/// fix here, before the agent loop and the chat REPL are built on top of it.
///
/// Scriptable by construction: on a pipe it emits exactly the answer, so
/// `apogee complete "…" | jq` and friends work. Tool use is not here — it
/// arrives with the agent loop as `complete --tools`.
class CompleteCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

/// What a one-shot asks besides its prompt: the request's own flags
/// `apogee complete` takes, each unset deferring to the backend's entry as
/// the flag does (`operations/run_settings.h`).
struct OneShotSettings {
    /// `-s`; empty takes the entry's `system_prompt`.
    std::string system_prompt{};
    /// `--context`, put before the prompt.
    std::string context{};
    std::optional<double> temperature{};
    std::optional<std::int64_t> max_tokens{};
    std::optional<harness::ThinkingMode> think{};
    std::optional<std::int64_t> think_budget{};
    std::optional<harness::Effort> effort{};
    /// Whether the utility model, when the config names one, decides an
    /// `auto` thinking and summarises a large tool result, as it does for
    /// `apogee complete`. Off, the run makes no call but the named
    /// backend's: `auto` is decided by rule (54b, the served `complete`).
    bool utility = true;
};

/// A one-shot's request and the loop's options for it.
struct OneShot {
    /// The system prompt, the context, then the prompt (`build_messages`).
    std::vector<harness::ChatMessage> messages{};
    /// The model, its sampling, its bound and its thinking, resolved as the
    /// flags and the entry say; the answer streamed through the reporter.
    agentloop::Options options{};
};

/// **The one-shot's core** (54b): the request `apogee complete -m <model>`
/// builds for `prompt` and the loop options it runs it with, before any
/// attachment, retrieval or tool is added. `apogee complete` builds every
/// run from it, and `apogee mcp serve`'s `complete` runs exactly it -- so a
/// served call is the command's call, never a second inference path.
[[nodiscard]] OneShot one_shot(const harness::Config& config, const std::string& model,
                               const std::string& prompt, const OneShotSettings& settings);

}  // namespace apogee::commands
