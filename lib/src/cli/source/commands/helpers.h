#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/rag.h"
#include "harness/config.h"
#include "harness/harness.h"
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

/// Where a turn's retrieval collection came from.
enum class RagSource : std::uint8_t {
    /// No retrieval this turn.
    None,
    /// `--rag <name>` on the command line.
    Flag,
    /// The config's `auto_rag` key, with no flag given.
    Config,
};

/// The collection a turn retrieves from, and why.
struct RagChoice {
    std::string collection;
    RagSource source = RagSource::None;

    [[nodiscard]] bool active() const noexcept {
        return !collection.empty();
    }
};

/// Decides which collection a turn retrieves from.
///
/// **One function, called by every surface**, because the precedence is the
/// whole contract: the flag beats the config, and an explicitly empty flag
/// (`--rag ""`) means *no retrieval this run* rather than "fall back to
/// `auto_rag`". A key that cannot be switched off for a single invocation is
/// a key people stop using. `flag_given` is therefore distinct from
/// `flag_value.empty()` -- an absent flag falls through to the config, a
/// present-but-empty one does not.
[[nodiscard]] RagChoice choose_rag_collection(bool flag_given, std::string_view flag_value,
                                              std::string_view auto_rag);

/// What to tell the user about a turn's retrieval, without the surface's own
/// tag or framing.
///
/// Always names the chunk count, the top score, and the retriever -- and says
/// when the collection came from `auto_rag` rather than a flag. **Silent
/// injection is the failure mode**: a user who does not know context was added
/// cannot tell why an answer went sideways, and that is doubly true when
/// nothing on the command line asked for it.
[[nodiscard]] std::string describe_retrieval(const RagChoice& choice,
                                             const agentloop::RagResult& result);

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

/// Why a backend cannot take the attachments it was given, or empty when it
/// can.
///
/// **One helper, called by every surface that accepts `--image`.** It exists
/// because the check was originally written inline in `complete.cpp` and simply
/// never written in `chat.cpp`, so `apogee chat --image` handed pictures to a
/// provider that had just said it could not read them. A capability check
/// present on one surface and absent on another is precisely what "parity is
/// the product" forbids, and the fix that lasts is one function rather than a
/// second copy.
///
/// It returns a message rather than printing or throwing, so each surface can
/// deliver it in its own idiom: prose on a terminal, an `error` event in
/// machine mode where stdout carries only JSONL.
///
/// The probe goes through `Harness::accepts_images()`, never a `dynamic_cast`
/// — so this stays correct the day a local backend gains vision without this
/// file learning that llamacpp exists.
[[nodiscard]] std::string attachment_refusal(const harness::Harness& harness,
                                             const std::string& model,
                                             const std::vector<harness::ContentPart>& attachments);

/// The message `attachment_refusal` gives, for `model`.
///
/// Exposed separately so its content can be asserted directly. It has to be:
/// in a build without llama.cpp no provider ever answers "no", so a test that
/// waits for a real refusal to inspect its wording never runs its own
/// assertions — which is exactly what the first version of that test did.
[[nodiscard]] std::string image_refusal_message(const std::string& model);

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
