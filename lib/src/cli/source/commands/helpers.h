#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/config.h"
#include "harness/types.h"

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

/// Resolution order for a per-request setting: an explicit flag, then the
/// backend entry's own value, then nothing.
///
/// The flag always wins -- it is the most specific thing the user said.
[[nodiscard]] std::optional<double> resolve_temperature(const std::optional<double>& flag_value,
                                                        const harness::Config& config,
                                                        std::string_view backend_name);

[[nodiscard]] std::optional<std::int64_t> resolve_max_tokens(
    const std::optional<std::int64_t>& flag_value, const harness::Config& config,
    std::string_view backend_name);

/// The system prompt for a turn: the flag when given, else the backend
/// entry's `system_prompt`, else empty.
[[nodiscard]] std::string resolve_system_prompt(const std::string& flag_value,
                                                const harness::Config& config,
                                                std::string_view backend_name);

/// Reads all of standard input. Used when no prompt argument was given.
[[nodiscard]] std::string read_stdin();

/// Whether stdin has piped or redirected content waiting.
///
/// False on a terminal: `apogee complete` with no argument at an interactive
/// prompt must print usage, not silently block reading the user's keystrokes
/// until they work out that Ctrl-D is what it wants.
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
