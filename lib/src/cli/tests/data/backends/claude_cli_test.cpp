#include "backends/claude_cli.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "contracts/errors.h"
#include "support/fake_child.h"

/// The provider, against a scripted child.
///
/// Every acceptance criterion on this item is about behaviour a live `claude`
/// will not produce on request — a child that dies mid-turn, a `--resume` the
/// CLI refuses, a schema field that goes missing. A scripted child makes each
/// of those an ordinary test, and costs no subscription quota to run.
namespace {

using apogee::backends::ClaudeCliMode;
using apogee::backends::ClaudeCliProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::FakeSpawner;

/// A turn's worth of wire output, ending in the terminal event.
[[nodiscard]] std::string turn_script(std::string_view text, std::string_view session = "sess-1") {
    std::string out;
    out += R"({"type":"system","subtype":"init","session_id":")";
    out += session;
    out += R"(","model":"claude-sonnet-5"})";
    out += "\n";
    out +=
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":")";
    out += text;
    out += R"("}}})";
    out += "\n";
    out += R"({"type":"result","subtype":"success","is_error":false,"session_id":")";
    out += session;
    out += R"(","result":")";
    out += text;
    out +=
        R"(","num_turns":1,"total_cost_usd":0.001,"usage":{"input_tokens":7,"output_tokens":3}})";
    out += "\n";
    return out;
}

struct Fixture {
    std::shared_ptr<FakeSpawner> spawner = std::make_shared<FakeSpawner>();
    std::unique_ptr<ClaudeCliProvider> provider;

    explicit Fixture(std::vector<std::string> scripts = {},
                     std::chrono::milliseconds turn_timeout = std::chrono::milliseconds{600000}) {
        spawner->stdout_scripts = std::move(scripts);

        ClaudeCliProvider::Options options;
        options.backend_name = "claude-cli";
        options.model = "claude-sonnet-5";
        options.turn_timeout = turn_timeout;
        auto spawner_copy = spawner;
        provider = std::make_unique<ClaudeCliProvider>(
            std::move(options),
            [spawner_copy](const apogee::platform::ChildCommand& command, std::string& error) {
                return (*spawner_copy)(command, error);
            });
    }
};

[[nodiscard]] ChatRequest turn(std::vector<ChatMessage> messages) {
    ChatRequest request;
    request.messages = std::move(messages);
    return request;
}

[[nodiscard]] bool has_flag(const std::vector<std::string>& arguments, std::string_view flag) {
    return std::find(arguments.begin(), arguments.end(), flag) != arguments.end();
}

[[nodiscard]] std::string value_after(const std::vector<std::string>& arguments,
                                      std::string_view flag) {
    const auto at = std::find(arguments.begin(), arguments.end(), flag);
    return at == arguments.end() || std::next(at) == arguments.end() ? std::string{}
                                                                     : *std::next(at);
}

/// The second bug sweep's probe (57b, and M34's reproduction): Claude Code
/// answering an expired login with `is_error` under `subtype: "success"`, its
/// reason in `result`.
constexpr std::string_view kExpiredLogin =
    "Failed to authenticate: OAuth session expired and could not be refreshed";

[[nodiscard]] std::string expired_login_script(std::string_view session = "title-sess") {
    std::string out;
    out += R"({"type":"system","subtype":"init","session_id":")";
    out += session;
    out += R"(","model":"claude-sonnet-5"})";
    out += "\n";
    out += R"({"type":"result","subtype":"success","is_error":true,"session_id":")";
    out += session;
    out += R"(","result":")";
    out += kExpiredLogin;
    out += R"(","num_turns":1})";
    out += "\n";
    return out;
}

/// A one-shot that streams half an answer and never finishes.
[[nodiscard]] std::string stalled_script(std::string_view session = "stall-sess") {
    std::string out;
    out += R"({"type":"system","subtype":"init","session_id":")";
    out += session;
    out += R"(","model":"claude-sonnet-5"})";
    out += "\n";
    out +=
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Half an"}}})";
    out += "\n";
    return out;
}

[[nodiscard]] ChatRequest side(std::string_view text) {
    ChatRequest request;
    request.messages = {ChatMessage::user(std::string{text})};
    request.transient.side_request = true;
    return request;
}

/// The message a call threw, or empty when it returned.
template <typename Call>
[[nodiscard]] std::string thrown_by(Call&& call) {
    try {
        (void)std::forward<Call>(call)();
    } catch (const apogee::harness::ProviderError& error) {
        return error.what();
    }
    return {};
}

}  // namespace

TEST_CASE("the invocation carries the flags that make streaming work",
          "[backends][claude_cli][flags]") {
    // These are load-bearing and easy to get subtly wrong, which is why the
    // flag policy is a pure function with its own test.
    apogee::backends::ClaudeCliInvocation invocation;
    const std::vector<std::string> arguments = apogee::backends::build_arguments(invocation);

    CHECK(has_flag(arguments, "-p"));
    CHECK(has_flag(arguments, "--output-format"));
    CHECK(has_flag(arguments, "stream-json"));
    // stream-json REQUIRES --verbose; the CLI rejects the pair without it.
    CHECK(has_flag(arguments, "--verbose"));
    // The flag that produces token deltas at all. Without it the stream is
    // message-granular -- the choppiness this whole design removes.
    CHECK(has_flag(arguments, "--include-partial-messages"));
    // stdin as a stream is what lets ONE process serve many turns.
    CHECK(has_flag(arguments, "--input-format"));

    // Subscription mode must NOT pass --bare: it would disable the very auth
    // the mode exists to use.
    CHECK_FALSE(has_flag(arguments, "--bare"));
}

TEST_CASE("bare mode passes --bare and subscription mode does not",
          "[backends][claude_cli][flags][auth]") {
    apogee::backends::ClaudeCliInvocation bare;
    bare.mode = ClaudeCliMode::Bare;
    CHECK(has_flag(apogee::backends::build_arguments(bare), "--bare"));

    apogee::backends::ClaudeCliInvocation subscription;
    subscription.mode = ClaudeCliMode::Subscription;
    CHECK_FALSE(has_flag(apogee::backends::build_arguments(subscription), "--bare"));
}

TEST_CASE("a one-shot invocation does not open stdin as a stream",
          "[backends][claude_cli][flags]") {
    apogee::backends::ClaudeCliInvocation invocation;
    invocation.streaming_stdin = false;
    CHECK_FALSE(has_flag(apogee::backends::build_arguments(invocation), "--input-format"));
}

TEST_CASE("a schema invocation passes the schema inline", "[backends][claude_cli][flags]") {
    // Inline JSON is accepted -- no temp file, and no cleanup to get wrong.
    apogee::backends::ClaudeCliInvocation invocation;
    invocation.json_schema = R"({"type":"object"})";
    const std::vector<std::string> arguments = apogee::backends::build_arguments(invocation);

    CHECK(has_flag(arguments, "--json-schema"));
    CHECK(has_flag(arguments, R"({"type":"object"})"));
}

TEST_CASE("an effort level is the CLI's own --effort, by name", "[backends][claude_cli][effort]") {
    using apogee::harness::Effort;
    using apogee::harness::Thinking;
    CHECK(apogee::backends::claude_effort(Thinking{.effort = Effort::XHigh}) == "xhigh");
    CHECK(apogee::backends::claude_effort(Thinking{.effort = Effort::Max}) == "max");
    CHECK(apogee::backends::claude_effort(Thinking{}).empty());
    CHECK(apogee::backends::claude_effort(
              Thinking{.mode = apogee::harness::ThinkingMode::Off, .effort = Effort::Low})
              .empty());

    apogee::backends::ClaudeCliInvocation invocation;
    CHECK_FALSE(has_flag(apogee::backends::build_arguments(invocation), "--effort"));
    invocation.effort = "high";
    const std::vector<std::string> arguments = apogee::backends::build_arguments(invocation);
    const auto at = std::find(arguments.begin(), arguments.end(), "--effort");
    REQUIRE(at != arguments.end());
    REQUIRE(std::next(at) != arguments.end());
    CHECK(*std::next(at) == "high");
}

TEST_CASE("a changed effort level restarts the child on its session, told only what is new",
          "[backends][claude_cli][session][effort]") {
    // The CLI reads --effort at launch alone (39a): a new level is a new
    // child, resumed on the same session so its history is its own.
    Fixture fixture{{turn_script("First.", "sess-e"), turn_script("Second.", "sess-e")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("alpha")}), {});
    CHECK_FALSE(has_flag(fixture.spawner->commands.front(), "--effort"));

    ChatRequest harder = turn(
        {ChatMessage::user("alpha"), ChatMessage::assistant("First."), ChatMessage::user("beta")});
    harder.thinking.effort = apogee::harness::Effort::High;
    const auto second = fixture.provider->chat(harder, {});
    CHECK(second.message.content.plain_text() == "Second.");

    REQUIRE(fixture.spawner->commands.size() == 2);
    const std::vector<std::string>& respawn = fixture.spawner->commands.back();
    CHECK(has_flag(respawn, "--effort"));
    CHECK(has_flag(respawn, "high"));
    CHECK(has_flag(respawn, "--resume"));
    CHECK(has_flag(respawn, "sess-e"));
    // The first child was closed, not killed; the second heard only "beta".
    CHECK(fixture.spawner->children.front()->stdin_closed);
    const auto& writes = fixture.spawner->children.back()->writes;
    REQUIRE(writes.size() == 1);
    CHECK(writes.front().find("beta") != std::string::npos);
    CHECK(writes.front().find("alpha") == std::string::npos);

    // The same level again is the same child.
    ChatRequest same = turn({ChatMessage::user("alpha"), ChatMessage::assistant("First."),
                             ChatMessage::user("beta"), ChatMessage::assistant("Second."),
                             ChatMessage::user("gamma")});
    same.thinking.effort = apogee::harness::Effort::High;
    (void)fixture.provider->stream_chat(same, {});
    CHECK(fixture.spawner->commands.size() == 2);
}

TEST_CASE("a multi-turn conversation reuses one child", "[backends][claude_cli][session]") {
    // THE acceptance criterion. A per-turn spawn pays process startup and MCP
    // discovery on every message; the persistent child is the entire reason
    // this backend is shaped the way it is, and "persistent" is only a claim
    // if it can be counted.
    //
    // ONE script holding BOTH turns, because that is what a persistent child
    // actually produces: a single continuous stream across turns. It is also
    // what proved the provider needed an event queue -- a read that carries
    // past a terminal event must not fold the next turn into this one.
    Fixture fixture{{turn_script("First.") + turn_script("Second.")}};

    const auto first = fixture.provider->chat(turn({ChatMessage::user("one")}), {});
    CHECK(first.message.content.plain_text() == "First.");
    CHECK(fixture.provider->spawn_count() == 1);

    const auto second =
        fixture.provider->chat(turn({ChatMessage::user("one"), ChatMessage::assistant("First."),
                                     ChatMessage::user("two")}),
                               {});
    CHECK(second.message.content.plain_text() == "Second.");

    // Still ONE child: the conversation did not restart.
    CHECK(fixture.provider->spawn_count() == 1);
    REQUIRE(fixture.spawner->children.size() == 1);
}

TEST_CASE("a later turn sends only what is new", "[backends][claude_cli][session]") {
    // The CLI keeps the conversation, so re-sending history every turn would
    // duplicate it -- the model would see the first question twice.
    Fixture fixture{{turn_script("First.") + turn_script("Second.")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("alpha")}), {});
    const std::size_t after_first = fixture.spawner->children.front()->writes.size();

    (void)fixture.provider->chat(turn({ChatMessage::user("alpha"), ChatMessage::assistant("First."),
                                       ChatMessage::user("beta")}),
                                 {});

    const auto& writes = fixture.spawner->children.front()->writes;
    CHECK(writes.size() == after_first + 1);
    CHECK(writes.back().find("beta") != std::string::npos);
    // The first question is not re-sent.
    CHECK(writes.back().find("alpha") == std::string::npos);
}

TEST_CASE("streamed tokens reach the sink and reassemble into the answer",
          "[backends][claude_cli][stream]") {
    Fixture fixture{{turn_script("Hello there")}};

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response = fixture.provider->stream_chat(turn({ChatMessage::user("hi")}), options);

    CHECK(streamed == "Hello there");
    CHECK(response.message.content.plain_text() == "Hello there");
    CHECK(response.usage.prompt_tokens == 7);
    CHECK(response.usage.completion_tokens == 3);
}

TEST_CASE("thinking reaches its own sink, never the answer",
          "[backends][claude_cli][stream][thinking]") {
    // Harness-wide rule: thinking is display only. If it reached the token
    // sink it would be persisted into history and re-sent on every later turn.
    std::string script =
        R"({"type":"system","subtype":"init","session_id":"s","model":"m"})"
        "\n"
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"pondering"}}})"
        "\n"
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"answer"}}})"
        "\n"
        R"({"type":"result","subtype":"success","is_error":false,"session_id":"s","result":"answer","num_turns":1})"
        "\n";
    Fixture fixture{{script}};

    std::string tokens;
    std::string thinking;
    apogee::harness::StreamOptions options;
    options.on_token = [&tokens](std::string_view chunk) { tokens += chunk; };
    options.on_thinking = [&thinking](std::string_view chunk) { thinking += chunk; };

    const auto response = fixture.provider->stream_chat(turn({ChatMessage::user("hi")}), options);

    CHECK(tokens == "answer");
    CHECK(thinking == "pondering");
    CHECK(response.message.content.plain_text().find("pondering") == std::string::npos);
}

TEST_CASE("a redacted-thinking turn reports progress and opens no thinking view",
          "[backends][claude_cli][stream][thinking]") {
    // The empty-payload guard, at provider level. A surface that opened a
    // thinking view on any thinking event would show an empty box all turn.
    std::string script =
        R"({"type":"system","subtype":"init","session_id":"s","model":"m"})"
        "\n"
        R"({"type":"system","subtype":"thinking_tokens","estimated_tokens":256})"
        "\n"
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":""}}})"
        "\n"
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":1,"delta":{"type":"text_delta","text":"done"}}})"
        "\n"
        R"({"type":"result","subtype":"success","is_error":false,"session_id":"s","result":"done","num_turns":1})"
        "\n";
    Fixture fixture{{script}};

    int thinking_calls = 0;
    std::vector<std::int64_t> estimates;
    apogee::harness::StreamOptions options;
    options.on_thinking = [&thinking_calls](std::string_view) { ++thinking_calls; };
    options.on_status = [&estimates](const apogee::harness::StatusEvent& event) {
        if (event.tokens.has_value()) {
            estimates.push_back(*event.tokens);
        }
    };

    (void)fixture.provider->stream_chat(turn({ChatMessage::user("hi")}), options);

    // Never called with an empty payload -- that is the guard.
    CHECK(thinking_calls == 0);
    // But progress is still reported, so a spinner has something to show.
    REQUIRE_FALSE(estimates.empty());
    CHECK(estimates.front() == 256);
}

TEST_CASE("the stream survives arriving one byte at a time", "[backends][claude_cli][stream]") {
    // The provider's own framing path, not just the parser's. A pipe hands
    // over whatever the kernel felt like.
    Fixture fixture{{turn_script("Chunky")}};
    fixture.spawner->chunk_size = 1;

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response = fixture.provider->stream_chat(turn({ChatMessage::user("hi")}), options);
    CHECK(streamed == "Chunky");
    CHECK(response.message.content.plain_text() == "Chunky");
}

TEST_CASE("the session id is captured and used to resume",
          "[backends][claude_cli][session][resume]") {
    // Captured from the terminal event and stored by US. This is exactly why
    // the harness never needs to read the CLI's session files off disk.
    Fixture fixture{{turn_script("First.", "sess-abc"), turn_script("Second.", "sess-abc")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});
    CHECK(fixture.provider->session_id() == "sess-abc");

    // Kill it, as a crash would.
    fixture.spawner->children.front()->kill_now();

    (void)fixture.provider->chat(turn({ChatMessage::user("one"), ChatMessage::assistant("First."),
                                       ChatMessage::user("two")}),
                                 {});

    REQUIRE(fixture.spawner->commands.size() == 2);
    const std::vector<std::string>& respawn = fixture.spawner->commands.back();
    CHECK(has_flag(respawn, "--resume"));
    CHECK(has_flag(respawn, "sess-abc"));
}

TEST_CASE("a refused resume falls back to a fresh child",
          "[backends][claude_cli][session][resume]") {
    // The conversation must not die because an optimisation went stale. Our
    // transcript is authoritative; the CLI's session is a cache.
    Fixture fixture{{turn_script("First.", "sess-xyz"), turn_script("Recovered.")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});
    REQUIRE(fixture.provider->session_id() == "sess-xyz");

    fixture.spawner->children.front()->kill_now();
    // The resume attempt (spawn #1) is refused; the fallback (spawn #2) works.
    fixture.spawner->failing_spawns = {1};

    const auto second =
        fixture.provider->chat(turn({ChatMessage::user("one"), ChatMessage::assistant("First."),
                                     ChatMessage::user("two")}),
                               {});

    CHECK(second.message.content.plain_text() == "Recovered.");
    REQUIRE(fixture.spawner->commands.size() == 3);
    // The refused attempt carried --resume; the fallback did not.
    CHECK(has_flag(fixture.spawner->commands[1], "--resume"));
    CHECK_FALSE(has_flag(fixture.spawner->commands[2], "--resume"));
}

TEST_CASE("a side request never touches the session child", "[backends][claude_cli][session]") {
    // The side-request rule, the same one the local backend applies
    // to its KV cache: background summarisation shares no prefix with the
    // conversation and may run concurrently with a real turn.
    Fixture fixture{{turn_script("Real."), turn_script("A title")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});
    REQUIRE(fixture.spawner->children.size() == 1);
    const std::size_t session_writes = fixture.spawner->children.front()->writes.size();

    ChatRequest background = turn({ChatMessage::user("summarise this")});
    background.transient.side_request = true;
    (void)fixture.provider->chat(background, {});

    // It ran on its OWN child...
    REQUIRE(fixture.spawner->children.size() == 2);
    // ...and the session child received nothing more.
    CHECK(fixture.spawner->children.front()->writes.size() == session_writes);

    // A one-shot carries its prompt in argv rather than streaming stdin.
    CHECK_FALSE(has_flag(fixture.spawner->commands.back(), "--input-format"));
}

TEST_CASE("a schema call returns the structured value and hides the prose",
          "[backends][claude_cli][schema]") {
    // `--json-schema` is a forced tool call AFTER a prose answer. Without
    // suppression the user watches a paragraph stream only to be replaced by a
    // JSON object.
    std::string script =
        R"({"type":"system","subtype":"init","session_id":"s","model":"m"})"
        "\n"
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"I think it is finance."}}})"
        "\n"
        R"({"type":"result","subtype":"success","is_error":false,"session_id":"s","result":"{\"category\":\"finance\"}","structured_output":{"category":"finance"},"num_turns":3})"
        "\n";
    Fixture fixture{{script}};

    std::string streamed;
    // The sink is wired, and must stay silent.
    const std::string value =
        fixture.provider->complete_structured("classify this", R"({"type":"object"})", {});

    CHECK(value == R"({"category":"finance"})");
    CHECK(streamed.empty());
    CHECK(has_flag(fixture.spawner->commands.back(), "--json-schema"));
}

TEST_CASE("a schema call degrades to the held prose when the field is missing",
          "[backends][claude_cli][schema]") {
    // If a future CLI stops populating `structured_output`, a clerk that
    // returned nothing would be worse than one that returns text to parse.
    std::string script =
        R"({"type":"system","subtype":"init","session_id":"s","model":"m"})"
        "\n"
        R"({"type":"stream_event","event":{"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"finance"}}})"
        "\n"
        R"({"type":"result","subtype":"success","is_error":false,"session_id":"s","result":"finance","num_turns":1})"
        "\n";
    Fixture fixture{{script}};

    CHECK(fixture.provider->complete_structured("classify", R"({"type":"object"})", {}) ==
          "finance");
}

TEST_CASE("an error result becomes a provider error naming its subtype",
          "[backends][claude_cli][errors]") {
    std::string script =
        R"({"type":"system","subtype":"init","session_id":"s","model":"m"})"
        "\n"
        R"({"type":"result","subtype":"error_max_turns","is_error":true,"session_id":"s","num_turns":25})"
        "\n";
    Fixture fixture{{script}};

    try {
        (void)fixture.provider->chat(turn({ChatMessage::user("go")}), {});
        FAIL("expected a ProviderError");
    } catch (const apogee::harness::ProviderError& error) {
        CHECK(std::string{error.what()}.find("error_max_turns") != std::string::npos);
    }
}

TEST_CASE("a spawn failure is reported, not swallowed", "[backends][claude_cli][errors]") {
    Fixture fixture{{turn_script("never reached")}};
    fixture.spawner->failing_spawns = {0};

    CHECK_THROWS_AS(fixture.provider->chat(turn({ChatMessage::user("hi")}), {}),
                    apogee::harness::ProviderError);
}

TEST_CASE("a rewritten history restarts the child", "[backends][claude_cli][session]") {
    // Compaction rewrites the past. A child that was told the old history
    // would answer against a conversation that no longer exists, so the only
    // correct move is to start over rather than feed it a contradiction.
    Fixture fixture{{turn_script("First."), turn_script("Fresh.")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("original question")}), {});
    CHECK(fixture.provider->spawn_count() == 1);

    // A completely different history -- not an extension of what was sent.
    const auto second = fixture.provider->chat(turn({ChatMessage::user("a summary instead")}), {});

    CHECK(second.message.content.plain_text() == "Fresh.");
    CHECK(fixture.provider->spawn_count() == 2);
}

TEST_CASE("the backend never reads the CLI's credential storage",
          "[backends][claude_cli][auth][secrets]") {
    // A Core constraint and a SPEC principle: the CLI authenticates, Apogee
    // only spawns it. Locked as a test rather than a comment because the
    // tempting shortcut -- reading a session file to recover state -- is
    // exactly what `--resume` exists to make unnecessary.
    Fixture fixture{{turn_script("fine")}};
    (void)fixture.provider->chat(turn({ChatMessage::user("hi")}), {});

    for (const std::vector<std::string>& command : fixture.spawner->commands) {
        for (const std::string& argument : command) {
            CHECK(argument.find(".claude") == std::string::npos);
            CHECK(argument.find("credentials") == std::string::npos);
            CHECK(argument.find("oauth") == std::string::npos);
        }
    }
}

TEST_CASE("ending a session closes stdin rather than killing", "[backends][claude_cli][session]") {
    // A clean close lets the child emit its terminal event -- which is where
    // the session id and cost accounting live. Killing loses them.
    Fixture fixture{{turn_script("hi")}};
    (void)fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
    REQUIRE(fixture.spawner->children.size() == 1);

    fixture.provider->end_session();

    CHECK(fixture.spawner->children.front()->stdin_closed);
    CHECK_FALSE(fixture.spawner->children.front()->terminated);
    CHECK_FALSE(fixture.provider->has_live_child());
}

TEST_CASE("list_models reports the configured model", "[backends][claude_cli]") {
    Fixture fixture{{turn_script("x")}};
    const auto models = fixture.provider->list_models({});
    REQUIRE(models.size() == 1);
    CHECK(models.front().provider == "claude-cli");
    CHECK(models.front().id == "claude-sonnet-5");
}

// --- 57b: a side request never takes the conversation's session ------------

TEST_CASE("a side request leaves the conversation's session id as it found it",
          "[backends][claude_cli][session][side]") {
    // Every one-shot's terminal event names its own session. Recorded on the
    // provider, it became the session every later respawn resumed: the second
    // bug sweep's probe, turn 2 answered "from session title-sess".
    Fixture fixture{{turn_script("Real.", "conv-1"), turn_script("A title", "title-sess")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("my name is Zed")}), {});
    REQUIRE(fixture.provider->session_id() == "conv-1");

    const auto title = fixture.provider->chat(side("title this"), {});
    CHECK(title.message.content.plain_text() == "A title");
    CHECK(fixture.provider->session_id() == "conv-1");
}

TEST_CASE("complete_structured leaves the conversation's session id as it found it",
          "[backends][claude_cli][session][schema][side]") {
    Fixture fixture{{turn_script("Real.", "conv-1"), turn_script("finance", "schema-sess")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});
    REQUIRE(fixture.provider->session_id() == "conv-1");

    CHECK(fixture.provider->complete_structured("classify", R"({"type":"object"})", {}) ==
          "finance");
    CHECK(fixture.provider->session_id() == "conv-1");
}

TEST_CASE("an effort change after a title resumes the conversation's session",
          "[backends][claude_cli][session][effort][side]") {
    // The probe, replayed: "my name is Zed", the auto-title, then the effort
    // picker. The respawned child must resume the conversation, told only the
    // new question -- never the title's session, which never heard turn 1.
    Fixture fixture{{turn_script("Hello Zed.", "conv-1"), turn_script("A title", "title-sess"),
                     turn_script("Your name is Zed.", "conv-1")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("my name is Zed")}), {});
    (void)fixture.provider->chat(side("title this"), {});

    ChatRequest harder =
        turn({ChatMessage::user("my name is Zed"), ChatMessage::assistant("Hello Zed."),
              ChatMessage::user("what is my name?")});
    harder.thinking.effort = apogee::harness::Effort::High;
    const auto second = fixture.provider->chat(harder, {});
    CHECK(second.message.content.plain_text() == "Your name is Zed.");

    REQUIRE(fixture.spawner->commands.size() == 3);
    const std::vector<std::string>& respawn = fixture.spawner->commands.back();
    CHECK(value_after(respawn, "--resume") == "conv-1");
    CHECK_FALSE(has_flag(respawn, "title-sess"));
    const auto& writes = fixture.spawner->children.back()->writes;
    REQUIRE(writes.size() == 1);
    CHECK(writes.front().find("what is my name?") != std::string::npos);
}

TEST_CASE("a dead child after a side request resumes the conversation's session",
          "[backends][claude_cli][session][resume][side]") {
    Fixture fixture{{turn_script("First.", "conv-1"), turn_script("finance", "schema-sess"),
                     turn_script("A title", "title-sess"), turn_script("Second.", "conv-1")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});
    (void)fixture.provider->complete_structured("classify", R"({"type":"object"})", {});
    (void)fixture.provider->chat(side("title this"), {});
    fixture.spawner->children.front()->kill_now();

    (void)fixture.provider->chat(turn({ChatMessage::user("one"), ChatMessage::assistant("First."),
                                       ChatMessage::user("two")}),
                                 {});

    const std::vector<std::string>& respawn = fixture.spawner->commands.back();
    CHECK(value_after(respawn, "--resume") == "conv-1");
}

TEST_CASE("a cancelled side request puts the session, its framing and its queue back",
          "[backends][claude_cli][session][cancel][side]") {
    // A throw out of the one-shot -- Ctrl-C while a title or a rewrite runs --
    // used to skip the put-back entirely: the conversation was left with the
    // one-shot's framer and event queue, its own next turn's events lost.
    // One script holding both of the conversation's turns, as a persistent
    // child produces them: the second turn's events wait in the queue.
    Fixture fixture{
        {turn_script("First.", "conv-1") + turn_script("Second.", "conv-1"), stalled_script()}};
    fixture.spawner->stalling_spawns = {1};

    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});

    const auto cancellation = apogee::harness::CancellationToken::create();
    apogee::harness::StreamOptions options;
    options.cancellation = cancellation;
    options.on_token = [&cancellation](std::string_view) { cancellation.cancel(); };
    CHECK_THROWS_AS(fixture.provider->stream_chat(side("rewrite this"), options),
                    apogee::harness::CancelledError);
    CHECK(fixture.provider->session_id() == "conv-1");

    // The live child serves the next turn from the queue it kept.
    const auto second =
        fixture.provider->chat(turn({ChatMessage::user("one"), ChatMessage::assistant("First."),
                                     ChatMessage::user("two")}),
                               {});
    CHECK(second.message.content.plain_text() == "Second.");
    CHECK(fixture.provider->spawn_count() == 2);
}

// --- 57b: a one-shot's failure is a failure --------------------------------

TEST_CASE("a side request's error result throws Claude Code's own words",
          "[backends][claude_cli][errors][side]") {
    // The expired login that became a chat's title: is_error under
    // subtype "success", the reason in `result`. Thrown, so the titler's
    // "a failed title is cosmetic" catch records no title.
    Fixture fixture{{turn_script("Real.", "conv-1"), expired_login_script()}};
    (void)fixture.provider->chat(turn({ChatMessage::user("one")}), {});

    const std::string message =
        thrown_by([&fixture] { return fixture.provider->chat(side("title this"), {}); });
    CHECK(message.find(std::string{"reported an error: "} + std::string{kExpiredLogin}) !=
          std::string::npos);
    CHECK(message.find("reported success") == std::string::npos);
    CHECK(fixture.provider->session_id() == "conv-1");
}

TEST_CASE("a schema call's error result throws rather than returning the sentence",
          "[backends][claude_cli][errors][schema]") {
    // Before: the held-prose fallback handed the clerk "Failed to
    // authenticate…" as if it were the structured answer.
    Fixture fixture{{expired_login_script("schema-sess")}};

    const std::string message = thrown_by([&fixture] {
        return fixture.provider->complete_structured("classify", R"({"type":"object"})", {});
    });
    CHECK(message.find(kExpiredLogin) != std::string::npos);
    CHECK(fixture.provider->session_id().empty());
}

TEST_CASE("a one-shot that misses its deadline throws, partial answer or not",
          "[backends][claude_cli][errors][timeout]") {
    // Half an answer at the deadline is not a result -- a title, a summary or
    // a clerk's object cut off mid-word.
    Fixture fixture{{stalled_script()}, std::chrono::milliseconds{50}};
    fixture.spawner->stalling_spawns = {0, 1};

    const std::string side_message =
        thrown_by([&fixture] { return fixture.provider->chat(side("title this"), {}); });
    CHECK(side_message.find("did not finish in time") != std::string::npos);

    const std::string schema_message = thrown_by([&fixture] {
        return fixture.provider->complete_structured("classify", R"({"type":"object"})", {});
    });
    CHECK(schema_message.find("did not finish in time") != std::string::npos);
}

TEST_CASE("a conversation turn's error result says Claude Code's words, never 'success'",
          "[backends][claude_cli][errors]") {
    // The one wording, shared (57b): the conversation's turn says what the
    // one-shots say. M34 adds the way back in after an expired login.
    Fixture fixture{{expired_login_script("conv-1")}};

    const std::string message = thrown_by(
        [&fixture] { return fixture.provider->chat(turn({ChatMessage::user("hi")}), {}); });
    CHECK(message.find(std::string{"the Claude CLI reported an error: "} +
                       std::string{kExpiredLogin}) != std::string::npos);
    CHECK(message.find("success") == std::string::npos);
}

TEST_CASE("the failure wording names a real subtype and never 'success'",
          "[backends][claude_cli][errors]") {
    using apogee::backends::claude_cli_failure;
    using apogee::backends::TurnComplete;

    CHECK(claude_cli_failure(TurnComplete{.final_text = std::string{kExpiredLogin},
                                          .is_error = true,
                                          .error_subtype = "success"}) ==
          "the Claude CLI reported an error: " + std::string{kExpiredLogin});
    CHECK(claude_cli_failure(TurnComplete{.is_error = true, .error_subtype = "error_max_turns"}) ==
          "the Claude CLI reported error_max_turns");
    CHECK(claude_cli_failure(TurnComplete{.final_text = "stopped after 25 turns",
                                          .is_error = true,
                                          .error_subtype = "error_max_turns"}) ==
          "the Claude CLI reported error_max_turns: stopped after 25 turns");
    CHECK(claude_cli_failure(TurnComplete{.is_error = true, .error_subtype = "success"}) ==
          "the Claude CLI reported an error");
    CHECK(claude_cli_failure(TurnComplete{.is_error = true}) == "the Claude CLI reported an error");
}
