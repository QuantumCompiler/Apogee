#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "harness/cancellation.h"
#include "harness/types.h"
#include "httpserver/sse_reporter.h"
#include "httpserver/sse_writer.h"

/// SSE framing and the Reporter → frame adapter, on their own.
namespace {

using apogee::harness::StatusEvent;
using apogee::httpserver::chat_chunk;
using apogee::httpserver::meta_chunk;
using apogee::httpserver::sse_frame;
using apogee::httpserver::SseReporter;
using apogee::httpserver::SseWriter;
using apogee::httpserver::status_event_json;
using apogee::httpserver::StreamIdentity;
using apogee::httpserver::tool_name_from_status;

StreamIdentity identity() {
    return StreamIdentity{"chatcmpl-test", "mock", 1};
}

/// Every frame a writer produced, parsed. `[DONE]` is recorded separately.
struct Captured {
    std::string bytes;
    std::vector<nlohmann::json> frames;
    bool done = false;

    apogee::httpserver::WriteFn writer() {
        return [this](std::string_view piece) {
            bytes += piece;
            std::string block{piece};
            REQUIRE(block.rfind("data: ", 0) == 0);
            REQUIRE(block.substr(block.size() - 2) == "\n\n");
            const std::string payload = block.substr(6, block.size() - 8);
            if (payload == "[DONE]") {
                done = true;
                return true;
            }
            const nlohmann::json json = nlohmann::json::parse(payload, nullptr, false);
            REQUIRE_FALSE(json.is_discarded());
            frames.push_back(json);
            return true;
        };
    }
};

}  // namespace

TEST_CASE("a frame is data, one JSON object, and a blank line", "[httpserver][sse]") {
    const std::string frame = sse_frame(nlohmann::json{{"a", "line one\nline two"}});
    CHECK(frame.rfind("data: ", 0) == 0);
    CHECK(frame.substr(frame.size() - 2) == "\n\n");
    // The newline inside the string is escaped, so the frame's only raw
    // newlines are its terminator: a client's parser splits on exactly that.
    const std::string body = frame.substr(6, frame.size() - 8);
    CHECK(body.find('\n') == std::string::npos);
    CHECK(nlohmann::json::parse(body)["a"] == "line one\nline two");
    CHECK(apogee::httpserver::kSseDone == "data: [DONE]\n\n");
}

TEST_CASE("a status event serializes its optional fields only when set", "[httpserver][sse]") {
    StatusEvent bare;
    bare.type = StatusEvent::Type::Thinking;
    bare.phase = StatusEvent::Phase::Start;
    const nlohmann::json minimal = status_event_json(bare);
    CHECK(minimal["type"] == "thinking");
    CHECK(minimal["phase"] == "start");
    CHECK_FALSE(minimal.contains("name"));
    CHECK_FALSE(minimal.contains("tokens"));
    CHECK_FALSE(minimal.contains("collection"));

    StatusEvent rag;
    rag.type = StatusEvent::Type::RagResult;
    rag.phase = StatusEvent::Phase::Done;
    rag.name = "notes";
    apogee::harness::RAGMeta meta;
    meta.db = "notes";
    meta.chunks_found = 3;
    meta.top_score = 0.42;
    meta.retriever = "hybrid";
    meta.reranked = true;
    rag.rag = meta;
    const nlohmann::json full = status_event_json(rag);
    CHECK(full["name"] == "notes");
    CHECK(full["chunks_found"] == 3);
    CHECK(full["top_score"] == 0.42);
    CHECK(full["retriever"] == "hybrid");
    CHECK(full["reranked"] == true);
    CHECK_FALSE(full.contains("graph_entities"));

    StatusEvent count;
    count.type = StatusEvent::Type::TokenCount;
    count.tokens = 42;
    count.tokens_per_second = 7.5;
    const nlohmann::json counted = status_event_json(count);
    CHECK(counted["tokens"] == 42);
    CHECK(counted["tokens_per_second"] == 7.5);
    CHECK_FALSE(counted.contains("used_tokens"));
}

TEST_CASE("a chunk carries a null finish reason until the last one", "[httpserver][sse]") {
    const nlohmann::json middle = chat_chunk(identity(), nlohmann::json{{"content", "x"}}, {});
    CHECK(middle["choices"][0]["finish_reason"].is_null());
    CHECK(middle["choices"][0]["delta"]["content"] == "x");
    CHECK(middle["object"] == "chat.completion.chunk");
    const nlohmann::json last = chat_chunk(identity(), nlohmann::json::object(), "stop");
    CHECK(last["choices"][0]["finish_reason"] == "stop");

    StatusEvent event;
    event.type = StatusEvent::Type::ToolCall;
    event.name = "echo";
    const nlohmann::json meta = meta_chunk(identity(), event);
    // The contract: a meta-frame rides an EMPTY delta.
    CHECK(meta["choices"][0]["delta"]["content"] == "");
    CHECK(meta["meta"]["type"] == "tool_call");
    CHECK(meta["meta"]["name"] == "echo");
}

TEST_CASE("a failed write closes the writer and cancels the turn", "[httpserver][sse]") {
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    int writes = 0;
    SseWriter writer{[&writes](std::string_view) { return ++writes < 2; }, token};

    CHECK(writer.send(nlohmann::json{{"n", 1}}));
    CHECK_FALSE(token.stop_requested());
    // The second write fails: the client is gone.
    CHECK_FALSE(writer.send(nlohmann::json{{"n", 2}}));
    CHECK(writer.closed());
    CHECK(token.stop_requested());
    // Nothing further is attempted.
    CHECK_FALSE(writer.send(nlohmann::json{{"n", 3}}));
    CHECK(writes == 2);
    CHECK(writer.frames_sent() == 1);
}

TEST_CASE("the tool name is the first word after the loop's tag", "[httpserver][sse]") {
    CHECK(tool_name_from_status("[tool] fetch_url https://example.test") == "fetch_url");
    CHECK(tool_name_from_status("[tool] echo") == "echo");
    CHECK(tool_name_from_status("echo something") == "echo");
    CHECK(tool_name_from_status("") == "");
}

TEST_CASE("the reporter turns loop events into frames in the order a client expects",
          "[httpserver][sse][reporter]") {
    Captured captured;
    const apogee::harness::CancellationToken token;
    SseWriter writer{captured.writer(), token};
    SseReporter reporter{writer, identity(), SseReporter::Options{.emit_events = true}};

    reporter.on_thinking();
    reporter.on_thinking_token("private working");
    reporter.on_tool_status("[tool] echo {}");
    reporter.on_thinking();  // the loop's "back to rest" after the tool
    reporter.on_thinking();  // the top of the next iteration
    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("Hello");
    reporter.on_answer_token("");
    reporter.on_answer_token(", world");
    reporter.on_answer_end();

    std::vector<std::string> sequence;
    for (const nlohmann::json& frame : captured.frames) {
        if (frame.contains("meta")) {
            sequence.push_back(frame["meta"]["type"].get<std::string>() + "/" +
                               frame["meta"]["phase"].get<std::string>());
        } else {
            sequence.push_back("content:" +
                               frame["choices"][0]["delta"]["content"].get<std::string>());
        }
    }
    const std::vector<std::string> expected{"thinking/start", "tool_call/start", "tool_call/done",
                                            "thinking/start", "content:Hello",   "content:, world"};
    CHECK(sequence == expected);
    // The empty token wrote nothing -- and in particular did not END anything.
    // `[DONE]` is the handler's to send; the reporter must never send it, and
    // an empty chunk is the one place a careless implementation would.
    CHECK_FALSE(captured.done);
    CHECK(reporter.tools_used() == std::vector<std::string>{"echo"});
    CHECK(reporter.answer() == "Hello, world");
    CHECK(reporter.emitted_answer());
    // Reasoning never became a frame of any kind.
    CHECK(captured.bytes.find("private working") == std::string::npos);
}

TEST_CASE("without the opt-in, only content frames are written", "[httpserver][sse][reporter]") {
    Captured captured;
    const apogee::harness::CancellationToken token;
    SseWriter writer{captured.writer(), token};
    SseReporter reporter{writer, identity(), SseReporter::Options{.emit_events = false}};

    reporter.on_thinking();
    reporter.on_tool_status("[tool] echo");
    reporter.on_thinking();
    reporter.on_answer_start();
    reporter.on_answer_token("hi");
    reporter.emit_meta(StatusEvent{});

    REQUIRE(captured.frames.size() == 1);
    CHECK_FALSE(captured.frames[0].contains("meta"));
    CHECK(captured.frames[0]["choices"][0]["delta"]["content"] == "hi");
    // The bookkeeping still happened: the final chunk names the tools.
    CHECK(reporter.tools_used() == std::vector<std::string>{"echo"});
}

TEST_CASE("model_ready is emitted once, on the first sign the model answered",
          "[httpserver][sse][reporter]") {
    Captured captured;
    const apogee::harness::CancellationToken token;
    SseWriter writer{captured.writer(), token};
    SseReporter reporter{writer, identity(),
                         SseReporter::Options{.emit_events = true, .model_loading_pending = true}};

    reporter.on_thinking();
    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("x");

    int ready = 0;
    for (const nlohmann::json& frame : captured.frames) {
        if (frame.contains("meta") && frame["meta"]["type"] == "model_ready") {
            ++ready;
            CHECK(frame["meta"]["phase"] == "done");
            CHECK(frame["meta"]["name"] == "mock");
        }
    }
    CHECK(ready == 1);
}
