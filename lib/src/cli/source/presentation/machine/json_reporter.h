#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/question.h"
#include "agentloop/reporter.h"
#include "contracts/types.h"
#include "machine/protocol.h"
#include "tasks/ledger.h"
#include "tasks/task.h"

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
/// A task run (`task run|resume --output-format stream-json`, 27j) adds its
/// lifecycle around those turns -- `task_started`, `task_plan`, `task_round`,
/// `task_grant`, `task_finished` -- one per transition its ledger writes,
/// each carrying that transition as the ledger wrote it. The task's events
/// are the one place this vocabulary is not a `Reporter` method: a task is
/// not a turn, and the runner is not the loop. They render the ledger, which
/// is the task's one source of truth.
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

/// What a session can do, announced on its `session` event (28d) so a host
/// knows before its first turn: the whole outbound vocabulary, the inbound
/// line types THIS session reads, whether tools are live, whether `ask_user`
/// (and the permission prompt) can reach the driver, and the vocabulary's
/// version. Built from what the surface actually wired, never a constant.
struct MachineCapabilities {
    /// The inbound types this session reads -- a driven chat all four, a
    /// one-shot `complete` reading its prompt on stdin only `hello`, a task
    /// none.
    std::vector<std::string_view> accepts;
    bool tools = false;
    bool ask = false;
};

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

    /// Emits the opening `session` event, with `capabilities` (28d). Call
    /// once, before the first turn -- unprompted: the child speaks first, and
    /// a driver's `hello` refines what it reads, never gates it. A session of
    /// `user` lines also names `next_turn` (28f): the number its next turn
    /// will carry, past those a resumed conversation already holds.
    void begin_session(std::string_view model, const MachineCapabilities& capabilities,
                       std::optional<std::int64_t> next_turn = std::nullopt);

    /// A `user` line was accepted as turn `turn` (28f): every event until
    /// `end_turn` carries `"turn": turn`, so a driver can attribute any event
    /// to its line by number alone; the answer streamed is kept, for a
    /// cancelled turn's `result`.
    void begin_turn(std::int64_t turn);
    /// The turn's `result` or `error` is out: events are unstamped again.
    void end_turn();
    /// The answer text the open turn has streamed so far.
    [[nodiscard]] const std::string& turn_text() const noexcept;

    void on_thinking() override;
    void on_thinking_token(std::string_view chunk) override;
    /// A `thinking` event carrying `budget_reached: true` (26i).
    void on_thinking_budget_reached() override;
    /// A `memory` event: the past chats and decisions a turn was handed (26l).
    void on_recall(int chats, int decisions) override;
    /// A side call, as the existing `tool_status` event: display prose,
    /// said when it starts (26n). No new event type.
    void on_side_call(const agentloop::SideCall& call) override;
    void on_tool_status(std::string_view detail) override;
    /// A `notice` event: `{"type":"notice","text":...}`.
    void on_notice(std::string_view text) override;
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

    /// A `question` event with `"kind": "permission"`, `tool` and `target`:
    /// the permission gate asking the driver. Answered like any question,
    /// with one `answer` line -- `yes`, `no`, `always`, or `session`. An
    /// outbound tool's target is a host, with `"outbound": true` and the URL
    /// as `detail`; its `always` adds the host to `tools.allowed_hosts`.
    void emit_permission_question(const agent::GateRequest& request);

    /// Emits an `error` event. Diagnostics also go to stderr; this is the
    /// machine-readable half, so a driver need not scrape prose.
    void emit_error(std::string_view message);

    /// A task's lifecycle (27j): the event the transition at `index` of
    /// `task`'s ledger is rendered as, carrying that transition exactly as
    /// the ledger wrote it (`transition`) -- `task_started` for `started` and
    /// `resumed`, with `resumed`, every transition before it (`history`) and
    /// the task's whole view (`task`), so a front-end that joins at a resume
    /// needs no other source; `task_plan` for `plan_started` and
    /// `plan_recorded`, with the plan turn (`round`) and, once recorded, the
    /// `plan`; `task_round` for `round_started` and `round_ended`, with the
    /// round and, once it has ended, each check's state (`checks`);
    /// `task_finished` for `finished`, with the `status`, the `reason` and
    /// the task's final view. `created` comes before any run, and reaches a
    /// driver in `task_started`'s history. `holder` is the task lock's holder
    /// now, which says whether a process runs the task. A view, never the
    /// ledger: a declared answer's text is in no event.
    void emit_task_transition(const tasks::Task& task, std::size_t index,
                              const std::optional<tasks::LockHolder>& holder);

    /// `task_grant`: a call the task's grant let through (27i) in `round` --
    /// the tool, its target and `by: grant`.
    void emit_task_grant(const tasks::Task& task, int round, const tasks::Permit& permit);

    /// Whether any answer text was emitted this run.
    [[nodiscard]] bool wrote_answer() const noexcept;

private:
    void write(nlohmann::json object);

    std::ostream* out_;
    bool wrote_answer_ = false;
    std::optional<std::int64_t> turn_;
    std::string turn_text_;
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

/// The words `--output-format` and `--input-format` take -- the two parsers
/// above accept the same pair -- for completion.
[[nodiscard]] std::vector<std::string_view> format_names();

/// How a read command renders: `--output-format` on a read (27j, the
/// convention 28h's reads join). A read prints one JSON document, never a
/// stream -- JSONL stays the turn surfaces' -- so its words are `text` and
/// `json`, and a turn's `stream-json` is refused on a read.
enum class ReadFormat : std::uint8_t {
    /// The human view.
    Text,
    /// One JSON document on stdout, the same facts as the human view, and
    /// nothing else there: diagnostics go to stderr, exit codes as ever.
    Json,
};

[[nodiscard]] std::string_view to_string(ReadFormat format) noexcept;
[[nodiscard]] std::optional<ReadFormat> read_format_from_string(std::string_view name) noexcept;
/// The words a read's `--output-format` takes, for its help and completion.
[[nodiscard]] std::vector<std::string_view> read_format_names();

/// Writes `document` to `out` as a read's machine face: one JSON document on
/// one line -- the bytes an admin route serves for the same read, then a
/// newline.
void write_document(std::ostream& out, const nlohmann::json& document);

/// One user turn read from a driver over stdin, in machine mode.
struct DriverMessage {
    enum class Kind : std::uint8_t {
        /// A user turn to answer.
        User,
        /// An answer to a pending `ask_user` question.
        Answer,
        /// A file, folder or glob to attach to the chat (26d); `text` is its
        /// path.
        Attach,
        /// A driver introducing itself (28d) -- its `client` name and version
        /// and what it `wants`, recorded for diagnostics; changes nothing.
        Hello,
        /// Stop the turn in flight, as Ctrl-C would (28f). Read by the
        /// session's stdin reader, never queued.
        Cancel,
        /// A line that parsed but carried no recognised type.
        Unknown,
    };

    Kind kind = Kind::Unknown;
    std::string text;
    /// An `attach` line's `graph` (27p): `code` or `off` as written, the
    /// attach's method over the config's; empty when the line has none. A
    /// value that is not a string arrives as its JSON, to be refused by name.
    std::string graph;
    /// A `hello` line's client (28d): `client.name` and `client.version` as
    /// sent, and `wants` as its JSON -- each empty when absent.
    std::string client_name;
    std::string client_version;
    std::string wants;
};

/// What a driver's `hello` is recorded as (28d): one line for the
/// operational log, never a value from the config or a secret -- the client's
/// own words, bounded.
[[nodiscard]] std::string describe_hello(const DriverMessage& hello);

/// Parses one line of driver input.
///
/// Returns `Unknown` rather than failing for anything unrecognised: a driver
/// sending an event type this build does not know must not kill the session,
/// which is the same tolerance this protocol demands of drivers.
///
/// ```jsonl
/// {"type":"user","text":"what is 2+2?"}
/// {"type":"answer","text":"yes"}
/// {"type":"attach","path":"report.pdf"}
/// {"type":"attach","path":"src","graph":"off"}
/// {"type":"hello","client":{"name":"my-host","version":"1.2"},"wants":["tools"]}
/// ```
[[nodiscard]] DriverMessage parse_driver_line(std::string_view line);

class DriverInput;

/// An `AskFn` that asks the driver, over the protocol.
///
/// Offered **only** when the driver reads structured input: with plain-line
/// stdin an answer is indistinguishable from the next user turn, and the
/// loop's rule is that the tool exists if and only if there is someone to
/// answer it. Throws if stdin closes with a question outstanding — the loop
/// then rolls the half-turn out of history, which is the honest outcome — and
/// throws `CancelledError` when the driver cancels the turn while it waits
/// (28f), failing the turn the same way without ending the session.
[[nodiscard]] agentloop::AskFn make_driver_ask_fn(JsonReporter& reporter, DriverInput& input);

}  // namespace apogee::commands
