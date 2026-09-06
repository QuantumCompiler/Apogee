#include "backends/ollama_cli.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "backends/ollama_cli_output.h"
#include "harness/errors.h"
#include "support/fake_child.h"

/// The Ollama backend, against a scripted child and recorded output.
///
/// Two of these tests carry more weight than the rest, and both come from the
/// characterization rather than from the family template:
///
///  * the backend must **never cause an Ollama server to start**, because
///    `ollama run` starts one itself when it cannot reach one — so the refusal
///    has to happen before the spawn, not inside it;
///  * `--nowordwrap` must always be passed, because without it the CLI writes
///    cursor-control bytes into stdout even when piped.
namespace {

using apogee::backends::OllamaCliProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::testing::FakeSpawner;

[[nodiscard]] std::string read_fixture(std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "ollama_cli" / name;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Fixture {
    std::shared_ptr<FakeSpawner> spawner = std::make_shared<FakeSpawner>();
    std::unique_ptr<OllamaCliProvider> provider;
    /// Whether the scripted probe reports a reachable server.
    std::shared_ptr<bool> server_up = std::make_shared<bool>(true);

    explicit Fixture(std::string script = {}) {
        if (!script.empty()) {
            spawner->stdout_scripts = {std::move(script)};
        }

        OllamaCliProvider::Options options;
        options.backend_name = "ollama";
        options.model = "gpt-oss:20b-cloud";

        auto spawner_copy = spawner;
        auto up = server_up;
        provider = std::make_unique<OllamaCliProvider>(
            std::move(options),
            [spawner_copy](const apogee::platform::ChildCommand& command, std::string& error) {
                return (*spawner_copy)(command, error);
            },
            [up](const std::string&) { return *up; });
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

TEST_CASE("the backend refuses rather than letting the CLI start a server",
          "[backends][ollama][invariant]") {
    // THE rule on this item. `ollama run` starts a server when it cannot reach
    // one, so spawning it with none up would make Apogee the cause of a
    // listening socket -- one process removed, but ours. The refusal must
    // therefore happen BEFORE the spawn.
    Fixture fixture{"hello\n"};
    *fixture.server_up = false;

    try {
        (void)fixture.provider->chat(turn({ChatMessage::user("hi")}), {});
        FAIL("expected a ProviderError");
    } catch (const apogee::harness::ProviderError& error) {
        const std::string message = error.what();
        // It names the fix, and says why it will not do it for you.
        CHECK(message.find("Start Ollama") != std::string::npos);
        CHECK(message.find("will not start it") != std::string::npos);
    }

    // The decisive assertion: nothing was spawned at all.
    CHECK(fixture.spawner->commands.empty());
}

TEST_CASE("no argv this backend can produce starts a server", "[backends][ollama][invariant]") {
    // Belt to the refusal's braces: even on the success path, the subcommand is
    // a literal `run`. No config value can turn this backend into one that
    // starts a daemon.
    OllamaCliProvider::Options options;
    options.model = "gpt-oss:20b-cloud";

    for (const bool hide : {false, true}) {
        const std::vector<std::string> args = OllamaCliProvider::build_arguments(options, hide);
        INFO("hide_thinking=" << hide);
        CHECK(args.front() == "run");
        CHECK_FALSE(has(args, "serve"));
        CHECK_FALSE(has(args, "start"));
    }
}

TEST_CASE("the nowordwrap flag is always passed", "[backends][ollama][flags]") {
    // Measured on 0.33.2: without it the CLI writes ESC[nD ESC[K into stdout to
    // re-wrap words, EVEN WHEN PIPED. A consumer without this flag parses
    // terminal escapes as answer text.
    OllamaCliProvider::Options options;
    options.model = "gpt-oss:20b-cloud";

    for (const bool hide : {false, true}) {
        CHECK(has(OllamaCliProvider::build_arguments(options, hide), "--nowordwrap"));
    }
}

TEST_CASE("a turn spawns once, with the model and the prompt", "[backends][ollama]") {
    Fixture fixture{read_fixture("thinking_then_answer.txt")};

    const auto response =
        fixture.provider->chat(turn({ChatMessage::user("Say exactly: hello")}), {});

    REQUIRE(fixture.spawner->commands.size() == 1);
    const std::vector<std::string>& args = fixture.spawner->commands.front();
    CHECK(has(args, "gpt-oss:20b-cloud"));
    CHECK(args.back().find("Say exactly: hello") != std::string::npos);

    CHECK(response.message.content.plain_text() == "hello");
    CHECK(response.model == "gpt-oss:20b-cloud");
}

TEST_CASE("recorded output separates thinking from the answer", "[backends][ollama][thinking]") {
    // Against the REAL captured bytes from a signed-in cloud session, not a
    // transcription of what the docs imply.
    Fixture fixture{read_fixture("thinking_then_answer.txt")};

    std::string answer;
    std::string thinking;
    apogee::harness::StreamOptions options;
    options.on_token = [&answer](std::string_view chunk) { answer += chunk; };
    options.on_thinking = [&thinking](std::string_view chunk) { thinking += chunk; };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("Say exactly: hello")}), options);

    CHECK(answer == "hello");
    CHECK_FALSE(thinking.empty());
    // The markers themselves are framing and must never reach either channel.
    CHECK(thinking.find("Thinking...") == std::string::npos);
    CHECK(thinking.find("done thinking") == std::string::npos);
    CHECK(answer.find("Thinking") == std::string::npos);
    // And the reasoning must never leak into the persisted answer.
    CHECK(response.message.content.plain_text() == "hello");
}

TEST_CASE("output with thinking suppressed still yields the answer",
          "[backends][ollama][thinking]") {
    // The `--hidethinking` capture: no markers at all. The demux must not
    // require them.
    Fixture fixture{read_fixture("answer_only.txt")};

    std::string thinking;
    apogee::harness::StreamOptions options;
    options.on_thinking = [&thinking](std::string_view chunk) { thinking += chunk; };

    const auto response =
        fixture.provider->stream_chat(turn({ChatMessage::user("Say exactly: hello")}), options);

    CHECK(response.message.content.plain_text() == "hello");
    CHECK(thinking.empty());
}

TEST_CASE("the recorded output parses identically at every chunk size",
          "[backends][ollama][replay]") {
    // The family's framing guardrail, applied to a prose stream. A marker is
    // eleven bytes; a pipe splits it roughly one turn in eleven, so this is a
    // routine case rather than an edge.
    const std::string bytes = read_fixture("thinking_then_answer.txt");

    std::string whole_answer;
    std::string whole_thinking;
    {
        apogee::backends::OllamaOutputDemux demux;
        demux.feed(
            bytes, [&](std::string_view c) { whole_answer += c; },
            [&](std::string_view c) { whole_thinking += c; });
        demux.flush([&](std::string_view c) { whole_answer += c; },
                    [&](std::string_view c) { whole_thinking += c; });
    }
    REQUIRE_FALSE(whole_answer.empty());

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{7},
                                    std::size_t{64}, std::size_t{4096}}) {
        INFO("chunk " << chunk);
        std::string answer;
        std::string thinking;
        apogee::backends::OllamaOutputDemux demux;
        for (std::size_t offset = 0; offset < bytes.size(); offset += chunk) {
            demux.feed(
                bytes.substr(offset, std::min(chunk, bytes.size() - offset)),
                [&](std::string_view c) { answer += c; },
                [&](std::string_view c) { thinking += c; });
        }
        demux.flush([&](std::string_view c) { answer += c; },
                    [&](std::string_view c) { thinking += c; });

        CHECK(answer == whole_answer);
        CHECK(thinking == whole_thinking);
    }
}

TEST_CASE("a marker only counts on its own line", "[backends][ollama][thinking]") {
    // The ambiguity in-band markers always carry: a model that WRITES the word
    // must not be mistaken for the CLI's framing.
    apogee::backends::OllamaOutputDemux demux;
    std::string answer;
    std::string thinking;
    const auto a = [&answer](std::string_view c) { answer += c; };
    const auto t = [&thinking](std::string_view c) { thinking += c; };

    demux.feed("I was Thinking... about lunch\n", a, t);
    demux.flush(a, t);

    CHECK(thinking.empty());
    CHECK(answer.find("Thinking...") != std::string::npos);
}

TEST_CASE("a later marker in the answer is the model's own words", "[backends][ollama][thinking]") {
    // The second bound: once the answer has begun, a line that happens to be
    // exactly the marker is prose. Without this, a model quoting the CLI would
    // silently swallow the rest of its own answer into the thinking channel.
    apogee::backends::OllamaOutputDemux demux;
    std::string answer;
    std::string thinking;
    const auto a = [&answer](std::string_view c) { answer += c; };
    const auto t = [&thinking](std::string_view c) { thinking += c; };

    demux.feed("Here is what it prints:\nThinking...\nand then more\n", a, t);
    demux.flush(a, t);

    CHECK(thinking.empty());
    CHECK(answer.find("and then more") != std::string::npos);
}

TEST_CASE("history is flattened into one prompt, labelled by speaker", "[backends][ollama]") {
    // There is no turn protocol, so the whole conversation is re-sent every
    // turn. A real fidelity cost, asserted so it stays visible.
    const std::string prompt = OllamaCliProvider::flatten_prompt(
        {ChatMessage::system("be brief"), ChatMessage::user("hello"), ChatMessage::assistant("hi"),
         ChatMessage::user("more")});

    CHECK(prompt.find("System: be brief") != std::string::npos);
    CHECK(prompt.find("User: hello") != std::string::npos);
    CHECK(prompt.find("Assistant: hi") != std::string::npos);
    CHECK(prompt.find("User: more") != std::string::npos);
}

TEST_CASE("a config with no model is refused by name", "[backends][ollama][errors]") {
    apogee::harness::BackendConfig config;
    config.type = apogee::harness::BackendType::OllamaCli;

    try {
        (void)OllamaCliProvider::from_config("ollama", config);
        FAIL("expected a ProviderError");
    } catch (const apogee::harness::ProviderError& error) {
        CHECK(std::string{error.what()}.find("model") != std::string::npos);
    }
}

TEST_CASE("a spawn failure is reported", "[backends][ollama][errors]") {
    Fixture fixture{"unused"};
    fixture.spawner->failing_spawns = {0};

    CHECK_THROWS_AS(fixture.provider->chat(turn({ChatMessage::user("hi")}), {}),
                    apogee::harness::ProviderError);
}

TEST_CASE("list_models reports the configured cloud model", "[backends][ollama]") {
    Fixture fixture{"hello\n"};
    const auto models = fixture.provider->list_models({});
    REQUIRE(models.size() == 1);
    CHECK(models.front().provider == "ollama-cli");
    CHECK(models.front().id == "gpt-oss:20b-cloud");
}
