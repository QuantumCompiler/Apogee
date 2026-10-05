#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backends/sampling.h"
#include "contracts/types.h"

/// The JSONL the `mlx` backend and its driver speak (27a): one object per
/// line in each direction, over the child's own stdin and stdout.
///
/// Pure, and apart from the provider, for the reason the vendor-CLI event
/// parsers are: the protocol is a contract on BOTH sides, and a contract is
/// pinned by golden lines -- what this side writes for a known request, and
/// what it makes of a known stream at every chunk size -- with no process in
/// the test. The driver's half of the same contract is
/// `assets/mlx/mlx_generate.py`, held to it by its stub-module suite.
///
/// **The driver renders, this side never does.** A request carries the
/// conversation in the shapes a Hugging Face chat template takes -- roles,
/// content, OpenAI-style tool calls and tool specs -- and the driver hands
/// them to the model's own template. Nothing here formats a prompt.
///
/// **Images are an addition, not a new version** (27c): a message with an
/// image carries its content as parts -- `{"type": "image", "image": <data
/// URI>}` and `{"type": "text", "text": ...}`, the content-part shape a
/// vision model's template places images by -- and only to a driver whose
/// `ready` said `vision`. A driver from before 27c never says it, so it is
/// never sent one.
namespace apogee::backends::mlx {

/// The protocol version this build speaks; the driver's `ready` names its.
inline constexpr int kProtocolVersion = 1;

/// One `generate` request.
struct GenerateRequest {
    /// Increasing per provider, so a late event from a cancelled request is
    /// told from the current one's.
    std::int64_t id = 0;
    std::vector<harness::ChatMessage> messages;
    std::vector<harness::Tool> tools;
    /// The values the sampling ladder resolved (26h); the driver guesses none.
    ResolvedSampling sampling;
    std::int64_t max_tokens = 2048;
    /// Where the reply ends if the model writes one of these.
    std::vector<std::string> stop;
    /// The template's own thinking switch (26i).
    bool thinking = true;
    /// False for a side request: answered on a cache of its own, so the
    /// conversation's is left exactly as it was.
    bool session = true;
};

/// The request as one line, without its newline.
[[nodiscard]] std::string generate_line(const GenerateRequest& request);

/// Asks the driver to end request `id`'s generation.
[[nodiscard]] std::string cancel_line(std::int64_t id);

/// One line from the driver.
struct Event {
    enum class Kind : std::uint8_t {
        /// The model is loaded: the fields below `model_type` are set.
        Ready,
        /// Answer text, as generated.
        Text,
        /// The model's reasoning, split out by its own format's markers.
        Reasoning,
        /// A call, parsed in the model's own format.
        ToolCall,
        /// The request is over: `finish` and the counts are set.
        Done,
        /// `error_kind` and `text` are set; `id` when a request caused it.
        Error,
    };

    Kind kind = Kind::Text;
    std::optional<std::int64_t> id;
    /// Text, Reasoning: the text. Error: the message.
    std::string text;
    /// ToolCall: the tool, and its arguments as JSON text.
    std::string name;
    std::string arguments;
    /// Done: "stop", "length", "tool_calls" or "cancelled".
    std::string finish;
    std::int64_t prompt_tokens = 0;
    /// Done: the prompt's leading tokens the session's cache already held.
    std::int64_t cached_tokens = 0;
    std::int64_t completion_tokens = 0;
    /// Error: "missing_dependency", "load", "request" or "protocol".
    std::string error_kind;
    /// Ready: what the driver found.
    int protocol = 0;
    std::string model_type;
    bool chat_template = false;
    /// mlx_lm's parser for the model's call format, empty when it has none.
    std::string tool_parser;
    bool thinking = false;
    std::string mlx_lm_version;
    /// Ready: the model was loaded through mlx-vlm and reads images (27c),
    /// and that package's version.
    bool vision = false;
    std::string mlx_vlm_version;
};

/// Parses one line, or nullopt for one that is not a protocol object -- a
/// stray line is dropped, never fatal, as on every child stream.
[[nodiscard]] std::optional<Event> parse_event(std::string_view line);

/// A short label for an event, so a test asserts on a sequence readably.
[[nodiscard]] std::string describe(const Event& event);

/// The finish reason a `done` line names; Stop for anything unknown.
[[nodiscard]] harness::FinishReason finish_reason(std::string_view finish) noexcept;

}  // namespace apogee::backends::mlx
