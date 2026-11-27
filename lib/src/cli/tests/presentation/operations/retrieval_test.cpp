#include "operations/retrieval.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
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
#include "operations/collections.h"
#include "support/env_guard.h"

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
    // The same seam the flag path uses.
    loop.transient_prefix =
        apogee::agentloop::injected_as(rag.prefix, apogee::agentloop::InjectedSource::Retrieval);
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

namespace {

[[nodiscard]] std::string bytes_of(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("a collection named by a path is refused before any file is opened",
          "[operations][rag][collection_name][M45]") {
    // CLI-M45: `--rag <abs>/foreign` once opened any SQLite file on the
    // machine and wrote the store's whole schema into it. The one reader
    // behind --rag, auto_rag, an agent's collection and serve --rag now
    // refuses the name, in embed's words, and opens nothing.
    const apogee::testing::TempDir root{"m45-retrieval-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard home{"APOGEE_HOME", (root.path() / "home").string()};
    std::filesystem::create_directories(root.path() / "home" / "embeddings");
    const std::filesystem::path foreign = root.path() / "foreign.db";
    {
        Store store{foreign};
        store.replace_source("a.md", {"the fox"});
    }
    const std::string before = bytes_of(foreign);
    REQUIRE_FALSE(before.empty());

    const apogee::harness::Harness harness{apogee::harness::Config{}};
    const apogee::harness::Config config;
    for (const std::string& name :
         {(root.path() / "foreign").string(), std::string{"../../foreign"}, std::string{"a/b"},
          std::string{".."}}) {
        INFO(name);
        const RagResult result =
            apogee::commands::retrieve_for_collection(harness, config, name, "fox", 4, {}, {}, {});
        CHECK(result.refused);
        CHECK(result.error == "'" + name + "' is not a plain collection name");
        CHECK(result.chunks == 0);
        CHECK(result.prefix.empty());
    }
    CHECK(bytes_of(foreign) == before);
    CHECK_FALSE(std::filesystem::exists(root.path() / "foreign.db-wal"));
    CHECK_FALSE(std::filesystem::exists(root.path() / "foreign.db-shm"));
    CHECK_FALSE(std::filesystem::exists(root.path() / "home" / "embeddings" / "a"));

    // A plain name that names nothing is still a reason to tell, never a
    // refusal: answering without context beats refusing to answer.
    const RagResult missing =
        apogee::commands::retrieve_for_collection(harness, config, "absent", "fox", 4, {}, {}, {});
    CHECK_FALSE(missing.refused);
    CHECK_FALSE(missing.error.empty());

    // Every store under the folder reads, whatever its name.
    for (const std::string name : {"my notes", "a..b", "a@b"}) {
        INFO(name);
        {
            Store store{apogee::commands::collection_path(name)};
            store.replace_source("a.md", {"the quick fox"});
        }
        const RagResult found =
            apogee::commands::retrieve_for_collection(harness, config, name, "fox", 4, {}, {}, {});
        CHECK_FALSE(found.refused);
        CHECK(found.error.empty());
        CHECK(found.chunks == 1);
    }
#ifndef _WIN32
    // The longest name a POSIX folder holds beside its `.db` and `-wal`:
    // no length is in the read rule.
    const std::string longest(244, 'n');
    {
        Store store{apogee::commands::collection_path(longest)};
        store.replace_source("a.md", {"the quick fox"});
    }
    CHECK(apogee::commands::retrieve_for_collection(harness, config, longest, "fox", 4, {}, {}, {})
              .chunks == 1);
#endif
    // Nor is case: where the filesystem folds it, `Notes` reaches notes.db
    // exactly as before.
    {
        Store store{apogee::commands::collection_path("notes")};
        store.replace_source("a.md", {"the quick fox"});
    }
    if (std::filesystem::exists(apogee::commands::collection_path("NOTES"))) {
        const RagResult folded = apogee::commands::retrieve_for_collection(harness, config, "Notes",
                                                                           "fox", 4, {}, {}, {});
        CHECK_FALSE(folded.refused);
        CHECK(folded.chunks == 1);
    }
}

TEST_CASE("a refused config value says which key holds it; a refused flag says only the words",
          "[operations][rag][collection_name][M45]") {
    // CLI-M45: an older config's pathed auto_rag or agent collection is a
    // value nobody typed this run, so its refusal names the key and the
    // doctor; the words themselves are the read rule's on every surface.
    using apogee::commands::choose_rag_collection;
    using apogee::commands::rag_choice_refusal;
    CHECK(rag_choice_refusal(choose_rag_collection(true, "../x", "notes")) ==
          "'../x' is not a plain collection name");
    CHECK(rag_choice_refusal(choose_rag_collection(false, {}, "../x")) ==
          "'../x' is not a plain collection name -- from auto_rag in the config; 'apogee "
          "check' names the fix");
    CHECK(rag_choice_refusal(choose_rag_collection(false, {}, "/x"), "agents.h.collection") ==
          "'/x' is not a plain collection name -- from agents.h.collection in the config; "
          "'apogee check' names the fix");
    CHECK(rag_choice_refusal(choose_rag_collection(false, {}, "my notes")).empty());
    CHECK(rag_choice_refusal(choose_rag_collection(true, "", "../x")).empty());  // --rag ""
    CHECK(rag_choice_refusal(choose_rag_collection(false, {}, {})).empty());
}
