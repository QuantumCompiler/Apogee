#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "acp/session.h"
#include "acp/translate.h"
#include "cli/command.h"
#include "contracts/config.h"
#include "logger/session.h"
#include "mcp/registry.h"

namespace CLI {
class App;
}  // namespace CLI

/// `apogee acp` (56a): Apogee as an agent for an Agent Client Protocol host
/// -- Xcode 26.6 and later, through Settings › Intelligence › Agents › "Add
/// an Agent" -- on this process's own stdin and stdout. The CLI's fifth core
/// surface (SPEC.md → Core surfaces): pipes, never a port.
///
/// **A translator, never a second session.** Each ACP session is a captured
/// child of this binary's own `execute`, in machine mode, in the session's
/// folder (`acp/session.h`): a prompt is one turn of execute's loop, opened
/// through execute's own path (ADR 0011) -- the project's context, the
/// folder attached, the coder persona -- and Apogee's gate asks through the
/// host's permission sheet.
///
/// **Execute's flags, by reference.** `bind_session_flags` binds execute's
/// own table here, so `-m`, `--suite`, `--permission-mode`, `--allow-bypass`,
/// `--allow`/`--deny` and the rest reach every session and complete as
/// execute's do; what the `acp` line carries is handed to each session's
/// child as given. The flags the protocol owns -- `--output-format`,
/// `--input-format`, `--resume`, `-c`, `--private`, `--no-color`, `--raw` --
/// are refused at launch, in the command's words, and hidden from its help
/// and completion.
///
/// **Every session is a saved chat (56c)**, in the one store: what the host
/// lists and reopens is read here, the composition root, with the reads
/// `chats list` and `chats show` use -- `acp/` holds no store of its own.
///
/// **`acp registration` (56d)** prints the fields Xcode's Add an Agent sheet
/// takes, built once by `operations/acp_registration` from this process's
/// facts -- this binary's path, its channel, the root it runs under, `PATH`
/// -- as text or the `acp.registration` document. A subcommand, so a read's
/// `--output-format` never sits on the protocol verb; bare `acp` takes no
/// positional.
namespace apogee::commands {

/// The flags the protocol owns, as the session table spells them.
[[nodiscard]] std::span<const std::string_view> acp_protocol_flags() noexcept;

/// The models an ACP host is offered: every configured backend that chats --
/// the subscription CLIs included, as the macOS app offers them; an MLX
/// entry over a model no chat runs (an embedder, a drafter) left out -- the
/// roles chain's default first (`harness::resolve_chat_backend`, `-m` the
/// override), then the rest by name. Each described by its type and model.
[[nodiscard]] std::vector<acp::ModelChoice> acp_models(const harness::Config& config,
                                                       std::string_view override_model);

/// The flags `command` was given, as words a child's command line takes --
/// every option the line set, each spelled by its long name, the protocol's
/// own and `--tools` (every session has it) left out.
[[nodiscard]] std::vector<std::string> forwarded_flags(const CLI::App& command);

/// The child's half of a session's handover (56b): the MCP servers an ACP
/// host named, read from the file `execute`'s hidden `--acp-host-servers`
/// names -- removed as it is read -- as the specs the session connects
/// (`BuiltInToolOptions::session_mcp_servers`). What the host's entries could
/// not be, and a handover that could not be read, go to `notes`, each the
/// text of an `[mcp] warning:` line.
[[nodiscard]] std::vector<mcp::ServerSpec> host_mcp_servers(const std::filesystem::path& handover,
                                                            std::vector<std::string>& notes);

/// A saved chat's entries as an ACP load replays them (56c):
/// `logger::saved_turns`, in order, each in the words `chats show` prints --
/// the user's words with what rode them named (`attached: ...`), each tool
/// call's name and one-line arguments, the answer's parts, a compaction's
/// summary.
[[nodiscard]] std::vector<acp::SavedEntry> replay_entries(const logger::Session& session);

/// The one session store as `acp` reads it (56c): `logger::list_sessions`
/// for `session/list`, `logger::load` and `replay_entries` for
/// `session/load` and `session/resume` -- the chat's own id beside its
/// entries, so the agent can tell an id from a name; refused in
/// `chats show`'s words (`no chat named '<id>'`).
[[nodiscard]] acp::SavedChats acp_saved_chats();

class AcpCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
