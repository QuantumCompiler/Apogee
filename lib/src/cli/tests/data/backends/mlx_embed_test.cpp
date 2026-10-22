#include "backends/mlx_embed.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "contracts/errors.h"
#include "support/env_guard.h"
#include "support/fake_mlx_driver.h"
#include "support/mlx_model.h"

/// An `mlx` entry over an embedding model (53h), over a scripted driver, and
/// its rungs of the mlx ladder.
///
/// The fake answers each `embed` with one vector a text -- the text's length,
/// then its place in the batch -- so a test can tell which vector is whose:
/// the slices the seam cuts, the order they come back in, one child holding
/// the model across them. What a real driver will not do on demand is
/// scripted: an answer a vector short, a vector of another width, a batch it
/// cannot embed, a death mid-batch with a traceback on stderr, a batch it
/// never answers while the call is cancelled.
namespace {

using apogee::backends::kTextsPerMlxBatch;
using apogee::backends::MlxEmbedProvider;
using apogee::backends::MlxHost;
using apogee::backends::MlxRefusal;
using apogee::testing::DriverState;
using apogee::testing::FakeDriver;

/// A provider over fake drivers, each spawn handed the next state.
struct Fixture {
    std::vector<std::shared_ptr<DriverState>> drivers;
    std::vector<apogee::platform::ChildCommand> commands;
    std::unique_ptr<MlxEmbedProvider> provider;

    explicit Fixture(DriverState first, MlxEmbedProvider::Options options = defaults()) {
        drivers.push_back(std::make_shared<DriverState>(std::move(first)));
        provider = std::make_unique<MlxEmbedProvider>(
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
                    next.embedded.clear();
                    drivers.push_back(std::make_shared<DriverState>(std::move(next)));
                }
                return std::make_unique<FakeDriver>(drivers[commands.size() - 1]);
            });
    }

    static MlxEmbedProvider::Options defaults() {
        MlxEmbedProvider::Options options;
        options.backend_name = "embed";
        options.model = "/models/Gemma-Embedding-300M/mlx/aaaaaaaaaaaa";
        options.model_dir = "/models/Gemma-Embedding-300M/mlx/aaaaaaaaaaaa";
        options.interpreter = "/venv/bin/python";
        options.driver = "/home/training/scripts/mlx_embed.py";
        options.width = 4;
        options.stall_timeout = std::chrono::seconds{5};
        options.cancel_timeout = std::chrono::milliseconds{300};
        return options;
    }
};

/// A driver that has loaded its model and answers each batch with 4-wide
/// vectors.
DriverState embedding_driver() {
    DriverState state;
    state.startup = std::string{apogee::testing::kEmbeddingReady};
    state.embed_width = 4;
    return state;
}

std::vector<std::string> texts(std::size_t count) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(std::string(i % 7 + 1, 'x'));
    }
    return out;
}

/// A fake runtime under `root`, as the ladder reads it: an interpreter, the
/// `mlx_lm` package, both seeded drivers -- and `mlx_embeddings` unless left
/// out.
MlxHost fake_host(const std::filesystem::path& root, bool embeddings = true) {
    MlxHost host;
    host.target = "macos-arm64";
    host.venv = root / "venv";
    host.driver = root / "scripts" / "mlx_generate.py";
    host.embedder = root / "scripts" / "mlx_embed.py";
    const std::filesystem::path site = host.venv / "lib" / "python3.99" / "site-packages";
    apogee::testing::write_fixture_file(host.venv / "bin" / "python", "#!/bin/sh\n");
    apogee::testing::write_fixture_file(site / "mlx_lm" / "__init__.py", "");
    if (embeddings) {
        apogee::testing::write_fixture_file(site / "mlx_embeddings" / "__init__.py", "");
        apogee::testing::write_fixture_file(site / "mlx_embeddings-0.1.0.dist-info" / "METADATA",
                                            "");
    }
    apogee::testing::write_fixture_file(host.driver, "# the generation driver\n");
    apogee::testing::write_fixture_file(host.embedder, "# the embedding driver\n");
    return host;
}

/// An MLX directory mlx-embeddings built: its record says `embedding`, its
/// config states the hidden size.
void write_embedding_dir(const std::filesystem::path& dir) {
    apogee::testing::write_mlx_model(dir, {.model_type = "gemma3_text", .kind = "embedding"});
    apogee::testing::write_fixture_file(
        dir / "config.json",
        R"({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text", "hidden_size": 768})");
}

}  // namespace

TEST_CASE("an mlx embedder cuts its inputs as the seam does, one driver holding the model",
          "[mlx][embedding][backends]") {
    Fixture fixture{embedding_driver()};
    MlxEmbedProvider& provider = *fixture.provider;
    // What the store records before anything is asked: the directory's width,
    // and a space named for the backend, unmetered.
    CHECK(provider.embedding_dimensions() == 4);
    CHECK(provider.embedding_model_name() == "mlx:/models/Gemma-Embedding-300M/mlx/aaaaaaaaaaaa");
    CHECK_FALSE(provider.embedding_is_metered());
    CHECK_FALSE(provider.generation_is_metered());
    CHECK_FALSE(provider.accepts_images());
    CHECK(provider.spawn_count() == 0);

    const std::vector<std::string> inputs = texts((2 * kTextsPerMlxBatch) + 2);
    const std::vector<std::vector<float>> vectors = provider.embed(inputs, {});
    REQUIRE(vectors.size() == inputs.size());
    // Each vector is its own text's, in order.
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        INFO(i);
        REQUIRE(vectors[i].size() == 4);
        CHECK(vectors[i][0] == static_cast<float>(inputs[i].size()));
        CHECK(vectors[i][1] == static_cast<float>(i % kTextsPerMlxBatch));
    }
    // One child, started as the driver, with the model directory.
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands.front().program == "/venv/bin/python");
    CHECK(fixture.commands.front().arguments ==
          std::vector<std::string>{"/home/training/scripts/mlx_embed.py", "--model",
                                   "/models/Gemma-Embedding-300M/mlx/aaaaaaaaaaaa"});
    // The seam's slices: 64, 64, 2 -- each its own request, ids increasing.
    const DriverState& driver = *fixture.drivers.front();
    REQUIRE(driver.embedded.size() == 3);
    CHECK(driver.embedded[0].size() == kTextsPerMlxBatch);
    CHECK(driver.embedded[1].size() == kTextsPerMlxBatch);
    CHECK(driver.embedded[2].size() == 2);
    CHECK(nlohmann::json::parse(driver.writes.at(0)).at("id") == 1);
    CHECK(nlohmann::json::parse(driver.writes.at(2)).at("id") == 3);
    CHECK(nlohmann::json::parse(driver.writes.at(0)).at("type") == "embed");

    // Again: the same child, the model held.
    CHECK(provider.embed({"again"}, {}).size() == 1);
    CHECK(provider.spawn_count() == 1);
    CHECK(provider.embed({}, {}).empty());
    CHECK(provider.spawn_count() == 1);
    REQUIRE(provider.driver_ready().has_value());
    CHECK(provider.driver_ready()->embedding);
    CHECK(provider.driver_ready()->mlx_embeddings_version == "0.1.0");
}

TEST_CASE("an mlx embedding entry answers no chat, refused before any driver starts",
          "[mlx][embedding][backends]") {
    Fixture fixture{embedding_driver()};
    MlxEmbedProvider& provider = *fixture.provider;
    apogee::harness::ChatRequest request;
    request.messages.push_back(apogee::harness::ChatMessage::user("hello?"));
    try {
        (void)provider.chat(request, {});
        FAIL("a chat on an embedding model was answered");
    } catch (const apogee::harness::ProviderError& e) {
        const std::string what = e.what();
        CHECK(what.find("an embedding model: the embedding role uses it, never a chat backend") !=
              std::string::npos);
        CHECK(what.find("apogee config set-default-embedding embed") != std::string::npos);
    }
    CHECK_THROWS_AS(provider.stream_chat(request, {}), apogee::harness::ProviderError);
    CHECK_THROWS_AS(provider.complete(request, {}), apogee::harness::ProviderError);
    CHECK(provider.spawn_count() == 0);
    CHECK(fixture.commands.empty());
    // Listed as its model, as every backend is.
    const auto listed = provider.list_models({});
    REQUIRE(listed.size() == 1);
    CHECK(listed.front().backend == "embed");
}

TEST_CASE(
    "an mlx embedder says the driver's failures in its words, and keeps the model when it can",
    "[mlx][embedding][backends]") {
    SECTION("a batch it cannot embed leaves the model loaded") {
        DriverState state = embedding_driver();
        state.embed_width = 0;
        state.replies = {
            R"({"type":"error","id":{id},"kind":"request","message":"RuntimeError: too long"})"
            "\n",
            R"({"type":"vectors","id":{id},"vectors":[[1,0,0,0]]})"
            "\n"};
        Fixture fixture{state};
        try {
            (void)fixture.provider->embed({"x"}, {});
            FAIL("the refused batch was answered");
        } catch (const apogee::harness::ProviderError& e) {
            CHECK(std::string{e.what()}.find(
                      "the MLX embedding driver could not embed: RuntimeError: too long") !=
                  std::string::npos);
        }
        CHECK(fixture.provider->has_live_child());
        CHECK(fixture.provider->embed({"x"}, {}).size() == 1);
        CHECK(fixture.provider->spawn_count() == 1);
    }
    SECTION("a vector short, or one of another width, is refused, never stored") {
        DriverState state = embedding_driver();
        state.embed_width = 0;
        state.replies = {R"({"type":"vectors","id":{id},"vectors":[[1,0,0,0]]})"
                         "\n",
                         R"({"type":"vectors","id":{id},"vectors":[[1,0,0,0],[1,0]]})"
                         "\n"};
        Fixture fixture{state};
        CHECK_THROWS_WITH(fixture.provider->embed({"a", "b"}, {}),
                          Catch::Matchers::ContainsSubstring("returned 1 vector(s) for 2 text(s)"));
        CHECK_THROWS_WITH(
            fixture.provider->embed({"a", "b"}, {}),
            Catch::Matchers::ContainsSubstring("a vector 2 wide among vectors 4 wide"));
    }
    SECTION("a driver that dies mid-batch is said with its last word, and the next call reloads") {
        DriverState state = embedding_driver();
        state.embed_width = 0;
        state.replies = {""};
        state.die_after_reply = true;
        state.stderr_text = "Traceback (most recent call last):\nMemoryError: out of memory\n";
        Fixture fixture{state};
        CHECK_THROWS_WITH(fixture.provider->embed({"x"}, {}),
                          Catch::Matchers::ContainsSubstring(
                              "the MLX embedding driver exited mid-batch: MemoryError: out of "
                              "memory"));
        CHECK_FALSE(fixture.provider->has_live_child());
        fixture.drivers.front()->embed_width = 4;
        fixture.drivers.front()->die_after_reply = false;
        CHECK(fixture.provider->embed({"x"}, {}).size() == 1);
        CHECK(fixture.provider->spawn_count() == 2);
    }
    SECTION("a load that fails is mlx-embeddings' own words") {
        DriverState state;
        state.startup =
            R"({"type":"error","id":null,"kind":"load","message":"could not load /m through mlx-embeddings: ValueError: Model type qwen9 not supported."})"
            "\n";
        state.die_after_startup = true;
        Fixture fixture{state};
        CHECK_THROWS_WITH(fixture.provider->embed({"x"}, {}),
                          Catch::Matchers::ContainsSubstring("Model type qwen9 not supported"));
        CHECK_FALSE(fixture.provider->has_live_child());
    }
    SECTION("a driver that is not the embedding one is refused at its ready line") {
        DriverState state = embedding_driver();
        state.startup = DriverState{}.startup;  // the generation driver's ready
        Fixture fixture{state};
        CHECK_THROWS_WITH(fixture.provider->embed({"x"}, {}),
                          Catch::Matchers::ContainsSubstring("did not load an embedding model"));
        CHECK_FALSE(fixture.provider->has_live_child());
    }
}

TEST_CASE("a cancelled embed settles the batch in flight, and a deaf driver is ended",
          "[mlx][embedding][backends][cancel]") {
    SECTION("a call cancelled before its slice: refused, the model kept") {
        Fixture fixture{embedding_driver()};
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        (void)fixture.provider->embed({"warm"}, {});
        token.cancel();
        CHECK_THROWS_AS(fixture.provider->embed(texts(3), token), apogee::harness::CancelledError);
        CHECK(fixture.provider->has_live_child());
        CHECK(fixture.provider->spawn_count() == 1);
    }
    SECTION("a batch never answered while the call is cancelled: the child ended") {
        DriverState state = embedding_driver();
        state.embed_width = 0;  // answers no batch
        Fixture fixture{state};
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        std::thread canceller{[&token] {
            std::this_thread::sleep_for(std::chrono::milliseconds{150});
            token.cancel();
        }};
        CHECK_THROWS_AS(fixture.provider->embed({"x"}, token), apogee::harness::CancelledError);
        canceller.join();
        CHECK_FALSE(fixture.provider->has_live_child());
        // A fresh one answers the next call.
        fixture.drivers.front()->embed_width = 4;
        CHECK(fixture.provider->embed({"x"}, {}).size() == 1);
        CHECK(fixture.provider->spawn_count() == 2);
    }
}

TEST_CASE("an embedding directory's ladder asks for mlx-embeddings and its own driver",
          "[mlx][embedding][backends][ladder]") {
    const apogee::testing::TempDir root{"mlx-embed-ladder-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path dir = root.path() / "models" / "embed";
    write_embedding_dir(dir);
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::Mlx;
    entry.model_path = dir.string();
    CHECK(apogee::backends::mlx_entry_embeds(entry));

    // Ready: and built as an embedder, from the ladder's own facts.
    const MlxHost host = fake_host(root.path() / "ready");
    CHECK(apogee::backends::probe_mlx_backend("embed", entry, host).ready());
    const auto provider = MlxEmbedProvider::from_config("embed", entry, host);
    CHECK(provider->embedding_dimensions() == 768);
    CHECK(provider->embedding_model_name() == "mlx:" + dir.string());
    CHECK(provider->spawn_count() == 0);
    const MlxEmbedProvider::Options options = MlxEmbedProvider::options_from("embed", entry, host);
    CHECK(options.driver == host.embedder);
    CHECK(options.interpreter == host.venv / "bin" / "python");

    // No mlx-embeddings: its own rung, its own fix -- never the chat driver's.
    const MlxHost bare = fake_host(root.path() / "bare", /*embeddings=*/false);
    const apogee::backends::MlxReadiness missing =
        apogee::backends::probe_mlx_backend("embed", entry, bare);
    CHECK(missing.refusal == MlxRefusal::NoMlxEmbeddings);
    CHECK(apogee::backends::to_string(missing.refusal) == "no mlx-embeddings");
    CHECK(missing.remedy == "apogee train setup --with mlx-embeddings");
    CHECK(missing.reason.find("is an embedding model mlx-embeddings built") != std::string::npos);
    CHECK_THROWS_WITH(MlxEmbedProvider::from_config("embed", entry, bare),
                      Catch::Matchers::ContainsSubstring("train setup --with mlx-embeddings"));
    CHECK(apogee::backends::find_mlx_embeddings(host).version == "0.1.0");

    // The embedding driver missing is the driver rung, naming that driver;
    // the chat one's absence is not this entry's problem.
    std::filesystem::remove(host.embedder);
    const apogee::backends::MlxReadiness undriven =
        apogee::backends::probe_mlx_backend("embed", entry, host);
    CHECK(undriven.refusal == MlxRefusal::NoDriver);
    CHECK(undriven.reason.find("mlx_embed.py") != std::string::npos);
    write_embedding_dir(dir);
    apogee::testing::write_fixture_file(host.embedder, "#\n");
    std::filesystem::remove(host.driver);
    CHECK(apogee::backends::probe_mlx_backend("embed", entry, host).ready());

    // A chat directory beside it is no embedder, and its ladder is as before.
    const std::filesystem::path talk = root.path() / "models" / "talk";
    apogee::testing::write_mlx_model(talk, {.kind = "chat"});
    apogee::harness::BackendConfig chat = entry;
    chat.model_path = talk.string();
    CHECK_FALSE(apogee::backends::mlx_entry_embeds(chat));
    CHECK(apogee::backends::probe_mlx_backend("talk", chat, host).refusal == MlxRefusal::NoDriver);
}
