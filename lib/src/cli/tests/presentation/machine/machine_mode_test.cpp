#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "machine/json_reporter.h"
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
    reporter.begin_session("test-model");
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
    reporter.begin_session("m");

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

    const apogee::agentloop::Answers answers =
        apogee::commands::make_driver_ask_fn(reporter, in)(one_question());

    CHECK(answers.values == std::vector<std::string>{"Yes"});
    CHECK(types(out.str()) == std::vector<std::string>{"question"});
}

TEST_CASE("free text is accepted where an option was offered", "[commands][machine][ask]") {
    // The offered options are a convenience, not a constraint on what the user
    // is allowed to say -- the same rule the terminal AskFn follows.
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{"{\"type\":\"answer\",\"text\":\"only if it is a backup\"}\n"};

    CHECK(apogee::commands::make_driver_ask_fn(reporter, in)(one_question()).values ==
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

    CHECK(apogee::commands::make_driver_ask_fn(reporter, in)(one_question()).values ==
          std::vector<std::string>{"No"});
}

TEST_CASE("a driver that hangs up mid-question fails the turn", "[commands][machine][ask]") {
    // Throwing is what the seam asks for: the loop rolls the half-turn out of
    // history, so nothing dangling is persisted. Fabricating an answer would
    // be worse than failing -- which is why the AskFn is allowed to throw.
    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{""};

    CHECK_THROWS_AS(apogee::commands::make_driver_ask_fn(reporter, in)(one_question()),
                    std::runtime_error);
}

TEST_CASE("every question must be answered before the turn resumes", "[commands][machine][ask]") {
    apogee::agentloop::QuestionRequest two = one_question();
    two.questions.push_back(two.questions.front());
    two.questions.back().question = "And overwrite the backup too?";

    std::ostringstream out;
    JsonReporter reporter{out};
    std::istringstream in{"{\"type\":\"answer\",\"text\":\"Yes\"}\n"};

    CHECK_THROWS_AS(apogee::commands::make_driver_ask_fn(reporter, in)(two), std::runtime_error);
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
