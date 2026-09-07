#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/question.h"
#include "agentloop/reporter.h"
#include "harness/types.h"

/// The `agentloop::Reporter` → JSONL adapter: Apogee's machine mode.
///
/// **The sibling of `CliReporter`, over the same seam.** The terminal renderer
/// turns loop events into escape codes; this turns the same events into one
/// JSON object per line. `serve`'s SSE frames will be the third. That is the
/// whole point of the Reporter interface: a capability reaches every surface
/// because there is one loop and it can only speak through one seam.
///
/// **The vocabulary mirrors that seam one-to-one, deliberately.** Every event
/// type below is a `Reporter` method; nothing is invented, aggregated, or
/// renamed for convenience. A second vocabulary would be a second thing to keep
/// in sync, and the first time it drifted a driver would see an event the
/// terminal never shows or miss one it does.
///
/// **stdout carries only JSONL. Every diagnostic goes to stderr.** This is the
/// same discipline Apogee demands of the vendor CLIs it drives — and having
/// been the consumer on the other side of it, the reason is concrete: a
/// diagnostic interleaved into the event stream breaks the driver's parser at
/// the worst possible moment.
///
/// ## The schema
///
/// One object per line. Every object has a `type`. A driver **must ignore
/// unknown types** — that is the tolerance Apogee practises as a consumer of
/// other CLIs, granted to its own consumers, and it is what lets this schema
/// grow without breaking drivers.
///
/// ```jsonl
/// {"type":"session","protocol_version":1,"model":"claude-sonnet-5"}
/// {"type":"thinking"}
/// {"type":"thinking_delta","text":"…"}
/// {"type":"tool_status","text":"fetch_url https://…"}
/// {"type":"answer_start"}
/// {"type":"answer_delta","text":"…"}
/// {"type":"answer_end"}
/// {"type":"result","text":"…","model":"…","usage":{…}}
/// {"type":"question","questions":[{"header":"…","question":"…","multi_select":false,
///   "options":[{"label":"…","description":"…"}]}]}
/// {"type":"error","message":"…"}
/// ```
///
/// `question` is the one event that expects a reply. The driver answers with
/// one `{"type":"answer","text":"…"}` line per question, in order, and the turn
/// blocks until they arrive — which is why the tool is offered only to a driver
/// reading the protocol on stdin.
///
/// `thinking_delta` is **distinctly typed so a driver can drop it**, and the
/// harness-wide rule holds here as everywhere: reasoning never appears in
/// `result.text`, in the returned text, or in persisted history. A driver that
/// ignores every `thinking*` event reconstructs exactly what a terminal user
/// saw.
namespace apogee::commands {

/// Bumped only when an existing event's meaning changes.
///
/// Adding a new event type does **not** bump it: drivers are required to ignore
/// unknown types, so an addition is compatible by construction. The version
/// exists for the case that is not — a field changing meaning under a name a
/// driver already reads.
inline constexpr int kMachineProtocolVersion = 1;

class JsonReporter final : public agentloop::Reporter {
public:
    /// `out` receives the JSONL. It is stdout in practice, and must carry
    /// nothing else for the whole run.
    explicit JsonReporter(std::ostream& out);

    ~JsonReporter() override = default;
    JsonReporter(const JsonReporter&) = delete;
    JsonReporter& operator=(const JsonReporter&) = delete;
    JsonReporter(JsonReporter&&) = delete;
    JsonReporter& operator=(JsonReporter&&) = delete;

    /// Emits the opening `session` event. Call once, before the first turn.
    void begin_session(std::string_view model);

    void on_thinking() override;
    void on_thinking_token(std::string_view chunk) override;
    void on_tool_status(std::string_view detail) override;
    void on_clear_status() override;
    void on_answer_start() override;
    void on_answer_token(std::string_view chunk) override;
    void on_answer_end() override;

    /// Emits the terminal `result` event for one turn.
    ///
    /// Carries the answer text so a driver that dropped every delta still has
    /// the whole answer — the same reason `stream_chat` returns the complete
    /// response alongside streaming it.
    void emit_result(const harness::ChatResponse& response);

    /// Emits a `question` event: `ask_user`, over the protocol.
    ///
    /// The driving GUI renders a native dialog instead of the tool being
    /// silently unavailable — the alternative a terminal-only AskFn would
    /// force.
    void emit_question(const agentloop::QuestionRequest& request);

    /// Emits an `error` event. Diagnostics also go to stderr; this is the
    /// machine-readable half, so a driver need not scrape prose.
    void emit_error(std::string_view message);

    /// Whether any answer text was emitted this run.
    [[nodiscard]] bool wrote_answer() const noexcept;

private:
    void write(const std::string& line);

    std::ostream* out_;
    bool wrote_answer_ = false;
};

/// How a driver feeds turns in. `--input-format`.
///
/// Separate from `OutputFormat` because the two directions genuinely differ:
/// `text` in means plain lines from a human or a pipe, `text` out means
/// rendered for a terminal. It defaults to whatever `--output-format` is, so
/// the ordinary cases — a human, or a driver — need one flag rather than two.
///
/// On `chat` the two must agree, and disagreeing is an error rather than a
/// silently-ignored flag: a JSONL-emitting REPL has no coherent meaning (slash
/// commands have no protocol event), and a driven session rendering prose gives
/// its driver nothing to parse.
enum class InputFormat : std::uint8_t {
    /// One plain line per user turn.
    Text,
    /// One JSON object per line: user turns and answers to questions.
    StreamJson,
};

[[nodiscard]] std::string_view to_string(InputFormat format) noexcept;
[[nodiscard]] std::optional<InputFormat> input_format_from_string(std::string_view name) noexcept;

/// How a surface renders its output. `--output-format`.
enum class OutputFormat : std::uint8_t {
    /// Human-facing: the terminal renderer.
    Text,
    /// Machine-facing: one JSON object per line on stdout.
    StreamJson,
};

[[nodiscard]] std::string_view to_string(OutputFormat format) noexcept;
[[nodiscard]] std::optional<OutputFormat> output_format_from_string(std::string_view name) noexcept;

/// One user turn read from a driver over stdin, in machine mode.
struct DriverMessage {
    enum class Kind : std::uint8_t {
        /// A user turn to answer.
        User,
        /// An answer to a pending `ask_user` question.
        Answer,
        /// A line that parsed but carried no recognised type.
        Unknown,
    };

    Kind kind = Kind::Unknown;
    std::string text;
};

/// Parses one line of driver input.
///
/// Returns `Unknown` rather than failing for anything unrecognised: a driver
/// sending an event type this build does not know must not kill the session,
/// which is the same tolerance this protocol demands of drivers.
///
/// ```jsonl
/// {"type":"user","text":"what is 2+2?"}
/// {"type":"answer","text":"yes"}
/// ```
[[nodiscard]] DriverMessage parse_driver_line(std::string_view line);

/// An `AskFn` that asks the driver, over the protocol.
///
/// Offered **only** when the driver reads structured input: with plain-line
/// stdin an answer is indistinguishable from the next user turn, and the
/// loop's rule is that the tool exists if and only if there is someone to
/// answer it. Throws if stdin closes with a question outstanding — the loop
/// then rolls the half-turn out of history, which is the honest outcome.
[[nodiscard]] agentloop::AskFn make_driver_ask_fn(JsonReporter& reporter, std::istream& input);

}  // namespace apogee::commands
