#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// The wire: JSON-RPC 2.0 framing and the MCP payloads this client speaks.
///
/// Hand-rolled, as Ommi's was -- the protocol subset is four methods, and a
/// dependency for it would be a dependency for nothing. Ids are integers the
/// client allocates; a server-initiated notification has no id, which is how
/// the read loop tells the two apart before it decodes anything else.
namespace apogee::mcp {

inline constexpr std::string_view kJsonRpcVersion = "2.0";
/// What this client asks for. A server may answer with its own; whatever it
/// says is recorded and accepted -- every server in the wild speaks either
/// this or `2024-11-05`, and the difference is `annotations`, which the
/// permission gate reads when present and ignores when not.
inline constexpr std::string_view kProtocolVersion = "2025-03-26";
inline constexpr std::string_view kClientName = "apogee";

/// The largest frame either side will hold; a line that never ends is a
/// peer that has gone wrong, and the bytes are dropped rather than grown.
inline constexpr std::size_t kMaxFrameBytes = 1024 * 1024;

inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kParseError = -32700;

struct RpcError {
    int code = 0;
    std::string message;
};

/// A decoded incoming frame: a response (id set) or a notification (no id).
struct IncomingMessage {
    std::optional<std::int64_t> id;
    /// Set for a notification or request from the server.
    std::string method;
    nlohmann::json params;
    nlohmann::json result;
    std::optional<RpcError> error;

    [[nodiscard]] bool is_notification() const noexcept {
        return !id.has_value();
    }
};

/// Parses one line. Nullopt for anything that is not a JSON-RPC object --
/// a malformed frame is skipped, never fatal.
[[nodiscard]] std::optional<IncomingMessage> parse_incoming(std::string_view line);

[[nodiscard]] nlohmann::json make_request(std::int64_t id, std::string_view method,
                                          nlohmann::json params);
[[nodiscard]] nlohmann::json make_notification(std::string_view method,
                                               nlohmann::json params = nlohmann::json::object());
[[nodiscard]] nlohmann::json make_result(const nlohmann::json& id, nlohmann::json result);
[[nodiscard]] nlohmann::json make_error(const nlohmann::json& id, int code,
                                        std::string_view message);

/// One tool as a server advertises it. `input_schema` is kept as raw JSON:
/// it passes straight into `agent::Tool::parameters_schema`, and the client
/// has no reason to understand it.
struct ToolInfo {
    std::string name;
    std::string description;
    nlohmann::json input_schema;
    /// `annotations.readOnlyHint`, when the server sent one. Absent means
    /// unknown -- and unknown is treated as destructive by the gate.
    std::optional<bool> read_only_hint;
};

/// What `tools/call` returned: the text blocks joined, and whether the server
/// flagged it as an error.
struct ToolCallResult {
    std::string text;
    bool is_error = false;
};

[[nodiscard]] nlohmann::json initialize_params(std::string_view client_version);
[[nodiscard]] std::vector<ToolInfo> parse_tools_list(const nlohmann::json& result);
[[nodiscard]] ToolCallResult parse_tool_call(const nlohmann::json& result);

/// `mcp__<server>__<tool>` -- double underscore, because tool names contain
/// single ones. `split_namespaced` splits on the FIRST `__` after the prefix.
[[nodiscard]] std::string namespaced_name(std::string_view server, std::string_view tool);
[[nodiscard]] bool split_namespaced(std::string_view name, std::string& server, std::string& tool);

}  // namespace apogee::mcp
