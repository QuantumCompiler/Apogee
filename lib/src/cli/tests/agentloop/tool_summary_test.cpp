#include "agentloop/tool_summary.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// A large tool result, summarised by the utility model first (26b).
namespace {

using apogee::agentloop::kToolSummaryThreshold;
using apogee::agentloop::summarize_tool_result;
using apogee::agentloop::summary_request;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::ToolCall;

[[nodiscard]] ToolCall read_call() {
    return ToolCall{.id = "c1", .name = "read_file", .arguments = R"({"path":"big.log"})"};
}

/// A chat model that calls `read_file` once and then answers, and a helper
/// that summarises.
/// Keeps the progress lines a run reports.
struct Progress : apogee::agentloop::Reporter {
    std::vector<std::string> lines;

    void on_progress(std::string_view line) override {
        lines.emplace_back(line);
    }
};

struct Two {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    std::shared_ptr<MockProvider> chat;
    std::shared_ptr<MockProvider> helper;

    explicit Two(std::string summary) {
        MockProvider::Options chat_options;
        chat_options.backend_name = "chat";
        chat_options.turns = {
            MockTurn{"", {read_call()}, apogee::harness::FinishReason::ToolCalls, {}},
            MockTurn{.text = "done"}};
        chat = std::make_shared<MockProvider>(std::move(chat_options));
        MockProvider::Options helper_options;
        helper_options.backend_name = "helper";
        helper_options.turns = {MockTurn{.text = std::move(summary)}};
        helper = std::make_shared<MockProvider>(std::move(helper_options));
        harness.register_provider("chat", chat);
        harness.register_provider("helper", helper);
        harness.use_default_router();
    }
};

/// A registry whose `read_file` returns `bytes` bytes.
[[nodiscard]] apogee::agent::ToolRegistry registry_returning(std::size_t bytes) {
    apogee::agent::ToolRegistry registry;
    apogee::agent::Tool tool;
    tool.name = "read_file";
    tool.description = "read";
    tool.parameters_schema = R"({"type":"object"})";
    tool.run = [bytes](std::string_view) {
        return apogee::agent::ToolOutcome{std::string(bytes, 'l')};
    };
    registry.add(tool);
    return registry;
}

/// What the chat model was shown as the tool's result on its second call.
[[nodiscard]] std::string result_seen(const MockProvider& chat) {
    REQUIRE(chat.requests().size() == 2);
    return chat.requests()[1].messages.back().content.plain_text();
}

}  // namespace

TEST_CASE("the summary request names the tool and is a side request", "[agentloop][summary]") {
    const apogee::harness::ChatRequest request =
        summary_request("helper", read_call(), "line one\nline two");
    CHECK(request.model == "helper");
    CHECK(request.transient.side_request);
    REQUIRE(request.messages.size() == 1);
    const std::string prompt = request.messages.front().content.plain_text();
    CHECK(prompt.find("Tool: read_file") != std::string::npos);
    CHECK(prompt.find(R"(Arguments: {"path":"big.log"})") != std::string::npos);
    CHECK(prompt.find("line one\nline two") != std::string::npos);

    // A result larger than it is shown is said to be cut.
    const std::string huge(200 * 1024, 'h');
    const std::string cut =
        summary_request("helper", read_call(), huge).messages.front().content.plain_text();
    CHECK(cut.find("the rest of the output is not shown here") != std::string::npos);
    CHECK(cut.size() < huge.size());
}

TEST_CASE("a summary is headed by what was returned, who summarised it, and how to see more",
          "[agentloop][summary]") {
    Two two{"The log shows three errors at 09:14."};
    const std::optional<std::string> text =
        summarize_tool_result(two.harness, "helper", read_call(), std::string(40 * 1024, 'l'), {});
    REQUIRE(text.has_value());
    CHECK(text->starts_with(
        "[read_file returned 40 KB; this is a summary by helper, and the full output was not "
        "kept. To see a part of it, call read_file again with a line range.]\n\n"));
    CHECK(text->ends_with("The log shows three errors at 09:14."));

    // A summary that could not be had leaves the result as it was.
    CHECK_FALSE(summarize_tool_result(two.harness, "ghost", read_call(), "x", {}).has_value());
    Two blank{"  \n "};
    CHECK_FALSE(summarize_tool_result(blank.harness, "helper", read_call(), "x", {}).has_value());
}

TEST_CASE("the loop summarises a result over the threshold, only with a utility model set",
          "[agentloop][summary]") {
    const apogee::agent::ToolRegistry big = registry_returning(kToolSummaryThreshold + 1);

    SECTION("set: the chat model reads the summary") {
        Two two{"three errors"};
        std::vector<ChatMessage> history{ChatMessage::user("what went wrong?")};
        apogee::agentloop::Options options;
        options.model = "chat";
        options.tools = &big;
        options.summary_model = "helper";
        Progress progress;
        (void)apogee::agentloop::run(two.harness, history, options, progress);
        // Said, for --verbose: which tool, how big, and who summarised it.
        REQUIRE(progress.lines.size() == 1);
        CHECK(progress.lines.front() == "read_file's 9 KB result summarised by helper");
        CHECK(result_seen(*two.chat).find("this is a summary by helper") != std::string::npos);
        CHECK(result_seen(*two.chat).ends_with("three errors"));
        REQUIRE(two.helper->requests().size() == 1);
        CHECK(two.helper->requests().front().transient.side_request);
    }

    SECTION("unset: the result as the tool returned it, and no helper asked") {
        Two two{"never"};
        std::vector<ChatMessage> history{ChatMessage::user("what went wrong?")};
        apogee::agentloop::Options options;
        options.model = "chat";
        options.tools = &big;
        (void)apogee::agentloop::run(two.harness, history, options);
        CHECK(result_seen(*two.chat) == std::string(kToolSummaryThreshold + 1, 'l'));
        CHECK(two.helper->requests().empty());
    }

    SECTION("at the threshold: left alone") {
        const apogee::agent::ToolRegistry exact = registry_returning(kToolSummaryThreshold);
        Two two{"never"};
        std::vector<ChatMessage> history{ChatMessage::user("what went wrong?")};
        apogee::agentloop::Options options;
        options.model = "chat";
        options.tools = &exact;
        options.summary_model = "helper";
        (void)apogee::agentloop::run(two.harness, history, options);
        CHECK(two.helper->requests().empty());
    }

    SECTION("a helper that fails: the result as it was") {
        Two two{"unused"};
        std::vector<ChatMessage> history{ChatMessage::user("what went wrong?")};
        apogee::agentloop::Options options;
        options.model = "chat";
        options.tools = &big;
        options.summary_model = "ghost";
        (void)apogee::agentloop::run(two.harness, history, options);
        CHECK(result_seen(*two.chat) == std::string(kToolSummaryThreshold + 1, 'l'));
    }
}
