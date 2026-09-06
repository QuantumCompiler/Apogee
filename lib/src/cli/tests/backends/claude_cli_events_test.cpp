#include "backends/claude_cli_events.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "backends/jsonl_framer.h"

/// The fixture-replay suite — this item's named guardrail.
///
/// The whole point is the **chunk-size sweep**. A child's stdout is a pipe, so
/// a JSON object routinely spans several reads, and the failure when that is
/// handled wrong is not a clean parse error: it is a line split into two
/// invalid halves, both silently dropped, and a turn that loses its middle.
/// Feeding each fixture at 1, 2, 3, 7, 4096 bytes and whole-file, and
/// requiring an identical event sequence every time, catches that entire class
/// offline — no network, no API charge.
///
/// **What these fixtures are.** They are transcribed from the `[verified
/// 2.1.233]` wire table in the item's design notes, not recorded from a live
/// session. That is a real limitation and it is recorded in MILESTONES: they
/// prove the parser matches the documented schema, and they would not catch
/// the CLI's schema drifting away from that document. Re-recording them
/// against a live `claude` is a small, well-defined follow-up.
namespace {

using apogee::backends::CliEvent;
using apogee::backends::claude_cli::describe;
using apogee::backends::claude_cli::parse_stream;

[[nodiscard]] std::filesystem::path fixture_dir() {
    return std::filesystem::path{APOGEE_TEST_FIXTURES} / "claude_cli";
}

[[nodiscard]] std::string read_fixture(std::string_view name) {
    const std::filesystem::path path = fixture_dir() / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::vector<std::string> described(const std::vector<CliEvent>& events) {
    std::vector<std::string> out;
    out.reserve(events.size());
    for (const CliEvent& event : events) {
        out.push_back(describe(event));
    }
    return out;
}

/// Replays `bytes` through the framer in `chunk`-sized pieces.
[[nodiscard]] std::vector<CliEvent> replay_in_chunks(std::string_view bytes, std::size_t chunk) {
    std::vector<CliEvent> events;
    apogee::backends::JsonlFramer framer;
    const auto handle = [&events](std::string_view line) {
        if (auto event = apogee::backends::claude_cli::parse_line(line)) {
            events.push_back(std::move(*event));
        }
    };
    for (std::size_t offset = 0; offset < bytes.size(); offset += chunk) {
        framer.feed(bytes.substr(offset, std::min(chunk, bytes.size() - offset)), handle);
    }
    framer.flush(handle);
    return events;
}

/// Every fixture the suite knows about.
[[nodiscard]] const std::vector<std::string>& all_fixtures() {
    static const std::vector<std::string> kFixtures{
        "simple_text.jsonl",       "thinking_text.jsonl",
        "thinking_redacted.jsonl", "structured_output.jsonl",
        "tool_use.jsonl",          "api_retry.jsonl",
        "error_max_turns.jsonl",   "truncated_mid_object.jsonl",
        "noise_before_json.jsonl",
    };
    return kFixtures;
}

}  // namespace

TEST_CASE("every fixture parses identically at every chunk size",
          "[backends][claude_cli][replay]") {
    // THE guardrail. One byte at a time is not a stunt: it is the only chunk
    // size that puts a boundary between every pair of bytes, so a framer that
    // mishandles any seam fails here.
    for (const std::string& name : all_fixtures()) {
        INFO("fixture: " << name);
        const std::string bytes = read_fixture(name);

        const std::vector<std::string> whole = described(parse_stream(bytes));
        REQUIRE_FALSE(whole.empty());

        for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                        std::size_t{7}, std::size_t{64}, std::size_t{4096}}) {
            INFO("chunk size: " << chunk);
            CHECK(described(replay_in_chunks(bytes, chunk)) == whole);
        }
    }
}

TEST_CASE("a plain text turn yields deltas and a terminal result",
          "[backends][claude_cli][replay]") {
    const std::vector<CliEvent> events = parse_stream(read_fixture("simple_text.jsonl"));
    const std::vector<std::string> seen = described(events);

    CHECK(seen.front() == "notice:init");
    CHECK(seen.back() == "result:success");

    // Framing events carry nothing and must not appear.
    for (const std::string& label : seen) {
        CHECK(label.find("message_start") == std::string::npos);
        CHECK(label.find("content_block_stop") == std::string::npos);
    }

    std::string answer;
    for (const CliEvent& event : events) {
        if (const auto* text = std::get_if<apogee::backends::TextDelta>(&event)) {
            answer += text->text;
        }
    }
    CHECK(answer == "Hello, world");

    const auto* complete = std::get_if<apogee::backends::TurnComplete>(&events.back());
    REQUIRE(complete != nullptr);
    CHECK(complete->session_id == "11111111-2222-3333-4444-555555555555");
    CHECK(complete->num_turns == 1);
    CHECK(complete->cost_usd > 0.0);
    CHECK(complete->input_tokens == 12);
    CHECK(complete->output_tokens == 4);
    CHECK_FALSE(complete->is_error);
}

TEST_CASE("thinking deltas never reach the answer stream",
          "[backends][claude_cli][replay][thinking]") {
    // The typed-union payoff. If thinking and answer text shared a variant,
    // separating them downstream would need in-band markers and a filter that
    // tolerates a marker split across reads.
    const std::vector<CliEvent> events = parse_stream(read_fixture("thinking_text.jsonl"));

    std::string answer;
    std::string thinking;
    for (const CliEvent& event : events) {
        if (const auto* text = std::get_if<apogee::backends::TextDelta>(&event)) {
            answer += text->text;
        }
        if (const auto* thought = std::get_if<apogee::backends::ThinkingDelta>(&event)) {
            thinking += thought->text;
        }
    }

    CHECK(answer == "The answer is 42.");
    CHECK(thinking == "Let me work through this.");
    CHECK(answer.find("Let me work") == std::string::npos);

    // signature_delta has no payload and must be dropped, not emitted as text.
    CHECK(answer.find("abc123") == std::string::npos);
    CHECK(thinking.find("abc123") == std::string::npos);
}

TEST_CASE("a redacted-thinking model reports progress with no thinking text",
          "[backends][claude_cli][replay][thinking]") {
    // The empty-payload guard is load-bearing, and this fixture is why it
    // exists: the model emits `thinking_delta` events whose text is EMPTY and
    // reports progress only through `estimated_tokens`. A surface that opens a
    // thinking view on the first delta shows an empty box for the whole turn.
    const std::vector<CliEvent> events = parse_stream(read_fixture("thinking_redacted.jsonl"));

    std::string thinking;
    std::vector<std::int64_t> estimates;
    for (const CliEvent& event : events) {
        if (const auto* thought = std::get_if<apogee::backends::ThinkingDelta>(&event)) {
            thinking += thought->text;
        }
        if (const auto* tokens = std::get_if<apogee::backends::ThinkingTokens>(&event)) {
            estimates.push_back(tokens->estimated);
        }
    }

    CHECK(thinking.empty());
    // Progress is available, and monotonic, so a spinner has something to show.
    REQUIRE(estimates.size() == 3);
    CHECK(estimates == std::vector<std::int64_t>{128, 512, 900});
}

TEST_CASE("a schema run carries its value on the result, beside the prose",
          "[backends][claude_cli][replay][schema]") {
    // `--json-schema` is implemented as a forced tool call AFTER a normal prose
    // answer. Both stream; only the terminal event has the conforming value.
    const std::vector<CliEvent> events = parse_stream(read_fixture("structured_output.jsonl"));

    std::string prose;
    for (const CliEvent& event : events) {
        if (const auto* text = std::get_if<apogee::backends::TextDelta>(&event)) {
            prose += text->text;
        }
    }
    CHECK(prose == "This document is about finance.");

    const auto* complete = std::get_if<apogee::backends::TurnComplete>(&events.back());
    REQUIRE(complete != nullptr);
    CHECK(complete->structured_output == R"({"category":"finance"})");
    // The extra generation the design notes warn about, visible in the count.
    CHECK(complete->num_turns == 3);
}

TEST_CASE("a turn with no schema has no structured output",
          "[backends][claude_cli][replay][schema]") {
    const std::vector<CliEvent> events = parse_stream(read_fixture("simple_text.jsonl"));
    const auto* complete = std::get_if<apogee::backends::TurnComplete>(&events.back());
    REQUIRE(complete != nullptr);
    CHECK(complete->structured_output.empty());
}

TEST_CASE("tool use and its result both surface", "[backends][claude_cli][replay]") {
    const std::vector<CliEvent> events = parse_stream(read_fixture("tool_use.jsonl"));
    const std::vector<std::string> seen = described(events);

    CHECK(std::find(seen.begin(), seen.end(), "tool_use:Read") != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), "tool_result:ok") != seen.end());
}

TEST_CASE("a retry is a notice, not a failure", "[backends][claude_cli][replay]") {
    // An api_retry means the CLI is handling a transient problem itself. A
    // backend that treated it as an error would abort a turn that recovers.
    const std::vector<CliEvent> events = parse_stream(read_fixture("api_retry.jsonl"));
    const std::vector<std::string> seen = described(events);

    CHECK(std::find(seen.begin(), seen.end(), "notice:api_retry") != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), "notice:rate_limit") != seen.end());
    CHECK(seen.back() == "result:success");
}

TEST_CASE("an error result names its subtype", "[backends][claude_cli][replay]") {
    const std::vector<CliEvent> events = parse_stream(read_fixture("error_max_turns.jsonl"));
    const auto* complete = std::get_if<apogee::backends::TurnComplete>(&events.back());
    REQUIRE(complete != nullptr);
    CHECK(complete->is_error);
    CHECK(complete->error_subtype == "error_max_turns");
}

TEST_CASE("a truncated final object costs one event, not the stream",
          "[backends][claude_cli][replay]") {
    // What a crashed child leaves behind. Everything before the truncation
    // must survive: dropping the whole stream would lose a turn's worth of
    // real output because its last line was cut.
    const std::vector<CliEvent> events = parse_stream(read_fixture("truncated_mid_object.jsonl"));
    const std::vector<std::string> seen = described(events);

    REQUIRE(seen.size() == 2);
    CHECK(seen[0] == "notice:init");
    CHECK(seen[1] == "text:Partial");
}

TEST_CASE("non-JSON lines on stdout are skipped, not fatal", "[backends][claude_cli][replay]") {
    // A vendor CLI is entitled to print a banner or a warning. Treating that
    // as a parse failure turns ordinary noise into a broken turn.
    const std::vector<CliEvent> events = parse_stream(read_fixture("noise_before_json.jsonl"));
    const std::vector<std::string> seen = described(events);

    CHECK(seen.front() == "notice:init");
    CHECK(seen.back() == "result:success");
}

TEST_CASE("an unknown wire type is dropped rather than fatal", "[backends][claude_cli]") {
    // The rule that keeps a CLI upgrade from being an outage. New event types
    // get added; this must be a no-op, not a crash.
    CHECK_FALSE(apogee::backends::claude_cli::parse_line(
                    R"({"type":"something_invented_next_year","payload":{"a":1}})")
                    .has_value());

    // And malformed bytes cost one line.
    CHECK_FALSE(apogee::backends::claude_cli::parse_line(R"({"type":"result",)").has_value());
    CHECK_FALSE(apogee::backends::claude_cli::parse_line("not json at all").has_value());
    CHECK_FALSE(apogee::backends::claude_cli::parse_line("").has_value());
    CHECK_FALSE(apogee::backends::claude_cli::parse_line("[1,2,3]").has_value());
}

TEST_CASE("a user message renders as one JSONL line", "[backends][claude_cli]") {
    // One object per line is what `--input-format stream-json` accepts, and an
    // embedded newline would be read as two malformed messages.
    const std::string line = apogee::backends::claude_cli::user_message_line("hi\nthere");
    CHECK(line.find('\n') == std::string::npos);

    const nlohmann::json parsed = nlohmann::json::parse(line);
    CHECK(parsed.at("type") == "user");
    CHECK(parsed.at("message").at("role") == "user");
    CHECK(parsed.at("message").at("content").at(0).at("text") == "hi\nthere");
}
