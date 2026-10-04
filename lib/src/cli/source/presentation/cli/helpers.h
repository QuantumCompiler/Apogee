#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/tool.h"
#include "agentloop/budget.h"
#include "agentloop/rag.h"
#include "agentloop/tool_selection.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "contracts/types.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "mcp/registry.h"
#include "operations/backend_names.h"
#include "operations/retrieval.h"
#include "operations/run_settings.h"
#include "tools/toolsets.h"
#include "transport/http_client.h"
#include "views/status_line.h"

/// Shared plumbing for the CLI commands.
///
/// Command files stay thin — flag parsing and rendering over library calls —
/// which is the split that makes exec-style testing and (later) HTTP parity
/// possible. Anything a second command would also need lands here rather than
/// in the first command that happened to want it.
namespace apogee::commands {

/// Process exit codes.
///
/// Distinguished so a script can react: a user error is worth fixing and
/// retrying, a backend error may be worth retrying unchanged, and cancellation
/// is neither. Collapsing them into 1 makes `apogee` unusable in a pipeline
/// that needs to tell "your prompt was wrong" from "the API was down".
enum ExitCode : int {
    kSuccess = 0,
    /// Bad flags, a missing file, no usable backend, an unroutable model.
    kUserError = 1,
    /// The provider failed: transport, API rejection, malformed response.
    kBackendError = 2,
    /// Interrupted. 130 is the shell convention for SIGINT.
    kCancelled = 130,
};

/// The built-in tool set behind `--tools` on every surface.
///
/// `fetch_url` only, for now. Web search comes from the provider's own
/// server-side tool (`--search`), not a local one -- see the decision recorded
/// on the complete item. Native filesystem toolsets and MCP register here
/// later. **One function, three callers**: `complete`, `chat`, and `serve`
/// build the same registry, so a tool added here reaches every surface at
/// once rather than the one someone remembered to edit.
/// What the built-in registry is built from.
struct BuiltInToolOptions {
    /// `permissions:` and `tools:`; null means every default.
    const harness::Config* config = nullptr;
    /// For the RAG tools' embedder; null means lexical-only searches.
    const harness::Harness* harness = nullptr;
    /// The review defaults `git_diff` and `git_log` fall back to, set by flags.
    tools::ReviewDefaults review;
    /// A live review the git tools read at call time instead (chat's
    /// `/branch`); see `tools::GitOptions::live_review`.
    std::shared_ptr<const tools::ReviewDefaults> live_review;

    /// **The agent's permission model.** `ReadOnly` keeps only tools that
    /// declare no `writes` -- MCP tools included, which are read-only exactly
    /// when their server said so. An `outbound` tool stays, gated per
    /// website: with nobody to ask it reaches only `tools.allowed_hosts`, so a
    /// non-interactive run still never blocks. `None` builds nothing and
    /// dials no server. `All` is every tool, under the gate, as `chat` has it.
    harness::AgentToolPolicy policy = harness::AgentToolPolicy::All;
    /// Which `mcp_servers:` entries to connect: null means every enabled one
    /// (the interactive surfaces), a list means only those (an agent names
    /// what it needs). A name that matches no entry is reported through
    /// `mcp_status` and skipped.
    std::optional<std::vector<std::string>> mcp_servers;

    /// When set, every enabled `mcp_servers:` entry is connected on this
    /// registry and its tools registered as `mcp__<server>__<tool>`. The
    /// caller owns it: the connections live as long as this object does.
    std::shared_ptr<mcp::Registry> mcp;
    /// Where `[mcp] connecting: …` and the connection results go.
    std::function<void(std::string_view)> mcp_status;
    /// Where a server's raw stderr goes; null discards it (the bounded tail
    /// is kept either way). `--verbose` points it at the terminal, `serve`
    /// at its own stderr.
    mcp::StderrTail::Sink mcp_server_log;
    /// How a server's transport is made; null means a real child. A test
    /// hands in the scripted fleet.
    std::function<std::unique_ptr<mcp::Transport>(const mcp::ServerSpec&, mcp::StderrTail::Sink,
                                                  std::string&)>
        mcp_spawn;
};

/// The status callback for an interactive surface: progress repaints the one
/// transient line, a warning stays -- a server that failed to connect is
/// something the user should still be able to read once the prompt is up.
[[nodiscard]] std::function<void(std::string_view)> mcp_status_line(StatusLine& status);

/// The most of a response `fetch_url` reads: past it the fetch is refused,
/// naming the size, rather than holding an unbounded body in memory to keep
/// its first 8 KB (5 MB, the reader item's recorded default, 2026-09-25).
inline constexpr std::size_t kFetchMaxBodyBytes = std::size_t{5} * 1024 * 1024;

/// `fetch_url`'s fetcher over `client`: one GET, redirects NOT followed (their
/// `Location` is handed back for the tool to gate), the body capped at
/// kFetchMaxBodyBytes. `make_built_in_tools` wires it to a real transport.
[[nodiscard]] agent::UrlFetcher make_http_fetcher(std::shared_ptr<backends::HttpClient> client);

/// `fetch_url` plus the native toolsets, honouring `tools.disabled`. The one
/// place that decides which tools a `--tools` run has.
[[nodiscard]] agent::ToolRegistry make_built_in_tools(const BuiltInToolOptions& options);

/// What a surface ranks its tools with (26g): the embedding role's vectors
/// when it resolves to an embedder that costs nothing to call, cached in
/// `cache_file`, and words otherwise. The spend rule: ranking is one embed
/// call per turn, and Apogee never pays for a call on its own initiative.
/// `ranked_by` says which, and why, for `--verbose`.
[[nodiscard]] std::shared_ptr<const agentloop::ToolRanker> make_tool_ranker(
    const harness::Harness& harness, const harness::Config& config,
    const agent::ToolRegistry& registry, const std::filesystem::path& cache_file,
    std::string& ranked_by);

/// The selection a conversation over `registry` offers its tools through
/// (26g): null when the registry is small enough to offer whole, so nothing
/// changes for it. A chat keeps it for every turn.
[[nodiscard]] std::unique_ptr<agentloop::ToolSelection> make_tool_selection(
    const harness::Harness& harness, const harness::Config& config,
    const agent::ToolRegistry& registry, const std::filesystem::path& config_path,
    std::string& ranked_by);

/// `registry` filtered by `policy`: `ReadOnly` drops every tool that
/// declares `writes`, `None` drops everything, `All` keeps it whole. The
/// filter is over the REGISTRY, which is what makes the policy structural:
/// a tool that is not registered is not advertised, whatever the model asks.
[[nodiscard]] agent::ToolRegistry apply_tool_policy(const agent::ToolRegistry& registry,
                                                    harness::AgentToolPolicy policy);

/// Reads all of standard input. Used when no prompt argument was given.
[[nodiscard]] std::string read_stdin();

/// Whether stdin has piped or redirected content waiting.
///
/// False on a terminal: `apogee complete` with no argument at an interactive
/// prompt must print usage, not silently block reading the user's keystrokes
/// until they work out that Ctrl-D is what it wants. True whenever std::cin's
/// buffer has been replaced, since that is where the input is read from.
[[nodiscard]] bool stdin_is_piped();

/// Reads `path` and returns it as an image content part carrying a `data:` URI.
///
/// Throws std::runtime_error when the file cannot be read or its type is not a
/// recognised image format -- guessed at from the extension, since the wire
/// format needs an explicit media type and there is nowhere to put "unknown".
[[nodiscard]] harness::ContentPart load_image_part(const std::filesystem::path& path);

/// Base64, for the `data:` URIs image parts are carried in.
[[nodiscard]] std::string base64_encode(std::string_view bytes);

/// The image media type implied by `path`'s extension, or empty when the
/// extension is not a format the cloud vendors accept.
[[nodiscard]] std::string image_media_type(const std::filesystem::path& path);

/// Why nothing configured can read `medium` for a conversation on `model`, or
/// empty when something can: the chat model natively, or the helper role that
/// reads it into text -- `vision` for an image or a video's frames,
/// `transcription` for audio (26e).
///
/// **One helper, reached by every surface that takes media.** It exists
/// because the check was originally written inline in `complete.cpp` and simply
/// never written in `chat.cpp`, so `apogee chat --image` handed pictures to a
/// provider that had just said it could not read them. A capability check
/// present on one surface and absent on another is precisely what "parity is
/// the product" forbids, and the fix that lasts is one function rather than a
/// second copy. Since 26e every surface's `--image`, `--attach` and `@path`
/// reach it through `ChatAttachments`.
///
/// It returns a message rather than printing or throwing, so each surface can
/// deliver it in its own idiom: prose on a terminal, a notice in machine mode
/// where stdout carries only JSONL.
///
/// The probes go through `Harness::can_read()`, never a `dynamic_cast` -- so
/// this stays correct the day a backend gains a medium without this file
/// learning that it exists.
[[nodiscard]] std::string attachment_refusal(const harness::Harness& harness,
                                             const std::string& model, harness::Medium medium);

/// The message `attachment_refusal` gives, for `model` and `medium`: what
/// cannot read it, and the helper role that would -- naming `helper`, the
/// model that role is set to, when it is set and cannot either.
///
/// Exposed separately so its content can be asserted directly. It has to be:
/// in a build without llama.cpp no provider ever answers "no", so a test that
/// waits for a real refusal to inspect its wording never runs its own
/// assertions -- which is exactly what the first version of that test did.
[[nodiscard]] std::string media_refusal_message(const std::string& model, harness::Medium medium,
                                                std::string_view helper = {});

/// Builds the message list for a one-shot turn.
///
/// Order matters and is fixed here so every surface produces the same shape:
/// system prompt, then any extra context, then the user's prompt with its
/// attachments. Context before the prompt because a model weights the last
/// message most, and the prompt is what it should be answering.
[[nodiscard]] std::vector<harness::ChatMessage> build_messages(
    const std::string& system_prompt, const std::string& context, const std::string& prompt,
    const std::vector<harness::ContentPart>& attachments);

}  // namespace apogee::commands
