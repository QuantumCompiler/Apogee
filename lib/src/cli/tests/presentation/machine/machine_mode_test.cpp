#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/loop.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/provider.h"
#include "harness/harness.h"
#include "machine/driver_input.h"
#include "machine/json_reporter.h"
#include "machine/protocol.h"
#include "tasks/ledger.h"
#include "tasks/task.h"
#include "tasks/view.h"
#include "transport/jsonl_framer.h"

/// Machine mode: the JSONL protocol a GUI drives Apogee over.
///
/// The suite is shaped by one idea from the vendor-CLI work: Apogee is now on
/// **both sides** of this kind of protocol. Every discipline it demands of the
/// CLIs it drives — a framing-safe stream, diagnostics off stdout, tolerance of
/// unknown event types — it owes its own consumers. So the adversarial
/// chunk-size replay that guards the claude and codex parsers guards this
/// emitter too, from the driver's side.
namespace {

using apogee::commands::DriverMessage;
using apogee::commands::JsonReporter;

/// Every line a run produced, as parsed JSON.
[[nodiscard]] std::vector<nlohmann::json> events(const std::string& stream) {
    std::vector<nlohmann::json> out;
    for (const std::string& line : apogee::backends::split_lines(stream)) {
        const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        REQUIRE_FALSE(parsed.is_discarded());
        out.push_back(parsed);
    }
    return out;
}

[[nodiscard]] std::vector<std::string> types(const std::string& stream) {
    std::vector<std::string> out;
    for (const nlohmann::json& event : events(stream)) {
        out.push_back(event.value("type", std::string{}));
    }
    return out;
}

/// Drives a reporter through a representative turn.
void one_turn(JsonReporter& reporter) {
    reporter.begin_session("test-model", {});
    reporter.on_thinking();
    reporter.on_thinking_token("reasoning about it");
    reporter.on_tool_status("fetch_url https://example.test");
    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("Hello");
    reporter.on_answer_token(", world");
    reporter.on_answer_end();

    apogee::harness::ChatResponse response;
    response.message = apogee::harness::ChatMessage::assistant("Hello, world");
    response.model = "test-model";
    response.usage.prompt_tokens = 12;
    response.usage.completion_tokens = 3;
    reporter.emit_result(response);
}

}  // namespace

TEST_CASE("every emitted line is a JSON object with a type", "[commands][machine]") {
    // The protocol's floor. A driver's parser reads line-oriented JSON, so one
    // stray line of prose breaks it -- which is exactly the failure Apogee
    // guards against when consuming other CLIs.
    std::ostringstream out;
    JsonReporter reporter{out};
    one_turn(reporter);

    const std::vector<nlohmann::json> seen = events(out.str());
    REQUIRE_FALSE(seen.empty());
    for (const nlohmann::json& event : seen) {
        CHECK(event.is_object());
        CHECK(event.contains("type"));
        CHECK(event.at("type").is_string());
    }
}

TEST_CASE("the event vocabulary mirrors the Reporter seam", "[commands][machine]") {
    // One event type per Reporter method, nothing invented. A second vocabulary
    // would be a second thing to keep in sync, and the first time it drifted a
    // driver would see an event the terminal never shows -- or miss one it
    // does.
    std::ostringstream out;
    JsonReporter reporter{out};
    one_turn(reporter);

    CHECK(types(out.str()) == std::vector<std::string>{"session", "thinking", "thinking_delta",
                                                       "tool_status", "answer_start",
                                                       "answer_delta", "answer_delta", "answer_end",
                                                       "result"});
}

TEST_CASE("the session event carries a protocol version", "[commands][machine]") {
    // Versioned so a driver can refuse a stream it does not understand. Adding
    // an event type does NOT bump it -- drivers must ignore unknown types, so
    // an addition is compatible by construction.
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.begin_session("m", {});

    const nlohmann::json session = events(out.str()).front();
    CHECK(session.at("type") == "session");
    CHECK(session.at("protocol_version") == apogee::commands::kMachineProtocolVersion);
    CHECK(session.at("model") == "m");
}

TEST_CASE("a side call is a tool_status line, and no new event type",
          "[commands][machine][side-call]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    apogee::agentloop::SideCall call{.role = "rerank", .detail = "judging 12 results with judge"};
    reporter.on_side_call(call);
    call.done = true;
    reporter.on_side_call(call);
    const std::vector<nlohmann::json> all = events(out.str());
    REQUIRE(all.size() == 1);
    CHECK(all[0].at("type") == "tool_status");
    CHECK(all[0].at("text") == "rerank — judging 12 results with judge");
}

TEST_CASE("what a turn recalled is a memory event, and nothing when it recalled nothing",
          "[commands][machine][recall]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.on_recall(0, 0);
    reporter.on_recall(2, 1);
    const std::vector<nlohmann::json> all = events(out.str());
    REQUIRE(all.size() == 1);
    CHECK(all[0].at("type") == "memory");
    CHECK(all[0].at("chats") == 2);
    CHECK(all[0].at("decisions") == 1);
}

TEST_CASE("a spent thinking budget is said on a thinking event", "[commands][machine][thinking]") {
    // 26i: an added field on an existing event, so a driver that does not
    // know it reads an ordinary thinking event.
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.on_thinking();
    reporter.on_thinking_budget_reached();
    const std::vector<nlohmann::json> all = events(out.str());
    REQUIRE(all.size() == 2);
    CHECK_FALSE(all[0].contains("budget_reached"));
    CHECK(all[1].at("type") == "thinking");
    CHECK(all[1].at("budget_reached") == true);
}

TEST_CASE("thinking is distinctly typed and never reaches the answer",
          "[commands][machine][thinking]") {
    // The harness-wide rule, enforced at this surface: a driver that drops
    // every thinking* event reconstructs exactly what a terminal user saw, and
    // reasoning never appears in the result text.
    std::ostringstream out;
    JsonReporter reporter{out};
    one_turn(reporter);

    std::string answer;
    std::string thinking;
    std::string result_text;
    for (const nlohmann::json& event : events(out.str())) {
        const std::string type = event.value("type", std::string{});
        if (type == "answer_delta") {
            answer += event.value("text", std::string{});
        } else if (type == "thinking_delta") {
            thinking += event.value("text", std::string{});
        } else if (type == "result") {
            result_text = event.value("text", std::string{});
        }
    }

    CHECK(answer == "Hello, world");
    CHECK(thinking == "reasoning about it");
    CHECK(result_text == "Hello, world");
    CHECK(result_text.find("reasoning") == std::string::npos);
    CHECK(answer.find("reasoning") == std::string::npos);
}

TEST_CASE("the result carries the whole answer and its usage", "[commands][machine]") {
    // So a driver that dropped every delta still has the answer -- the same
    // reason stream_chat returns the complete response as well as streaming it.
    std::ostringstream out;
    JsonReporter reporter{out};
    one_turn(reporter);

    const nlohmann::json result = events(out.str()).back();
    REQUIRE(result.at("type") == "result");
    CHECK(result.at("text") == "Hello, world");
    CHECK(result.at("model") == "test-model");
    CHECK(result.at("usage").at("input_tokens") == 12);
    CHECK(result.at("usage").at("output_tokens") == 3);
}

TEST_CASE("a turn with no usage omits the field rather than reporting zero",
          "[commands][machine]") {
    // Zero means "the provider reported none", which is not the same as "no
    // tokens were used". A driver displaying 0 would be stating a measurement
    // nobody made.
    std::ostringstream out;
    JsonReporter reporter{out};

    apogee::harness::ChatResponse response;
    response.message = apogee::harness::ChatMessage::assistant("hi");
    reporter.emit_result(response);

    CHECK_FALSE(events(out.str()).front().contains("usage"));
}

TEST_CASE("clearing a status emits nothing", "[commands][machine]") {
    // Erasing a transient indicator is a terminal concern; there is nothing to
    // erase in a stream of records. An event for it would put a rendering
    // detail into the protocol.
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.on_clear_status();
    CHECK(out.str().empty());
}

TEST_CASE("empty chunks are not emitted", "[commands][machine]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.on_answer_token("");
    reporter.on_thinking_token("");
    reporter.on_tool_status("");
    CHECK(out.str().empty());
}

TEST_CASE("text with newlines and quotes survives the round trip", "[commands][machine]") {
    // One object per LINE, so an embedded newline would split one event into
    // two invalid halves. JSON escaping is what prevents that, and it is worth
    // pinning because the failure looks like a framing bug rather than an
    // encoding one.
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.on_answer_token("line one\nline two \"quoted\"\\ and a backslash");

    const std::vector<nlohmann::json> seen = events(out.str());
    REQUIRE(seen.size() == 1);
    CHECK(seen.front().at("text") == "line one\nline two \"quoted\"\\ and a backslash");
}

namespace {

/// U+FFFD, the replacement character, as UTF-8.
constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

/// A backend that says its reasoning and its answer in exactly the pieces it
/// is given: cut wherever a token or a pipe read ends, which is anywhere --
/// llama.cpp says a byte-fallback token one byte at a time.
class PiecesProvider final : public apogee::harness::LLMProvider {
public:
    std::vector<std::string> thinking;
    std::vector<std::string> answer;

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "pieces";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const apogee::harness::ChatRequest& request,
        const apogee::harness::CancellationToken& cancellation) override {
        apogee::harness::StreamOptions options;
        options.cancellation = cancellation;
        return stream_chat(request, options);
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const apogee::harness::ChatRequest& /*request*/,
        const apogee::harness::StreamOptions& options) override {
        std::string whole;
        for (const std::string& piece : thinking) {
            if (options.on_thinking) {
                options.on_thinking(piece);
            }
        }
        for (const std::string& piece : answer) {
            if (options.on_token) {
                options.on_token(piece);
            }
            whole += piece;
        }
        apogee::harness::ChatResponse response;
        response.message = apogee::harness::ChatMessage::assistant(whole);
        response.model = "pieces-1";
        return response;
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken& /*cancellation*/) override {
        return {};
    }
};

/// One machine-mode turn, as `chat --output-format stream-json` runs it: the
/// loop speaking through the reporter, then the turn's `result` from the
/// history it kept. The JSONL it wrote.
[[nodiscard]] std::string machine_turn(std::vector<std::string> thinking,
                                       std::vector<std::string> answer) {
    auto provider = std::make_shared<PiecesProvider>();
    provider->thinking = std::move(thinking);
    provider->answer = std::move(answer);
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("pieces", provider);
    harness.use_default_router();

    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.begin_session("pieces", {});
    std::vector<apogee::harness::ChatMessage> history{apogee::harness::ChatMessage::user("hi")};
    apogee::agentloop::Options options;
    options.model = "pieces";
    (void)apogee::agentloop::run(harness, history, options, reporter);
    apogee::harness::ChatResponse response;
    response.message = history.back();
    response.model = "pieces";
    reporter.emit_result(response);
    return out.str();
}

/// The `text` of every event of `type`, in order.
[[nodiscard]] std::vector<std::string> texts(const std::string& stream, std::string_view type) {
    std::vector<std::string> out;
    for (const nlohmann::json& event : events(stream)) {
        if (event.value("type", std::string{}) == type) {
            out.push_back(event.value("text", std::string{}));
        }
    }
    return out;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& pieces) {
    std::string out;
    for (const std::string& piece : pieces) {
        out += piece;
    }
    return out;
}

}  // namespace

TEST_CASE("a character split across streamed pieces reaches the driver whole",
          "[commands][machine][utf8]") {
    // A valid é cut between two pieces, in the reasoning and in the answer:
    // each delta used to be written as it came, and half a character is not
    // JSON -- the session died mid-turn. Every line parses, each delta is
    // whole text, and the deltas add up to the answer the result carries.
    std::string stream;
    REQUIRE_NOTHROW(stream =
                        machine_turn({"r\xC3", "\xA9sum\xC3", "\xA9"}, {"caf\xC3", "\xA9 ok"}));
    CHECK(joined(texts(stream, "thinking_delta")) == "r\xC3\xA9sum\xC3\xA9");
    CHECK(joined(texts(stream, "answer_delta")) == "caf\xC3\xA9 ok");
    REQUIRE(texts(stream, "result").size() == 1);
    CHECK(texts(stream, "result").front() == "caf\xC3\xA9 ok");
}

TEST_CASE("a stream that ends inside a character ends in U+FFFD, and the result says the same",
          "[commands][machine][utf8]") {
    // llama.cpp stopping at max_tokens halfway through a character: the last
    // delta is the replacement character, and the result -- what history,
    // the session file and a task's ledger keep -- is the same text.
    std::string stream;
    REQUIRE_NOTHROW(stream = machine_turn({"hm\xE2\x80"}, {"caf\xC3"}));
    const std::string replaced = "caf" + std::string{kReplacement};
    CHECK(joined(texts(stream, "thinking_delta")) == "hm" + std::string{kReplacement});
    const std::vector<std::string> deltas = texts(stream, "answer_delta");
    CHECK(joined(deltas) == replaced);
    REQUIRE_FALSE(deltas.empty());
    CHECK(deltas.back() == kReplacement);
    REQUIRE(texts(stream, "result").size() == 1);
    CHECK(texts(stream, "result").front() == replaced);
}

TEST_CASE("an event whose text is not UTF-8 is written, never thrown",
          "[commands][machine][utf8]") {
    // A notice or an error can carry bytes no model wrote -- a vendor CLI's
    // stderr tail cut at a byte bound, a Latin-1 name -- and the writer used
    // to throw on them, ending the session over a notice. Each is one line of
    // JSON with U+FFFD where the bytes were not text; valid text is unchanged.
    std::ostringstream out;
    JsonReporter reporter{out};
    REQUIRE_NOTHROW(reporter.on_notice("stderr: caf\xE9"));
    REQUIRE_NOTHROW(reporter.emit_error("ollama: \xE2\x80"));
    REQUIRE_NOTHROW(reporter.on_notice("caf\xC3\xA9"));
    std::istringstream lines{out.str()};
    std::vector<nlohmann::json> events;
    for (std::string line; std::getline(lines, line);) {
        REQUIRE_NOTHROW(events.push_back(nlohmann::json::parse(line)));
    }
    REQUIRE(events.size() == 3);
    CHECK(events[0].dump().find("caf" + std::string{kReplacement}) != std::string::npos);
    CHECK(events[1].dump().find("ollama: " + std::string{kReplacement}) != std::string::npos);
    CHECK(events[2].dump().find("caf\xC3\xA9") != std::string::npos);
}

TEST_CASE("a driver reading at any chunk size sees the same events",
          "[commands][machine][replay]") {
    // The adversarial replay, from the DRIVER's side. Apogee demands this of
    // the CLIs it consumes; it owes the same to whoever consumes it. One byte
    // at a time is the case that puts a boundary between every pair of bytes.
    std::ostringstream out;
    JsonReporter reporter{out};
    one_turn(reporter);
    const std::string stream = out.str();

    const std::vector<std::string> whole = types(stream);
    REQUIRE_FALSE(whole.empty());

    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7}, std::size_t{4096}}) {
        INFO("chunk " << chunk);
        std::vector<std::string> seen;
        apogee::backends::JsonlFramer framer;
        const auto handle = [&seen](std::string_view line) {
            const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
            if (!parsed.is_discarded()) {
                seen.push_back(parsed.value("type", std::string{}));
            }
        };
        for (std::size_t offset = 0; offset < stream.size(); offset += chunk) {
            framer.feed(stream.substr(offset, std::min(chunk, stream.size() - offset)), handle);
        }
        framer.flush(handle);
        CHECK(seen == whole);
    }
}

TEST_CASE("driver input parses user turns and answers", "[commands][machine][input]") {
    const DriverMessage user =
        apogee::commands::parse_driver_line(R"({"type":"user","text":"what is 2+2?"})");
    CHECK(user.kind == DriverMessage::Kind::User);
    CHECK(user.text == "what is 2+2?");

    const DriverMessage answer =
        apogee::commands::parse_driver_line(R"({"type":"answer","text":"yes"})");
    CHECK(answer.kind == DriverMessage::Kind::Answer);
    CHECK(answer.text == "yes");
}

TEST_CASE("an unrecognised driver line is ignored, never fatal", "[commands][machine][input]") {
    // The tolerance this protocol asks of drivers, applied in the other
    // direction: a driver sending something this build does not know must not
    // kill the session.
    for (const std::string_view line : {R"({"type":"invented_next_year"})", R"({"no":"type"})",
                                        "not json at all", "", "[1,2,3]", R"({"type":"user")"}) {
        INFO(line);
        CHECK(apogee::commands::parse_driver_line(line).kind == DriverMessage::Kind::Unknown);
    }
}

TEST_CASE("the output format round-trips through its name", "[commands][machine]") {
    using apogee::commands::output_format_from_string;
    using apogee::commands::OutputFormat;

    CHECK(output_format_from_string("text") == OutputFormat::Text);
    CHECK(output_format_from_string("stream-json") == OutputFormat::StreamJson);
    // Empty means the default, so a caller need not special-case an unset flag.
    CHECK(output_format_from_string("") == OutputFormat::Text);
    CHECK_FALSE(output_format_from_string("json").has_value());
    CHECK_FALSE(output_format_from_string("STREAM-JSON").has_value());

    CHECK(to_string(OutputFormat::StreamJson) == "stream-json");
    CHECK(to_string(OutputFormat::Text) == "text");
}

TEST_CASE("an error is a machine-readable event", "[commands][machine]") {
    // So a driver need not scrape prose off stderr to know a turn failed.
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.emit_error("no usable backend is configured");

    const nlohmann::json error = events(out.str()).front();
    CHECK(error.at("type") == "error");
    CHECK(error.at("message") == "no usable backend is configured");
}

namespace {

/// A question with one answer expected.
[[nodiscard]] apogee::agentloop::QuestionRequest one_question() {
    apogee::agentloop::QuestionRequest request;
    apogee::agentloop::Question question;
    question.header = "Overwrite";
    question.question = "The file already exists. Overwrite it?";
    question.options = {{"Yes", "Replace the current contents"}, {"No", "Leave the file alone"}};
    request.questions.push_back(question);
    return request;
}

}  // namespace

TEST_CASE("a question reaches the driver with its options intact", "[commands][machine][ask]") {
    // ask_user rides the protocol so a driving GUI renders a native dialog.
    // The alternative -- a terminal-only AskFn -- would leave the tool
    // silently unavailable on the one surface built for a real UI.
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.emit_question(one_question());

    const nlohmann::json event = events(out.str()).front();
    REQUIRE(event.at("type") == "question");
    const nlohmann::json& question = event.at("questions").at(0);
    CHECK(question.at("header") == "Overwrite");
    CHECK(question.at("multi_select") == false);
    CHECK(question.at("options").size() == 2);
    CHECK(question.at("options").at(0).at("label") == "Yes");
    CHECK(question.at("options").at(1).at("description") == "Leave the file alone");
}

TEST_CASE("the driver's answer comes back through the AskFn", "[commands][machine][ask]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{"{\"type\":\"answer\",\"text\":\"Yes\"}\n"};
    apogee::commands::DriverInput driver{in};

    const apogee::agentloop::Answers answers =
        apogee::commands::make_driver_ask_fn(reporter, driver)(one_question());

    CHECK(answers.values == std::vector<std::string>{"Yes"});
    CHECK(types(out.str()) == std::vector<std::string>{"question"});
}

TEST_CASE("free text is accepted where an option was offered", "[commands][machine][ask]") {
    // The offered options are a convenience, not a constraint on what the user
    // is allowed to say -- the same rule the terminal AskFn follows.
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{"{\"type\":\"answer\",\"text\":\"only if it is a backup\"}\n"};
    apogee::commands::DriverInput driver{in};

    CHECK(apogee::commands::make_driver_ask_fn(reporter, driver)(one_question()).values ==
          std::vector<std::string>{"only if it is a backup"});
}

TEST_CASE("noise before an answer is skipped, not treated as one", "[commands][machine][ask]") {
    // A driver may not interleave a new user turn into a pending question, and
    // an unknown type must not become the user's answer by accident.
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{
        "not json\n"
        "{\"type\":\"user\",\"text\":\"a different question\"}\n"
        "{\"type\":\"invented_later\"}\n"
        "{\"type\":\"answer\",\"text\":\"No\"}\n"};
    apogee::commands::DriverInput driver{in};

    CHECK(apogee::commands::make_driver_ask_fn(reporter, driver)(one_question()).values ==
          std::vector<std::string>{"No"});
}

TEST_CASE("a driver that hangs up mid-question fails the turn", "[commands][machine][ask]") {
    // Throwing is what the seam asks for: the loop rolls the half-turn out of
    // history, so nothing dangling is persisted. Fabricating an answer would
    // be worse than failing -- which is why the AskFn is allowed to throw.
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{""};
    apogee::commands::DriverInput driver{in};

    CHECK_THROWS_AS(apogee::commands::make_driver_ask_fn(reporter, driver)(one_question()),
                    std::runtime_error);
}

TEST_CASE("every question must be answered before the turn resumes", "[commands][machine][ask]") {
    apogee::agentloop::QuestionRequest two = one_question();
    two.questions.push_back(two.questions.front());
    two.questions.back().question = "And overwrite the backup too?";

    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{"{\"type\":\"answer\",\"text\":\"Yes\"}\n"};
    apogee::commands::DriverInput driver{in};

    CHECK_THROWS_AS(apogee::commands::make_driver_ask_fn(reporter, driver)(two),
                    std::runtime_error);
}

TEST_CASE("the input format parses independently of the output one", "[commands][machine]") {
    using apogee::commands::input_format_from_string;
    using apogee::commands::InputFormat;

    CHECK(input_format_from_string("text") == InputFormat::Text);
    CHECK(input_format_from_string("stream-json") == InputFormat::StreamJson);
    CHECK(input_format_from_string("") == InputFormat::Text);
    CHECK_FALSE(input_format_from_string("jsonl").has_value());

    CHECK(to_string(InputFormat::StreamJson) == "stream-json");
    CHECK(to_string(InputFormat::Text) == "text");
}

TEST_CASE("a permission prompt is a question event with kind, tool and target",
          "[commands][machine][permission]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.emit_permission_question(apogee::agent::GateRequest{"run_command", "rm -rf build"});
    const nlohmann::json event = nlohmann::json::parse(out.str());
    CHECK(event["type"] == "question");
    CHECK(event["kind"] == "permission");
    CHECK(event["tool"] == "run_command");
    CHECK(event["target"] == "rm -rf build");
    REQUIRE(event["questions"].size() == 1);
    CHECK(event["questions"][0]["header"] == "Permission");
    CHECK(event["questions"][0]["multi_select"] == false);
    std::vector<std::string> labels;
    for (const nlohmann::json& option : event["questions"][0]["options"]) {
        labels.push_back(option["label"].get<std::string>());
    }
    CHECK(labels == std::vector<std::string>{"yes", "no", "always", "session"});
}

TEST_CASE("an outbound permission prompt names the host, flags it, and carries the URL",
          "[commands][machine][permission]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.emit_permission_question(apogee::agent::GateRequest{
        "fetch_url", "docs.python.org", "https://docs.python.org/3/", true});
    const nlohmann::json event = nlohmann::json::parse(out.str());
    CHECK(event["kind"] == "permission");
    CHECK(event["tool"] == "fetch_url");
    CHECK(event["target"] == "docs.python.org");
    CHECK(event["outbound"] == true);
    CHECK(event["detail"] == "https://docs.python.org/3/");
    CHECK(event["questions"][0]["question"] == "Allow fetch_url to reach docs.python.org?");
    CHECK(event["questions"][0]["options"][2]["description"].get<std::string>().find(
              "tools.allowed_hosts") != std::string::npos);

    // A writing tool's event carries neither field.
    std::ostringstream plain;
    JsonReporter plain_reporter{plain};
    plain_reporter.emit_permission_question(apogee::agent::GateRequest{"write_file", "x"});
    const nlohmann::json write_event = nlohmann::json::parse(plain.str());
    CHECK_FALSE(write_event.contains("outbound"));
    CHECK_FALSE(write_event.contains("detail"));
}

TEST_CASE("a driver attaches a file with an attach line", "[machine][attachments]") {
    const DriverMessage attach =
        apogee::commands::parse_driver_line(R"({"type":"attach","path":"docs/report.pdf"})");
    CHECK(attach.kind == DriverMessage::Kind::Attach);
    CHECK(attach.text == "docs/report.pdf");
    CHECK(attach.graph.empty());

    // 27p: its method, as `/attach`'s `--graph`, kept as written for the
    // chat to resolve -- or refuse by name. A value that is no string arrives
    // as its JSON, never as nothing; a null is no method at all.
    const auto graph_of = [](std::string_view line) {
        return apogee::commands::parse_driver_line(line).graph;
    };
    CHECK(graph_of(R"({"type":"attach","path":"src","graph":"off"})") == "off");
    CHECK(graph_of(R"({"type":"attach","path":"src","graph":"tree"})") == "tree");
    CHECK(graph_of(R"({"type":"attach","path":"src","graph":true})") == "true");
    CHECK(graph_of(R"({"type":"attach","path":"src","graph":null})").empty());
    CHECK(apogee::commands::parse_driver_line(R"({"type":"attach","path":"src","graph":"off"})")
              .text == "src");
    // Only an attach line carries one.
    CHECK(graph_of(R"({"type":"user","text":"hi","graph":"off"})").empty());
}

namespace {

namespace t = apogee::tasks;

constexpr std::string_view kTaskAnswer = "TASK-ANSWER-PROBE-31e8";

/// A task as its run writes it: every transition kind in the ledger, a
/// round with a grant used and a question answered by the declared answer.
[[nodiscard]] t::Task lived_task() {
    t::Task task;
    task.id = "task-20261004-120000";
    task.goal = "Find the answer";
    task.checks = {{t::CheckKind::Require, "42"}};
    task.session_id = "20261004-120000-abcd";
    task.tools = true;
    task.policy.on_question = t::OnQuestion::Answer;
    task.policy.answer = std::string{kTaskAnswer};
    task.policy.grants = {"write_file"};
    t::record_transition(task, t::kCreatedEvent, "t0", 0, task.goal);
    t::record_transition(task, t::kStartedEvent, "t1");
    task.rounds.push_back(t::Round{.index = 0, .kind = std::string{t::kPlanRound}});
    t::record_transition(task, t::kPlanStartedEvent, "t2", 0, "plan");
    task.rounds.back().outcome = std::string{t::kCompleted};
    task.plan = "1. Answer.";
    task.status = std::string{t::kRunning};
    t::record_transition(task, t::kPlanRecordedEvent, "t3");
    task.rounds.push_back(t::Round{.index = 1, .kind = std::string{t::kExecuteRound}});
    t::record_transition(task, t::kRoundStartedEvent, "t4", 1, "execute");
    task.rounds.back().allowed = {{.tool = "write_file", .target = "out.txt", .by = "grant"}};
    task.rounds.back().answered = {
        {.question = "Which colour?", .answer = std::string{kTaskAnswer}, .by = "declared"}};
    t::conclude_round(task, "It is 42.\nTASK STATUS: DONE");
    t::record_transition(task, t::kRoundEndedEvent, "t5", 1, "1 of 1 check passed");
    task.reason = "every check passed";
    t::record_transition(task, t::kFinishedEvent, "t6", 0, task.reason);
    return task;
}

}  // namespace

TEST_CASE("each ledger transition is one task event carrying it as the ledger wrote it",
          "[commands][machine][task]") {
    const t::Task task = lived_task();
    std::ostringstream out;
    JsonReporter reporter{out};
    for (std::size_t index = 0; index < task.transitions.size(); ++index) {
        reporter.emit_task_transition(task, index, std::nullopt);
    }
    const std::vector<nlohmann::json> seen = events(out.str());
    // `created` precedes any run: it reaches a driver as history.
    CHECK(types(out.str()) == std::vector<std::string>{"task_started", "task_plan", "task_plan",
                                                       "task_round", "task_round",
                                                       "task_finished"});
    REQUIRE(seen.size() == task.transitions.size() - 1);
    for (std::size_t index = 0; index < seen.size(); ++index) {
        CHECK(seen[index]["task_id"] == task.id);
        CHECK(seen[index]["transition"] == t::transition_to_json(task.transitions[index + 1]));
    }
    // Started: not resumed, its history the transitions before it, and the
    // task's whole view.
    CHECK(seen[0]["resumed"] == false);
    CHECK(seen[0]["history"] ==
          nlohmann::json::array({t::transition_to_json(task.transitions[0])}));
    CHECK(seen[0]["task"] == t::to_json(t::make_task_view(task, std::nullopt)));
    // The plan turn, and once recorded the plan.
    CHECK(seen[1]["round"]["kind"] == "plan");
    CHECK_FALSE(seen[1].contains("plan"));
    CHECK(seen[2]["plan"] == "1. Answer.");
    // A round, and once ended each check's state and the rounds used.
    CHECK(seen[3]["round"]["round"] == 1);
    CHECK_FALSE(seen[3].contains("checks"));
    CHECK(seen[4]["checks"][0]["passed"] == true);
    CHECK(seen[4]["rounds_used"] == 1);
    CHECK(seen[4]["round"]["allowed"][0]["by"] == "grant");
    // Finished: the outcome and the final view.
    CHECK(seen[5]["status"] == task.status);
    CHECK(seen[5]["reason"] == "every check passed");
    CHECK(seen[5]["task"]["status"] == task.status);
}

TEST_CASE("a resumed task's first event is task_started with resumed and its whole history",
          "[commands][machine][task]") {
    t::Task task = lived_task();
    task.transitions.pop_back();  // as if halted before it finished
    task.status = std::string{t::kRunning};
    t::record_transition(task, t::kResumedEvent, "t7");
    std::ostringstream out;
    JsonReporter reporter{out};
    const t::LockHolder ours{.pid = 4153, .task_id = task.id, .running = true};
    reporter.emit_task_transition(task, task.transitions.size() - 1, ours);
    const nlohmann::json started = events(out.str()).front();
    CHECK(started["type"] == "task_started");
    CHECK(started["resumed"] == true);
    CHECK(started["history"].size() == task.transitions.size() - 1);
    CHECK(started["task"]["process"] == 4153);
    CHECK(started["task"]["rounds_used"] == 1);
}

TEST_CASE("a grant used is a task_grant event", "[commands][machine][task]") {
    const t::Task task = lived_task();
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.emit_task_grant(task, 1, task.rounds.back().allowed.front());
    CHECK(events(out.str()).front() == nlohmann::json::parse(R"({"type": "task_grant",
        "task_id": "task-20261004-120000", "round": 1, "tool": "write_file",
        "target": "out.txt", "by": "grant"})"));
}

TEST_CASE("a declared answer reaches no task event", "[commands][machine][task][leak]") {
    const t::Task task = lived_task();
    REQUIRE(t::task_to_json(task).dump().find(kTaskAnswer) != std::string::npos);
    std::ostringstream out;
    JsonReporter reporter{out};
    for (std::size_t index = 0; index < task.transitions.size(); ++index) {
        reporter.emit_task_transition(task, index, std::nullopt);
    }
    for (const t::Permit& permit : task.rounds.back().allowed) {
        reporter.emit_task_grant(task, 1, permit);
    }
    REQUIRE_FALSE(out.str().empty());
    CHECK(out.str().find(kTaskAnswer) == std::string::npos);
    // The question was answered, and by the declared answer: said, never what.
    CHECK(out.str().find("Which colour?") != std::string::npos);
}

TEST_CASE("a read's format is text or one JSON document, never a stream",
          "[commands][machine][read]") {
    using apogee::commands::read_format_from_string;
    using apogee::commands::ReadFormat;
    CHECK(read_format_from_string("text") == ReadFormat::Text);
    CHECK(read_format_from_string("") == ReadFormat::Text);
    CHECK(read_format_from_string("json") == ReadFormat::Json);
    CHECK_FALSE(read_format_from_string("stream-json").has_value());
    CHECK(apogee::commands::read_format_names() == std::vector<std::string_view>{"text", "json"});
    std::ostringstream out;
    apogee::commands::write_document(out, nlohmann::json{{"a", 1}, {"b", "two"}});
    CHECK(out.str() == "{\"a\":1,\"b\":\"two\"}\n");
}

// --- 28d: the handshake ---------------------------------------------------------

TEST_CASE("the session announces its capabilities, today's fields unchanged",
          "[commands][machine][handshake]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.begin_session(
        "m", apogee::commands::MachineCapabilities{
                 .accepts = {"user", "answer", "attach", "hello"}, .tools = true, .ask = true});
    nlohmann::json session = events(out.str()).front();
    const nlohmann::json capabilities = session.at("capabilities");
    CHECK(capabilities.at("accepts") ==
          nlohmann::json::array({"user", "answer", "attach", "hello"}));
    CHECK(capabilities.at("tools") == true);
    CHECK(capabilities.at("ask") == true);
    CHECK(capabilities.at("schema") == std::string{apogee::commands::kMachineSchemaVersion});
    std::vector<std::string> announced;
    for (const nlohmann::json& type : capabilities.at("events")) {
        announced.push_back(type.get<std::string>());
    }
    const auto declared = apogee::commands::machine_event_types();
    CHECK(announced == std::vector<std::string>(declared.begin(), declared.end()));
    for (const std::string_view type :
         {"session", "result", "question", "error", "task_finished"}) {
        CHECK(std::ranges::find(announced, type) != announced.end());
    }
    // The golden: everything a v1 driver read before is byte for byte as it was.
    session.erase("capabilities");
    CHECK(session.dump() == R"({"model":"m","protocol_version":1,"type":"session"})");
    // No secret, path or config value has a slot to ride in.
    CHECK(capabilities.size() == 5);
}

TEST_CASE("a session with nothing to read accepts nothing and asks nobody",
          "[commands][machine][handshake]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.begin_session("m", {});
    const nlohmann::json capabilities = events(out.str()).front().at("capabilities");
    CHECK(capabilities.at("accepts").empty());
    CHECK(capabilities.at("tools") == false);
    CHECK(capabilities.at("ask") == false);
}

TEST_CASE("hello is parsed, recorded in words, and malformed fields are just absent",
          "[commands][machine][handshake]") {
    using apogee::commands::DriverMessage;
    const DriverMessage hello = apogee::commands::parse_driver_line(
        R"({"type":"hello","client":{"name":"my-host","version":"1.2"},"wants":["tools"]})");
    CHECK(hello.kind == DriverMessage::Kind::Hello);
    CHECK(hello.client_name == "my-host");
    CHECK(hello.client_version == "1.2");
    CHECK(apogee::commands::describe_hello(hello) == R"(hello from my-host 1.2, wants ["tools"])");

    const DriverMessage odd = apogee::commands::parse_driver_line(
        R"({"type":"hello","client":{"name":7,"version":null},"extra":true})");
    CHECK(odd.kind == DriverMessage::Kind::Hello);
    CHECK(odd.client_name.empty());
    CHECK(apogee::commands::describe_hello(odd) == "hello from an unnamed client");
    CHECK(apogee::commands::parse_driver_line(R"({"type":"hello"})").kind ==
          DriverMessage::Kind::Hello);

    const DriverMessage long_name = apogee::commands::parse_driver_line(
        R"({"type":"hello","client":{"name":")" + std::string(500, 'x') + R"("}})");
    CHECK(apogee::commands::describe_hello(long_name).size() < 260);

    const auto inbound = apogee::commands::machine_inbound_types();
    CHECK(std::vector<std::string_view>(inbound.begin(), inbound.end()) ==
          std::vector<std::string_view>{"user", "answer", "attach", "hello", "cancel"});
}

// --- 28f: turn ids and cancel ---------------------------------------------------

TEST_CASE("every event of an open turn carries its number; the session never does",
          "[commands][machine][turns]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    reporter.begin_session("m", {}, 4);
    reporter.on_notice("between turns");
    reporter.begin_turn(4);
    reporter.on_thinking();
    reporter.on_tool_status("reading");
    reporter.on_answer_start();
    reporter.on_answer_token("par");
    reporter.on_answer_token("tial");
    CHECK(reporter.turn_text() == "partial");
    reporter.on_answer_end();
    reporter.emit_error("it broke");
    reporter.end_turn();
    reporter.on_notice("after");
    CHECK(reporter.turn_text().empty());

    const std::vector<nlohmann::json> seen = events(out.str());
    REQUIRE(seen.size() == 10);
    CHECK(seen[0].at("type") == "session");
    CHECK(seen[0].at("next_turn") == 4);
    CHECK_FALSE(seen[0].contains("turn"));
    CHECK_FALSE(seen[1].contains("turn"));  // a notice between turns
    for (std::size_t i = 2; i < 9; ++i) {
        INFO(seen[i].dump());
        CHECK(seen[i].at("turn") == 4);
    }
    CHECK_FALSE(seen[9].contains("turn"));

    // A session with no user lines names no next turn.
    std::ostringstream plain;
    JsonReporter quiet{plain};
    quiet.begin_session("m", {});
    CHECK_FALSE(events(plain.str()).front().contains("next_turn"));
}

TEST_CASE("cancel is read as its own line type", "[commands][machine][turns]") {
    CHECK(apogee::commands::parse_driver_line(R"({"type":"cancel"})").kind ==
          apogee::commands::DriverMessage::Kind::Cancel);
    CHECK(apogee::commands::parse_driver_line(R"({"type":"cancel","turn":3})").kind ==
          apogee::commands::DriverMessage::Kind::Cancel);
}

namespace {

/// An input a test feeds as it goes: a read waits for what was pushed, and
/// ends once closed -- so a test controls which line the reader sees when.
class FeedBuffer final : public std::streambuf {
public:
    void push(const std::string& text) {
        const std::lock_guard lock{mutex_};
        pending_ += text;
        ready_.notify_all();
    }

    void close() {
        const std::lock_guard lock{mutex_};
        closed_ = true;
        ready_.notify_all();
    }

protected:
    int_type underflow() override {
        std::unique_lock lock{mutex_};
        ready_.wait(lock, [this] { return !pending_.empty() || closed_; });
        if (pending_.empty()) {
            return traits_type::eof();
        }
        current_ = std::move(pending_);
        pending_.clear();
        setg(current_.data(), current_.data(), current_.data() + current_.size());
        return traits_type::to_int_type(current_.front());
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::string pending_;
    std::string current_;
    bool closed_ = false;
};

}  // namespace

TEST_CASE("the driver's reader: a cancel reaches the open turn, and is ignored with none",
          "[commands][machine][turns]") {
    using apogee::commands::DriverInput;
    using apogee::commands::DriverLine;

    SECTION("a cancel right after a user line reaches it, even before it starts") {
        std::istringstream in{R"({"type":"user","text":"one"})"
                              "\n"
                              R"({"type":"cancel"})"
                              "\n"};
        DriverInput driver{in};
        const std::optional<DriverLine> line = driver.next_line();
        REQUIRE(line.has_value());
        CHECK(line->sequence == 1);
        CHECK(line->turn.stop_requested());
        CHECK_FALSE(driver.next_line().has_value());  // the cancel was consumed, never queued
    }
    SECTION("a cancel with no turn open is ignored, and the next turn is untouched") {
        std::istringstream in{R"({"type":"cancel"})"
                              "\n"
                              R"({"type":"attach","path":"x"})"
                              "\n"
                              R"({"type":"user","text":"two"})"
                              "\n"};
        DriverInput driver{in};
        const std::optional<DriverLine> attach = driver.next_line();
        REQUIRE(attach.has_value());
        CHECK(attach->sequence == 0);
        const std::optional<DriverLine> user = driver.next_line();
        REQUIRE(user.has_value());
        CHECK_FALSE(user->turn.stop_requested());
    }
    SECTION("an ended turn is out of a later cancel's reach") {
        // Fed a line at a time, so the cancel arrives after the turn ended --
        // the race a driver's Stop button loses by a moment.
        FeedBuffer feed;
        std::istream in{&feed};
        DriverInput driver{in};
        feed.push(R"({"type":"user","text":"one"})"
                  "\n");
        const std::optional<DriverLine> line = driver.next_line();
        REQUIRE(line.has_value());
        driver.end_turn(line->sequence);
        feed.push(R"({"type":"cancel"})"
                  "\n"
                  R"({"type":"user","text":"two"})"
                  "\n");
        const std::optional<DriverLine> next = driver.next_line();
        REQUIRE(next.has_value());
        CHECK_FALSE(line->turn.stop_requested());
        CHECK_FALSE(next->turn.stop_requested());
        feed.close();
        CHECK_FALSE(driver.next_line().has_value());
    }
}

TEST_CASE("a question cancelled while it waits fails its turn as Ctrl-C would",
          "[commands][machine][turns][ask]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{R"({"type":"user","text":"go"})"
                          "\n"
                          R"({"type":"cancel"})"
                          "\n"};
    apogee::commands::DriverInput driver{in};
    const std::optional<apogee::commands::DriverLine> turn = driver.next_line();
    REQUIRE(turn.has_value());
    CHECK_THROWS_AS(apogee::commands::make_driver_ask_fn(reporter, driver)(one_question()),
                    apogee::harness::CancelledError);
    CHECK(types(out.str()) == std::vector<std::string>{"question"});
}

// --- 28g: the one declaration ----------------------------------------------------

namespace {

/// The declared line for `type`, or null.
[[nodiscard]] const apogee::commands::LineSpec* declared(
    std::span<const apogee::commands::LineSpec> lines, const std::string& type) {
    for (const apogee::commands::LineSpec& line : lines) {
        if (line.type == type) {
            return &line;
        }
    }
    return nullptr;
}

/// Every key of `event` declared for its type, and every required one there.
void held_to_declaration(const nlohmann::json& event) {
    INFO(event.dump());
    const std::string type = event.at("type").get<std::string>();
    const apogee::commands::LineSpec* line = declared(apogee::commands::machine_events(), type);
    REQUIRE(line != nullptr);
    for (const auto& [key, value] : event.items()) {
        if (key == "type" || (key == "turn" && type != "session")) {
            continue;
        }
        INFO("field: " << key);
        CHECK(std::ranges::any_of(line->fields, [&key](const apogee::commands::FieldSpec& field) {
            return field.name == key;
        }));
    }
    for (const apogee::commands::FieldSpec& field : line->fields) {
        if (field.required) {
            INFO("required: " << field.name);
            CHECK(event.contains(std::string{field.name}));
        }
    }
}

}  // namespace

TEST_CASE("every event every emitter writes is the declaration's, field for field",
          "[commands][machine][schema]") {
    std::ostringstream out;
    JsonReporter reporter{out};
    apogee::commands::MachineCapabilities capabilities;
    capabilities.accepts = {"user"};
    capabilities.tools = true;
    capabilities.ask = true;
    reporter.begin_session("m", capabilities, 1);
    reporter.begin_turn(1);
    reporter.on_recall(2, 1);
    reporter.on_thinking();
    reporter.on_thinking_budget_reached();
    reporter.on_thinking_token("hmm");
    reporter.on_tool_status("read_file x");
    apogee::agentloop::SideCall side;
    side.role = "utility";
    side.detail = "titling";
    reporter.on_side_call(side);
    reporter.on_notice("a note");
    reporter.on_answer_start();
    reporter.on_answer_token("hi");
    reporter.on_answer_end();
    reporter.emit_question(one_question());
    reporter.emit_permission_question(
        apogee::agent::GateRequest{"fetch_url", "example.test", "https://example.test/x", true});
    apogee::harness::ChatResponse response;
    response.message = apogee::harness::ChatMessage::assistant("hi");
    response.model = "m";
    response.usage.prompt_tokens = 3;
    response.usage.completion_tokens = 1;
    reporter.emit_result(response);
    reporter.emit_error("broke");
    reporter.end_turn();
    const t::Task task = lived_task();
    for (std::size_t index = 0; index < task.transitions.size(); ++index) {
        reporter.emit_task_transition(task, index, std::nullopt);
    }
    reporter.emit_task_grant(task, 1, task.rounds.back().allowed.front());

    std::set<std::string> seen_types;
    for (const nlohmann::json& event : events(out.str())) {
        held_to_declaration(event);
        seen_types.insert(event.at("type").get<std::string>());
    }
    // Every declared event was exercised -- a declaration nothing emits is
    // the drift this pins, in the other direction.
    for (const std::string_view type : apogee::commands::machine_event_types()) {
        INFO("never emitted: " << type);
        CHECK(seen_types.contains(std::string{type}));
    }
}

TEST_CASE("the schema is generated from the declaration and keeps the promise",
          "[commands][machine][schema]") {
    const nlohmann::json schema = apogee::commands::machine_schema();
    CHECK(schema.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(schema.at("x-apogee").at("schema") ==
          std::string{apogee::commands::kMachineSchemaVersion});
    CHECK(schema.at("x-apogee").at("protocol_version") ==
          apogee::commands::kMachineProtocolVersion);
    const nlohmann::json& defs = schema.at("$defs");
    for (const apogee::commands::LineSpec& line : apogee::commands::machine_events()) {
        const nlohmann::json& def = defs.at("event_" + std::string{line.type});
        CHECK(def.at("additionalProperties") == true);
        CHECK(def.at("properties").at("type").at("const") == std::string{line.type});
        CHECK(def.at("properties").contains("turn") == (line.type != "session"));
    }
    for (const apogee::commands::LineSpec& line : apogee::commands::machine_inbound()) {
        CHECK(defs.contains("line_" + std::string{line.type}));
    }
    // An unknown type is held to nothing but being an object with a string type.
    CHECK(defs.at("outbound").at("allOf").size() == apogee::commands::machine_events().size());
    CHECK(defs.at("outbound").at("properties").at("type").at("type") == "string");
    // A value that may grow is a string, never a closed enum.
    CHECK_FALSE(defs.at("event_result").at("properties").at("finish_reason").contains("enum"));
}
