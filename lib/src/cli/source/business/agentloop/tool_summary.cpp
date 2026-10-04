#include "agentloop/tool_summary.h"

#include <exception>
#include <string_view>

#include "contracts/errors.h"

namespace apogee::agentloop {
namespace {

/// The most of a result the utility model is shown. The tools cap their own
/// output well below this; the cap is for a tool that does not.
constexpr std::size_t kLargestShown = std::size_t{96} * 1024;

/// The summary's budget.
constexpr std::int64_t kSummaryTokens = 1024;

constexpr std::string_view kPrompt =
    "Summarise this tool output for someone who will act on it. Keep exact names, numbers, "
    "paths, identifiers and error messages, and quote any lines the task is likely to need. "
    "Say briefly what kinds of content you left out. Reply with the summary only.";

/// How the chat model gets back what the summary dropped, for `tool`.
[[nodiscard]] std::string how_to_see_more(std::string_view tool) {
    if (tool == "read_file") {
        return "call read_file again with a line range";
    }
    if (tool == "fetch_url") {
        return "call fetch_url again with the offset it gave";
    }
    return "call " + std::string{tool} + " again, narrower";
}

/// `bytes` as "40 KB" (rounded up), or "N bytes" below one.
[[nodiscard]] std::string size_of(std::size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " bytes";
    }
    return std::to_string((bytes + 1023) / 1024) + " KB";
}

}  // namespace

harness::ChatRequest summary_request(const std::string& backend, const harness::ToolCall& call,
                                     const std::string& content) {
    std::string shown = content.substr(0, kLargestShown);
    if (shown.size() < content.size()) {
        shown += "\n[... the rest of the output is not shown here]";
    }
    harness::ChatRequest request;
    request.model = backend;
    request.messages.push_back(
        harness::ChatMessage::user(std::string{kPrompt} + "\n\nTool: " + call.name +
                                   "\nArguments: " + call.arguments + "\n\nOutput:\n" + shown));
    request.max_tokens = kSummaryTokens;
    request.temperature = 0.0;
    // Not a turn of the conversation: the chat's cache is untouched, and a
    // local utility model runs it on its own context.
    request.transient.side_request = true;
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

std::optional<std::string> summarize_tool_result(const harness::Harness& harness,
                                                 const std::string& backend,
                                                 const harness::ToolCall& call,
                                                 const std::string& content,
                                                 const harness::CancellationToken& cancellation) {
    std::string summary;
    try {
        summary = harness.chat(summary_request(backend, call, content), cancellation)
                      .message.content.plain_text();
    } catch (const harness::CancelledError&) {
        throw;
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (summary.find_first_not_of(" \t\r\n") == std::string::npos) {
        return std::nullopt;
    }
    return "[" + call.name + " returned " + size_of(content.size()) + "; this is a summary by " +
           backend + ", and the full output was not kept. To see a part of it, " +
           how_to_see_more(call.name) + ".]\n\n" + summary;
}

}  // namespace apogee::agentloop
