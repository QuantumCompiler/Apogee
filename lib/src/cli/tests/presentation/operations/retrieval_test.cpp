#include "operations/retrieval.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "agentloop/loop.h"
#include "agentloop/rag.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "embedstore/store.h"
#include "harness/harness.h"

/// The surfaces' retrieval choice, carried through the loop: `auto_rag` with
/// no flag picks the collection, and what it injects rides the request and
/// never the history. Moved from `agentloop/rag_test` (A4): the choice is the
/// surfaces' (Presentation) and the transient path the loop's (Business), so
/// the test of both lives in the higher layer (ADR 0004).
namespace {

using apogee::agentloop::build_rag_prefix;
using apogee::agentloop::RagResult;
using apogee::embedstore::Store;

struct Scratch {
    // Named by a random draw, not a per-process counter: ctest runs these
    // cases as parallel PROCESSES, and a counter produced the same path in
    // several of them at once -- each deleting the others' directories.
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("apogee-rag-" + std::to_string(std::random_device{}()) + "-" + std::to_string(counter()));

    Scratch() {
        std::error_code code;
        std::filesystem::create_directories(dir, code);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        std::filesystem::remove_all(dir, code);
    }

    [[nodiscard]] std::filesystem::path db() const {
        return dir / "collection.db";
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

/// A collection holding one very distinctive sentence, so a grep for it can
/// only match injected context.
constexpr std::string_view kSecret = "the zarquon protocol requires seventeen widgets";

/// Seeds `scratch` in place. `Scratch` is deliberately neither copyable nor
/// movable -- it owns a directory it deletes -- so this fills one rather than
/// returning a new one.
void seed(const Scratch& scratch) {
    Store store{scratch.db()};
    store.replace_source("notes.md", {std::string{kSecret}, "an unrelated second chunk"});
}

}  // namespace

TEST_CASE("context injected by auto_rag reaches the request and never persisted history",
          "[agentloop][rag][transient][config]") {
    // The transient-history rule, extended to the CONFIG path. Injection that
    // nobody typed a flag for is held to exactly the rule the flag is: it rides
    // the outgoing request, and the saved transcript never learns it happened.
    //
    // Two assertions, and the first is what keeps the second honest: the
    // secret MUST appear in what the provider was sent, so this cannot pass by
    // simply not injecting anything.
    const Scratch scratch;
    seed(scratch);

    apogee::harness::Config config;
    config.auto_rag = "notes";  // the key, with no --rag flag anywhere
    const apogee::commands::RagChoice choice =
        apogee::commands::choose_rag_collection(false, {}, config.auto_rag);
    REQUIRE(choice.source == apogee::commands::RagSource::Config);
    REQUIRE(choice.collection == "notes");

    const RagResult rag = build_rag_prefix(scratch.db(), "zarquon protocol widgets", 4);
    REQUIRE(rag.chunks > 0);

    bool secret_reached_the_model = false;
    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = "answered"}};
    options.on_request = [&secret_reached_the_model](const apogee::harness::ChatRequest& request) {
        for (const apogee::harness::ChatMessage& message : request.messages) {
            if (message.content.plain_text().find(kSecret) != std::string::npos) {
                secret_reached_the_model = true;
            }
        }
    };

    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("what does the protocol require?")};

    apogee::agentloop::Options loop;
    loop.model = "mock";
    loop.transient_prefix = rag.prefix;  // the same seam the flag path uses
    loop.stream_answer = false;

    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(harness, history, loop, reporter);

    CHECK(secret_reached_the_model);

    std::string persisted;
    for (const apogee::harness::ChatMessage& message : history) {
        persisted += message.content.plain_text();
        persisted += "\n";
    }
    INFO("persisted history:\n" << persisted);
    CHECK(persisted.find(kSecret) == std::string::npos);
    CHECK(persisted.find("what does the protocol require?") != std::string::npos);
}
