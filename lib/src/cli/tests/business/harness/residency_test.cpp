#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "backends/llamacpp.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "contracts/provider.h"
#include "harness/harness.h"
#include "support/fake_llama.h"

/// A suite's residency in a session (27e): the in-use hold over each
/// provider's own idle clock, the warmup walk, and the loads a surface hears.
/// The clock is the provider's injected one, so "longer than its idle_unload"
/// is a number, never a sleep.
namespace {

using apogee::backends::LlamaCppProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::Config;
using apogee::harness::Harness;
using apogee::harness::SessionHold;
using apogee::harness::StatusEvent;
using apogee::testing::FakeLlamaRuntime;
using Clock = std::chrono::steady_clock;

/// A llamacpp provider over a scripted runtime, on a clock the test moves.
struct Local {
    FakeLlamaRuntime* runtime = nullptr;
    std::shared_ptr<LlamaCppProvider> provider;

    Local(const std::string& name, Clock::time_point& now,
          std::chrono::seconds idle = std::chrono::seconds{60}) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        runtime = owned.get();
        LlamaCppProvider::Options options;
        options.backend_name = name;
        options.model = name + "-model";
        options.model_path = "/models/" + name + ".gguf";
        options.idle_unload = idle;
        options.clock = [&now] { return now; };
        provider = std::make_shared<LlamaCppProvider>(std::move(options), std::move(owned));
    }
};

[[nodiscard]] ChatRequest ask(const std::string& backend) {
    ChatRequest request;
    request.model = backend;
    request.messages = {ChatMessage::user("hello")};
    return request;
}

/// research: root answers, helper does the chores; fast: root alone.
[[nodiscard]] Config suites_config() {
    return apogee::harness::parse_config(R"(models:
  default: root
  default_suite: research
backends:
  root:
    type: llamacpp
    model_path: /models/root.gguf
    idle_unload_seconds: 60
  helper:
    type: llamacpp
    model_path: /models/helper.gguf
    idle_unload_seconds: 60
  outsider:
    type: llamacpp
    model_path: /models/outsider.gguf
    idle_unload_seconds: 60
suites:
  research:
    members:
      chat: root
      utility: helper
  fast:
    members:
      chat: root
)",
                                         "residency_test");
}

/// A provider that reports a status but holds no model of its own: a vendor
/// CLI's shape -- it says "ready", and there is nothing of it in memory here.
class StatusOnly final : public apogee::harness::LLMProvider,
                         public apogee::harness::StatusReporting {
public:
    int preloads = 0;

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "vendor";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const ChatRequest& /*request*/,
        const apogee::harness::CancellationToken& /*cancellation*/) override {
        return {};
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const ChatRequest& /*request*/,
        const apogee::harness::StreamOptions& /*options*/) override {
        return {};
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken& /*cancellation*/) override {
        return {};
    }

    [[nodiscard]] StatusEvent model_status() const override {
        StatusEvent event;
        event.type = StatusEvent::Type::ModelReady;
        return event;
    }

    void preload(const apogee::harness::StatusSink& /*on_status*/) override {
        ++preloads;
    }
};

[[nodiscard]] std::shared_ptr<apogee::backends::MockProvider> cloud(const std::string& name) {
    apogee::backends::MockProvider::Options options;
    options.backend_name = name;
    return std::make_shared<apogee::backends::MockProvider>(std::move(options));
}

}  // namespace

TEST_CASE("a suite session's member outlasts its idle window between turns, and not after",
          "[harness][residency]") {
    Clock::time_point now = Clock::now();
    const Config config = suites_config();
    Harness harness{config};
    const Local helper{"helper", now};
    const Local outsider{"outsider", now};
    harness.register_provider("helper", helper.provider);
    harness.register_provider("outsider", outsider.provider);
    harness.use_default_router();

    {
        const SessionHold hold{harness};
        CHECK(helper.provider->held_resident());
        CHECK_FALSE(outsider.provider->held_resident());

        (void)harness.chat(ask("helper"));
        (void)harness.chat(ask("outsider"));
        REQUIRE(helper.runtime->loads == 1);

        // Two turns further apart than the idle window: the member is still
        // resident, the backend outside the suite paid its load again.
        now += std::chrono::seconds{120};
        (void)harness.chat(ask("helper"));
        (void)harness.chat(ask("outsider"));
        CHECK(helper.runtime->loads == 1);
        CHECK(outsider.runtime->loads == 2);
    }

    // The session is over: the hold went with it, and the clock rules again
    // from the last use -- the next request past the window unloads first.
    CHECK_FALSE(helper.provider->held_resident());
    CHECK(harness.held().empty());
    now += std::chrono::seconds{120};
    (void)harness.chat(ask("helper"));
    CHECK(helper.runtime->loads == 2);
}

TEST_CASE("the hold follows the suite, and a rebuilt member keeps it", "[harness][residency]") {
    Clock::time_point now = Clock::now();
    Harness harness{suites_config()};
    const Local root{"root", now};
    const Local helper{"helper", now};
    harness.register_provider("root", root.provider);
    harness.register_provider("helper", helper.provider);
    harness.use_default_router();

    // No session: a suite is active, and nothing is held.
    CHECK(harness.held().empty());
    harness.set_active_suite("fast");
    CHECK(harness.held().empty());
    harness.set_active_suite("research");

    const SessionHold hold{harness};
    CHECK(harness.held() == std::vector<std::string>{"helper", "root"});

    // A member pinned to a new window is rebuilt: the new provider is held as
    // it is registered.
    const Local rebuilt{"helper", now};
    harness.register_provider("helper", rebuilt.provider);
    CHECK(rebuilt.provider->held_resident());

    // `/suite fast` (activate_suite sets the harness's view): helper left the
    // set and is let go; root stays held.
    harness.set_active_suite("fast");
    CHECK(harness.held() == std::vector<std::string>{"root"});
    CHECK_FALSE(rebuilt.provider->held_resident());
    CHECK(root.provider->held_resident());

    // `/suite off`: nothing is held.
    harness.set_active_suite("");
    CHECK(harness.held().empty());
    CHECK_FALSE(root.provider->held_resident());
}

TEST_CASE("a member is held by name as the config spells it", "[harness][residency]") {
    Clock::time_point now = Clock::now();
    const Config config = apogee::harness::parse_config(R"(models:
  default: Helper
  default_suite: s
backends:
  Helper:
    type: llamacpp
    model_path: /models/helper.gguf
suites:
  s:
    members:
      utility: HELPER
)",
                                                        "residency_test");
    Harness harness{config};
    const Local helper{"Helper", now};
    harness.register_provider("Helper", helper.provider);
    const SessionHold hold{harness};
    CHECK(helper.provider->held_resident());
}

TEST_CASE("warmup loads what is not resident, numbered, and skips what holds nothing",
          "[harness][residency]") {
    Clock::time_point now = Clock::now();
    Harness harness{Config{}};
    const Local cold{"cold", now};
    const Local warm{"warm", now};
    const Local broken{"broken", now};
    broken.runtime->load_error = "could not load the model at '/models/broken.gguf'";
    harness.register_provider("cold", cold.provider);
    harness.register_provider("warm", warm.provider);
    harness.register_provider("broken", broken.provider);
    harness.register_provider("cloud", cloud("cloud"));
    const auto vendor = std::make_shared<StatusOnly>();
    harness.register_provider("vendor", vendor);
    harness.use_default_router();
    warm.provider->preload({});

    CHECK(harness.resident("cold") == false);
    CHECK(harness.resident("warm") == true);
    // A model this process does not hold has no residency to report.
    CHECK_FALSE(harness.resident("cloud").has_value());
    CHECK_FALSE(harness.resident("nowhere").has_value());
    // One that says "ready" with nothing of its own here is not resident.
    CHECK_FALSE(harness.resident("vendor").has_value());

    std::vector<std::tuple<std::string, std::size_t, std::size_t>> said;
    const apogee::harness::WarmResult result =
        harness.warm({"cold", "warm", "cloud", "vendor", "broken"},
                     [&said](std::string_view backend, std::size_t done, std::size_t total) {
                         said.emplace_back(std::string{backend}, done, total);
                     });
    using Step = std::tuple<std::string, std::size_t, std::size_t>;
    CHECK(said == std::vector<Step>{Step{"cold", 1, 2}, Step{"broken", 2, 2}});
    CHECK(result.loaded == std::vector<std::string>{"cold"});
    CHECK(result.resident == std::vector<std::string>{"warm"});
    REQUIRE(result.failed.size() == 1);
    CHECK(result.failed.front().first == "broken");
    CHECK(result.failed.front().second.find("/models/broken.gguf") != std::string::npos);
    CHECK(harness.resident("cold") == true);
    CHECK(cold.runtime->loads == 1);
    CHECK(vendor->preloads == 0);

    // A warmup is not a use: the idle clock has not started, so a request
    // long after it still finds the model loaded.
    now += std::chrono::seconds{600};
    (void)harness.chat(ask("cold"));
    CHECK(cold.runtime->loads == 1);
}

TEST_CASE("every load is heard with its backend's name, whichever request made it",
          "[harness][residency]") {
    Clock::time_point now = Clock::now();
    Harness harness{Config{}};
    const Local embedder{"embedder", now};
    harness.register_provider("embedder", embedder.provider);
    harness.use_default_router();

    std::vector<std::pair<std::string, StatusEvent>> heard;
    harness.listen_for_loads([&heard](std::string_view backend, const StatusEvent& event) {
        heard.emplace_back(std::string{backend}, event);
    });
    // Registered after the listener: heard too.
    const Local helper{"helper", now};
    harness.register_provider("helper", helper.provider);
    harness.use_default_router();

    // An embedding streams no status of its own: before 27e this load was
    // silent.
    apogee::harness::EmbeddingCapable* embed = harness.embedder_for("embedder");
    REQUIRE(embed != nullptr);
    (void)embed->embed({"a question"}, {});
    (void)harness.chat(ask("helper"));
    (void)harness.chat(ask("helper"));  // loaded: nothing to hear

    REQUIRE(heard.size() == 4);
    CHECK(heard[0].first == "embedder");
    CHECK(heard[0].second.type == StatusEvent::Type::ModelLoading);
    CHECK(heard[0].second.phase == StatusEvent::Phase::Start);
    CHECK(heard[1].first == "embedder");
    CHECK(heard[1].second.type == StatusEvent::Type::ModelReady);
    CHECK(heard[2].first == "helper");
    CHECK(heard[2].second.phase == StatusEvent::Phase::Start);
    CHECK(heard[3].second.type == StatusEvent::Type::ModelReady);

    // A failed load is heard as one.
    const Local broken{"broken", now};
    broken.runtime->load_error = "no such file";
    harness.register_provider("broken", broken.provider);
    harness.use_default_router();
    CHECK_THROWS((void)harness.chat(ask("broken")));
    REQUIRE(heard.size() == 6);
    CHECK(heard[5].first == "broken");
    CHECK(heard[5].second.phase == StatusEvent::Phase::Error);
    CHECK(heard[5].second.detail == "no such file");
}
