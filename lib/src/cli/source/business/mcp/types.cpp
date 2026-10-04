#include "mcp/types.h"

namespace apogee::mcp {

std::optional<IncomingMessage> parse_incoming(std::string_view line) {
    const nlohmann::json object = nlohmann::json::parse(line, nullptr, false);
    if (object.is_discarded() || !object.is_object()) {
        return std::nullopt;
    }
    IncomingMessage message;
    if (const auto id = object.find("id"); id != object.end() && !id->is_null()) {
        if (id->is_number_integer()) {
            message.id = id->get<std::int64_t>();
        } else if (id->is_string()) {
            // A string id is legal JSON-RPC; this client never sends one, so
            // it cannot match a pending call -- but a server request could.
            try {
                message.id = std::stoll(id->get<std::string>());
            } catch (const std::exception&) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    message.method = object.value("method", std::string{});
    if (const auto params = object.find("params"); params != object.end()) {
        message.params = *params;
    }
    if (const auto result = object.find("result"); result != object.end()) {
        message.result = *result;
    }
    if (const auto error = object.find("error"); error != object.end() && error->is_object()) {
        RpcError rpc;
        rpc.code = error->value("code", 0);
        rpc.message = error->value("message", std::string{});
        message.error = rpc;
    }
    return message;
}

nlohmann::json make_request(std::int64_t id, std::string_view method, nlohmann::json params) {
    return nlohmann::json{{"jsonrpc", std::string{kJsonRpcVersion}},
                          {"id", id},
                          {"method", std::string{method}},
                          {"params", std::move(params)}};
}

nlohmann::json make_notification(std::string_view method, nlohmann::json params) {
    return nlohmann::json{{"jsonrpc", std::string{kJsonRpcVersion}},
                          {"method", std::string{method}},
                          {"params", std::move(params)}};
}

nlohmann::json make_result(const nlohmann::json& id, nlohmann::json result) {
    return nlohmann::json{
        {"jsonrpc", std::string{kJsonRpcVersion}}, {"id", id}, {"result", std::move(result)}};
}

nlohmann::json make_error(const nlohmann::json& id, int code, std::string_view message) {
    return nlohmann::json{{"jsonrpc", std::string{kJsonRpcVersion}},
                          {"id", id},
                          {"error", {{"code", code}, {"message", std::string{message}}}}};
}

nlohmann::json initialize_params(std::string_view client_version) {
    return nlohmann::json{
        {"protocolVersion", std::string{kProtocolVersion}},
        {"capabilities", {{"tools", nlohmann::json::object()}}},
        {"clientInfo",
         {{"name", std::string{kClientName}}, {"version", std::string{client_version}}}}};
}

std::vector<ToolInfo> parse_tools_list(const nlohmann::json& result) {
    std::vector<ToolInfo> tools;
    const auto list = result.find("tools");
    if (list == result.end() || !list->is_array()) {
        return tools;
    }
    for (const nlohmann::json& entry : *list) {
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
            continue;
        }
        ToolInfo tool;
        tool.name = entry["name"].get<std::string>();
        tool.description = entry.value("description", std::string{});
        if (const auto schema = entry.find("inputSchema");
            schema != entry.end() && schema->is_object()) {
            tool.input_schema = *schema;
        } else {
            tool.input_schema = nlohmann::json{{"type", "object"}};
        }
        if (const auto annotations = entry.find("annotations");
            annotations != entry.end() && annotations->is_object()) {
            if (const auto hint = annotations->find("readOnlyHint");
                hint != annotations->end() && hint->is_boolean()) {
                tool.read_only_hint = hint->get<bool>();
            }
        }
        tools.push_back(std::move(tool));
    }
    return tools;
}

ToolCallResult parse_tool_call(const nlohmann::json& result) {
    ToolCallResult out;
    out.is_error = result.value("isError", false);
    if (const auto content = result.find("content");
        content != result.end() && content->is_array()) {
        for (const nlohmann::json& block : *content) {
            if (block.is_object() && block.value("type", std::string{}) == "text" &&
                block.contains("text") && block["text"].is_string()) {
                // Text blocks only; image and resource blocks are preserved
                // on the wire and skipped here, as Ommi's client did.
                out.text += block["text"].get<std::string>();
            }
        }
    }
    return out;
}

std::string namespaced_name(std::string_view server, std::string_view tool) {
    return "mcp__" + std::string{server} + "__" + std::string{tool};
}

bool split_namespaced(std::string_view name, std::string& server, std::string& tool) {
    constexpr std::string_view kPrefix = "mcp__";
    if (!name.starts_with(kPrefix)) {
        return false;
    }
    const std::string_view rest = name.substr(kPrefix.size());
    const std::size_t split = rest.find("__");
    if (split == std::string_view::npos || split == 0 || split + 2 >= rest.size()) {
        return false;
    }
    server = std::string{rest.substr(0, split)};
    tool = std::string{rest.substr(split + 2)};
    return true;
}

}  // namespace apogee::mcp
