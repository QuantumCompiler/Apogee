#include "mcp/serve_stdio.h"

#include <istream>
#include <ostream>
#include <string>

namespace apogee::mcp {
namespace {

void write_frame(std::ostream& out, const nlohmann::json& frame) {
    out << frame.dump() << '\n';
    out.flush();
}

}  // namespace

int serve_stdio(std::istream& in, std::ostream& out, std::string_view server_name,
                std::string_view version, const std::vector<ServedTool>& tools,
                const Dispatch& dispatch) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() > kMaxFrameBytes) {
            continue;
        }
        const nlohmann::json frame = nlohmann::json::parse(line, nullptr, false);
        if (frame.is_discarded() || !frame.is_object()) {
            continue;  // like the bundled servers: junk is skipped, never fatal
        }
        const std::string method = frame.value("method", std::string{});
        const bool has_id = frame.contains("id") && !frame["id"].is_null();
        const nlohmann::json id = has_id ? frame["id"] : nlohmann::json{};
        if (method == "initialize") {
            if (has_id) {
                write_frame(
                    out, make_result(id, {{"protocolVersion", std::string{kProtocolVersion}},
                                          {"capabilities", {{"tools", nlohmann::json::object()}}},
                                          {"serverInfo",
                                           {{"name", std::string{server_name}},
                                            {"version", std::string{version}}}}}));
            }
        } else if (method == "notifications/initialized") {
            // A notification: no reply.
        } else if (method == "tools/list") {
            nlohmann::json list = nlohmann::json::array();
            for (const ServedTool& tool : tools) {
                list.push_back({{"name", tool.name},
                                {"description", tool.description},
                                {"inputSchema", tool.input_schema.is_null()
                                                    ? nlohmann::json{{"type", "object"}}
                                                    : tool.input_schema},
                                {"annotations", {{"readOnlyHint", tool.read_only}}}});
            }
            if (has_id) {
                write_frame(out, make_result(id, {{"tools", std::move(list)}}));
            }
        } else if (method == "tools/call") {
            const nlohmann::json params = frame.contains("params") && frame["params"].is_object()
                                              ? frame["params"]
                                              : nlohmann::json::object();
            const std::string name = params.value("name", std::string{});
            const nlohmann::json args =
                params.contains("arguments") && params["arguments"].is_object()
                    ? params["arguments"]
                    : nlohmann::json::object();
            const ToolCallResult result = dispatch(name, args);
            if (has_id) {
                write_frame(
                    out,
                    make_result(id, {{"content", nlohmann::json::array(
                                                     {{{"type", "text"}, {"text", result.text}}})},
                                     {"isError", result.is_error}}));
            }
        } else if (has_id) {
            write_frame(out, make_error(id, kMethodNotFound, "method not found: " + method));
        }
        // An unknown notification is ignored.
    }
    return 0;
}

std::vector<ServedTool> read_only_tools(const agent::ToolRegistry& registry) {
    std::vector<ServedTool> out;
    for (const std::string& name : registry.names()) {
        const agent::Tool* tool = registry.find(name);
        if (tool == nullptr || tool->writes) {
            continue;
        }
        ServedTool served;
        served.name = tool->name;
        served.description = tool->description;
        served.input_schema = nlohmann::json::parse(tool->parameters_schema, nullptr, false);
        if (served.input_schema.is_discarded()) {
            served.input_schema = nlohmann::json{{"type", "object"}};
        }
        served.read_only = true;
        out.push_back(std::move(served));
    }
    return out;
}

Dispatch dispatch_through(const agent::ToolRegistry& registry) {
    return [&registry](std::string_view name, const nlohmann::json& args) {
        const agent::Tool* tool = registry.find(name);
        ToolCallResult result;
        if (tool == nullptr || tool->writes) {
            result.is_error = true;
            result.text = "Error: no such tool '" + std::string{name} + "'";
            return result;
        }
        harness::ToolCall call;
        call.id = "stdio";
        call.name = std::string{name};
        call.arguments = args.dump();
        // No permission context: only read-only tools are served, and the
        // gate never consults it for those.
        const agent::ToolOutcome outcome =
            agent::dispatch(registry, call, agent::DispatchContext{});
        result.text = outcome.content;
        result.is_error = outcome.is_error;
        return result;
    };
}

}  // namespace apogee::mcp
