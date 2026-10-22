#include "backends/mlx_local.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "backends/adapters.h"
#include "backends/factory.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "support/env_guard.h"
#include "support/fake_mlx_driver.h"
#include "support/towered_configs.h"

/// The mlx backend (27a) over a scripted driver, and its refusal ladder.
///
/// The fake answers requests the way the driver does -- `ready` at spawn, a
/// scripted reply per `generate` with its id, `done cancelled` for a cancel
/// -- so every path a real model will not take on demand is an ordinary test:
/// a stream cut between any two bytes, a driver that dies mid-turn with a
/// traceback on stderr, one that goes silent, a cancel it never answers, a
/// load that fails. The ladder is driven over a fake runtime laid out in a
/// temporary directory, one rung removed at a time.
namespace {

using apogee::backends::MlxHost;
using apogee::backends::MlxLocalProvider;
using apogee::backends::MlxReadiness;
using apogee::backends::MlxRefusal;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::DriverState;
using apogee::testing::FakeDriver;
using nlohmann::json;

/// A provider over fake drivers, each spawn handed the next state.
struct Fixture {
    std::vector<std::shared_ptr<DriverState>> drivers;
    std::vector<apogee::platform::ChildCommand> commands;
    std::unique_ptr<MlxLocalProvider> provider;

    explicit Fixture(DriverState first, MlxLocalProvider::Options options = defaults()) {
        drivers.push_back(std::make_shared<DriverState>(std::move(first)));
        provider = std::make_unique<MlxLocalProvider>(
            std::move(options),
            [this](const apogee::platform::ChildCommand& command,
                   std::string&) -> std::unique_ptr<FakeDriver> {
                commands.push_back(command);
                if (commands.size() > drivers.size()) {
                    // A respawn: the same script, fresh.
                    DriverState next = *drivers.front();
                    next.out.clear();
                    next.offset = 0;
                    next.writes.clear();
                    next.replied = 0;
                    next.dying = false;
                    next.die_after_startup = false;
                    next.die_after_reply = false;
                    next.stderr_read = false;
                    next.stdin_closed = false;
                    next.terminated = false;
                    next.heard.clear();
                    drivers.push_back(std::make_shared<DriverState>(std::move(next)));
                }
                drivers[commands.size() - 1]->vision_spawn =
                    std::ranges::find(command.arguments, "--vision") != command.arguments.end();
                return std::make_unique<FakeDriver>(drivers[commands.size() - 1]);
            });
    }

    static MlxLocalProvider::Options defaults() {
        MlxLocalProvider::Options options;
        options.backend_name = "local";
        options.model = "Llama-3.2-3B-Instruct";
        options.model_dir = "/models/llama";
        options.interpreter = "/venv/bin/python";
        options.driver = "/home/training/scripts/mlx_generate.py";
        options.info.model_type = "llama";
        options.info.chat_template = true;
        options.info.sampling.temperature = 0.6;
        options.info.sampling.top_p = 0.9;
        options.stall_timeout = std::chrono::seconds{5};
        options.cancel_timeout = std::chrono::seconds{5};
        return options;
    }

    [[nodiscard]] json written(std::size_t driver, std::size_t line) const {
        return json::parse(drivers.at(driver)->writes.at(line));
    }
};

std::string text(const std::string& value, const std::string& type = "text") {
    return json{{"type", type}, {"id", "{id}"}, {"text", value}}.dump() + "\n";
}

std::string done(const std::string& finish = "stop", int prompt = 20, int cached = 0,
                 int completion = 3) {
    return R"({"type":"done","id":{id},"finish":")" + finish + R"(","prompt_tokens":)" +
           std::to_string(prompt) + R"(,"cached_tokens":)" + std::to_string(cached) +
           R"(,"completion_tokens":)" + std::to_string(completion) + "}\n";
}

/// Fixes the `"{id}"` quoting `text` leaves: ids are numbers on the wire.
std::string numbered(std::string line) {
    for (std::size_t at = line.find("\"{id}\""); at != std::string::npos;
         at = line.find("\"{id}\"", at)) {
        line.replace(at, 6, "{id}");
    }
    return line;
}

ChatRequest ask(const std::string& question) {
    ChatRequest request;
    request.messages.push_back(ChatMessage::user(question));
    return request;
}

struct Streamed {
    std::string tokens;
    std::string thinking;
    std::vector<apogee::harness::StatusEvent> statuses;
    apogee::harness::StreamOptions options;

    Streamed() {
        options.on_token = [this](std::string_view piece) { tokens += piece; };
        options.on_thinking = [this](std::string_view piece) { thinking += piece; };
        options.on_status = [this](const apogee::harness::StatusEvent& event) {
            statuses.push_back(event);
        };
    }

    [[nodiscard]] const apogee::harness::StatusEvent* status(
        apogee::harness::StatusEvent::Type type) const {
        for (const apogee::harness::StatusEvent& event : statuses) {
            if (event.type == type) {
                return &event;
            }
        }
        return nullptr;
    }
};

/// A fake runtime under `root`: an interpreter, mlx-lm's package, the
/// seeded driver and a model directory -- every rung satisfied.
struct Runtime {
    apogee::testing::TempDir dir{"mlx-runtime"};
    MlxHost host;
    apogee::harness::BackendConfig entry;

    Runtime() {
        const std::filesystem::path venv = dir.path() / "training" / "venv";
        touch(venv / "bin" / "python");
        const std::filesystem::path package =
            venv / "lib" / "python3.14" / "site-packages" / "mlx_lm";
        touch(package / "__init__.py");
        std::ofstream{package / "_version.py"} << "__version__ = '0.32.0'\n";
        touch(dir.path() / "training" / "scripts" / "mlx_generate.py");
        std::filesystem::create_directories(dir.path() / "model");
        std::ofstream{dir.path() / "model" / "config.json"} << R"({"model_type":"llama"})";
        host = MlxHost::at(dir.path());
        host.target = "macos-arm64";
        entry.type = apogee::harness::BackendType::Mlx;
        entry.model_path = (dir.path() / "model").string();
    }

    static void touch(const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path} << "";
    }

    [[nodiscard]] MlxReadiness probe() const {
        return apogee::backends::probe_mlx_backend("local", entry, host);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Turns
// ---------------------------------------------------------------------------

TEST_CASE("turns stream through one child that holds the conversation",
          "[backends][mlx][session]") {
    DriverState state;
    state.replies = {numbered(text("Hel") + text("lo.")) + done("stop", 20, 0, 3),
                     numbered(text("Again.")) + done("stop", 30, 23, 2)};
    Fixture fixture{state};

    Streamed first;
    const apogee::harness::ChatResponse one =
        fixture.provider->stream_chat(ask("hi"), first.options);
    CHECK(first.tokens == "Hello.");
    CHECK(one.message.content.plain_text() == "Hello.");
    CHECK(one.finish_reason == apogee::harness::FinishReason::Stop);
    CHECK(one.usage.prompt_tokens == 20);
    CHECK(one.usage.completion_tokens == 3);
    // The load said, before and after.
    REQUIRE(first.status(apogee::harness::StatusEvent::Type::ModelLoading) != nullptr);
    REQUIRE(first.status(apogee::harness::StatusEvent::Type::ModelReady) != nullptr);
    CHECK(first.status(apogee::harness::StatusEvent::Type::ModelReady)->detail ==
          "mlx-lm 0.32.0, llama");

    ChatRequest next = ask("hi");
    next.messages.push_back(ChatMessage::assistant("Hello."));
    next.messages.push_back(ChatMessage::user("again"));
    Streamed second;
    const apogee::harness::ChatResponse two = fixture.provider->stream_chat(next, second.options);
    CHECK(two.message.content.plain_text() == "Again.");

    // ONE child for both: the persistent claim, counted.
    CHECK(fixture.provider->spawn_count() == 1);
    CHECK(fixture.provider->has_live_child());
    CHECK(fixture.written(0, 0)["id"] == 1);
    CHECK(fixture.written(0, 1)["id"] == 2);
    CHECK(fixture.written(0, 1)["messages"].size() == 3);
    // What the cache kept, said where the user asked to see it.
    const apogee::harness::StatusEvent* cache =
        second.status(apogee::harness::StatusEvent::Type::PromptCache);
    REQUIRE(cache != nullptr);
    CHECK(cache->tokens == 30);
    CHECK(cache->used_tokens == 23);
    CHECK(cache->detail.find("23 from the cache, 7 read") != std::string::npos);

    // The command that started it: the environment's interpreter, the
    // seeded driver, the model directory -- nothing else.
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands[0].program == "/venv/bin/python");
    CHECK(fixture.commands[0].arguments ==
          std::vector<std::string>{"/home/training/scripts/mlx_generate.py", "--model",
                                   "/models/llama"});
    CHECK(fixture.commands[0].extra_environment.empty());
}

TEST_CASE(
    "a request carries the conversation in template shapes and the sampling the ladder "
    "resolved",
    "[backends][mlx][request]") {
    DriverState state;
    state.replies = {numbered(text("ok")) + done()};
    Fixture fixture{state};

    ChatRequest request;
    request.messages.push_back(ChatMessage::system("You are terse."));
    request.messages.push_back(ChatMessage::system("Today is Sunday."));
    request.messages.push_back(ChatMessage::user("q"));
    request.tools.push_back(
        {.name = "read_file", .description = "Read", .parameters_schema = "{}"});
    (void)fixture.provider->chat(request, {});
    const json line = fixture.written(0, 0);
    CHECK(line["type"] == "generate");
    // A template takes one system message, first: the opening ones joined.
    REQUIRE(line["messages"].size() == 2);
    CHECK(line["messages"][0]["content"] == "You are terse.\n\nToday is Sunday.");
    CHECK(line["tools"][0]["function"]["name"] == "read_file");
    // The model directory's own recommendation, then its family's card.
    CHECK(line["sampling"]["temperature"] == 0.6);
    CHECK(line["sampling"]["top_p"] == 0.9);
    CHECK(line["max_tokens"] == 2048);
    CHECK(line["thinking"] == true);
    CHECK(line["session"] == true);

    // The request outranks the file; off is the template's switch; a side
    // request asks for a cache of its own.
    ChatRequest side = ask("title");
    side.temperature = 0.2;
    side.max_tokens = 16;
    side.thinking.mode = apogee::harness::ThinkingMode::Off;
    side.transient.side_request = true;
    Streamed streamed;
    (void)fixture.provider->stream_chat(side, streamed.options);
    const json second = fixture.written(0, 1);
    CHECK(second["sampling"]["temperature"] == 0.2);
    CHECK(second["max_tokens"] == 16);
    CHECK(second["thinking"] == false);
    CHECK(second["session"] == false);
    // A side request is not the conversation: no cache line for it.
    CHECK(streamed.status(apogee::harness::StatusEvent::Type::PromptCache) == nullptr);
}

TEST_CASE("a schema is stated in the prompt and said once: there is no grammar here",
          "[backends][mlx][request]") {
    DriverState state;
    state.replies = {numbered(text("{}")) + done()};
    Fixture fixture{state};
    ChatRequest request = ask("q");
    request.transient.response_schema = R"({"type":"object"})";
    Streamed first;
    (void)fixture.provider->stream_chat(request, first.options);
    const json line = fixture.written(0, 0);
    REQUIRE(line["messages"].size() == 2);
    CHECK(line["messages"][0]["role"] == "system");
    CHECK(line["messages"][0]["content"].get<std::string>().find("OUTPUT FORMAT") == 0);
    REQUIRE(first.status(apogee::harness::StatusEvent::Type::Notice) != nullptr);
    CHECK(first.status(apogee::harness::StatusEvent::Type::Notice)->detail.find("no grammar") !=
          std::string::npos);
    Streamed second;
    (void)fixture.provider->stream_chat(request, second.options);
    CHECK(second.status(apogee::harness::StatusEvent::Type::Notice) == nullptr);
}

TEST_CASE("the driver's calls and reasoning arrive typed: calls with ids, reasoning to its sink",
          "[backends][mlx][tools]") {
    DriverState state;
    state.replies = {
        numbered(text("weighing it", "reasoning") + text("Let me look.")) +
        R"({"type":"tool_call","id":{id},"name":"read_file","arguments":{"path":"a.txt"}})"
        "\n" +
        done("tool_calls")};
    Fixture fixture{state};
    Streamed streamed;
    ChatRequest request = ask("read a.txt");
    request.tools.push_back({.name = "read_file", .description = "", .parameters_schema = "{}"});
    const apogee::harness::ChatResponse response =
        fixture.provider->stream_chat(request, streamed.options);
    CHECK(streamed.thinking == "weighing it");
    CHECK(streamed.tokens == "Let me look.");
    CHECK(response.message.content.plain_text() == "Let me look.");
    REQUIRE(response.message.tool_calls.size() == 1);
    CHECK(response.message.tool_calls[0].name == "read_file");
    CHECK(json::parse(response.message.tool_calls[0].arguments) == json{{"path", "a.txt"}});
    CHECK(response.message.tool_calls[0].id.size() == 9);
    CHECK(response.finish_reason == apogee::harness::FinishReason::ToolCalls);
}

TEST_CASE(
    "a family whose format streams as text is read by its profile at every chunk size: "
    "gpt-oss's channels and its native call",
    "[backends][mlx][framing]") {
    const std::string answer =
        numbered(text("<|channel|>analysis<|message|>They want Paris.<|end|>") +
                 text("<|start|>assistant<|channel|>final<|message|>Par") + text("is")) +
        done();
    const std::string call =
        numbered(text("<|channel|>analysis<|message|>Read it.<|end|><|start|>assistant") +
                 text("<|channel|>commentary to=functions.read_file <|constrain|>json") +
                 text(R"(<|message|>{"path":"a.txt"})")) +
        done();
    MlxLocalProvider::Options options = Fixture::defaults();
    options.info.model_type = "gpt_oss";
    options.model = "my-tuned-model";
    for (const std::size_t chunk : {std::size_t{1}, std::size_t{3}, std::size_t{4096}}) {
        INFO("chunk " << chunk);
        DriverState state;
        state.chunk = chunk;
        state.replies = {answer, call};
        Fixture fixture{state, options};
        CHECK(fixture.provider->model_behavior().profile == "gpt-oss");

        Streamed first;
        const apogee::harness::ChatResponse said =
            fixture.provider->stream_chat(ask("capital?"), first.options);
        CHECK(said.message.content.plain_text() == "Paris");
        CHECK(first.tokens == "Paris");
        CHECK(first.thinking == "They want Paris.");

        Streamed second;
        ChatRequest request = ask("read a.txt");
        request.tools.push_back(
            {.name = "read_file", .description = "", .parameters_schema = "{}"});
        const apogee::harness::ChatResponse called =
            fixture.provider->stream_chat(request, second.options);
        REQUIRE(called.message.tool_calls.size() == 1);
        CHECK(called.message.tool_calls[0].name == "read_file");
        CHECK(json::parse(called.message.tool_calls[0].arguments) == json{{"path", "a.txt"}});
        CHECK(called.finish_reason == apogee::harness::FinishReason::ToolCalls);
        CHECK(second.tokens.find("<|") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Failures
// ---------------------------------------------------------------------------

TEST_CASE(
    "a driver that dies mid-turn fails at once with its stderr's last line, and the next turn "
    "starts a new one",
    "[backends][mlx][failure]") {
    DriverState state;
    state.replies = {numbered(text("par"))};
    state.die_after_reply = true;
    state.stderr_text =
        "Traceback (most recent call last):\n  File ...\nRuntimeError: [metal] out of memory\n";
    Fixture fixture{state};

    const auto start = std::chrono::steady_clock::now();
    try {
        (void)fixture.provider->chat(ask("hi"), {});
        FAIL("a dead driver must fail the turn");
    } catch (const apogee::harness::ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("exited mid-turn") != std::string::npos);
        CHECK(message.find("RuntimeError: [metal] out of memory") != std::string::npos);
        CHECK(message.find("Traceback") == std::string::npos);  // one line, the last
    }
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{2});
    CHECK_FALSE(fixture.provider->has_live_child());

    fixture.drivers.front()->replies = {numbered(text("fine")) + done()};
    CHECK(fixture.provider->chat(ask("again"), {}).message.content.plain_text() == "fine");
    CHECK(fixture.provider->spawn_count() == 2);
}

TEST_CASE("a load that fails names its fix, in the driver's own words or its stderr's",
          "[backends][mlx][failure]") {
    SECTION("mlx-lm missing from the environment") {
        DriverState state;
        state.startup =
            R"({"type":"error","id":null,"kind":"missing_dependency","message":"mlx-lm is not )"
            R"(installed in the Python environment (No module named 'mlx_lm'). Run: apogee train )"
            R"(setup --with mlx"})"
            "\n";
        state.die_after_startup = true;
        Fixture fixture{state};
        try {
            (void)fixture.provider->chat(ask("hi"), {});
            FAIL("expected a refusal");
        } catch (const apogee::harness::ProviderError& e) {
            CHECK(std::string{e.what()}.find("apogee train setup --with mlx") != std::string::npos);
        }
        CHECK_FALSE(fixture.provider->has_live_child());
    }
    SECTION("an architecture this mlx-lm cannot build") {
        DriverState state;
        state.startup = R"({"type":"error","id":null,"kind":"load","message":"could not load /m: )"
                        R"(ValueError: Model type gemma9 not supported."})"
                        "\n";
        state.die_after_startup = true;
        Fixture fixture{state};
        CHECK_THROWS_WITH(fixture.provider->chat(ask("hi"), {}),
                          "local: could not load /m: ValueError: Model type gemma9 not supported.");
    }
    SECTION("an exit before it said anything") {
        DriverState state;
        state.startup.clear();
        state.die_after_startup = true;
        state.stderr_text = "Fatal Python error: init_import_site\n";
        Fixture fixture{state};
        try {
            (void)fixture.provider->chat(ask("hi"), {});
            FAIL("expected a failure");
        } catch (const apogee::harness::ProviderError& e) {
            const std::string message = e.what();
            CHECK(message.find("exited while loading /models/llama") != std::string::npos);
            CHECK(message.find("Fatal Python error: init_import_site") != std::string::npos);
        }
    }
    SECTION("a driver from another protocol") {
        DriverState state;
        state.startup = R"({"type":"ready","protocol":2,"model_type":"llama"})"
                        "\n";
        Fixture fixture{state};
        try {
            (void)fixture.provider->chat(ask("hi"), {});
            FAIL("expected a refusal");
        } catch (const apogee::harness::ProviderError& e) {
            CHECK(std::string{e.what()}.find("apogee check --fix") != std::string::npos);
        }
    }
}

TEST_CASE("a request the driver cannot answer fails the turn, and the child lives on",
          "[backends][mlx][failure]") {
    DriverState state;
    state.replies = {R"({"type":"error","id":{id},"kind":"request","message":"TemplateError: This )"
                     R"(model only supports single tool-calls at once!"})"
                     "\n",
                     numbered(text("fine")) + done()};
    Fixture fixture{state};
    CHECK_THROWS_WITH(fixture.provider->chat(ask("hi"), {}),
                      "local: the MLX driver could not answer: TemplateError: This model only "
                      "supports single tool-calls at once!");
    CHECK(fixture.provider->has_live_child());
    CHECK(fixture.provider->chat(ask("again"), {}).message.content.plain_text() == "fine");
    CHECK(fixture.provider->spawn_count() == 1);
}

TEST_CASE("a cancelled turn asks the driver to stop, waits for its done, and keeps the model",
          "[backends][mlx][cancel]") {
    DriverState state;
    state.replies = {numbered(text("partial")), numbered(text("next")) + done()};
    Fixture fixture{state};

    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    Streamed streamed;
    streamed.options.cancellation = token;
    streamed.options.on_token = [&token](std::string_view) { token.cancel(); };
    CHECK_THROWS_AS(fixture.provider->stream_chat(ask("long"), streamed.options),
                    apogee::harness::CancelledError);
    REQUIRE(fixture.drivers.front()->writes.size() == 2);
    CHECK(fixture.written(0, 1) == json{{"type", "cancel"}, {"id", 1}});
    CHECK(fixture.provider->has_live_child());

    CHECK(fixture.provider->chat(ask("next"), {}).message.content.plain_text() == "next");
    CHECK(fixture.provider->spawn_count() == 1);
}

TEST_CASE("a cancelled request's late word is never the next turn's, and a throwing sink stops it",
          "[backends][mlx][cancel]") {
    DriverState state;
    // Request 1's reply runs on after its first word; request 2's opens with
    // a line request 1 left behind.
    state.replies = {numbered(text("one ") + text("two")) + done(),
                     R"({"type":"text","id":1,"text":"stale"})"
                     "\n" +
                         numbered(text("fresh")) + done()};
    Fixture fixture{state};

    Streamed streamed;
    streamed.options.on_token = [](std::string_view) {
        throw std::runtime_error{"the surface stopped listening"};
    };
    CHECK_THROWS_AS(fixture.provider->stream_chat(ask("first"), streamed.options),
                    std::runtime_error);
    // The driver was asked to stop, and its done read.
    REQUIRE(fixture.drivers.front()->writes.size() == 2);
    CHECK(fixture.written(0, 1) == json{{"type", "cancel"}, {"id", 1}});

    Streamed next;
    const apogee::harness::ChatResponse answer =
        fixture.provider->stream_chat(ask("second"), next.options);
    CHECK(answer.message.content.plain_text() == "fresh");
    CHECK(next.tokens == "fresh");
    CHECK(fixture.provider->spawn_count() == 1);
}

TEST_CASE("a driver that goes silent, or will not stop, is given up on",
          "[backends][mlx][cancel]") {
    SECTION("silent mid-turn: the turn fails, the child answers its cancel and stays") {
        DriverState state;
        state.replies = {numbered(text("half"))};
        MlxLocalProvider::Options options = Fixture::defaults();
        options.stall_timeout = std::chrono::milliseconds{200};
        Fixture fixture{state, options};
        CHECK_THROWS_WITH(fixture.provider->chat(ask("hi"), {}),
                          "local: the MLX driver went silent mid-turn");
        CHECK(fixture.provider->has_live_child());
    }
    SECTION("deaf to a cancel: the child is ended, and the next turn starts another") {
        DriverState state;
        state.replies = {numbered(text("half"))};
        state.deaf = true;
        MlxLocalProvider::Options options = Fixture::defaults();
        options.cancel_timeout = std::chrono::milliseconds{200};
        Fixture fixture{state, options};
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        Streamed streamed;
        streamed.options.cancellation = token;
        streamed.options.on_token = [&token](std::string_view) { token.cancel(); };
        CHECK_THROWS_AS(fixture.provider->stream_chat(ask("long"), streamed.options),
                        apogee::harness::CancelledError);
        CHECK_FALSE(fixture.provider->has_live_child());
        CHECK(fixture.drivers.front()->terminated);
    }
}

TEST_CASE("ending the session closes the driver's stdin, and so does the provider's end",
          "[backends][mlx][session]") {
    DriverState state;
    state.replies = {numbered(text("x")) + done()};
    auto fixture = std::make_unique<Fixture>(state);
    (void)fixture->provider->chat(ask("hi"), {});
    const std::shared_ptr<DriverState> driver = fixture->drivers.front();
    fixture->provider->end_session();
    CHECK(driver->stdin_closed);
    CHECK_FALSE(driver->terminated);  // asked, not killed
    CHECK_FALSE(fixture->provider->has_live_child());

    (void)fixture->provider->chat(ask("again"), {});
    const std::shared_ptr<DriverState> second = fixture->drivers.back();
    fixture->provider.reset();
    CHECK(second->stdin_closed);
}

TEST_CASE("an idle spell ends the child, and the next turn starts another",
          "[backends][mlx][session]") {
    DriverState state;
    state.replies = {numbered(text("x")) + done()};
    MlxLocalProvider::Options options = Fixture::defaults();
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours{1};
    options.clock = [&now] { return now; };
    options.idle_unload = std::chrono::seconds{60};
    Fixture fixture{state, options};
    (void)fixture.provider->chat(ask("hi"), {});
    now += std::chrono::seconds{30};
    (void)fixture.provider->chat(ask("hi"), {});
    CHECK(fixture.provider->spawn_count() == 1);
    now += std::chrono::seconds{61};
    (void)fixture.provider->chat(ask("hi"), {});
    CHECK(fixture.provider->spawn_count() == 2);
    CHECK(fixture.drivers.front()->stdin_closed);
}

TEST_CASE("a held child outlasts its idle spell, and the clock rules again once let go",
          "[backends][mlx][session][residency]") {
    // 27e: a session using this model holds it; the idle clock is not
    // consulted until the hold goes, and then from the last use.
    DriverState state;
    state.replies = {numbered(text("x")) + done()};
    MlxLocalProvider::Options options = Fixture::defaults();
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::hours{1};
    options.clock = [&now] { return now; };
    options.idle_unload = std::chrono::seconds{60};
    Fixture fixture{state, options};
    fixture.provider->hold_resident(true);
    CHECK(fixture.provider->held_resident());
    (void)fixture.provider->chat(ask("hi"), {});
    now += std::chrono::seconds{120};
    (void)fixture.provider->chat(ask("hi"), {});
    CHECK(fixture.provider->spawn_count() == 1);

    fixture.provider->hold_resident(false);
    now += std::chrono::seconds{61};
    (void)fixture.provider->chat(ask("hi"), {});
    CHECK(fixture.provider->spawn_count() == 2);
}

TEST_CASE("every start of the driver is heard by the load listener",
          "[backends][mlx][session][residency]") {
    DriverState state;
    state.replies = {numbered(text("x")) + done()};
    Fixture fixture{state};
    std::vector<apogee::harness::StatusEvent> heard;
    fixture.provider->set_load_listener(
        [&heard](const apogee::harness::StatusEvent& event) { heard.push_back(event); });
    // A request streaming no status of its own: heard all the same.
    (void)fixture.provider->chat(ask("hi"), {});
    REQUIRE(heard.size() == 2);
    CHECK(heard[0].type == apogee::harness::StatusEvent::Type::ModelLoading);
    CHECK(heard[0].phase == apogee::harness::StatusEvent::Phase::Start);
    CHECK(heard[1].type == apogee::harness::StatusEvent::Type::ModelReady);
    (void)fixture.provider->chat(ask("again"), {});
    CHECK(heard.size() == 2);
}

// ---------------------------------------------------------------------------
// What it answers about itself
// ---------------------------------------------------------------------------

TEST_CASE("the family profile comes from the directory's model_type, then its names",
          "[backends][mlx][behavior]") {
    const auto profile = [](std::string model_type, std::string text_type,
                            const std::string& name) {
        apogee::backends::MlxModelInfo info;
        info.model_type = std::move(model_type);
        info.text_model_type = std::move(text_type);
        const apogee::backends::ModelProfile* found =
            apogee::backends::resolve_mlx_profile(info, name);
        return found == nullptr ? std::string{"none"} : found->name;
    };
    // HF's spelling of the GGUF architecture, matched as a fact.
    CHECK(profile("gpt_oss", "", "my-finetune") == "gpt-oss");
    CHECK(profile("llama", "", "") == "llama3");
    CHECK(profile("qwen3", "", "") == "qwen3");
    CHECK(profile("qwen3_moe", "", "") == "qwen3");
    // A vision-language model's text model, then the name.
    CHECK(profile("qwen3_vl", "qwen3_vl_text", "Qwen--Qwen3-VL-8B-Instruct") == "qwen3");
    CHECK(profile("gemma4", "gemma4_text", "google--gemma-4-12B-it") == "gemma3");
    CHECK(profile("mystery", "", "mystery-model") == "none");
}

TEST_CASE("a directory with no chat template is a base model; images are refused honestly",
          "[backends][mlx][behavior]") {
    MlxLocalProvider::Options options = Fixture::defaults();
    options.info.chat_template = false;
    options.context_size = 8192;
    Fixture fixture{DriverState{}, options};
    CHECK(fixture.provider->model_behavior().base_model);
    CHECK(fixture.provider->model_behavior().profile == "llama3");
    CHECK_FALSE(fixture.provider->accepts_images());
    CHECK_FALSE(fixture.provider->generation_is_metered());
    CHECK(fixture.provider->context_window() == 8192);
    // Asked before any turn, answered without starting the driver.
    CHECK(fixture.provider->spawn_count() == 0);

    Fixture plain{DriverState{}};
    CHECK_FALSE(plain.provider->model_behavior().base_model);
    CHECK(plain.provider->context_window() == 0);
    const std::vector<apogee::harness::ModelInfo> models = plain.provider->list_models({});
    REQUIRE(models.size() == 1);
    CHECK(models[0].provider == "mlx");
    CHECK(models[0].backend == "local");
}

// ---------------------------------------------------------------------------
// Images (27c)
// ---------------------------------------------------------------------------

namespace {

constexpr std::string_view kPicture = "data:image/png;base64,iVBORw0KGgo=";

ChatRequest look(std::string_view uri = kPicture) {
    ChatRequest request;
    request.messages.push_back(
        ChatMessage{.role = apogee::harness::Role::User,
                    .content = apogee::harness::MessageContent::from_parts(
                        {apogee::harness::ContentPart::from_image_url(std::string{uri}),
                         apogee::harness::ContentPart::from_text("what is this?")})});
    return request;
}

MlxLocalProvider::Options seeing() {
    MlxLocalProvider::Options options = Fixture::defaults();
    options.vision = true;
    return options;
}

bool asked_for_vision(const apogee::platform::ChildCommand& command) {
    return std::ranges::find(command.arguments, "--vision") != command.arguments.end();
}

}  // namespace

TEST_CASE(
    "a model reads images natively only as a vision model with mlx-vlm installed: the answer "
    "flips with the directory's markers and the environment's package",
    "[backends][mlx][vision]") {
    Runtime runtime;
    const std::filesystem::path model = runtime.dir.path() / "model";
    const auto options = [&runtime] {
        return MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    };
    const auto vision = [&runtime, &model] {
        return apogee::backends::probe_mlx_vision(apogee::backends::inspect_mlx_model(model),
                                                  runtime.host);
    };
    // A text model: no.
    CHECK_FALSE(options().vision);
    CHECK(vision().reason.find("not a vision model") != std::string::npos);
    CHECK(vision().remedy.empty());

    // A vision tower but no processor to feed it: still no.
    std::ofstream{model / "config.json"}
        << R"({"model_type":"qwen3_vl","vision_config":{"depth":27},)"
           R"("text_config":{"model_type":"qwen3_vl_text"}})";
    CHECK_FALSE(apogee::backends::inspect_mlx_model(model).vision);
    CHECK_FALSE(options().vision);

    // Both: a vision model -- but mlx-vlm is not installed, and the fix is named.
    std::ofstream{model / "preprocessor_config.json"} << "{}";
    CHECK(apogee::backends::inspect_mlx_model(model).vision);
    CHECK(vision().model);
    CHECK_FALSE(vision().reads_images());
    CHECK(vision().reason.find("mlx-vlm is not installed in the Python environment at " +
                               runtime.host.venv.string()) != std::string::npos);
    CHECK(vision().remedy == "apogee train setup --with mlx-vlm");
    CHECK_FALSE(options().vision);
    CHECK(options().vision_gap.find("apogee train setup --with mlx-vlm") != std::string::npos);

    // Gemma 4's spelling of the processor's file counts the same.
    std::filesystem::remove(model / "preprocessor_config.json");
    std::ofstream{model / "processor_config.json"} << "{}";
    CHECK(apogee::backends::inspect_mlx_model(model).vision);

    // mlx-vlm installed: yes, its version from its distribution record --
    // asked of the files, before any turn, with no driver started.
    const std::filesystem::path site = runtime.host.venv / "lib" / "python3.14" / "site-packages";
    Runtime::touch(site / "mlx_vlm" / "__init__.py");
    std::filesystem::create_directories(site / "mlx_vlm-0.3.9.dist-info");
    CHECK(vision().reads_images());
    CHECK(vision().vlm.version == "0.3.9");
    CHECK(vision().reason.empty());
    CHECK(options().vision);
    Fixture fixture{DriverState{}, options()};
    CHECK(fixture.provider->accepts_images());
    CHECK(fixture.provider->spawn_count() == 0);

    // With no record, the version the package declares.
    std::filesystem::remove_all(site / "mlx_vlm-0.3.9.dist-info");
    std::ofstream{site / "mlx_vlm" / "version.py"} << "__version__ = \"0.3.1\"\n";
    CHECK(vision().vlm.version == "0.3.1");

    // A vision_config that is not one is no tower.
    std::ofstream{model / "config.json"} << R"({"model_type":"llama","vision_config":null})";
    CHECK_FALSE(vision().model);
}

TEST_CASE("an image travels in place to a driver loaded through mlx-vlm, and the answer streams",
          "[backends][mlx][vision]") {
    DriverState state;
    state.vision_startup = std::string{apogee::testing::kVisionReady};
    state.replies = {numbered(text("A red ")) + numbered(text("square.")) + done("stop", 300)};
    Fixture fixture{std::move(state), seeing()};
    Streamed streamed;
    const apogee::harness::ChatResponse response =
        fixture.provider->stream_chat(look(), streamed.options);
    CHECK(streamed.tokens == "A red square.");
    CHECK(response.message.content.plain_text() == "A red square.");

    REQUIRE(fixture.commands.size() == 1);
    CHECK(asked_for_vision(fixture.commands[0]));
    CHECK(fixture.provider->has_vision_child());
    REQUIRE(fixture.provider->driver_ready().has_value());
    CHECK(fixture.provider->driver_ready()->vision);
    // The picture where it sat, as the data: URI it arrived as.
    const json sent = fixture.written(0, 0);
    CHECK(sent["messages"][0]["content"] ==
          json::array({{{"type", "image"}, {"image", std::string{kPicture}}},
                       {{"type", "text"}, {"text", "what is this?"}}}));
    // Said: the load through mlx-vlm, and a cache line that does not claim one.
    const apogee::harness::StatusEvent* loading =
        streamed.status(apogee::harness::StatusEvent::Type::ModelLoading);
    REQUIRE(loading != nullptr);
    CHECK(loading->detail.find("mlx-vlm") != std::string::npos);
    const apogee::harness::StatusEvent* ready =
        streamed.status(apogee::harness::StatusEvent::Type::ModelReady);
    REQUIRE(ready != nullptr);
    CHECK(ready->detail.find("mlx-vlm 0.3.9") != std::string::npos);
    const apogee::harness::StatusEvent* cache =
        streamed.status(apogee::harness::StatusEvent::Type::PromptCache);
    REQUIRE(cache != nullptr);
    CHECK(cache->detail.find("read whole each turn") != std::string::npos);
}

TEST_CASE(
    "a text driver gives way to a vision one when a picture arrives, and the vision one answers "
    "every turn after",
    "[backends][mlx][vision][session]") {
    DriverState state;
    state.vision_startup = std::string{apogee::testing::kVisionReady};
    state.replies = {numbered(text("ok")) + done()};
    Fixture fixture{std::move(state), seeing()};

    (void)fixture.provider->chat(ask("hello"), {});
    REQUIRE(fixture.commands.size() == 1);
    CHECK_FALSE(asked_for_vision(fixture.commands[0]));
    CHECK_FALSE(fixture.provider->has_vision_child());

    (void)fixture.provider->chat(look(), {});
    CHECK(fixture.provider->spawn_count() == 2);
    REQUIRE(fixture.commands.size() == 2);
    CHECK(asked_for_vision(fixture.commands[1]));
    // The text driver was ended the ordinary way: its stdin closed.
    CHECK(fixture.drivers[0]->stdin_closed);
    CHECK(fixture.provider->has_vision_child());

    // Text turns stay on it: one model resident, never loaded back and forth.
    (void)fixture.provider->chat(ask("and now?"), {});
    CHECK(fixture.provider->spawn_count() == 2);
    CHECK(fixture.drivers[1]->writes.size() == 2);
}

TEST_CASE("a driver from before 27c is never sent a picture: it is refused, the fix named",
          "[backends][mlx][vision]") {
    // An older seeded copy ignores --vision and says nothing of it.
    DriverState state;
    state.replies = {numbered(text("never")) + done()};
    Fixture fixture{std::move(state), seeing()};
    try {
        (void)fixture.provider->chat(look(), {});
        FAIL("an older driver must not be sent an image");
    } catch (const apogee::harness::ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("does not read images") != std::string::npos);
        CHECK(message.find("apogee check --fix") != std::string::npos);
    }
    CHECK_FALSE(fixture.provider->has_live_child());
    CHECK(fixture.drivers[0]->writes.empty());
}

TEST_CASE(
    "what a model cannot read is refused before anything is sent: a picture to a model that "
    "reads none, a remote image, audio",
    "[backends][mlx][vision]") {
    MlxLocalProvider::Options blind = Fixture::defaults();
    blind.vision_gap =
        "it is a vision model, but mlx-vlm is not installed in the Python environment at /venv "
        "-- apogee train setup --with mlx-vlm";
    Fixture text_only{DriverState{}, blind};
    CHECK_FALSE(text_only.provider->accepts_images());
    try {
        (void)text_only.provider->chat(look(), {});
        FAIL("a model that reads no images was sent one");
    } catch (const apogee::harness::ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("cannot read images") != std::string::npos);
        CHECK(message.find("apogee train setup --with mlx-vlm") != std::string::npos);
    }
    CHECK(text_only.provider->spawn_count() == 0);

    Fixture sighted{DriverState{}, seeing()};
    try {
        (void)sighted.provider->chat(look("https://example.com/cat.png"), {});
        FAIL("a remote image was accepted");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find("never fetched") != std::string::npos);
    }
    ChatRequest audio;
    audio.messages.push_back(
        ChatMessage{.role = apogee::harness::Role::User,
                    .content = apogee::harness::MessageContent::from_parts(
                        {apogee::harness::ContentPart::from_audio("UklGRg==", "wav")})});
    try {
        (void)sighted.provider->chat(audio, {});
        FAIL("audio was accepted");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find("cannot hear audio") != std::string::npos);
    }
    CHECK(sighted.provider->spawn_count() == 0);
}

TEST_CASE(
    "a directory mlx-vlm built answers every turn through mlx-vlm, text included; one recorded "
    "as mlx-lm's reads no images; one with no record keeps 27c's way",
    "[backends][mlx][vision][kind]") {
    // 53f: the backend chooses by the recorded kind (53e's reader), never by
    // the files alone.
    Runtime runtime;
    const std::filesystem::path model = runtime.dir.path() / "model";
    std::ofstream{model / "config.json"}
        << R"({"model_type":"qwen3_vl","vision_config":{"depth":27},)"
           R"("text_config":{"model_type":"qwen3_vl_text"}})";
    std::ofstream{model / "preprocessor_config.json"} << "{}";
    const std::filesystem::path site = runtime.host.venv / "lib" / "python3.14" / "site-packages";
    Runtime::touch(site / "mlx_vlm" / "__init__.py");
    std::filesystem::create_directories(site / "mlx_vlm-0.6.1.dist-info");
    const auto record = [&model](std::string_view kind) {
        std::ofstream{model / "apogee-snapshot.json"}
            << R"({"source": "convert", "transform": "a converter", "files": [], "kind": ")" << kind
            << R"("})";
    };
    const auto options = [&runtime] {
        return MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    };
    const auto sighted = [] {
        DriverState state;
        state.vision_startup = std::string{apogee::testing::kVisionReady};
        state.replies = {numbered(text("ok")) + done()};
        return state;
    };

    SECTION("no record: text through mlx-lm, its cache kept, mlx-vlm from the first picture") {
        const MlxLocalProvider::Options unrecorded = options();
        CHECK(unrecorded.info.kind.source == apogee::models::MlxKindSource::Files);
        CHECK(unrecorded.vision);
        CHECK_FALSE(unrecorded.info.through_vlm);
        Fixture fixture{sighted(), unrecorded};
        (void)fixture.provider->chat(ask("hello"), {});
        REQUIRE(fixture.commands.size() == 1);
        CHECK_FALSE(asked_for_vision(fixture.commands[0]));
        CHECK(apogee::backends::probe_mlx_backend("local", runtime.entry, runtime.host).ready());
    }
    SECTION("recorded vision: mlx-vlm from the first turn, one driver for every turn") {
        record("vision");
        const MlxLocalProvider::Options built = options();
        CHECK(built.info.kind.source == apogee::models::MlxKindSource::Recorded);
        CHECK(built.info.through_vlm);
        CHECK(built.vision);
        Fixture fixture{sighted(), built};
        CHECK(fixture.provider->accepts_images());
        Streamed streamed;
        (void)fixture.provider->stream_chat(ask("hello"), streamed.options);
        REQUIRE(fixture.commands.size() == 1);
        CHECK(asked_for_vision(fixture.commands[0]));
        CHECK(fixture.provider->has_vision_child());
        const apogee::harness::StatusEvent* loading =
            streamed.status(apogee::harness::StatusEvent::Type::ModelLoading);
        REQUIRE(loading != nullptr);
        CHECK(loading->detail == "through mlx-vlm, which built it");
        // A picture, then text again: the same driver, never swapped.
        (void)fixture.provider->chat(look(), {});
        (void)fixture.provider->chat(ask("and now?"), {});
        CHECK(fixture.provider->spawn_count() == 1);
        CHECK(fixture.drivers[0]->writes.size() == 3);

        // Preloaded the same way.
        Fixture preloaded{sighted(), built};
        preloaded.provider->preload({});
        REQUIRE(preloaded.commands.size() == 1);
        CHECK(asked_for_vision(preloaded.commands[0]));

        // A driver from before 27c ignores --vision: refused, the fix named.
        DriverState older;
        older.replies = {numbered(text("never")) + done()};
        Fixture stale{std::move(older), built};
        try {
            (void)stale.provider->chat(ask("hello"), {});
            FAIL("an older driver must not answer for a directory mlx-vlm built");
        } catch (const apogee::harness::ProviderError& e) {
            const std::string message = e.what();
            CHECK(message.find("does not load models through mlx-vlm") != std::string::npos);
            CHECK(message.find("apogee check --fix") != std::string::npos);
        }
        CHECK(stale.drivers[0]->writes.empty());
    }
    SECTION("recorded chat: mlx-lm's build of its language model reads no images") {
        record("chat");
        const MlxLocalProvider::Options text_only = options();
        CHECK(text_only.info.vision_files);
        CHECK_FALSE(text_only.vision);
        CHECK_FALSE(text_only.info.through_vlm);
        const apogee::backends::MlxVision vision = apogee::backends::probe_mlx_vision(
            apogee::backends::inspect_mlx_model(model), runtime.host);
        CHECK_FALSE(vision.model);
        CHECK(vision.reason.find("its record says it is mlx-lm's build of its language model "
                                 "alone (kind chat), its vision tower left out") !=
              std::string::npos);
        Fixture fixture{sighted(), text_only};
        CHECK_FALSE(fixture.provider->accepts_images());
        try {
            (void)fixture.provider->chat(look(), {});
            FAIL("a picture went to mlx-lm's build of a language model");
        } catch (const apogee::harness::ProviderError& e) {
            CHECK(std::string{e.what()}.find("cannot read images: its record says") !=
                  std::string::npos);
        }
        CHECK(fixture.provider->spawn_count() == 0);
        (void)fixture.provider->chat(ask("hello"), {});
        REQUIRE(fixture.commands.size() == 1);
        CHECK_FALSE(asked_for_vision(fixture.commands[0]));
    }
}

// ---------------------------------------------------------------------------
// Audio (53g)
// ---------------------------------------------------------------------------

namespace {

/// A short WAV as Apogee's ffmpeg runner writes one: RIFF, a PCM format
/// chunk at 16 kHz, mono, 16-bit, then `samples` bytes -- their value the
/// count, so two recordings of different lengths differ in every byte.
std::string wav(std::size_t samples = 32) {
    const auto le = [](std::uint32_t value, int width) {
        std::string out;
        for (int at = 0; at < width; ++at) {
            out.push_back(static_cast<char>((value >> (8U * static_cast<unsigned>(at))) & 0xFFU));
        }
        return out;
    };
    const auto size = static_cast<std::uint32_t>(samples);
    return "RIFF" + le(36 + size, 4) + "WAVE" + "fmt " + le(16, 4) + le(1, 2) + le(1, 2) +
           le(16000, 4) + le(32000, 4) + le(2, 2) + le(16, 2) + "data" + le(size, 4) +
           std::string(samples, static_cast<char>(samples % 251));
}

/// Base64, as the IR carries a recording.
std::string base64(std::string_view bytes) {
    constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t at = 0; at < bytes.size(); at += 3) {
        const auto byte = [&bytes](std::size_t index) -> std::uint32_t {
            return index < bytes.size() ? static_cast<unsigned char>(bytes[index]) : 0U;
        };
        const std::uint32_t chunk = (byte(at) << 16U) | (byte(at + 1) << 8U) | byte(at + 2);
        out.push_back(kAlphabet[(chunk >> 18U) & 0x3FU]);
        out.push_back(kAlphabet[(chunk >> 12U) & 0x3FU]);
        out.push_back(at + 1 < bytes.size() ? kAlphabet[(chunk >> 6U) & 0x3FU] : '=');
        out.push_back(at + 2 < bytes.size() ? kAlphabet[chunk & 0x3FU] : '=');
    }
    return out;
}

/// A question about a recording, the recording after the words.
ChatRequest listen(const std::string& recording, std::string format = "wav") {
    ChatRequest request;
    request.messages.push_back(ChatMessage{
        .role = apogee::harness::Role::User,
        .content = apogee::harness::MessageContent::from_parts(
            {apogee::harness::ContentPart::from_text("what is said?"),
             apogee::harness::ContentPart::from_audio(base64(recording), std::move(format))})});
    return request;
}

/// An omni directory mlx-vlm built, hearing through it.
MlxLocalProvider::Options hearing() {
    MlxLocalProvider::Options options = seeing();
    options.model = "gemma-4-E4B";
    options.info.model_type = "gemma4";
    options.info.kind = {.word = std::string{apogee::models::kMlxVision},
                         .source = apogee::models::MlxKindSource::Recorded};
    options.info.through_vlm = true;
    options.audio = true;
    return options;
}

DriverState hearing_driver(std::vector<std::string> replies) {
    DriverState state;
    state.vision_startup = std::string{apogee::testing::kHearingReady};
    state.replies = std::move(replies);
    return state;
}

}  // namespace

TEST_CASE(
    "a directory hears only as mlx-vlm's build of a set with an audio tower, with mlx-vlm and its "
    "type's module installed: each rung refuses by name, from the files alone",
    "[backends][mlx][audio]") {
    using apogee::backends::MlxAudioRefusal;
    Runtime runtime;
    const std::filesystem::path model = runtime.dir.path() / "model";
    const std::filesystem::path site = runtime.host.venv / "lib" / "python3.14" / "site-packages";
    const std::filesystem::path vlm = site / "mlx_vlm";
    const auto configure = [&model](const char* config) {
        std::ofstream{model / "config.json"} << config;
    };
    const auto record = [&model](std::string_view kind) {
        std::ofstream{model / "apogee-snapshot.json"}
            << R"({"source": "convert", "transform": "mlx_vlm.convert 4bit", "files": [], )"
            << R"("kind": ")" << kind << R"("})";
    };
    const auto audio = [&runtime, &model] {
        return apogee::backends::probe_mlx_audio(apogee::backends::inspect_mlx_model(model),
                                                 runtime.host);
    };

    // Gemma 4's E4B with its processor, but no record: its files make it a
    // vision model, and nothing says mlx-vlm built it -- taken for mlx-lm's
    // layout, which keeps no audio tower.
    configure(apogee::testing::kGemma4Config);
    std::ofstream{model / "processor_config.json"} << "{}";
    CHECK(apogee::backends::inspect_mlx_model(model).towers ==
          std::vector<std::string>{"vision_config", "audio_config"});
    CHECK(audio().refusal == MlxAudioRefusal::NotMlxVlmBuild);
    CHECK(audio().message().starts_with(
        "not mlx-vlm's build: no record says mlx-vlm built it (kind vision, inferred from its "
        "files), and only mlx-vlm's build of a set keeps an audio tower -- 'apogee models "
        "convert <its SafeTensors set> --mlx' builds it through mlx-vlm, the towers kept"));

    // mlx-lm's build of it: the towers left out.
    record("chat");
    CHECK(audio().refusal == MlxAudioRefusal::NotMlxVlmBuild);
    CHECK(audio().reason.find("its record says it is mlx-lm's build of its language model alone "
                              "(kind chat), its towers left out") != std::string::npos);
    // Another kind altogether.
    record("embedding");
    CHECK(audio().refusal == MlxAudioRefusal::NotMlxVlmBuild);
    CHECK(audio().reason.find("kind embedding") != std::string::npos);

    // mlx-vlm's build of a set that only sees.
    record("vision");
    configure(apogee::testing::kQwen3VlConfig);
    CHECK(audio().refusal == MlxAudioRefusal::NoAudioTower);
    CHECK(audio().message() ==
          "no audio tower: its config.json declares none -- no audio_config, nor an omni "
          "model's thinker_config");

    // mlx-vlm's build of Gemma 4, with no mlx-vlm to hear through.
    configure(apogee::testing::kGemma4Config);
    CHECK(audio().refusal == MlxAudioRefusal::NoMlxVlm);
    CHECK(audio().reason.find(runtime.host.venv.string()) != std::string::npos);
    CHECK(audio().remedy == "apogee train setup --with mlx-vlm");

    // An mlx-vlm without the type's module -- a module file of the name is not
    // the package its loader imports.
    Runtime::touch(vlm / "__init__.py");
    std::filesystem::create_directories(site / "mlx_vlm-0.6.0.dist-info");
    Runtime::touch(vlm / "models" / "qwen3_vl" / "__init__.py");
    Runtime::touch(vlm / "models" / "gemma4.py");
    CHECK(audio().refusal == MlxAudioRefusal::NoTypeModule);
    CHECK(audio().message() ==
          "no mlx-vlm module for its type: the installed mlx-vlm 0.6.0 has no module for its "
          "type, gemma4, so it cannot build the model to hear with -- apogee train setup --with "
          "mlx-vlm");

    // With it, it hears: asked of the files, before any turn, no driver started.
    Runtime::touch(vlm / "models" / "gemma4" / "__init__.py");
    CHECK(audio().hears());
    CHECK(audio().message().empty());
    CHECK(audio().vlm.version == "0.6.0");
    const MlxLocalProvider::Options options =
        MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    CHECK(options.audio);
    CHECK(options.audio_gap.empty());
    Fixture fixture{DriverState{}, options};
    CHECK(fixture.provider->accepts_audio());
    // The contract's 16 kHz, which mlx-vlm converts to its own.
    CHECK(fixture.provider->audio_sample_rate() == 0);
    CHECK(fixture.provider->spawn_count() == 0);

    // Qwen3-Omni: its audio tower nested in its thinker, no vision_config at
    // the top -- its record alone makes it mlx-vlm's.
    configure(apogee::testing::kQwen3OmniConfig);
    CHECK(apogee::backends::inspect_mlx_model(model).towers ==
          std::vector<std::string>{"thinker_config"});
    CHECK(audio().refusal == MlxAudioRefusal::NoTypeModule);
    CHECK(audio().reason.find("type, qwen3_omni_moe,") != std::string::npos);
    // Built by another type's package, through mlx-vlm's remap table...
    std::ofstream{vlm / "utils.py"}
        << "MODEL_REMAPPING = {\n    \"llava-qwen2\": \"llava_bunny\",\n"
        << "    'qwen3_omni_moe': 'qwen3_omni',\n}\n";
    CHECK(audio().refusal == MlxAudioRefusal::NoTypeModule);
    Runtime::touch(vlm / "models" / "qwen3_omni" / "__init__.py");
    CHECK(audio().hears());
    // ...or by its own.
    std::filesystem::remove(vlm / "utils.py");
    CHECK_FALSE(audio().hears());
    Runtime::touch(vlm / "models" / "qwen3_omni_moe" / "__init__.py");
    CHECK(audio().hears());
    CHECK(apogee::backends::mlx_vlm_has_module(audio().vlm, "qwen3_omni_moe"));
    CHECK_FALSE(apogee::backends::mlx_vlm_has_module(audio().vlm, ""));
    CHECK_FALSE(apogee::backends::mlx_vlm_has_module({}, "qwen3_omni_moe"));

    // An entry that cannot hear carries its rung into its options.
    configure(apogee::testing::kQwen3VlConfig);
    const MlxLocalProvider::Options deaf =
        MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    CHECK_FALSE(deaf.audio);
    CHECK(deaf.audio_gap.starts_with("no audio tower: "));
    CHECK(deaf.vision);
}

TEST_CASE(
    "a recording reaches a driver that hears as a WAV file of the turn's own, where its part sat, "
    "and is gone once the turn is over, however it ends",
    "[backends][mlx][audio]") {
    Fixture fixture{hearing_driver({numbered(text("It says hello.")) + done("stop", 300)}),
                    hearing()};
    const std::string recording = wav(64);
    Streamed streamed;
    (void)fixture.provider->stream_chat(listen(recording), streamed.options);
    CHECK(streamed.tokens == "It says hello.");
    REQUIRE(fixture.commands.size() == 1);
    CHECK(asked_for_vision(fixture.commands[0]));

    // The part where it sat, as the path of the file written for it...
    const json sent = fixture.written(0, 0);
    const json& content = sent["messages"][0]["content"];
    REQUIRE(content.size() == 2);
    CHECK(content[0] == json{{"type", "text"}, {"text", "what is said?"}});
    CHECK(content[1]["type"] == "audio");
    const std::filesystem::path path{content[1]["audio"].get<std::string>()};
    CHECK(path.filename() == "audio-0.wav");
    CHECK(path.parent_path().filename().string().starts_with("apogee-mlx-audio-"));
    // ...the recording byte for byte while the driver read it, never its
    // base64 on the line...
    REQUIRE(fixture.drivers[0]->heard.size() == 1);
    CHECK(fixture.drivers[0]->heard[0].path == path.string());
    CHECK(fixture.drivers[0]->heard[0].bytes == recording);
    CHECK(fixture.drivers[0]->writes[0].find(base64(recording)) == std::string::npos);
    // ...and nothing of it outlives the turn.
    CHECK_FALSE(std::filesystem::exists(path.parent_path()));

    // Two recordings in two messages: each where it sat, in prompt order.
    ChatRequest two = listen(wav(8));
    two.messages.push_back(ChatMessage::assistant("It says hello."));
    two.messages.push_back(
        ChatMessage{.role = apogee::harness::Role::User,
                    .content = apogee::harness::MessageContent::from_parts(
                        {apogee::harness::ContentPart::from_audio(base64(wav(16)), "WAV"),
                         apogee::harness::ContentPart::from_text("and this one?")})});
    (void)fixture.provider->chat(two, {});
    const json second = fixture.written(0, 1);
    const std::filesystem::path first_path{
        second["messages"][0]["content"][1]["audio"].get<std::string>()};
    const std::filesystem::path second_path{
        second["messages"][2]["content"][0]["audio"].get<std::string>()};
    CHECK(first_path.filename() == "audio-0.wav");
    CHECK(second_path.filename() == "audio-1.wav");
    CHECK(first_path.parent_path() == second_path.parent_path());
    CHECK(first_path.parent_path() != path.parent_path());
    REQUIRE(fixture.drivers[0]->heard.size() == 3);
    CHECK(fixture.drivers[0]->heard[1].bytes == wav(8));
    CHECK(fixture.drivers[0]->heard[2].bytes == wav(16));
    CHECK_FALSE(std::filesystem::exists(first_path.parent_path()));

    // A text turn after it: the same driver, nothing to hear.
    (void)fixture.provider->chat(ask("thanks"), {});
    CHECK(fixture.provider->spawn_count() == 1);
    CHECK(fixture.drivers[0]->heard.size() == 3);

    // A turn the driver fails still leaves nothing behind.
    Fixture failing{hearing_driver({numbered(R"({"type":"error","id":"{id}","kind":"request",)"
                                             R"("message":"ValueError: the audio is too long"})"
                                             "\n")}),
                    hearing()};
    CHECK_THROWS_AS(failing.provider->chat(listen(wav()), {}), apogee::harness::ProviderError);
    REQUIRE(failing.drivers[0]->heard.size() == 1);
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path{failing.drivers[0]->heard[0].path}));
}

TEST_CASE(
    "what cannot be heard is refused before anything starts, the rung said; a recording that is "
    "not a WAV starts nothing either; a driver from before 53g is never sent one",
    "[backends][mlx][audio]") {
    // A directory that cannot hear: the rung and its reason, nothing spawned.
    MlxLocalProvider::Options deaf = hearing();
    deaf.audio = false;
    deaf.audio_gap =
        "no audio tower: its config.json declares none -- no audio_config, nor an omni model's "
        "thinker_config";
    Fixture blind{hearing_driver({numbered(text("never")) + done()}), deaf};
    CHECK_FALSE(blind.provider->accepts_audio());
    try {
        (void)blind.provider->chat(listen(wav()), {});
        FAIL("a model that hears nothing was sent a recording");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find(
                  "gemma-4-E4B cannot hear audio on the mlx backend -- no audio tower: its "
                  "config.json declares none") != std::string::npos);
    }
    CHECK(blind.provider->spawn_count() == 0);

    // One that hears, sent what is not a WAV: by its container, or its bytes.
    Fixture fixture{hearing_driver({numbered(text("never")) + done()}), hearing()};
    try {
        (void)fixture.provider->chat(listen(wav(), "mp3"), {});
        FAIL("an mp3 part was handed on");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find("as a WAV file, and this one is mp3") !=
              std::string::npos);
    }
    try {
        (void)fixture.provider->chat(listen("ID3 an mp3 said to be a WAV"), {});
        FAIL("bytes that are no WAV were handed on");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find("as a WAV file, and this one is not one") !=
              std::string::npos);
    }
    CHECK(fixture.provider->spawn_count() == 0);

    // 53f's driver: it loads through mlx-vlm, and says nothing of audio.
    DriverState older;
    older.vision_startup = std::string{apogee::testing::kVisionReady};
    older.replies = {numbered(text("hello")) + done()};
    Fixture stale{std::move(older), hearing()};
    try {
        (void)stale.provider->chat(listen(wav()), {});
        FAIL("an older driver must not be sent a recording");
    } catch (const apogee::harness::ProviderError& e) {
        const std::string message = e.what();
        CHECK(message.find("does not hand recordings to the model") != std::string::npos);
        CHECK(message.find("apogee check --fix") != std::string::npos);
    }
    CHECK(stale.drivers[0]->writes.empty());
    // It answers text as it did: kept, not loaded again.
    CHECK(stale.provider->has_live_child());
    (void)stale.provider->chat(ask("hello"), {});
    CHECK(stale.provider->spawn_count() == 1);
}

TEST_CASE("preload starts the driver and waits for its model", "[backends][mlx][session]") {
    Fixture fixture{DriverState{}};
    CHECK(fixture.provider->model_status().type ==
          apogee::harness::StatusEvent::Type::ModelLoading);
    fixture.provider->preload({});
    CHECK(fixture.provider->spawn_count() == 1);
    REQUIRE(fixture.provider->driver_ready().has_value());
    CHECK(fixture.provider->driver_ready()->model_type == "llama");
    CHECK(fixture.provider->model_status().type == apogee::harness::StatusEvent::Type::ModelReady);
}

// ---------------------------------------------------------------------------
// The refusal ladder
// ---------------------------------------------------------------------------

TEST_CASE("the refusal ladder: every rung refuses with its own reason and its exact fix",
          "[backends][mlx][ladder]") {
    std::set<std::string> messages;
    {
        const Runtime runtime;
        const MlxReadiness ready = runtime.probe();
        CHECK(ready.ready());
        CHECK(ready.message().empty());
        CHECK(ready.version == "0.32.0");
        CHECK(ready.model_dir == runtime.dir.path() / "model");
    }
    const auto refused = [&messages](const MlxReadiness& readiness, MlxRefusal rung,
                                     std::string_view reason, std::string_view remedy) {
        INFO(readiness.message());
        CHECK(readiness.refusal == rung);
        CHECK(readiness.reason.find(reason) != std::string::npos);
        CHECK(readiness.remedy.find(remedy) != std::string::npos);
        messages.insert(readiness.message());
    };
    {
        Runtime runtime;
        runtime.host.target = "linux-x64";
        refused(runtime.probe(), MlxRefusal::Platform,
                "Apple silicon macOS only, and this build is linux-x64", "llamacpp");
    }
    {
        const Runtime runtime;
        std::filesystem::remove(runtime.host.venv / "bin" / "python");
        refused(runtime.probe(), MlxRefusal::NoEnvironment,
                "no Python environment at " + runtime.host.venv.string(),
                "apogee train setup --with mlx");
    }
    {
        const Runtime runtime;
        std::filesystem::remove_all(runtime.host.venv / "lib");
        refused(runtime.probe(), MlxRefusal::NoMlxLm, "mlx-lm is not installed",
                "apogee train setup --with mlx");
    }
    {
        Runtime runtime;
        runtime.entry.model_path.clear();
        refused(runtime.probe(), MlxRefusal::NoModelPath, "model_path is not set",
                "apogee config add-backend local --type mlx --model-path");
    }
    {
        Runtime runtime;
        runtime.entry.model_path = (runtime.dir.path() / "gone").string();
        refused(runtime.probe(), MlxRefusal::ModelMissing, "model_path does not exist",
                "--model-path <model directory>");
    }
    {
        Runtime runtime;
        Runtime::touch(runtime.dir.path() / "model.gguf");
        runtime.entry.model_path = (runtime.dir.path() / "model.gguf").string();
        refused(runtime.probe(), MlxRefusal::NotAModelDirectory, "is a file", "--type mlx");
    }
    {
        const Runtime runtime;
        std::filesystem::remove(runtime.dir.path() / "model" / "config.json");
        refused(runtime.probe(), MlxRefusal::NotAModelDirectory, "has no config.json",
                "--type mlx");
    }
    {
        // A directory mlx-vlm built (53f), with no mlx-vlm to load it -- asked
        // before the driver, which could not load it either.
        const Runtime runtime;
        std::ofstream{runtime.dir.path() / "model" / "apogee-snapshot.json"}
            << R"({"source": "convert", "transform": "mlx_vlm.convert 4bit", "files": [],)"
               R"( "kind": "vision"})";
        std::filesystem::remove(runtime.host.driver);
        refused(runtime.probe(), MlxRefusal::NoMlxVlm,
                "was built by mlx-vlm (its record says vision), which loads it for every turn, "
                "and mlx-vlm is not installed",
                "apogee train setup --with mlx-vlm");
    }
    {
        const Runtime runtime;
        std::filesystem::remove(runtime.host.driver);
        refused(runtime.probe(), MlxRefusal::NoDriver, "the MLX driver is missing",
                "apogee check --fix");
    }
    // Nine rungs, nine different sentences.
    CHECK(messages.size() == 9);
    CHECK(apogee::backends::to_string(MlxRefusal::NoMlxVlm) == "no mlx-vlm");
}

TEST_CASE("the ladder asks the platform first, the runtime before the model",
          "[backends][mlx][ladder]") {
    Runtime runtime;
    std::filesystem::remove_all(runtime.host.venv);
    runtime.entry.model_path.clear();
    CHECK(runtime.probe().refusal == MlxRefusal::NoEnvironment);
    runtime.host.target = "windows-x64";
    CHECK(runtime.probe().refusal == MlxRefusal::Platform);

    const Runtime runtime_only;
    std::filesystem::remove(runtime_only.host.driver);
    CHECK(apogee::backends::probe_mlx_runtime(runtime_only.host).refusal == MlxRefusal::NoDriver);
    std::filesystem::remove_all(runtime_only.host.venv / "lib");
    CHECK(apogee::backends::probe_mlx_runtime(runtime_only.host).refusal == MlxRefusal::NoMlxLm);
}

TEST_CASE("construction refuses with the rung's message, and the factory skips the entry",
          "[backends][mlx][ladder][factory]") {
    const Runtime runtime;
    std::filesystem::remove(runtime.host.venv / "bin" / "python");
    try {
        (void)MlxLocalProvider::from_config("local", runtime.entry, runtime.host);
        FAIL("construction must refuse");
    } catch (const apogee::harness::ProviderError& e) {
        CHECK(std::string{e.what()}.find("apogee train setup --with mlx") != std::string::npos);
    }

    // Through the factory, against a data directory with no environment: the
    // entry is skipped and its reason names the rung, wherever this runs.
    const apogee::testing::TempDir home{"mlx-factory"};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::string reason;
    CHECK(apogee::backends::make_provider("local", runtime.entry, reason) == nullptr);
    const bool apple = MlxHost::current().target == apogee::backends::kMlxTarget;
    CHECK(reason.find(apple ? "no Python environment" : "Apple silicon macOS only") !=
          std::string::npos);
}

TEST_CASE("a model directory says its type, its template and its authors' sampling",
          "[backends][mlx][model]") {
    const apogee::backends::MlxModelInfo fixture = apogee::backends::inspect_mlx_model(
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "mlx" / "model");
    CHECK(fixture.model_type == "llama");
    CHECK(fixture.chat_template);
    CHECK(fixture.sampling.temperature == 0.6);
    CHECK(fixture.sampling.top_p == 0.9);
    CHECK_FALSE(fixture.sampling.top_k.has_value());

    const apogee::testing::TempDir dir{"mlx-model"};
    std::ofstream{dir.path() / "config.json"}
        << R"({"model_type":"qwen3_vl","text_config":{"model_type":"qwen3_vl_text"}})";
    std::ofstream{dir.path() / "tokenizer_config.json"} << R"({"chat_template":null})";
    std::ofstream{dir.path() / "generation_config.json"}
        << R"({"do_sample":false,"temperature":0.7,"top_k":20,"repetition_penalty":1.05})";
    apogee::backends::MlxModelInfo info = apogee::backends::inspect_mlx_model(dir.path());
    CHECK(info.text_model_type == "qwen3_vl_text");
    CHECK_FALSE(info.chat_template);
    // do_sample false is the authors saying greedy.
    CHECK(info.sampling.temperature == 0.0);
    CHECK(info.sampling.top_k == 20);
    CHECK(info.sampling.repeat_penalty == 1.05);

    std::ofstream{dir.path() / "chat_template.jinja"} << "{{ messages }}";
    CHECK(apogee::backends::inspect_mlx_model(dir.path()).chat_template);
}

TEST_CASE("the window is config.json's under 26a's default, the entry's when it sets one",
          "[backends][mlx][window]") {
    // 27b: read from the model directory, never guessed -- the same window a
    // GGUF chat of the model would get, so warnings and compaction agree.
    Runtime runtime;
    std::ofstream{runtime.dir.path() / "model" / "config.json"}
        << R"({"model_type":"llama","max_position_embeddings":131072})";
    const auto window_of = [&runtime]() {
        Fixture fixture{DriverState{},
                        MlxLocalProvider::options_from("local", runtime.entry, runtime.host)};
        return fixture.provider->context_window();
    };
    const MlxLocalProvider::Options options =
        MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    CHECK(options.trained_window == 131072);
    CHECK(options.config_read);
    CHECK(window_of() == 32768);

    // Trained for less than the default: the trained window.
    std::ofstream{runtime.dir.path() / "model" / "config.json"}
        << R"({"model_type":"llama","text_config":{"max_position_embeddings":4096}})";
    CHECK(window_of() == 4096);

    // The entry's own, over anything the model says.
    runtime.entry.context_size = 2048;
    CHECK(window_of() == 2048);

    // A directory with no configuration says nothing.
    runtime.entry.context_size.reset();
    std::filesystem::remove(runtime.dir.path() / "model" / "config.json");
    CHECK(window_of() == 0);
}

TEST_CASE("an entry's fields map onto the provider's options", "[backends][mlx][config]") {
    Runtime runtime;
    runtime.entry.model = "llama-3b";
    runtime.entry.max_tokens = 512;
    runtime.entry.context_size = 16384;
    runtime.entry.idle_unload_seconds = 900;
    runtime.entry.temperature = 0.3;
    runtime.entry.top_k = 40;
    runtime.entry.seed = 7;
    const MlxLocalProvider::Options options =
        MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    CHECK(options.backend_name == "local");
    CHECK(options.model == "llama-3b");
    CHECK(options.model_dir == runtime.dir.path() / "model");
    CHECK(options.interpreter == runtime.host.venv / "bin" / "python");
    CHECK(options.driver == runtime.host.driver);
    CHECK(options.max_tokens == 512);
    CHECK(options.context_size == 16384);
    CHECK(options.idle_unload == std::chrono::seconds{900});
    CHECK(options.sampling.temperature == 0.3);
    CHECK(options.sampling.top_k == 40);
    CHECK(options.seed == 7U);
    CHECK(options.info.model_type == "llama");
}

// ---------------------------------------------------------------------------
// Adapters (53a)
// ---------------------------------------------------------------------------

namespace {

/// A model directory with a shard, an adapter directory beside it, and the
/// options that run the one over the other, the adapter pinned as it is now.
struct AdapterBench {
    apogee::testing::TempDir dir{"mlx-adapters-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = dir.path() / "model";
    std::filesystem::path adapter = dir.path() / "style-adapter";
    std::filesystem::path memo = dir.path() / "cache" / "weight-hashes.json";

    AdapterBench() {
        std::filesystem::create_directories(model);
        std::ofstream{model / "config.json"} << R"({"model_type":"llama"})";
        std::ofstream{model / "model.safetensors"} << "weights";
        std::filesystem::create_directories(adapter);
        std::ofstream{adapter / "adapter_config.json"} << "{}";
        std::ofstream{adapter / "adapters.safetensors"} << "delta";
    }

    [[nodiscard]] MlxLocalProvider::Options options() const {
        MlxLocalProvider::Options options = Fixture::defaults();
        options.model_dir = model;
        options.weight_hashing.memo = memo;
        options.adapters = {
            {.path = adapter.string(), .base_sha256 = apogee::backends::weights_sha256(model)}};
        return options;
    }

    /// The ready line of a driver that applied the adapter.
    [[nodiscard]] std::string ready() const {
        return json{{"type", "ready"},        {"protocol", 1},
                    {"model_type", "llama"},  {"chat_template", true},
                    {"tool_parser", nullptr}, {"thinking", false},
                    {"mlx_lm", "0.32.0"},     {"adapter", adapter.string()}}
                   .dump() +
               "\n";
    }
};

}  // namespace

TEST_CASE("an adapter reaches the driver as mlx-lm's adapter_path, the argv pinned",
          "[backends][mlx][adapters]") {
    const AdapterBench bench;
    DriverState state;
    state.startup = bench.ready();
    state.replies = {numbered(text("Styled.")) + done()};
    Fixture fixture{state, bench.options()};
    Streamed streamed;
    CHECK(fixture.provider->stream_chat(ask("hi"), streamed.options).message.content.plain_text() ==
          "Styled.");
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands[0].arguments ==
          std::vector<std::string>{"/home/training/scripts/mlx_generate.py", "--model",
                                   bench.model.string(), "--adapter", bench.adapter.string()});
    REQUIRE(streamed.status(apogee::harness::StatusEvent::Type::ModelReady) != nullptr);
    CHECK(streamed.status(apogee::harness::StatusEvent::Type::ModelReady)->detail ==
          "mlx-lm 0.32.0, llama, adapter style-adapter");
    // One driver, the adapter applied once as it loaded.
    CHECK(fixture.provider->spawn_count() == 1);
}

TEST_CASE("a driver that never names the adapter it was handed is refused, the fix named",
          "[backends][mlx][adapters]") {
    // One older than 53a ignores --adapter and loads the bare base: answering
    // from it would read as the adapter's answer.
    const AdapterBench bench;
    DriverState state;
    state.replies = {numbered(text("Bare.")) + done()};
    Fixture fixture{state, bench.options()};
    Streamed streamed;
    CHECK_THROWS_WITH(fixture.provider->stream_chat(ask("hi"), streamed.options),
                      Catch::Matchers::ContainsSubstring("does not apply adapters") &&
                          Catch::Matchers::ContainsSubstring("apogee check --fix"));
    CHECK_FALSE(fixture.provider->has_live_child());
}

TEST_CASE("an adapter pinned to other weights is refused before a driver starts",
          "[backends][mlx][adapters][refusal]") {
    const AdapterBench bench;
    MlxLocalProvider::Options options = bench.options();
    std::ofstream{bench.model / "model.safetensors"} << "retrained weights";
    DriverState state;
    state.startup = bench.ready();
    Fixture fixture{state, std::move(options)};
    Streamed streamed;
    CHECK_THROWS_WITH(fixture.provider->stream_chat(ask("hi"), streamed.options),
                      Catch::Matchers::ContainsSubstring("the adapter " + bench.adapter.string() +
                                                         " was pinned to other base weights"));
    CHECK(fixture.provider->spawn_count() == 0);
    // An adapter directory gone: refused by name, nothing started either.
    std::filesystem::remove_all(bench.adapter);
    Fixture gone{state, bench.options()};
    CHECK_THROWS_WITH(gone.provider->stream_chat(ask("hi"), streamed.options),
                      Catch::Matchers::ContainsSubstring("is not there"));
    CHECK(gone.provider->spawn_count() == 0);
}

TEST_CASE("an mlx entry's adapter maps onto the provider's options",
          "[backends][mlx][adapters][config]") {
    Runtime runtime;
    runtime.entry.adapters = {{.path = "/adapters/style", .base_sha256 = std::string(64, 'c')}};
    const MlxLocalProvider::Options options =
        MlxLocalProvider::options_from("local", runtime.entry, runtime.host);
    CHECK(options.adapters == runtime.entry.adapters);
    CHECK(options.weight_hashing.memo == apogee::harness::weight_hash_cache_path());
}
