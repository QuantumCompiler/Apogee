#include "backends/mlx_protocol.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "transport/jsonl_framer.h"

/// The MLX protocol (27a), this side's half: golden lines both directions.
///
/// What this side writes for a known request is pinned byte for byte, and
/// what it makes of a recorded driver stream is pinned event by event -- at
/// every chunk size, through the one framer, since a pipe hands over bytes
/// in whatever sizes it likes and a line split between two reads must cost
/// nothing. The driver's half of the same lines is held by its stub-module
/// suite (`mlx_driver_test.cpp`).
namespace {

using apogee::backends::mlx::Event;
using apogee::backends::mlx::GenerateRequest;

const std::filesystem::path kFixtures = std::filesystem::path{APOGEE_TEST_FIXTURES} / "mlx";

std::string read(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(line);
    }
    return out;
}

GenerateRequest golden_request() {
    GenerateRequest request;
    request.id = 7;
    request.messages.push_back(apogee::harness::ChatMessage::system("Be terse."));
    request.messages.push_back(apogee::harness::ChatMessage::user("Read notes.txt"));
    apogee::harness::ChatMessage call = apogee::harness::ChatMessage::assistant("");
    call.tool_calls.push_back(
        {.id = "call1", .name = "read_file", .arguments = R"({"path":"notes.txt"})"});
    request.messages.push_back(call);
    request.messages.push_back(apogee::harness::ChatMessage::from_tool_result(
        {.tool_call_id = "call1", .name = "read_file", .content = "milk", .is_error = false}));
    request.messages.push_back(apogee::harness::ChatMessage::user("thanks"));
    request.tools.push_back(
        {.name = "read_file",
         .description = "Read a file",
         .parameters_schema = R"({"type":"object","properties":{"path":{"type":"string"}}})"});
    request.sampling.temperature.value = 0.6;
    request.sampling.top_p.value = 0.9;
    request.sampling.seed = 42;
    request.max_tokens = 512;
    request.stop = {"</s>"};
    request.thinking = false;
    return request;
}

/// The events a stream yields when it arrives `chunk` bytes at a time.
std::vector<std::string> events_at(const std::string& stream, std::size_t chunk) {
    apogee::backends::JsonlFramer framer;
    std::vector<std::string> out;
    const auto on_line = [&out](std::string_view line) {
        if (const auto event = apogee::backends::mlx::parse_event(line)) {
            out.push_back(apogee::backends::mlx::describe(*event));
        }
    };
    for (std::size_t at = 0; at < stream.size(); at += chunk) {
        framer.feed(std::string_view{stream}.substr(at, chunk), on_line);
    }
    framer.flush(on_line);
    return out;
}

}  // namespace

TEST_CASE("a generate request is the golden line, and a cancel its own",
          "[backends][mlx][protocol]") {
    const std::vector<std::string> golden = lines_of(read(kFixtures / "generate_request.jsonl"));
    REQUIRE(golden.size() == 2);
    // The template shapes: a call's arguments as an object, a tool's schema
    // as one, the tool result naming the call it answers.
    CHECK(apogee::backends::mlx::generate_line(golden_request()) == golden[0]);
    CHECK(apogee::backends::mlx::cancel_line(8) == golden[1]);
}

TEST_CASE("text that is not JSON crosses as the string it is", "[backends][mlx][protocol]") {
    GenerateRequest request;
    apogee::harness::ChatMessage call = apogee::harness::ChatMessage::assistant("");
    call.tool_calls.push_back({.id = "c", .name = "f", .arguments = "not json"});
    request.messages.push_back(call);
    request.tools.push_back({.name = "f", .description = "", .parameters_schema = "{oops"});
    const std::string line = apogee::backends::mlx::generate_line(request);
    CHECK(line.find(R"("arguments":"not json")") != std::string::npos);
    // A schema that is not an object becomes an empty one: a template walks it.
    CHECK(line.find(R"("parameters":{})") != std::string::npos);
    CHECK(line.find(R"("seed":null)") != std::string::npos);
    CHECK(line.find(R"("session":true)") != std::string::npos);
}

TEST_CASE("the driver's stream parses to the same events at every chunk size",
          "[backends][mlx][protocol][framing]") {
    const std::string stream = read(kFixtures / "driver_stream.jsonl");
    const std::vector<std::string> expected{
        "ready gpt_oss",
        "#7 reasoning 'The user wants the file.'",
        "#7 text 'Reading it \xE2\x80\x94 now.'",
        // The stray line and the newer driver's unknown type are dropped, and
        // so is a call with no name: none carries anything to act on.
        R"(#7 tool_call read_file {"path":"notes.txt"})",
        "#7 done tool_calls 120/96/31",
        "error load: could not load /m: ValueError: Model type gemma9 not supported.",
    };
    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{3}, std::size_t{7}, std::size_t{4096}, stream.size()}) {
        INFO("chunk " << chunk);
        CHECK(events_at(stream, chunk) == expected);
    }
}

TEST_CASE("a ready line says what the driver found", "[backends][mlx][protocol]") {
    const auto ready = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"model_type":"qwen3_vl","chat_template":true,)"
        R"("tool_parser":"json_tools","thinking":true,"mlx_lm":"0.32.0"})");
    REQUIRE(ready.has_value());
    CHECK(ready->kind == Event::Kind::Ready);
    CHECK(ready->protocol == 1);
    CHECK(ready->model_type == "qwen3_vl");
    CHECK(ready->chat_template);
    CHECK(ready->tool_parser == "json_tools");
    CHECK(ready->thinking);
    CHECK(ready->mlx_lm_version == "0.32.0");
    CHECK_FALSE(ready->id.has_value());
    // A driver from before 27c says nothing of vision: it reads no images.
    CHECK_FALSE(ready->vision);
    CHECK(ready->mlx_vlm_version.empty());

    const auto vision = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"model_type":"qwen3_vl","chat_template":true,)"
        R"("tool_parser":null,"thinking":false,"mlx_lm":"0.32.0","vision":true,)"
        R"("mlx_vlm":"0.3.9"})");
    REQUIRE(vision.has_value());
    CHECK(vision->vision);
    CHECK(vision->mlx_vlm_version == "0.3.9");
    // A text driver's own word, null and false, reads as no.
    const auto text = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"vision":false,"mlx_vlm":null})");
    REQUIRE(text.has_value());
    CHECK_FALSE(text->vision);
    CHECK(text->mlx_vlm_version.empty());

    CHECK_FALSE(apogee::backends::mlx::parse_event("[1,2]").has_value());
    CHECK_FALSE(apogee::backends::mlx::parse_event("").has_value());
}

TEST_CASE("a done line's finish maps onto the IR's reasons", "[backends][mlx][protocol]") {
    using apogee::backends::mlx::finish_reason;
    using apogee::harness::FinishReason;
    CHECK(finish_reason("stop") == FinishReason::Stop);
    CHECK(finish_reason("length") == FinishReason::Length);
    CHECK(finish_reason("tool_calls") == FinishReason::ToolCalls);
    CHECK(finish_reason("cancelled") == FinishReason::Cancelled);
    CHECK(finish_reason("something newer") == FinishReason::Stop);
}

TEST_CASE(
    "a message carrying a picture crosses as its parts in order, the image a data: URI where it "
    "sat; a text message stays a string",
    "[backends][mlx][protocol][vision]") {
    // 27c: the content-part shape a vision model's template places images
    // by. The image's bytes are never decoded or fetched on this side.
    GenerateRequest request;
    request.messages.push_back(apogee::harness::ChatMessage::system("Look closely."));
    request.messages.push_back(apogee::harness::ChatMessage{
        .role = apogee::harness::Role::User,
        .content = apogee::harness::MessageContent::from_parts(
            {apogee::harness::ContentPart::from_image_url("data:image/png;base64,iVBORw0KGgo="),
             apogee::harness::ContentPart::from_text("What is this?"),
             apogee::harness::ContentPart::from_image_url("data:image/jpeg;base64,/9j/4AAQ")})});
    // Text parts alone are not rich: they stay one string.
    request.messages.push_back(
        apogee::harness::ChatMessage{.role = apogee::harness::Role::User,
                                     .content = apogee::harness::MessageContent::from_parts(
                                         {apogee::harness::ContentPart::from_text("and "),
                                          apogee::harness::ContentPart::from_text("this")})});
    const nlohmann::json line =
        nlohmann::json::parse(apogee::backends::mlx::generate_line(request));
    const nlohmann::json& messages = line["messages"];
    CHECK(messages[0]["content"] == "Look closely.");
    CHECK(
        messages[1]["content"] ==
        nlohmann::json::array({{{"type", "image"}, {"image", "data:image/png;base64,iVBORw0KGgo="}},
                               {{"type", "text"}, {"text", "What is this?"}},
                               {{"type", "image"}, {"image", "data:image/jpeg;base64,/9j/4AAQ"}}}));
    CHECK(messages[2]["content"] == "and this");
}

TEST_CASE(
    "a recording crosses as the WAV file the provider wrote for it, where its part sat and in "
    "prompt order; one with no file is not sent",
    "[backends][mlx][protocol][audio]") {
    // 53g: the content-part shape an omni model's template places audio by.
    // The recording's bytes never cross the pipe: the provider decoded them
    // to a file of the turn's own, and the path is what the driver hands on.
    GenerateRequest request;
    request.messages.push_back(apogee::harness::ChatMessage{
        .role = apogee::harness::Role::User,
        .content = apogee::harness::MessageContent::from_parts(
            {apogee::harness::ContentPart::from_audio("UklGRg==", "wav"),
             apogee::harness::ContentPart::from_text("What is said?")})});
    request.messages.push_back(apogee::harness::ChatMessage::assistant("Hello."));
    request.messages.push_back(apogee::harness::ChatMessage{
        .role = apogee::harness::Role::User,
        .content = apogee::harness::MessageContent::from_parts(
            {apogee::harness::ContentPart::from_text("And here?"),
             apogee::harness::ContentPart::from_image_url("data:image/png;base64,iVBORw0KGgo="),
             apogee::harness::ContentPart::from_audio("UklGRg==", "wav")})});
    request.audio_files = {"/tmp/turn/audio-0.wav", "/tmp/turn/audio-1.wav"};
    const nlohmann::json line =
        nlohmann::json::parse(apogee::backends::mlx::generate_line(request));
    const nlohmann::json& messages = line["messages"];
    CHECK(messages[0]["content"] ==
          nlohmann::json::array({{{"type", "audio"}, {"audio", "/tmp/turn/audio-0.wav"}},
                                 {{"type", "text"}, {"text", "What is said?"}}}));
    CHECK(messages[1]["content"] == "Hello.");
    CHECK(
        messages[2]["content"] ==
        nlohmann::json::array({{{"type", "text"}, {"text", "And here?"}},
                               {{"type", "image"}, {"image", "data:image/png;base64,iVBORw0KGgo="}},
                               {{"type", "audio"}, {"audio", "/tmp/turn/audio-1.wav"}}}));
    // The base64 the IR carried is nowhere on the line.
    CHECK(line.dump().find("UklGRg") == std::string::npos);

    // A part the provider wrote no file for is left out, never sent bare.
    request.audio_files = {"/tmp/turn/audio-0.wav"};
    const nlohmann::json short_line =
        nlohmann::json::parse(apogee::backends::mlx::generate_line(request));
    CHECK(short_line["messages"][2]["content"] ==
          nlohmann::json::array(
              {{{"type", "text"}, {"text", "And here?"}},
               {{"type", "image"}, {"image", "data:image/png;base64,iVBORw0KGgo="}}}));
}

TEST_CASE("a ready line says whether the driver hands recordings on; one before 53g never does",
          "[backends][mlx][protocol][audio]") {
    const auto hearing = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"model_type":"gemma4","chat_template":true,)"
        R"("tool_parser":null,"thinking":false,"mlx_lm":"0.32.0","vision":true,)"
        R"("mlx_vlm":"0.6.1","audio":true})");
    REQUIRE(hearing.has_value());
    CHECK(hearing->vision);
    CHECK(hearing->audio);
    // 53f's driver: vision, and not a word of audio.
    const auto older = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"vision":true,"mlx_vlm":"0.6.1"})");
    REQUIRE(older.has_value());
    CHECK(older->vision);
    CHECK_FALSE(older->audio);
    const auto text = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"vision":false,"audio":false})");
    REQUIRE(text.has_value());
    CHECK_FALSE(text->audio);
}

TEST_CASE("a ready line names the drafter it loaded beside the model; one before 53i never does",
          "[backends][mlx][protocol][drafter]") {
    const auto drafting = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"model_type":"gemma4","vision":true,"mlx_vlm":"0.6.1",)"
        R"("audio":true,"draft":"/m/Gemma4-E4B-Assistant/mlx/aaaaaaaaaaaa","draft_kind":"mtp",)"
        R"("draft_block_size":6})");
    REQUIRE(drafting.has_value());
    CHECK(drafting->draft == "/m/Gemma4-E4B-Assistant/mlx/aaaaaaaaaaaa");
    CHECK(drafting->draft_kind == "mtp");
    CHECK(drafting->draft_block_size == 6);
    // The drafter's own block: null on the wire, 0 here.
    const auto own = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"draft":"/d","draft_kind":"mtp","draft_block_size":null})");
    REQUIRE(own.has_value());
    CHECK(own->draft == "/d");
    CHECK(own->draft_block_size == 0);
    // 53h's driver: not a word of drafting.
    const auto older = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"vision":true,"mlx_vlm":"0.6.1","audio":true})");
    REQUIRE(older.has_value());
    CHECK(older->draft.empty());
    CHECK(older->draft_kind.empty());
    CHECK(older->draft_block_size == 0);
}

TEST_CASE("an embed request is its texts in one line, any bytes said as text",
          "[backends][mlx][protocol][embedding]") {
    // 53h: the embedding driver's request, pinned byte for byte. A file's
    // bytes need not be UTF-8: a Latin-1 é crosses as U+FFFD rather than
    // ending the batch at the dump.
    CHECK(apogee::backends::mlx::embed_line({.id = 3, .texts = {"alpha", "beta"}}) ==
          R"({"id":3,"texts":["alpha","beta"],"type":"embed"})");
    CHECK(apogee::backends::mlx::embed_line({.id = 4, .texts = {}}) ==
          R"({"id":4,"texts":[],"type":"embed"})");
    CHECK(apogee::backends::mlx::embed_line({.id = 5, .texts = {"caf\xe9"}}) ==
          "{\"id\":5,\"texts\":[\"caf\xef\xbf\xbd\"],\"type\":\"embed\"}");
}

TEST_CASE("an embedding driver's lines parse: its ready, its vectors, and a malformed answer said",
          "[backends][mlx][protocol][embedding]") {
    const std::optional<Event> ready = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"embedding":true,"model_type":"gemma3_text",)"
        R"("mlx_embeddings":"0.1.0","max_length":2048})");
    REQUIRE(ready.has_value());
    CHECK(ready->kind == Event::Kind::Ready);
    CHECK(ready->embedding);
    CHECK(ready->mlx_embeddings_version == "0.1.0");
    CHECK(ready->model_type == "gemma3_text");
    // The generation driver's ready never says it.
    const std::optional<Event> chat = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"model_type":"llama","mlx_lm":"0.32.0"})");
    REQUIRE(chat.has_value());
    CHECK_FALSE(chat->embedding);

    const std::optional<Event> vectors = apogee::backends::mlx::parse_event(
        R"({"type":"vectors","id":7,"vectors":[[0.5,-1,2e-3],[0,0,1]]})");
    REQUIRE(vectors.has_value());
    CHECK(vectors->kind == Event::Kind::Vectors);
    CHECK(vectors->id == 7);
    CHECK(vectors->vectors ==
          std::vector<std::vector<float>>{{0.5F, -1.0F, 2e-3F}, {0.0F, 0.0F, 1.0F}});
    CHECK(apogee::backends::mlx::describe(*vectors) == "#7 vectors 2x3");
    const std::optional<Event> none =
        apogee::backends::mlx::parse_event(R"({"type":"vectors","id":8,"vectors":[]})");
    REQUIRE(none.has_value());
    CHECK(none->kind == Event::Kind::Vectors);
    CHECK(none->vectors.empty());

    // Anything not a list of number lists is a protocol error for its id,
    // never fewer vectors and never a dropped line a provider would wait on.
    for (const char* line :
         {R"({"type":"vectors","id":9})", R"({"type":"vectors","id":9,"vectors":[[1,"2"]]})",
          R"({"type":"vectors","id":9,"vectors":[1,2]})",
          R"({"type":"vectors","id":9,"vectors":{"0":[1]}})"}) {
        INFO(line);
        const std::optional<Event> bad = apogee::backends::mlx::parse_event(line);
        REQUIRE(bad.has_value());
        CHECK(bad->kind == Event::Kind::Error);
        CHECK(bad->error_kind == "protocol");
        CHECK(bad->id == 9);
        CHECK(bad->vectors.empty());
    }
}
