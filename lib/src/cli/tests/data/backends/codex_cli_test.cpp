#include "backends/codex_cli.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "backends/codex_cli_events.h"
#include "contracts/errors.h"
#include "platform/child_process.h"
#include "support/fake_child.h"
#include "transport/jsonl_framer.h"

/// The Codex backend, against recorded output from a real ChatGPT session.
///
/// Two assertions here matter more than the rest, and both come from the
/// characterization rather than the family template:
///
///  * the sandbox is pinned to `read-only` — this CLI executes model-generated
///    shell commands, so a widened sandbox would turn a chat turn into
///    arbitrary local execution;
///  * `exec resume` omits the flags that subcommand rejects, because sharing
///    one argv builder works on turn one and fails on turn two.
namespace {

using apogee::backends::CodexCliProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::FakeSpawner;

[[nodiscard]] std::string read_fixture(std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "codex_cli" / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Fixture {
    std::shared_ptr<FakeSpawner> spawner = std::make_shared<FakeSpawner>();
    std::unique_ptr<CodexCliProvider> provider;

    explicit Fixture(std::vector<std::string> scripts = {}) {
        spawner->stdout_scripts = std::move(scripts);

        CodexCliProvider::Options options;
        options.backend_name = "codex";
        options.model = "gpt-5-codex";

        auto copy = spawner;
        provider = std::make_unique<CodexCliProvider>(
            std::move(options), [copy](const apogee::platform::ChildCommand& command,
                                       std::string& error) { return (*copy)(command, error); });
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

}  // namespace

TEST_CASE("the sandbox is pinned read-only and cannot be widened", "[backends][codex][safety]") {
    // THE safety rule on this backend. `codex exec` runs model-generated shell
    // commands; every other backend Apogee drives only produces text. read-only
    // is the policy under which "answer this" cannot become "modify this
    // machine", and it is a literal in the builder rather than a config key.
    CodexCliProvider::Options options;
    options.model = "gpt-5-codex";

    const std::vector<std::string> args =
        CodexCliProvider::build_arguments(options, CodexCliProvider::Invocation::Fresh, {}, {});

    REQUIRE(has(args, "--sandbox"));
    const auto sandbox = std::find(args.begin(), args.end(), "--sandbox");
    REQUIRE(std::next(sandbox) != args.end());
    CHECK(*std::next(sandbox) == "read-only");

    // The dangerous escapes must never appear.
    CHECK_FALSE(has(args, "workspace-write"));
    CHECK_FALSE(has(args, "danger-full-access"));
    CHECK_FALSE(has(args, "--dangerously-bypass-approvals-and-sandbox"));
    CHECK_FALSE(has(args, "--yolo"));
}

TEST_CASE("resume omits the flags that subcommand rejects", "[backends][codex][flags]") {
    // Verified on 0.153.4: `codex exec resume` fails with "unexpected argument"
    // on --color, and its usage line shows a narrower surface than `exec`'s.
    // A shared builder would work on turn one and break on turn two.
    CodexCliProvider::Options options;
    options.model = "gpt-5-codex";

    const std::vector<std::string> fresh =
        CodexCliProvider::build_arguments(options, CodexCliProvider::Invocation::Fresh, {}, {});
    const std::vector<std::string> resumed = CodexCliProvider::build_arguments(
        options, CodexCliProvider::Invocation::Resume, "thread-abc", {});

    CHECK(has(fresh, "--color"));
    CHECK(has(fresh, "--sandbox"));

    CHECK_FALSE(has(resumed, "--color"));
    CHECK_FALSE(has(resumed, "--sandbox"));
    CHECK(has(resumed, "resume"));
    CHECK(has(resumed, "thread-abc"));

    // Both forms still need the event stream and the repo-check escape.
    for (const auto& args : {fresh, resumed}) {
        CHECK(has(args, "--json"));
        CHECK(has(args, "--skip-git-repo-check"));
    }
}

TEST_CASE("a schema is passed as a file path", "[backends][codex][flags]") {
    // The opposite of Claude's --json-schema, which took inline JSON. Passing
    // the schema bytes here would be read as a filename.
    CodexCliProvider::Options options;
    const std::vector<std::string> args = CodexCliProvider::build_arguments(
        options, CodexCliProvider::Invocation::Fresh, {}, "/tmp/schema.json");

    CHECK(has(args, "--output-schema"));
    CHECK(has(args, "/tmp/schema.json"));
}

TEST_CASE("recorded output yields the answer and real usage", "[backends][codex][replay]") {
    Fixture fixture{{read_fixture("simple_text.jsonl")}};

    std::string streamed;
    apogee::harness::StreamOptions options;
    options.on_token = [&streamed](std::string_view chunk) { streamed += chunk; };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("Say exactly: hello")}), options);

    CHECK(response.message.content.plain_text() == "hello");
    CHECK(streamed == "hello");
    // Unlike Ollama's, this CLI reports genuine accounting.
    CHECK(response.usage.prompt_tokens == 15042);
    CHECK(response.usage.completion_tokens == 5);
}

TEST_CASE("a multi-line answer still arrives as a single event", "[backends][codex][replay]") {
    // The honest limitation, pinned by the fixture that demonstrates it: this
    // CLI has no delta events, so a long answer is one chunk. If a future
    // release adds streaming, this test is the thing that notices.
    Fixture fixture{{read_fixture("multiline_answer.jsonl")}};

    int chunks = 0;
    apogee::harness::StreamOptions options;
    options.on_token = [&chunks](std::string_view) { ++chunks; };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("Count to 12")}), options);

    CHECK(chunks == 1);
    CHECK(response.message.content.plain_text().find('\n') != std::string::npos);
}

TEST_CASE("a schema run's conforming JSON arrives as the message text",
          "[backends][codex][replay][schema]") {
    Fixture fixture{{read_fixture("structured_output.jsonl")}};

    const auto response = fixture.provider->chat(turn({ChatMessage::user("Greet me")}), {});
    const std::string text = response.message.content.plain_text();

    CHECK(text.find("\"greeting\"") != std::string::npos);
    // It is the message itself, not a side channel -- so a caller parses the
    // answer rather than reading a separate field.
    CHECK(text.front() == '{');
}

TEST_CASE("the thread id is captured and the next turn resumes it", "[backends][codex][session]") {
    // Session continuity without a persistent child: turn one spawns `exec`
    // and records the thread, turn two spawns `exec resume`. Apogee stores the
    // id itself, which is why nothing here reads the CLI's session files.
    Fixture fixture{{read_fixture("simple_text.jsonl"), read_fixture("resumed_thread.jsonl")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("Say exactly: hello")}), {});
    CHECK(fixture.provider->thread_id() == "01a078e2-8493-7be1-80d8-a677340a062a");

    (void)fixture.provider->chat(
        turn({ChatMessage::user("Say exactly: hello"), ChatMessage::assistant("hello"),
              ChatMessage::user("and again?")}),
        {});

    // Per-turn spawn is expected here -- but the second one resumed.
    CHECK(fixture.provider->spawn_count() == 2);
    const std::vector<std::string>& second = fixture.spawner->commands.back();
    CHECK(has(second, "resume"));
    CHECK(has(second, "01a078e2-8493-7be1-80d8-a677340a062a"));
}

TEST_CASE("a resumed turn sends only the new message", "[backends][codex][session]") {
    // The CLI holds the conversation on a resumed thread, so re-sending the
    // history would duplicate it.
    Fixture fixture{{read_fixture("simple_text.jsonl"), read_fixture("resumed_thread.jsonl")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("first question")}), {});
    (void)fixture.provider->chat(
        turn({ChatMessage::user("first question"), ChatMessage::assistant("hello"),
              ChatMessage::user("second question")}),
        {});

    const std::string& prompt = fixture.spawner->commands.back().back();
    CHECK(prompt == "second question");
    CHECK(prompt.find("first question") == std::string::npos);
}

TEST_CASE("a side request never joins the conversation's thread", "[backends][codex][session]") {
    // The family's SideRequest rule, applied where the session IS a thread id:
    // a background title must not become a turn of the conversation.
    Fixture fixture{{read_fixture("simple_text.jsonl"), read_fixture("simple_text.jsonl"),
                     read_fixture("resumed_thread.jsonl")}};

    (void)fixture.provider->chat(turn({ChatMessage::user("real question")}), {});
    const std::string conversation_thread = fixture.provider->thread_id();
    REQUIRE_FALSE(conversation_thread.empty());

    ChatRequest background = turn({ChatMessage::user("summarise this")});
    background.transient.side_request = true;
    (void)fixture.provider->chat(background, {});

    // It ran on a FRESH thread...
    CHECK_FALSE(has(fixture.spawner->commands.back(), "resume"));
    // ...and the conversation's own thread survived untouched.
    CHECK(fixture.provider->thread_id() == conversation_thread);
}

TEST_CASE("the event stream parses identically at every chunk size", "[backends][codex][replay]") {
    // The family framing guardrail. This CLI emits real JSONL, so the shared
    // framer applies and the whole class of split-line bugs is covered here.
    for (const std::string& name : {"simple_text.jsonl", "multiline_answer.jsonl",
                                    "structured_output.jsonl", "resumed_thread.jsonl"}) {
        INFO("fixture: " << name);
        const std::string bytes = read_fixture(name);

        std::vector<std::string> whole;
        for (const auto& event : apogee::backends::codex_cli::parse_stream(bytes)) {
            whole.push_back(apogee::backends::codex_cli::describe(event));
        }
        REQUIRE_FALSE(whole.empty());

        for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3},
                                        std::size_t{7}, std::size_t{64}, std::size_t{4096}}) {
            INFO("chunk " << chunk);
            std::vector<std::string> seen;
            apogee::backends::JsonlFramer framer;
            const auto handle = [&seen](std::string_view line) {
                if (auto event = apogee::backends::codex_cli::parse_line(line)) {
                    seen.push_back(apogee::backends::codex_cli::describe(*event));
                }
            };
            for (std::size_t offset = 0; offset < bytes.size(); offset += chunk) {
                framer.feed(bytes.substr(offset, std::min(chunk, bytes.size() - offset)), handle);
            }
            framer.flush(handle);
            CHECK(seen == whole);
        }
    }
}

TEST_CASE("an unknown event type is dropped rather than fatal", "[backends][codex]") {
    CHECK_FALSE(
        apogee::backends::codex_cli::parse_line(R"({"type":"invented.next.year","payload":1})")
            .has_value());
    CHECK_FALSE(apogee::backends::codex_cli::parse_line(R"({"type":"turn.started"})").has_value());
    CHECK_FALSE(apogee::backends::codex_cli::parse_line("not json").has_value());
    CHECK_FALSE(
        apogee::backends::codex_cli::parse_line(R"({"type":"item.completed",)").has_value());
}

TEST_CASE("a mode that this CLI does not have is refused, not ignored",
          "[backends][codex][config]") {
    // The codex CLI has no --bare analogue. Silently accepting `mode: bare`
    // would promise an isolation this backend cannot deliver.
    // from_config refuses the platform before it reads the config where child
    // processes are unsupported (Windows, recorded skip): the message under
    // test never forms there.
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::CodexCli;
    config.mode = "bare";

    try {
        (void)CodexCliProvider::from_config("codex", config);
        FAIL("expected a ProviderError");
    } catch (const apogee::harness::ProviderError& error) {
        CHECK(std::string{error.what()}.find("no auth modes") != std::string::npos);
    }
}

TEST_CASE("list_models reports the configured model", "[backends][codex]") {
    Fixture fixture{{read_fixture("simple_text.jsonl")}};
    const auto models = fixture.provider->list_models({});
    REQUIRE(models.size() == 1);
    CHECK(models.front().provider == "codex-cli");
    CHECK(models.front().id == "gpt-5-codex");
}

// --- 34: the CLI's own catalog ------------------------------------------------

namespace {

/// The shape `codex debug models` printed on codex-cli 0.153.4, trimmed: a
/// listed model, one the CLI hides, one with no visibility given.
constexpr const char* kCatalog = R"({"models": [
  {"slug": "gpt-5.5", "display_name": "GPT-5.5", "visibility": "list", "supported_in_api": false},
  {"slug": "codex-auto-review", "display_name": "Codex Auto Review", "visibility": "hide"},
  {"slug": "kimi-k3:cloud", "display_name": "kimi-k3:cloud", "visibility": "list"},
  {"slug": "plain-model"}
]})";

}  // namespace

TEST_CASE("the catalog is the CLI's own: what it lists, slugs verbatim",
          "[backends][codex][catalog]") {
    Fixture fixture{{kCatalog}};
    const auto models = fixture.provider->list_catalog({});
    REQUIRE(fixture.spawner->commands.size() == 1);
    CHECK(fixture.spawner->commands.front() == std::vector<std::string>{"debug", "models"});
    CHECK(fixture.spawner->children.front()->stdin_closed);
    std::vector<std::string> ids;
    for (const auto& model : models) {
        ids.push_back(model.id);
        CHECK(model.provider == "codex-cli");
        CHECK(model.backend == "codex");
    }
    CHECK(ids == std::vector<std::string>{"gpt-5.5", "kimi-k3:cloud", "plain-model"});
    CHECK(models.front().name == "GPT-5.5");
    CHECK(models.back().name == "plain-model");
}

TEST_CASE(
    "a catalog the CLI did not print whole, or failed to print, is refused, never read in part",
    "[backends][codex][catalog]") {
    for (const char* odd : {"", "not json", "[]", R"({"models": {}})",
                            R"({"models": [{"slug": "gpt-5.5"}, {"display_name": "no slug"}]})",
                            R"({"models": [{"slug": ""}]})", R"({"models": ["gpt-5.5"]})"}) {
        Fixture fixture{{odd}};
        INFO(odd);
        try {
            (void)fixture.provider->list_catalog({});
            FAIL("an unreadable catalog was accepted");
        } catch (const apogee::harness::ProviderError& error) {
            CHECK(std::string{error.what()}.find("codex debug models") != std::string::npos);
        }
    }

    // A failed run says its status and the CLI's own words.
    Fixture failed{{kCatalog}};
    failed.spawner->exit_status = 2;
    failed.spawner->stderr_script = "error: not logged in";
    try {
        (void)failed.provider->list_catalog({});
        FAIL("a failed run was accepted");
    } catch (const apogee::harness::ProviderError& error) {
        CHECK(std::string{error.what()}.find("exited with status 2: error: not logged in") !=
              std::string::npos);
    }
}

TEST_CASE("a catalog run that hangs is cut off at its bound", "[backends][codex][catalog]") {
    auto spawner = std::make_shared<FakeSpawner>();
    spawner->hang = true;
    CodexCliProvider::Options options;
    options.backend_name = "codex";
    options.catalog_timeout = std::chrono::milliseconds{50};
    CodexCliProvider provider{std::move(options),
                              [spawner](const apogee::platform::ChildCommand& command,
                                        std::string& error) { return (*spawner)(command, error); }};
    CHECK_THROWS_AS(provider.list_catalog({}), apogee::harness::ProviderError);
    CHECK(spawner->children.front()->terminated);
}
