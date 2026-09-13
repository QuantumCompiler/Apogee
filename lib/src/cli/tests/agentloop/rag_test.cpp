#include "agentloop/rag.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "agentloop/loop.h"
#include "backends/mock.h"
#include "commands/helpers.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/harness.h"

/// RAG injection, and the one property that makes it safe to use every turn:
/// **retrieved context reaches the outgoing request and nothing else.**
///
/// If it reached persisted history, two things would go wrong and both get
/// worse over time. The transcript would fill with injected documents and grow
/// without bound; and turn one's chunks would still be sitting there on turn
/// five, competing for the model's attention with the chunks that actually
/// answer the new question.
namespace {

using apogee::agentloop::build_rag_prefix;
using apogee::agentloop::RagResult;
using apogee::agentloop::render_rag_context;
using apogee::embedstore::Store;

struct Scratch {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("apogee-rag-" + std::to_string(counter()));

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

TEST_CASE("retrieval finds a chunk and reports its retriever", "[agentloop][rag]") {
    const Scratch scratch;
    seed(scratch);
    const RagResult result = build_rag_prefix(scratch.db(), "zarquon protocol widgets", 4);

    CHECK(result.error.empty());
    CHECK(result.chunks > 0);
    CHECK(result.top_score > 0.0);
    // Always named. Lexical and vector scores are incomparable, and a number
    // without its retriever invites exactly that comparison.
    CHECK(result.retriever == "lexical");
    REQUIRE(result.prefix.size() == 1);
    CHECK(result.prefix.front().content.plain_text().find(kSecret) != std::string::npos);
}

TEST_CASE("injected context never reaches persisted history", "[agentloop][rag][transient]") {
    // THE acceptance criterion, asserted as a grep: the distinctive sentence
    // must appear in what the model was sent and NOT in the history the
    // session would save.
    const Scratch scratch;
    seed(scratch);
    const RagResult rag = build_rag_prefix(scratch.db(), "zarquon protocol widgets", 4);
    REQUIRE(rag.chunks > 0);

    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = "answered"}};

    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("what does the protocol require?")};

    apogee::agentloop::Options loop;
    loop.model = "mock";
    loop.transient_prefix = rag.prefix;
    loop.stream_answer = false;

    apogee::agentloop::NullReporter reporter;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(harness, history, loop, reporter);

    CHECK(result.answer == "answered");

    // The grep. Every message the session would persist, concatenated.
    std::string persisted;
    for (const apogee::harness::ChatMessage& message : history) {
        persisted += message.content.plain_text();
        persisted += "\n";
    }
    INFO("persisted history:\n" << persisted);
    CHECK(persisted.find(kSecret) == std::string::npos);
    // ...while the question and the answer are both there, so this is not
    // passing because history is simply empty.
    CHECK(persisted.find("what does the protocol require?") != std::string::npos);
    CHECK(persisted.find("answered") != std::string::npos);
}

TEST_CASE("a missing collection is reported without failing the turn", "[agentloop][rag]") {
    // Answering without retrieved context is much better than refusing to
    // answer because a collection was not there.
    const RagResult result = build_rag_prefix("/no/such/collection.db", "anything", 4);

    CHECK_FALSE(result.error.empty());
    CHECK(result.prefix.empty());
    CHECK(result.chunks == 0);
}

TEST_CASE("a corpus with nothing relevant is a normal empty result", "[agentloop][rag]") {
    // Not an error. A collection that has nothing to say about this question is
    // not a fault, and must not be reported as one.
    const Scratch scratch;
    seed(scratch);
    const RagResult result = build_rag_prefix(scratch.db(), "xylophone bicycle", 4);

    CHECK(result.error.empty());
    CHECK(result.chunks == 0);
    CHECK(result.prefix.empty());
}

TEST_CASE("the limit caps how much is injected", "[agentloop][rag]") {
    Scratch scratch;
    {
        Store store{scratch.db()};
        store.replace_source("doc",
                             {"alpha one", "alpha two", "alpha three", "alpha four", "alpha five"});
    }
    const RagResult result = build_rag_prefix(scratch.db(), "alpha", 2);
    CHECK(result.chunks == 2);
}

TEST_CASE("injected context is framed as retrieved excerpts", "[agentloop][rag]") {
    // Without the framing a model reads an injected document as something the
    // user said, and answers about the document rather than the question --
    // and the more relevant the retrieval, the more confidently it does so.
    const std::string rendered = render_rag_context({"first chunk", "second chunk"});

    CHECK(rendered.find("retrieved") != std::string::npos);
    CHECK(rendered.find("first chunk") != std::string::npos);
    CHECK(rendered.find("second chunk") != std::string::npos);
    // Each excerpt is delimited, so the model can tell where one ends.
    CHECK(rendered.find("excerpt 1") != std::string::npos);
    CHECK(rendered.find("excerpt 2") != std::string::npos);
}

TEST_CASE("rendering nothing yields nothing", "[agentloop][rag]") {
    CHECK(render_rag_context({}).empty());
}

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
