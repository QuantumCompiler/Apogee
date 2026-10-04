#include "agentloop/recall.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "agentloop/rag.h"
#include "backends/mock.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "harness/harness.h"

/// Recall across chats (26l): what a summariser is asked, the index of
/// summaries, and the summaries found again through the one retrieval path.
namespace {

namespace fs = std::filesystem;
using apogee::agentloop::RecallEntry;
using apogee::agentloop::RecallIndex;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;

struct Scratch {
    fs::path root =
        fs::temp_directory_path() / ("apogee-recall-" + std::to_string(std::random_device{}()));
    fs::path index = root / "memory" / "chats.db";

    Scratch() {
        fs::create_directories(root);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        fs::remove_all(root, code);
    }
};

/// A harness whose `summariser` answers `reply`.
struct Summariser {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    std::vector<ChatRequest> asked;

    explicit Summariser(std::string reply) {
        apogee::backends::MockProvider::Options options;
        options.backend_name = "summariser";
        options.turns = {apogee::backends::MockTurn{.text = std::move(reply)}};
        options.on_request = [this](const ChatRequest& request) { asked.push_back(request); };
        harness.register_provider("summariser",
                                  std::make_shared<apogee::backends::MockProvider>(options));
        harness.use_default_router();
    }
};

apogee::agentloop::Embedder toy_embedder(bool metered) {
    apogee::agentloop::Embedder embedder;
    embedder.backend = "toy";
    embedder.model = "toy-embed";
    embedder.dimensions = 2;
    embedder.metered = metered;
    embedder.embed = [](const std::vector<std::string>& texts,
                        const apogee::harness::CancellationToken&) {
        std::vector<std::vector<float>> out;
        for (const std::string& text : texts) {
            out.push_back(text.find("Postgres") != std::string::npos
                              ? std::vector<float>{1.0F, 0.0F}
                              : std::vector<float>{0.0F, 1.0F});
        }
        return out;
    };
    return embedder;
}

[[nodiscard]] bool owner_only(const fs::path& path) {
    return (fs::status(path).permissions() & (fs::perms::group_all | fs::perms::others_all)) ==
           fs::perms::none;
}

}  // namespace

TEST_CASE("a summariser is asked for what a later chat would want, with nothing to reason about",
          "[recall]") {
    std::vector<ChatMessage> messages = {
        ChatMessage::system("be brief"),
        ChatMessage::user("Our project uses Postgres 16."),
        ChatMessage::assistant("Noted."),
        ChatMessage::user(std::string(3000, 'x')),
    };
    const ChatRequest request = apogee::agentloop::recall_summary_request("summariser", messages);
    CHECK(request.model == "summariser");
    CHECK(request.transient.side_request);
    CHECK(request.thinking.off());
    CHECK(request.temperature == 0.0);
    REQUIRE(request.messages.size() == 2);
    CHECK(request.messages.front().content.plain_text().find("what was decided") !=
          std::string::npos);
    const std::string transcript = request.messages.back().content.plain_text();
    CHECK(transcript.find("User: Our project uses Postgres 16.") != std::string::npos);
    CHECK(transcript.find("Assistant: Noted.") != std::string::npos);
    // The system prompt is not the conversation, and a long message is cut.
    CHECK(transcript.find("be brief") == std::string::npos);
    CHECK(transcript.size() < 2000);

    // Past the whole budget, the middle goes: the opening and the end stay.
    std::vector<ChatMessage> long_chat;
    for (int index = 0; index < 60; ++index) {
        long_chat.push_back(
            ChatMessage::user("question " + std::to_string(index) + " " + std::string(1000, 'q')));
    }
    const std::string kept = apogee::agentloop::recall_summary_request("summariser", long_chat)
                                 .messages.back()
                                 .content.plain_text();
    CHECK(kept.size() <= 24 * 1024);
    CHECK(kept.find("question 0 ") != std::string::npos);
    CHECK(kept.find("question 59 ") != std::string::npos);
    CHECK(kept.find("question 30 ") == std::string::npos);
}

TEST_CASE("a summary comes back trimmed, and a blank or failed one says why", "[recall]") {
    const std::vector<ChatMessage> messages = {ChatMessage::user("a"), ChatMessage::assistant("b")};
    std::string note;
    Summariser good{"  The project uses Postgres 16.\n"};
    CHECK(apogee::agentloop::summarise_chat(good.harness, "summariser", messages, {}, note) ==
          "The project uses Postgres 16.");
    REQUIRE(good.asked.size() == 1);
    CHECK(good.asked.front().transient.side_request);

    Summariser blank{"  \n"};
    CHECK_FALSE(apogee::agentloop::summarise_chat(blank.harness, "summariser", messages, {}, note));
    CHECK(note == "the summary came back empty");
    CHECK_FALSE(apogee::agentloop::summarise_chat(blank.harness, "nowhere", messages, {}, note));
    CHECK(note.find("the summary failed") != std::string::npos);
}

TEST_CASE("a summary is indexed dated and titled, and said as counts", "[recall]") {
    CHECK(apogee::agentloop::recall_chunk({.chat_id = "c",
                                           .title = "Database Plans",
                                           .updated_at = "2026-10-03T12:00:00Z",
                                           .summary = "Uses Postgres 16."}) ==
          "An earlier conversation (2026-10-03), \"Database Plans\": Uses Postgres 16.");
    CHECK(apogee::agentloop::recall_chunk({.chat_id = "c", .summary = "Plain."}) ==
          "An earlier conversation: Plain.");
    CHECK(apogee::agentloop::describe_recall(2, 1) == "2 past chats, 1 decision");
    CHECK(apogee::agentloop::describe_recall(1, 0) == "1 past chat");
    CHECK(apogee::agentloop::describe_recall(0, 3) == "3 decisions");
}

TEST_CASE("the index holds one summary a chat, privately, and forgets one on request", "[recall]") {
    const Scratch scratch;
    RecallIndex index{scratch.index, toy_embedder(false)};
    std::string note;
    REQUIRE(index.put(
        {.chat_id = "chat-a", .updated_at = "2026-10-01T00:00:00Z", .summary = "Uses Postgres 16."},
        {}, note));
    REQUIRE(index.put(
        {.chat_id = "chat-b", .updated_at = "2026-10-02T00:00:00Z", .summary = "Prefers tabs."}, {},
        note));
    // A chat summarised again replaces what it had.
    REQUIRE(index.put({.chat_id = "chat-a",
                       .updated_at = "2026-10-03T00:00:00Z",
                       .summary = "Uses Postgres 17 now."},
                      {}, note));
    CHECK(index.chats() == 2);
    CHECK(index.summarised_at("chat-a") == "2026-10-03T00:00:00Z");
    CHECK(index.summarised_at("missing").empty());
    if (apogee::harness::supports_private_modes()) {
        CHECK(owner_only(scratch.index));
        CHECK(owner_only(scratch.index.parent_path()));
    }
    // With vectors from an embedder that costs nothing.
    CHECK(apogee::embedstore::Store{scratch.index}.embedding_model().recorded());

    CHECK(index.remove("chat-a"));
    CHECK_FALSE(index.remove("chat-a"));
    CHECK(index.chats() == 1);
}

TEST_CASE("a summary is never embedded by a billed embedder", "[recall]") {
    const Scratch scratch;
    RecallIndex index{scratch.index, toy_embedder(true)};
    std::string note;
    REQUIRE(index.put({.chat_id = "chat-a", .summary = "Uses Postgres 16."}, {}, note));
    CHECK_FALSE(apogee::embedstore::Store{scratch.index}.embedding_model().recorded());
}

TEST_CASE("past chats are found through the one retrieval path, never this chat's own",
          "[recall]") {
    const Scratch scratch;
    RecallIndex index{scratch.index, std::nullopt};
    std::string note;
    REQUIRE(
        index.put({.chat_id = "earlier", .summary = "The project uses Postgres 16."}, {}, note));
    REQUIRE(index.put({.chat_id = "now", .summary = "The project uses Postgres 15."}, {}, note));
    REQUIRE(index.put({.chat_id = "other", .summary = "Prefers tabs over spaces."}, {}, note));

    apogee::agentloop::RagTurn turn;
    turn.store_path = scratch.index;
    turn.question = "which Postgres version?";
    turn.limit = apogee::agentloop::kRecallItems;
    turn.rerank_flag = "off";
    turn.exclude_sources = {"now"};
    turn.header = std::string{apogee::agentloop::kRecallHeader};
    const apogee::agentloop::RagResult result = apogee::agentloop::retrieve_for_turn(turn);
    REQUIRE(result.error.empty());
    REQUIRE(result.prefix.size() == 1);
    const std::string injected = result.prefix.front().content.plain_text();
    CHECK(injected.starts_with(std::string{apogee::agentloop::kRecallHeader}));
    CHECK(injected.find("Postgres 16") != std::string::npos);
    CHECK(injected.find("Postgres 15") == std::string::npos);
    CHECK(injected.find("user's documents") == std::string::npos);
    CHECK(result.chunks >= 1);
}
