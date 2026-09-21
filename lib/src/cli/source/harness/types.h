#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The canonical message IR.
///
/// Every provider translates its own dialect to and from these types at the
/// backends boundary, and nothing above that boundary ever sees an
/// Anthropic block or an OpenAI choice. That is what "backend-agnostic core"
/// means concretely: the agent loop, the surfaces, and the session log are
/// written once against this IR, and adding a fifth provider changes none of
/// them.
///
/// The IR never grows provider-specific fields. When a provider needs
/// something nothing else has, it belongs in that backend's own config entry
/// or its adapter -- not here. The moment one vendor's concept appears in this
/// header, every other backend has to decide what to do about it.
namespace apogee::harness {

/// Who produced a message.
enum class Role : std::uint8_t { System, User, Assistant, Tool };

[[nodiscard]] std::string_view to_string(Role role) noexcept;
[[nodiscard]] std::optional<Role> role_from_string(std::string_view name) noexcept;

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------

/// One element of a multi-part message.
///
/// Mirrors the OpenAI content-part schema, which llama-server and several
/// vendors already accept, so the common case needs no translation at all.
struct ContentPart {
    enum class Kind : std::uint8_t { Text, ImageUrl };

    Kind kind = Kind::Text;
    std::string text;       ///< set when kind == Text
    std::string image_url;  ///< https://… or a data: URI; set when kind == ImageUrl
    std::string detail;     ///< optional "low" | "high" | "auto" for images

    [[nodiscard]] static ContentPart from_text(std::string value);
    [[nodiscard]] static ContentPart from_image_url(std::string url, std::string detail = {});
};

/// A message's content: either plain text or a sequence of typed parts.
///
/// Dual-mode from day one rather than "text now, parts when images land".
/// Retrofitting multimodal content into an IR that assumed a string means
/// touching every provider, every surface, and every persisted session file at
/// once -- Ommi's recorded reason for building it this way, and it serializes
/// as a bare JSON string in the common case so nothing pays for the option it
/// does not use.
class MessageContent {
public:
    MessageContent() = default;

    /// Plain text. Implicit on purpose: `msg.content = "hello"` should work.
    MessageContent(std::string text);  // NOLINT(google-explicit-constructor)
    MessageContent(const char* text);  // NOLINT(google-explicit-constructor)

    [[nodiscard]] static MessageContent from_parts(std::vector<ContentPart> parts);

    /// A string view of the content. For multi-part content the text parts are
    /// concatenated and non-text parts dropped -- the safe fallback for a
    /// provider that cannot accept images.
    [[nodiscard]] std::string plain_text() const;

    /// The parts, or an empty span for plain-text content.
    [[nodiscard]] const std::vector<ContentPart>& parts() const noexcept {
        return parts_;
    }

    /// Whether any non-text part is present. A provider without image support
    /// checks this to decide between degrading and refusing.
    [[nodiscard]] bool is_rich() const noexcept;

    /// Whether there is no content at all.
    [[nodiscard]] bool empty() const noexcept;

    friend bool operator==(const MessageContent& lhs, const MessageContent& rhs);

private:
    std::string text_;
    std::vector<ContentPart> parts_;
};

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

/// A callable offered to the model. OpenAI function-calling compatible.
struct Tool {
    std::string name;
    std::string description;
    /// JSON Schema for the arguments, as a JSON object. Held as text because
    /// it is passed through verbatim to every provider and never inspected
    /// here -- parsing it would only create a place to lose fidelity.
    std::string parameters_schema = "{}";
};

/// A tool invocation the model asked for.
struct ToolCall {
    std::string id;
    std::string name;
    /// JSON-encoded arguments, e.g. `{"query":"..."}`. Text for the same
    /// reason as Tool::parameters_schema.
    std::string arguments = "{}";
};

/// The outcome of running a tool, on its way back to the model.
///
/// A convenience over "assemble a Role::Tool message by hand": the tool_call_id
/// linkage is the part callers forget, and a tool result that does not name the
/// call it answers is silently dropped by several providers.
struct ToolResult {
    std::string tool_call_id;
    std::string name;
    std::string content;
    bool is_error = false;
};

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

/// One turn of a conversation.
struct ChatMessage {
    Role role = Role::User;
    MessageContent content;

    /// Set when role == Assistant and the model asked to call tools.
    std::vector<ToolCall> tool_calls;

    /// Set when role == Tool: the call this message answers.
    std::string tool_call_id;

    /// Optional tool name, for role == Tool.
    std::string name;

    [[nodiscard]] static ChatMessage system(MessageContent content);
    [[nodiscard]] static ChatMessage user(MessageContent content);
    [[nodiscard]] static ChatMessage assistant(MessageContent content);
    [[nodiscard]] static ChatMessage from_tool_result(const ToolResult& result);
};

/// A request as the harness understands it.
struct ChatRequest {
    std::vector<ChatMessage> messages;

    /// Backend key or model name; empty means "the configured default".
    std::string model;

    std::optional<double> temperature;
    std::optional<std::int64_t> max_tokens;
    std::vector<Tool> tools;

    /// Per-request, process-local state that must NEVER reach the wire or a
    /// session file.
    ///
    /// This is a nested struct with no JSON conversion deliberately: there is
    /// no `to_json` for it anywhere, so serializing it does not compile. The
    /// alternative -- remembering to exclude three fields in every serializer
    /// anyone ever writes -- is exactly the kind of rule that holds until it
    /// doesn't, and the failure is silent (a RAG blob quietly persisted into
    /// history, then re-sent on every later turn).
    struct Transient {
        /// Marks `messages[start, start + length)` as injected for this request
        /// only -- a per-turn RAG context pair, typically. Not part of the next
        /// request's history. A provider with a persistent prompt cache must
        /// not write these into its cached prefix.
        std::size_t start = 0;
        std::size_t length = 0;

        /// This request is not a turn of the bound chat session -- a background
        /// title summary, a one-off clerk call. A provider with a per-session
        /// cache must run it with no cache at all: its prompt shares no prefix
        /// with the conversation, so a write-back would clobber the session's
        /// state, and it may run concurrently with a real turn.
        bool side_request = false;

        /// When set, asks the provider to constrain its answer to this JSON
        /// Schema. Providers that cannot leave it alone and the caller's
        /// prose-parsing fallback applies.
        std::string response_schema;

        [[nodiscard]] bool has_region() const noexcept {
            return length > 0;
        }
    };

    Transient transient;

    /// Whether `index` falls inside the transient region.
    [[nodiscard]] bool is_transient(std::size_t index) const noexcept;

    /// The messages that belong in persisted history -- everything outside the
    /// transient region, in order.
    [[nodiscard]] std::vector<ChatMessage> durable_messages() const;
};

/// Why a response stopped.
enum class FinishReason : std::uint8_t { Stop, Length, ToolCalls, ContentFilter, Cancelled, Other };

[[nodiscard]] std::string_view to_string(FinishReason reason) noexcept;
[[nodiscard]] std::optional<FinishReason> finish_reason_from_string(std::string_view name) noexcept;

/// Token accounting for one turn. Zero means "the provider did not report it",
/// which is not the same as "no tokens were used" -- callers that display these
/// should say nothing rather than "0" when a provider stays silent.
struct Usage {
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;

    [[nodiscard]] std::int64_t total_tokens() const noexcept {
        return prompt_tokens + completion_tokens;
    }

    [[nodiscard]] bool reported() const noexcept {
        return prompt_tokens > 0 || completion_tokens > 0;
    }
};

struct ChatResponse {
    ChatMessage message;
    FinishReason finish_reason = FinishReason::Stop;
    Usage usage;
    /// The model that actually served the request, which may differ from what
    /// was asked for (an alias resolved to a pinned id).
    std::string model;
};

/// One model a provider can serve.
struct ModelInfo {
    std::string id;
    std::string name;
    std::string provider;  ///< the backend type: "anthropic", "llamacpp", …
    std::string backend;   ///< the config entry it came from
};

// ---------------------------------------------------------------------------
// Observability
// ---------------------------------------------------------------------------

/// Metadata about one RAG retrieval, carried alongside injected context.
struct RAGMeta {
    std::string db;
    int chunks_found = 0;
    /// Cosine similarity for the vector retriever, normalized BM25 for
    /// lexical. **The two scales are not comparable** -- check `retriever`
    /// before interpreting this.
    double top_score = 0.0;
    std::string retriever;  ///< "vector" | "lexical" | "hybrid"
    /// When true, a generation model reordered these results, so `top_score`
    /// is the retrieval score of whatever the judge ranked first -- NOT the
    /// highest retrieval score in the set, and not a measure of match quality.
    bool reranked = false;
    int graph_entities = 0;
};

/// A point-in-time activity, rendered by the terminal status line and (later)
/// emitted as an HTTP meta-frame. One vocabulary for both, so a surface never
/// invents its own progress taxonomy.
struct StatusEvent {
    enum class Type : std::uint8_t {
        ModelLoading,
        ModelReady,
        Thinking,
        RagSearch,
        RagResult,
        ToolCall,
        TokenCount,
        ContextWarning,
    };
    enum class Phase : std::uint8_t { Start, Done, Error };

    Type type = Type::Thinking;
    Phase phase = Phase::Start;
    std::string name;    ///< model, tool, or store name
    std::string detail;  ///< free text; the error message when phase == Error

    std::optional<RAGMeta> rag;
    std::optional<std::int64_t> tokens;
    std::optional<double> tokens_per_second;
    std::optional<std::int64_t> used_tokens;
    std::optional<std::int64_t> context_size;
    std::optional<double> usage_percent;
};

[[nodiscard]] std::string_view to_string(StatusEvent::Type type) noexcept;
[[nodiscard]] std::string_view to_string(StatusEvent::Phase phase) noexcept;

// ---------------------------------------------------------------------------
// JSON (found by ADL)
// ---------------------------------------------------------------------------

void to_json(nlohmann::json& out, const ContentPart& value);
void from_json(const nlohmann::json& in, ContentPart& value);
void to_json(nlohmann::json& out, const MessageContent& value);
void from_json(const nlohmann::json& in, MessageContent& value);
void to_json(nlohmann::json& out, const Tool& value);
void from_json(const nlohmann::json& in, Tool& value);
void to_json(nlohmann::json& out, const ToolCall& value);
void from_json(const nlohmann::json& in, ToolCall& value);
void to_json(nlohmann::json& out, const ChatMessage& value);
void from_json(const nlohmann::json& in, ChatMessage& value);
void to_json(nlohmann::json& out, const ChatRequest& value);
void from_json(const nlohmann::json& in, ChatRequest& value);
void to_json(nlohmann::json& out, const ChatResponse& value);
void from_json(const nlohmann::json& in, ChatResponse& value);
void to_json(nlohmann::json& out, const ModelInfo& value);
void from_json(const nlohmann::json& in, ModelInfo& value);

bool operator==(const ContentPart& lhs, const ContentPart& rhs);
bool operator==(const ToolCall& lhs, const ToolCall& rhs);
bool operator==(const ChatMessage& lhs, const ChatMessage& rhs);

}  // namespace apogee::harness
