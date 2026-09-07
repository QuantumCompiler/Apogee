#include "backends/gemini_cli.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "backends/gemini_cli_events.h"
#include "backends/jsonl_framer.h"
#include "harness/errors.h"
#include "support/fake_child.h"

/// The Gemini backend, against recorded output from a real Google-login session.
///
/// Three assertions here carry more weight than the rest, and all three come
/// from the characterization rather than the family template:
///
///  * `--approval-mode plan` AND `--skip-trust` are always both present — this
///    CLI was observed silently overriding the read-only pin in an untrusted
///    folder, so pinning without the second flag pins nothing;
///  * deltas are concatenated with no boundary assumption, because a recorded
///    delta boundary fell inside a two-digit number;
///  * the user turn the CLI echoes back is dropped, or every answer would be
///    prefixed with its own question.
namespace {

using apogee::backends::GeminiCliProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::FakeSpawner;

constexpr std::string_view kPinnedId = "00000000-0000-4000-8000-000000000002";

[[nodiscard]] std::string read_fixture(std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "gemini_cli" / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Fixture {
    std::shared_ptr<FakeSpawner> spawner = std::make_shared<FakeSpawner>();
    std::unique_ptr<GeminiCliProvider> provider;

    explicit Fixture(std::vector<std::string> scripts = {}) {
        spawner->stdout_scripts = std::move(scripts);

        GeminiCliProvider::Options options;
        options.backend_name = "gem-sub";
        options.model = "gemini-3.5-flash";

        auto copy = spawner;
        provider = std::make_unique<GeminiCliProvider>(
            std::move(options),
            [copy](const apogee::platform::ChildCommand& command, std::string& error) {
                return (*copy)(command, error);
            },
            [] { return std::string{kPinnedId}; });
    }
};

[[nodiscard]] ChatRequest turn(std::vector<ChatMessage> messages) {
    ChatRequest request;
    request.messages = std::move(messages);
    return request;
}

[[nodiscard]] bool has(const std::vector<std::string>& args, std::string_view value) {
    return std::find(args.begin(), args.end(), value) != args.end();
}

[[nodiscard]] std::string value_after(const std::vector<std::string>& args, std::string_view flag) {
    const auto it = std::find(args.begin(), args.end(), flag);
    if (it == args.end() || std::next(it) == args.end()) {
        return {};
    }
    return *std::next(it);
}

}  // namespace

TEST_CASE("the read-only pin and its trust escape are inseparable", "[backends][gemini][safety]") {
    // THE safety rule on this backend, and the reason it needs its own test:
    // `--approval-mode plan` alone is NOT sufficient. Invoking in an untrusted
    // folder was observed printing 'Approval mode overridden to "default"
    // because the current folder is not trusted' -- so the pin without
    // --skip-trust is a setting the CLI has already shown it will replace.
    // Apogee spawns wherever the user's shell is, which is usually untrusted.
    GeminiCliProvider::Options options;
    options.model = "gemini-3.5-flash";

    for (const auto invocation :
         {GeminiCliProvider::Invocation::Fresh, GeminiCliProvider::Invocation::Resume}) {
        const std::vector<std::string> args =
            GeminiCliProvider::build_arguments(options, invocation, "sid-1");

        REQUIRE(has(args, "--approval-mode"));
        CHECK(value_after(args, "--approval-mode") == "plan");
        // Without this the pin above is advisory, and a headless run in an
        // untrusted directory refuses to start at all.
        CHECK(has(args, "--skip-trust"));

        // The escapes must never appear.
        CHECK_FALSE(has(args, "--yolo"));
        CHECK_FALSE(has(args, "-y"));
        CHECK_FALSE(has(args, "yolo"));
        CHECK_FALSE(has(args, "auto_edit"));
        CHECK_FALSE(has(args, "default"));
    }
}

TEST_CASE("output sanitisation is never disabled", "[backends][gemini][safety]") {
    // --raw-output disables sanitisation of model output and the CLI itself
    // warns it is a security risk. There is no config key that reaches it.
    GeminiCliProvider::Options options;
    const std::vector<std::string> args =
        GeminiCliProvider::build_arguments(options, GeminiCliProvider::Invocation::Fresh, "sid-1");
    CHECK_FALSE(has(args, "--raw-output"));
}

TEST_CASE("a fresh turn names the session and a resumed turn resumes it",
          "[backends][gemini][flags]") {
    // Verified on 0.46.0: --resume takes the session UUID. An earlier note
    // written before a login existed claimed it took only `latest` or an index
    // number; the recording disproved it, and this pins the corrected fact.
    GeminiCliProvider::Options options;
    options.model = "gemini-3.5-flash";

    const std::vector<std::string> fresh =
        GeminiCliProvider::build_arguments(options, GeminiCliProvider::Invocation::Fresh, "sid-1");
    const std::vector<std::string> resumed =
        GeminiCliProvider::build_arguments(options, GeminiCliProvider::Invocation::Resume, "sid-1");

    CHECK(value_after(fresh, "--session-id") == "sid-1");
    CHECK_FALSE(has(fresh, "--resume"));

    CHECK(value_after(resumed, "--resume") == "sid-1");
    CHECK_FALSE(has(resumed, "--session-id"));

    // Both forms need the event stream and the model.
    for (const auto& args : {fresh, resumed}) {
        CHECK(value_after(args, "--output-format") == "stream-json");
        CHECK(value_after(args, "--model") == "gemini-3.5-flash");
    }
}

TEST_CASE("gemini: recorded output yields the answer and real usage",
          "[backends][gemini][replay]") {
    Fixture fixture{{read_fixture("simple_text.jsonl")}};

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("Reply with exactly: OK")}), options);

    CHECK(response.message.content.plain_text() == "OK");
    CHECK(streamed == "OK");
    CHECK(response.usage.prompt_tokens == 9284);
    CHECK(response.usage.completion_tokens == 27);
}

TEST_CASE("the echoed user turn never reaches the answer", "[backends][gemini][replay]") {
    // The CLI replays the prompt back as a `message` with role "user". Keeping
    // it would put the question at the top of its own answer.
    Fixture fixture{{read_fixture("simple_text.jsonl")}};

    const auto response =
        fixture.provider->chat(turn({ChatMessage::user("Reply with exactly: OK")}), {});
    const std::string text = response.message.content.plain_text();

    CHECK(text == "OK");
    CHECK(text.find("Reply with exactly") == std::string::npos);
}

TEST_CASE("a long answer arrives as several deltas, concatenated verbatim",
          "[backends][gemini][replay]") {
    // The capability that separates this backend from codex (message-level) and
    // ollama (no stream at all). The reassembled answer must be exact: one
    // recorded boundary falls INSIDE the number 13, so any per-chunk trimming
    // or line assumption corrupts it.
    Fixture fixture{{read_fixture("streamed_deltas.jsonl")}};

    std::vector<std::string> chunks;
    apogee::harness::StreamOptions options;
    options.on_token = [&chunks](std::string_view chunk) { chunks.emplace_back(chunk); };

    const auto response = fixture.provider->stream_chat(
        turn({ChatMessage::user("Count from 1 to 20, one number per line, nothing else.")}),
        options);

    CHECK(chunks.size() == 3);

    std::string expected;
    for (int n = 1; n <= 20; ++n) {
        expected += std::to_string(n);
        if (n != 20) {
            expected += "\n";
        }
    }
    CHECK(response.message.content.plain_text() == expected);

    // The split really is mid-number -- the property this test exists to pin.
    CHECK(chunks[1].back() == '1');
    CHECK(chunks[2].front() == '3');
}

TEST_CASE("the answering model is reported rather than the CLI placeholder",
          "[backends][gemini][replay]") {
    // `init` says "auto" and one turn runs across several models. Reporting the
    // one with the most output tokens gives a caller something actionable;
    // "auto" would name nothing a user could pin or price.
    Fixture fixture{{read_fixture("simple_text.jsonl")}};

    const auto response = fixture.provider->chat(turn({ChatMessage::user("hi")}), {});

    CHECK(response.model == "gemini-3.1-flash-lite");
    CHECK(response.model != "auto");
}

TEST_CASE("tool activity is surfaced as status, not as answer text",
          "[backends][gemini][replay][tools]") {
    // This CLI runs its own tools while answering. They belong in the status
    // channel; folding their output into the answer would show the user the
    // machinery instead of the reply.
    Fixture fixture{{read_fixture("tool_use_plan_mode.jsonl")}};

    std::vector<std::string> tools;
    apogee::harness::StreamOptions options;
    options.on_status = [&tools](const apogee::harness::StatusEvent& event) {
        if (event.type == apogee::harness::StatusEvent::Type::ToolCall &&
            event.phase == apogee::harness::StatusEvent::Phase::Start) {
            tools.push_back(event.name);
        }
    };

    const auto response = fixture.provider->stream_chat(
        turn({ChatMessage::user("Create a file named canary.txt")}), options);

    REQUIRE(tools.size() == 1);
    CHECK(tools.front() == "update_topic");

    const std::string text = response.message.content.plain_text();
    CHECK(text.find("Plan Mode") != std::string::npos);
    // The tool's own output must not have leaked into the answer.
    CHECK(text.find("[!STRATEGY]") == std::string::npos);
}

TEST_CASE("the session id is Apogee's and the next turn resumes it",
          "[backends][gemini][session]") {
    // The cleanest continuity story in the family: Apogee GENERATES the id, so
    // there is nothing to capture from the stream and no session file to read.
    Fixture fixture{{read_fixture("simple_text.jsonl"), read_fixture("resumed_session.jsonl")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("Count to 20")}), {});
    CHECK(fixture.provider->session_id() == kPinnedId);

    // Turn one named the session; it did not learn it.
    const std::vector<std::string>& first = fixture.spawner->commands.front();
    CHECK(value_after(first, "--session-id") == std::string{kPinnedId});

    (void)fixture.provider->chat(
        turn({ChatMessage::user("Count to 20"), ChatMessage::assistant("...20"),
              ChatMessage::user("What number did you stop at?")}),
        {});

    // Per-turn spawn is expected here -- but the second one resumed.
    CHECK(fixture.provider->spawn_count() == 2);
    const std::vector<std::string>& second = fixture.spawner->commands.back();
    CHECK(value_after(second, "--resume") == std::string{kPinnedId});
}

TEST_CASE("gemini: a resumed turn sends only the new message", "[backends][gemini][session]") {
    // The CLI holds the conversation on a resumed session, so re-sending the
    // history would duplicate it.
    Fixture fixture{{read_fixture("simple_text.jsonl"), read_fixture("resumed_session.jsonl")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("first question")}), {});
    (void)fixture.provider->chat(
        turn({ChatMessage::user("first question"), ChatMessage::assistant("ok"),
              ChatMessage::user("second question")}),
        {});

    const std::string& prompt = fixture.spawner->commands.back().back();
    CHECK(prompt == "second question");
    CHECK(prompt.find("first question") == std::string::npos);
}

TEST_CASE("the prompt is passed as a flag, never as a bare positional",
          "[backends][gemini][flags]") {
    // The positional `query` defaults to INTERACTIVE on this CLI, so a backend
    // that passed the prompt positionally would hang waiting for a terminal
    // that is not there.
    Fixture fixture{{read_fixture("simple_text.jsonl")}};
    (void)fixture.provider->chat(turn({ChatMessage::user("hello there")}), {});

    const std::vector<std::string>& args = fixture.spawner->commands.front();
    REQUIRE(args.size() >= 2);
    CHECK(args[args.size() - 2] == "--prompt");
    // A fresh session sends the rendered history, so the text carries its role
    // prefix -- what matters here is that it is the VALUE of --prompt.
    CHECK(args.back() == "User: hello there");
}

TEST_CASE("gemini: a side request never joins the conversation's session",
          "[backends][gemini][session]") {
    // The family's SideRequest rule: a background title must not become a turn
    // of the conversation the user is having.
    Fixture fixture{{read_fixture("simple_text.jsonl"), read_fixture("simple_text.jsonl"),
                     read_fixture("resumed_session.jsonl")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("real question")}), {});
    const std::string conversation = fixture.provider->session_id();
    REQUIRE_FALSE(conversation.empty());

    ChatRequest background = turn({ChatMessage::user("summarise this")});
    background.transient.side_request = true;
    (void)fixture.provider->chat(background, {});

    // It ran on a FRESH session...
    CHECK_FALSE(has(fixture.spawner->commands.back(), "--resume"));
    // ...and the conversation's own session survived untouched.
    CHECK(fixture.provider->session_id() == conversation);
}

TEST_CASE("an unknown event type is ignored rather than fatal",
          "[backends][gemini][compatibility]") {
    // The family survival rule: a CLI upgrade that adds an event type must not
    // be an outage. Apogee demands this tolerance of its own consumers in
    // machine mode; it owes the same to the CLIs it consumes.
    const std::string bytes =
        R"({"type":"init","session_id":"s","model":"auto"})"
        "\n"
        R"({"type":"telemetry_from_a_later_build","payload":{"anything":1}})"
        "\n"
        R"({"type":"message","role":"assistant","content":"fine","delta":true})"
        "\n"
        R"({"type":"result","status":"success","stats":{"input_tokens":1,"output_tokens":2}})"
        "\n";

    Fixture fixture{{bytes}};
    const auto response = fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
    CHECK(response.message.content.plain_text() == "fine");
}

TEST_CASE("a failing result becomes a provider error", "[backends][gemini][errors]") {
    const std::string bytes = R"({"type":"init","session_id":"s","model":"auto"})"
                              "\n"
                              R"({"type":"result","status":"quota_exceeded","stats":{}})"
                              "\n";

    Fixture fixture{{bytes}};
    CHECK_THROWS_AS(fixture.provider->chat(turn({ChatMessage::user("hi")}), {}),
                    apogee::harness::ProviderError);
}

TEST_CASE("stdout noise on stderr cannot corrupt the event stream", "[backends][gemini][replay]") {
    // Every real run of this CLI writes a 256-color warning and [STARTUP] lines
    // to stderr. They must stay there: merged into stdout they would sit in
    // front of the framer as non-JSON.
    Fixture fixture{{read_fixture("simple_text.jsonl")}};
    fixture.spawner->stderr_script =
        "Warning: 256-color support not detected.\n"
        "[STARTUP] Phase 'cleanup_ops' was started but never ended. Skipping metrics.\n";

    const auto response = fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
    CHECK(response.message.content.plain_text() == "OK");
}

TEST_CASE("gemini: the event stream parses identically at every chunk size",
          "[backends][gemini][replay]") {
    // The family framing guardrail. This CLI emits real deltas, so a split line
    // here loses part of an ANSWER rather than one whole message -- which makes
    // the adversarial replay matter more on this backend than on codex.
    for (const std::string& name : {"simple_text.jsonl", "streamed_deltas.jsonl",
                                    "resumed_session.jsonl", "tool_use_plan_mode.jsonl"}) {
        INFO("fixture: " << name);
        const std::string bytes = read_fixture(name);

        std::vector<std::string> whole;
        for (const auto& event : apogee::backends::gemini_cli::parse_stream(bytes)) {
            whole.push_back(apogee::backends::gemini_cli::describe(event));
        }
        REQUIRE_FALSE(whole.empty());

        for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                        std::size_t{7}, std::size_t{64}, std::size_t{4096}}) {
            INFO("chunk " << chunk);
            std::vector<std::string> seen;
            apogee::backends::JsonlFramer framer;
            const auto handle = [&seen](std::string_view line) {
                if (auto event = apogee::backends::gemini_cli::parse_line(line)) {
                    seen.push_back(apogee::backends::gemini_cli::describe(*event));
                }
            };
            for (std::size_t offset = 0; offset < bytes.size(); offset += chunk) {
                framer.feed(bytes.substr(offset, chunk), handle);
            }
            framer.flush(handle);
            CHECK(seen == whole);
        }
    }
}
