#include "cli/chat_attachments.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/media.h"
#include "backends/mlx_local.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/fake_mlx_driver.h"
#include "support/media_fakes.h"

using apogee::commands::ChatAttachments;
using apogee::commands::existing_mention;
using apogee::commands::mentioned_paths;

namespace {

/// An embedding provider that counts the texts it embeds.
class CountingEmbeddings final : public apogee::harness::LLMProvider,
                                 public apogee::harness::EmbeddingCapable {
public:
    std::atomic<int> texts{0};
    bool metered = false;

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "embedder";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const apogee::harness::ChatRequest&, const apogee::harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const apogee::harness::ChatRequest&, const apogee::harness::StreamOptions&) override {
        return {};
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs,
        const apogee::harness::CancellationToken&) override {
        texts += static_cast<int>(inputs.size());
        std::vector<std::vector<float>> out;
        for (const std::string& input : inputs) {
            out.push_back({static_cast<float>(input.size() % 97), 1.0F, 0.25F});
        }
        return out;
    }

    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override {
        return 3;
    }

    [[nodiscard]] std::string embedding_model_name() const override {
        return "count-embed";
    }

    [[nodiscard]] bool embedding_is_metered() const noexcept override {
        return metered;
    }
};

/// A home, a harness with a chat mock on an 8,000-token window and a counting
/// embedder as the embedding role, and a folder of files to attach.
struct Fixture {
    apogee::testing::TempDir home{"chat-attach-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path work = home.path() / "work";
    std::shared_ptr<CountingEmbeddings> embeddings = std::make_shared<CountingEmbeddings>();
    std::unique_ptr<apogee::harness::Harness> harness;
    apogee::logger::Session session;
    std::vector<std::string> said;

    explicit Fixture(bool embedding_role = true) {
        std::filesystem::create_directories(work);
        const std::string models = embedding_role ? "models:\n  default_embedding: embedder\n" : "";
        const apogee::harness::Config config = apogee::harness::parse_config(
            models +
                "backends:\n  chat:\n    type: mock\n    context_size: 8000\n  embedder:\n"
                "    type: mock\n",
            "<test>");
        harness = std::make_unique<apogee::harness::Harness>(config);
        harness->register_provider("chat", std::make_shared<apogee::backends::MockProvider>(
                                               apogee::backends::MockProvider::Options{}));
        harness->register_provider("embedder", embeddings);
        harness->use_default_router();
        session.chat_id = "chat-1";
        session.backend = "chat";
    }

    [[nodiscard]] ChatAttachments::Hooks hooks() {
        return ChatAttachments::Hooks{
            .say = [this](const std::string& line, bool) { said.push_back(line); },
            .progress = {},
            .confirm_large = {},
            .save = false};
    }

    void write(const std::string& name, const std::string& text) const {
        const std::filesystem::path path = work / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;
    }

    [[nodiscard]] bool heard(std::string_view needle) const {
        return std::ranges::any_of(
            said, [&](const std::string& line) { return line.find(needle) != std::string::npos; });
    }

    [[nodiscard]] apogee::agentloop::TurnBudget budget() const {
        return apogee::agentloop::turn_budget(*harness, "chat", std::nullopt);
    }
};

/// A document far past an 8,000-token window's attachment share.
std::string long_document() {
    std::string text;
    for (int line = 1; line <= 400; ++line) {
        text += "section " + std::to_string(line) + ": the reactor coolant runs at temperature " +
                std::to_string(300 + line) + " kelvin\n";
    }
    return text;
}

}  // namespace

TEST_CASE("a small file rides the next message whole; a big one is retrieved each turn",
          "[commands][attachments]") {
    Fixture fixture;
    fixture.write("notes.md", "The launch code is 7731.\n");
    fixture.write("manual.txt", long_document());
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};

    REQUIRE(attached.attach("notes.md", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached notes.md: 1 file, 1 chunk -- inlined whole"));
    CHECK_FALSE(attached.retrieves());

    // The next message is where it rides; nothing needs searching.
    ChatAttachments::Turn turn = attached.for_turn(2, "what is the code?", fixture.budget(), 4, {});
    REQUIRE(turn.inlined.size() == 1);
    CHECK(turn.inlined.front().message == 2);
    CHECK(turn.inlined.front().text.find("The launch code is 7731.") != std::string::npos);
    CHECK_FALSE(turn.retrieved.has_value());
    CHECK(fixture.session.attachments.front().inline_at == std::optional<std::size_t>{2});

    REQUIRE(attached.attach("manual.txt", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached manual.txt: 1 file"));
    CHECK(fixture.heard("-- its excerpts are retrieved each turn"));
    CHECK(attached.retrieves());
    turn = attached.for_turn(4, "coolant temperature section 250", fixture.budget(), 4, {});
    CHECK(turn.inlined.size() == 1);  // notes.md still rides message 2
    REQUIRE(turn.retrieved.has_value());
    REQUIRE(turn.retrieved->prefix.size() == 1);
    const std::string sent = turn.retrieved->prefix.front().content.plain_text();
    CHECK(sent.find("--- manual.txt:") != std::string::npos);
    CHECK(sent.find("7731") == std::string::npos);  // the inlined file is not searched
    // Words and meaning together: the index has vectors.
    CHECK(turn.retrieved->retriever == "hybrid");

    // Even a question that names it never searches the inlined file.
    turn = attached.for_turn(4, "launch code 7731 coolant", fixture.budget(), 4, {});
    REQUIRE(turn.retrieved.has_value());
    REQUIRE_FALSE(turn.retrieved->prefix.empty());
    CHECK(turn.retrieved->prefix.front().content.plain_text().find("7731") == std::string::npos);

    // A question naming code is searched by its words, for the names alone.
    turn = attached.for_turn(4, "what does section_250 say?", fixture.budget(), 4, {});
    REQUIRE(turn.retrieved.has_value());
    CHECK(turn.retrieved->retriever == "lexical");

    const std::vector<std::string> described = attached.describe();
    REQUIRE(described.size() == 2);
    CHECK(described[0] == "notes.md -- 1 file, 25 bytes, inlined");
    CHECK(described[1].ends_with(", retrieved each turn"));
}

TEST_CASE("resuming reuses the index: nothing is embedded again",
          "[commands][attachments][resume]") {
    Fixture fixture;
    fixture.write("manual.txt", long_document());
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("manual.txt", fixture.work));
        attached.settle();
    }
    const int embedded = fixture.embeddings->texts;
    CHECK(embedded > 10);

    // The resumed chat: the same session, a fresh object, no attach.
    ChatAttachments resumed{*fixture.harness, fixture.session, ChatAttachments::index_for("chat-1"),
                            fixture.hooks()};
    const ChatAttachments::Turn turn =
        resumed.for_turn(1, "coolant section 12", fixture.budget(), 4, {});
    REQUIRE(turn.retrieved.has_value());
    CHECK_FALSE(turn.retrieved->prefix.empty());
    // One text: the question, embedded to search by. The document is not.
    CHECK(fixture.embeddings->texts == embedded + 1);
}

TEST_CASE(
    "an inlined attachment the budget could not send, or compaction folded, is "
    "retrieved from then on",
    "[commands][attachments]") {
    Fixture fixture;
    fixture.write("a.md", "alpha\n");
    fixture.write("b.md", "beta\n");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("a.md", fixture.work));
    REQUIRE(attached.attach("b.md", fixture.work));
    attached.settle();
    (void)attached.for_turn(0, "q", fixture.budget(), 4, {});
    CHECK_FALSE(attached.retrieves());

    attached.after_turn({"a.md"});
    CHECK(fixture.heard("a.md no longer fits the conversation whole"));
    CHECK_FALSE(fixture.session.attachments[0].inline_at.has_value());
    CHECK(fixture.session.attachments[1].inline_at.has_value());
    CHECK(attached.retrieves());

    attached.after_compaction();
    CHECK_FALSE(fixture.session.attachments[1].inline_at.has_value());
    CHECK(fixture.heard("compaction folded the messages the attachments rode"));
}

TEST_CASE("detaching removes a file from the index unless another attachment holds it",
          "[commands][attachments]") {
    Fixture fixture;
    fixture.write("a.md", "shared content\n");
    fixture.write("copy/a.md", "shared content\n");
    fixture.write("b.md", "only here\n");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("a.md", fixture.work));
    REQUIRE(attached.attach("copy", fixture.work));
    REQUIRE(attached.attach("b.md", fixture.work));
    attached.settle();
    CHECK(attached.names() == std::vector<std::string>{"a.md", "copy", "b.md"});

    const std::string shared = fixture.session.attachments[0].files[0].sha256;
    const std::string only = fixture.session.attachments[2].files[0].sha256;
    const apogee::agentloop::AttachmentIndex index{
        ChatAttachments::index_for("chat-1"), {}, std::nullopt};
    CHECK(attached.detach("a.md"));
    CHECK(index.holds(shared));  // still attached as copy/a.md
    CHECK(attached.detach("b.md"));
    CHECK_FALSE(index.holds(only));
    CHECK_FALSE(attached.detach("nothing"));
    CHECK(attached.names() == std::vector<std::string>{"copy"});
}

TEST_CASE("a folder over the size guard is refused where nobody can be asked",
          "[commands][attachments]") {
    Fixture fixture;
    for (int file = 0; file < 501; ++file) {
        fixture.write("many/f" + std::to_string(file) + ".txt", "x");
    }
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    CHECK_FALSE(attached.attach("many", fixture.work));
    CHECK(fixture.heard("many is over 500 files or 50.0 MB -- not attached"));

    // Asked, and answered yes: attached.
    ChatAttachments::Hooks hooks = fixture.hooks();
    std::string asked;
    hooks.confirm_large = [&asked](const std::string& question) {
        asked = question;
        return true;
    };
    ChatAttachments willing{*fixture.harness, fixture.session, ChatAttachments::index_for("chat-1"),
                            hooks};
    CHECK(willing.attach("many", fixture.work));
    CHECK(asked == "attach 501 files (501 bytes) from many?");
}

TEST_CASE("without a named embedding model, or with a billed one, attachments are lexical",
          "[commands][attachments]") {
    SECTION("none named: the chat model is never drafted in") {
        Fixture fixture{false};
        fixture.write("a.md", "alpha\n");
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("a.md", fixture.work));
        attached.settle();
        CHECK(
            fixture.heard("searched by its words: no embedding model is set ('apogee config "
                          "set-default-embedding')"));
        CHECK(fixture.embeddings->texts == 0);
    }
    SECTION("a billed one: nothing is vectorised on Apogee's initiative") {
        Fixture fixture;
        fixture.embeddings->metered = true;
        fixture.write("a.md", "alpha\n");
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("a.md", fixture.work));
        attached.settle();
        CHECK(fixture.heard("is billed per call"));
        CHECK(fixture.embeddings->texts == 0);
    }
}

TEST_CASE("a chat's index is removed with its side files", "[commands][attachments]") {
    Fixture fixture;
    fixture.write("a.md", "alpha\n");
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("a.md", fixture.work));
        attached.settle();
    }
    REQUIRE(std::filesystem::exists(ChatAttachments::index_for("chat-1")));
    ChatAttachments::remove_index("chat-1");
    CHECK_FALSE(std::filesystem::exists(ChatAttachments::index_for("chat-1")));
}

TEST_CASE("@ mentions are paths at the start of a word, quoted or not",
          "[commands][attachments][mentions]") {
    CHECK(mentioned_paths("summarize @report.pdf please") ==
          std::vector<std::string>{"report.pdf"});
    CHECK(mentioned_paths("@a.md and @\"my file.txt\" and @\"open folder/") ==
          std::vector<std::string>{"a.md", "my file.txt", "open folder/"});
    // An address is not a mention.
    CHECK(mentioned_paths("mail me at taylor@example.com").empty());
    CHECK(mentioned_paths("@").empty());

    const apogee::testing::TempDir work{"mentions-" + std::to_string(std::random_device{}())};
    std::ofstream{work.path() / "report.pdf"} << "x";
    CHECK(existing_mention("report.pdf", work.path()) == std::optional<std::string>{"report.pdf"});
    // The sentence's full stop is not the file's.
    CHECK(existing_mention("report.pdf.", work.path()) == std::optional<std::string>{"report.pdf"});
    CHECK(existing_mention("report.pdf),", work.path()) ==
          std::optional<std::string>{"report.pdf"});
    CHECK_FALSE(existing_mention("nothing.txt", work.path()).has_value());
#if !defined(_WIN32)
    // Where a name can end in a dot, one that does is a different file from
    // the name without it, and keeps its name. (Windows reads the two as one
    // file, and the sentence's dot goes, as above.)
    std::ofstream{work.path() / "notes.txt."} << "y";
    std::ofstream{work.path() / "notes.txt"} << "z";
    CHECK(existing_mention("notes.txt.", work.path()) == std::optional<std::string>{"notes.txt."});
#endif
}

TEST_CASE("an attachment retrieved per turn costs nothing against the inline share",
          "[commands][attachments]") {
    Fixture fixture;
    fixture.write("manual.txt", long_document());
    fixture.write("notes.md", "The launch code is 7731.\n");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("manual.txt", fixture.work));
    REQUIRE(attached.attach("notes.md", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached manual.txt: 1 file"));
    CHECK(fixture.heard("attached notes.md: 1 file, 1 chunk -- inlined whole"));
}

TEST_CASE("inlined attachments share one share: the one that would overflow it is retrieved",
          "[commands][attachments]") {
    Fixture fixture;
    // Each fits the 1,200-token share alone; the two together do not.
    std::string first;
    std::string second;
    for (int line = 0; line < 70; ++line) {
        first += "the first pump runs at pressure " + std::to_string(line) + " bar, steady\n";
        second += "the second valve opens at angle " + std::to_string(line) + " degrees, slow\n";
    }
    fixture.write("first.md", first);
    fixture.write("second.md", second);
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("first.md", fixture.work));
    REQUIRE(attached.attach("second.md", fixture.work));
    attached.settle();
    const auto said = [&](std::string_view name) {
        const auto it = std::ranges::find_if(fixture.said, [&](const std::string& line) {
            return line.starts_with("attached " + std::string{name} + ":");
        });
        return it == fixture.said.end() ? std::string{} : *it;
    };
    CHECK(said("first.md").ends_with("-- inlined whole"));
    CHECK(said("second.md").ends_with("-- its excerpts are retrieved each turn"));
}

TEST_CASE("a question naming code finds it by the name, past the question's other words",
          "[commands][attachments]") {
    Fixture fixture{false};
    // Both past the inline share, so both are searched. The question's other
    // words are all in one chunk of generic.txt, and outrank the name when
    // the whole question is what is searched.
    std::string generic;
    for (int line = 0; line < 6; ++line) {
        generic += "where is the function defined, give the file and the line it is on\n";
    }
    for (int line = 0; line < 300; ++line) {
        generic += "alpha bravo charlie delta echo " + std::to_string(line) + "\n";
    }
    fixture.write("generic.txt", generic);
    std::string code;
    for (int line = 0; line < 400; ++line) {
        code += "int filler_" + std::to_string(line) + " = " + std::to_string(line) + ";\n";
    }
    code += "std::size_t fitting_prefix(int share) { return share; }\n";
    fixture.write("budget.cpp", code);
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("generic.txt", fixture.work));
    REQUIRE(attached.attach("budget.cpp", fixture.work));
    attached.settle();
    CHECK_FALSE(fixture.heard("inlined whole"));
    const ChatAttachments::Turn turn = attached.for_turn(
        0, "Where is the function fitting_prefix defined? Give the file and the line.",
        apogee::agentloop::TurnBudget{}, 1, {});
    REQUIRE(turn.retrieved.has_value());
    REQUIRE_FALSE(turn.retrieved->prefix.empty());
    CHECK(turn.retrieved->prefix.front().content.plain_text().find("--- budget.cpp:") !=
          std::string::npos);
    CHECK(turn.retrieved->prefix.front().content.plain_text().find("fitting_prefix(int share)") !=
          std::string::npos);
}

// --- Images, audio and video (26e) ----------------------------------------

namespace {

/// A chat on `chat`, a MediaProvider, beside `eyes`, another, with the roles
/// `roles` sets -- and a folder of media files to attach.
struct MediaChat {
    apogee::testing::TempDir home{"chat-media-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path work = home.path() / "work";
    std::shared_ptr<apogee::testing::MediaProvider> chat =
        std::make_shared<apogee::testing::MediaProvider>();
    std::shared_ptr<apogee::testing::MediaProvider> eyes =
        std::make_shared<apogee::testing::MediaProvider>();
    std::unique_ptr<apogee::harness::Harness> harness;
    apogee::logger::Session session;
    std::vector<std::string> said;
    std::function<bool(const std::string&)> confirm;

    explicit MediaChat(const std::string& roles = "") {
        std::filesystem::create_directories(work);
        const apogee::harness::Config config = apogee::harness::parse_config(
            "models:\n  default: chat\n" + roles +
                "backends:\n  chat:\n    type: mock\n    context_size: 8000\n  eyes:\n    type: "
                "mock\n",
            "<test>");
        harness = std::make_unique<apogee::harness::Harness>(config);
        harness->register_provider("chat", chat);
        harness->register_provider("eyes", eyes);
        harness->use_default_router();
        session.chat_id = "chat-media";
        session.backend = "chat";
    }

    [[nodiscard]] ChatAttachments::Hooks hooks() {
        return ChatAttachments::Hooks{
            .say = [this](const std::string& line, bool) { said.push_back(line); },
            .progress = {},
            .confirm_large = confirm,
            .save = false};
    }

    void write(const std::string& name, const std::string& bytes = "PNGBYTES") const {
        std::ofstream{work / name, std::ios::binary} << bytes;
    }

    [[nodiscard]] bool heard(std::string_view needle) const {
        return std::ranges::any_of(
            said, [&](const std::string& line) { return line.find(needle) != std::string::npos; });
    }

    [[nodiscard]] apogee::agentloop::TurnBudget budget() const {
        return apogee::agentloop::turn_budget(*harness, "chat", std::nullopt);
    }
};

}  // namespace

TEST_CASE("an image is read as it is with the next message, then its description stands in",
          "[commands][attachments][media]") {
    MediaChat fixture;
    fixture.chat->sees = true;
    fixture.chat->reply = [](const apogee::harness::ChatRequest&) {
        return std::string{"A red screen that says STOP."};
    };
    fixture.write("stop.png");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    REQUIRE(attached.attach("stop.png", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached stop.png: 1 file, 1 chunk, described by chat, "));
    CHECK(fixture.heard(" -- read as it is with your next message, then inlined whole"));
    REQUIRE(attached.describe().size() == 1);
    CHECK(attached.describe().front().find("described by chat, read as it is with your next "
                                           "message, then inlined with your next message") !=
          std::string::npos);

    // The turn it is attached on: the picture, and not its description too.
    const ChatAttachments::Turn first =
        attached.for_turn(0, "what is it?", fixture.budget(), 4, {});
    REQUIRE(first.inlined.size() == 1);
    CHECK(first.inlined.front().name == "stop.png (as it is)");
    CHECK(first.inlined.front().text.empty());
    REQUIRE(first.inlined.front().parts.size() == 1);
    CHECK(first.inlined.front().parts.front().image_url ==
          "data:image/png;base64," + apogee::agentloop::base64_encode("PNGBYTES"));
    CHECK_FALSE(first.retrieved.has_value());

    // Every turn after: the description, on the message it was attached with.
    const ChatAttachments::Turn second =
        attached.for_turn(2, "and the colour?", fixture.budget(), 4, {});
    REQUIRE(second.inlined.size() == 1);
    CHECK(second.inlined.front().message == 0);
    CHECK(second.inlined.front().parts.empty());
    CHECK(second.inlined.front().text.find("A red screen that says STOP.") != std::string::npos);
}

TEST_CASE("attaching an image again looks at it again, without describing it again",
          "[commands][attachments][media]") {
    MediaChat fixture;
    fixture.chat->sees = true;
    fixture.write("stop.png");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    REQUIRE(attached.attach("stop.png", fixture.work));
    attached.settle();
    (void)attached.for_turn(0, "what is it?", fixture.budget(), 4, {});
    CHECK(fixture.chat->requests().size() == 1);

    REQUIRE(attached.attach("stop.png", fixture.work));
    attached.settle();
    CHECK(fixture.chat->requests().size() == 1);
    const ChatAttachments::Turn again =
        attached.for_turn(2, "look closer", fixture.budget(), 4, {});
    const auto as_it_is = std::ranges::find(again.inlined, std::string{"stop.png (as it is)"},
                                            &apogee::agentloop::InlineAttachment::name);
    REQUIRE(as_it_is != again.inlined.end());
    CHECK(as_it_is->message == 2);
    CHECK_FALSE(as_it_is->parts.empty());
}

TEST_CASE("a chat model that cannot see has its images described by the vision role",
          "[commands][attachments][media]") {
    MediaChat fixture{"  default_vision: eyes\n"};
    fixture.eyes->sees = true;
    fixture.eyes->reply = [](const apogee::harness::ChatRequest&) {
        return std::string{"An invoice for 1,284.50 EUR."};
    };
    fixture.write("invoice.png");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    REQUIRE(attached.attach("invoice.png", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached invoice.png: 1 file, 1 chunk, described by eyes, "));
    CHECK_FALSE(fixture.heard("read as it is"));
    CHECK(fixture.chat->requests().empty());
    const ChatAttachments::Turn turn = attached.for_turn(0, "how much?", fixture.budget(), 4, {});
    REQUIRE(turn.inlined.size() == 1);
    CHECK(turn.inlined.front().parts.empty());
    CHECK(turn.inlined.front().text.find("1,284.50 EUR") != std::string::npos);
}

namespace {

/// A chat on an `mlx` entry (27c): the real provider over a scripted
/// driver, so what the attachments decide is what the backend is asked.
struct MlxMediaChat {
    apogee::testing::TempDir home{"chat-mlx-media-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path work = home.path() / "work";
    std::shared_ptr<apogee::testing::DriverState> driver =
        std::make_shared<apogee::testing::DriverState>();
    std::vector<apogee::platform::ChildCommand> commands;
    std::shared_ptr<apogee::backends::MlxLocalProvider> chat;
    std::shared_ptr<apogee::testing::MediaProvider> eyes =
        std::make_shared<apogee::testing::MediaProvider>();
    std::unique_ptr<apogee::harness::Harness> harness;
    apogee::logger::Session session;
    std::vector<std::string> said;

    MlxMediaChat(bool vision, const std::string& roles) {
        std::filesystem::create_directories(work);
        driver->vision_startup = std::string{apogee::testing::kVisionReady};
        driver->replies = {R"({"type":"text","id":{id},"text":"A red sign that says STOP."})"
                           "\n"
                           R"({"type":"done","id":{id},"finish":"stop","prompt_tokens":9,)"
                           R"("cached_tokens":0,"completion_tokens":7})"
                           "\n"};
        apogee::backends::MlxLocalProvider::Options options;
        options.backend_name = "chat";
        options.model = "qwen3-vl";
        options.model_dir = home.path() / "model";
        options.info.model_type = "qwen3_vl";
        options.info.chat_template = true;
        options.info.vision = vision;
        options.vision = vision;
        options.vision_gap = vision ? "" : "it is not a vision model";
        options.context_size = 8000;
        chat = std::make_shared<apogee::backends::MlxLocalProvider>(
            std::move(options),
            [this](const apogee::platform::ChildCommand& command,
                   std::string&) -> std::unique_ptr<apogee::testing::FakeDriver> {
                commands.push_back(command);
                driver->vision_spawn =
                    std::ranges::find(command.arguments, "--vision") != command.arguments.end();
                return std::make_unique<apogee::testing::FakeDriver>(driver);
            });
        const apogee::harness::Config config = apogee::harness::parse_config(
            "models:\n  default: chat\n" + roles +
                "backends:\n  chat:\n    type: mlx\n    model_path: " +
                (home.path() / "model").string() + "\n  eyes:\n    type: mock\n",
            "<test>");
        harness = std::make_unique<apogee::harness::Harness>(config);
        harness->register_provider("chat", chat);
        harness->register_provider("eyes", eyes);
        harness->use_default_router();
        session.chat_id = "chat-mlx-media";
        session.backend = "chat";
        std::ofstream{work / "stop.png", std::ios::binary} << "PNGBYTES";
    }

    [[nodiscard]] ChatAttachments::Hooks hooks() {
        return ChatAttachments::Hooks{
            .say = [this](const std::string& line, bool) { said.push_back(line); },
            .progress = {},
            .confirm_large = {},
            .save = false};
    }

    [[nodiscard]] bool heard(std::string_view needle) const {
        return std::ranges::any_of(
            said, [&](const std::string& line) { return line.find(needle) != std::string::npos; });
    }
};

}  // namespace

TEST_CASE(
    "an mlx vision model reads a picture as it is, through the one image pipeline; an mlx text "
    "model has it described by the vision role instead",
    "[commands][attachments][media][mlx]") {
    SECTION("a vision model: read as it is, and described by itself for the turns after") {
        MlxMediaChat fixture{true, ""};
        CHECK(fixture.harness->can_read("chat", apogee::harness::Medium::Image));
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-mlx-media"), fixture.hooks()};
        REQUIRE(attached.attach("stop.png", fixture.work));
        attached.settle();
        CHECK(fixture.heard("attached stop.png: 1 file, 1 chunk, described by chat, "));
        CHECK(fixture.heard("read as it is with your next message"));
        // The description went to the driver as a picture, loaded through mlx-vlm.
        REQUIRE(fixture.commands.size() == 1);
        CHECK(std::ranges::find(fixture.commands[0].arguments, "--vision") !=
              fixture.commands[0].arguments.end());
        const nlohmann::json sent = nlohmann::json::parse(fixture.driver->writes.at(0));
        CHECK(sent["session"] == false);
        CHECK(sent["messages"][0]["content"][0]["type"] == "image");
        CHECK(sent["messages"][0]["content"][0]["image"] ==
              "data:image/png;base64," + apogee::agentloop::base64_encode("PNGBYTES"));

        const ChatAttachments::Turn first = attached.for_turn(
            0, "what is it?",
            apogee::agentloop::turn_budget(*fixture.harness, "chat", std::nullopt), 4, {});
        REQUIRE(first.inlined.size() == 1);
        CHECK(first.inlined.front().name == "stop.png (as it is)");
        REQUIRE(first.inlined.front().parts.size() == 1);
        CHECK(fixture.eyes->requests().empty());
    }
    SECTION("a text model: the vision role describes it, and the chat model is never sent it") {
        MlxMediaChat fixture{false, "  default_vision: eyes\n"};
        fixture.eyes->sees = true;
        fixture.eyes->reply = [](const apogee::harness::ChatRequest&) {
            return std::string{"A red sign that says STOP."};
        };
        CHECK_FALSE(fixture.harness->can_read("chat", apogee::harness::Medium::Image));
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-mlx-media"), fixture.hooks()};
        REQUIRE(attached.attach("stop.png", fixture.work));
        attached.settle();
        CHECK(fixture.heard("attached stop.png: 1 file, 1 chunk, described by eyes, "));
        CHECK_FALSE(fixture.heard("read as it is"));
        const ChatAttachments::Turn turn = attached.for_turn(
            0, "what is it?",
            apogee::agentloop::turn_budget(*fixture.harness, "chat", std::nullopt), 4, {});
        REQUIRE(turn.inlined.size() == 1);
        CHECK(turn.inlined.front().parts.empty());
        CHECK(turn.inlined.front().text.find("STOP") != std::string::npos);
        CHECK(fixture.commands.empty());
    }
    SECTION("a text model and no vision role: refused, naming the role and the mlx way to see") {
        MlxMediaChat fixture{false, ""};
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-mlx-media"), fixture.hooks()};
        CHECK_FALSE(attached.attach("stop.png", fixture.work));
        CHECK(fixture.heard("backend 'chat' cannot read images"));
        CHECK(fixture.heard("an mlx backend over a vision model with mlx-vlm installed"));
        CHECK(fixture.commands.empty());
    }
}

TEST_CASE("media nothing here can read is refused, naming the role that would",
          "[commands][attachments][media]") {
    MediaChat fixture;
    fixture.write("note.m4a", "audio");
    fixture.write("stop.png");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    CHECK_FALSE(attached.attach("note.m4a", fixture.work));
    CHECK(
        fixture.heard("note.m4a: backend 'chat' cannot hear audio, and no transcription model "
                      "is set -- set one with 'apogee config set-default-transcription"));
    CHECK_FALSE(attached.attach("stop.png", fixture.work));
    CHECK(fixture.heard("set-default-vision"));
    attached.settle();
    CHECK(fixture.session.attachments.empty());
}

TEST_CASE("many descriptions by a model billed per call are asked about first",
          "[commands][attachments][media]") {
    MediaChat fixture;
    fixture.chat->sees = true;
    fixture.chat->metered = true;
    std::filesystem::create_directories(fixture.work / "shots");
    for (int index = 0; index < 13; ++index) {
        fixture.write("shots/s" + std::to_string(index) + ".png", "s" + std::to_string(index));
    }
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-media"), fixture.hooks()};
        CHECK_FALSE(attached.attach("shots", fixture.work));
        CHECK(
            fixture.heard("shots not attached: it needs about 13 image and frame descriptions "
                          "by chat, which is billed per call"));
        CHECK(fixture.chat->requests().empty());
    }
    std::string asked;
    fixture.confirm = [&asked](const std::string& question) {
        asked = question;
        return true;
    };
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    CHECK(attached.attach("shots", fixture.work));
    attached.settle();
    CHECK(asked.find("about 13 image and frame descriptions by chat") != std::string::npos);
    CHECK(fixture.chat->requests().size() == 13);

    // Twelve or fewer are not asked about: with no one to ask, they attach.
    fixture.confirm = {};
    std::filesystem::create_directories(fixture.work / "few");
    for (int index = 0; index < 12; ++index) {
        fixture.write("few/f" + std::to_string(index) + ".png", "f" + std::to_string(index));
    }
    ChatAttachments unasked{*fixture.harness, fixture.session,
                            ChatAttachments::index_for("chat-media"), fixture.hooks()};
    CHECK(unasked.attach("few", fixture.work));
}

TEST_CASE("media the budget could not send as it is is said, and its text stands in",
          "[commands][attachments][media]") {
    MediaChat fixture;
    fixture.chat->sees = true;
    fixture.write("stop.png");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    REQUIRE(attached.attach("stop.png", fixture.work));
    attached.settle();
    (void)attached.for_turn(0, "what is it?", fixture.budget(), 4, {});
    attached.after_turn({"stop.png (as it is)"});
    CHECK(
        fixture.heard("stop.png did not fit this request as it is, so the model did not see it "
                      "-- its text stands in from now on"));
    // The description is untouched: still inlined.
    REQUIRE(fixture.session.attachments.size() == 1);
    CHECK(fixture.session.attachments.front().inline_at.has_value());
}

TEST_CASE("a recording over a minute is read through its text, not as it is",
          "[commands][attachments][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::FakeFfmpeg ffmpeg{"audio", "90"};
    MediaChat fixture;
    fixture.chat->hears = true;
    fixture.chat->reply = [](const apogee::harness::ChatRequest&) {
        return std::string{"some words"};
    };
    fixture.write("talk.m4a", "audio");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-media"), fixture.hooks()};
    REQUIRE(attached.attach("talk.m4a", fixture.work));
    attached.settle();
    CHECK(
        fixture.heard("talk.m4a runs 1:30, over a minute, so the chat model reads its "
                      "transcript rather than the recording itself"));
    CHECK_FALSE(fixture.heard("read as it is"));
    const ChatAttachments::Turn turn =
        attached.for_turn(0, "what was said?", fixture.budget(), 4, {});
    for (const apogee::agentloop::InlineAttachment& inlined : turn.inlined) {
        CHECK(inlined.parts.empty());
    }

    // Under a minute, it is heard as it is.
    const apogee::testing::FakeFfmpeg short_one{"audio", "40"};
    fixture.write("short.m4a", "short audio");
    REQUIRE(attached.attach("short.m4a", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached short.m4a: 1 file, 1 chunk, transcribed by chat"));
    CHECK(fixture.heard(" -- read as it is with your next message, then"));
}

namespace {

/// The inlined entry named `name`, or null.
const apogee::agentloop::InlineAttachment* riding(const ChatAttachments::Turn& turn,
                                                  std::string_view name) {
    const auto it =
        std::ranges::find(turn.inlined, name, &apogee::agentloop::InlineAttachment::name);
    return it == turn.inlined.end() ? nullptr : &*it;
}

}  // namespace

TEST_CASE("a folder rides its message with a map of itself; one file is its own map",
          "[commands][attachments][map]") {
    Fixture fixture;
    fixture.write("proj/src/a.cpp", "int a;\n");
    fixture.write("proj/src/b.cpp", "int b;\n");
    fixture.write("proj/docs/x.md", "the docs\n");
    fixture.write("solo.md", "alone\n");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("proj", fixture.work));
    REQUIRE(attached.attach("solo.md", fixture.work));
    REQUIRE(attached.attach("proj/src/*.cpp", fixture.work));
    attached.settle();
    CHECK(fixture.heard("attached proj: 3 files, 3 chunks, with a map of its folders -- "));
    CHECK_FALSE(fixture.heard("attached solo.md: 1 file, 1 chunk, with a map"));

    const ChatAttachments::Turn turn = attached.for_turn(2, "where is b?", fixture.budget(), 4, {});
    const auto* map = riding(turn, "proj (map)");
    REQUIRE(map != nullptr);
    CHECK(map->message == 2);
    CHECK(map->text ==
          "--- map of attachment: proj ---\n"
          "3 files, 3 chunks. Every path in it starts with proj/; its directories, each with the "
          "files under it:\n"
          "docs/  1 file\n"
          "src/  2 files\n"
          "By extension: .cpp 2, .md 1\n"
          "--- end of map: proj ---\n");
    // After the text it maps, so it is read first and trimmed last.
    const auto* text = riding(turn, "proj");
    REQUIRE(text != nullptr);
    CHECK(text < map);
    CHECK(riding(turn, "solo.md (map)") == nullptr);
    // A glob is mapped where its files start, named as it was spelled.
    const auto* glob = riding(turn, "proj/src/*.cpp (map)");
    REQUIRE(glob != nullptr);
    CHECK(
        glob->text.starts_with("--- map of attachment: proj/src/*.cpp ---\n2 files, 2 chunks. "
                               "Every path in it starts with proj/src/;"));

    CHECK(fixture.session.attachments[0].map_at == std::optional<std::size_t>{2});
    CHECK_FALSE(fixture.session.attachments[1].map_at.has_value());
    CHECK(attached.describe()[0].ends_with(", inlined, with a map of its folders"));

    // Resumed, the same bytes on the same message: a cached prompt holds.
    ChatAttachments resumed{*fixture.harness, fixture.session, ChatAttachments::index_for("chat-1"),
                            fixture.hooks()};
    const ChatAttachments::Turn later = resumed.for_turn(4, "and a?", fixture.budget(), 4, {});
    const auto* again = riding(later, "proj (map)");
    REQUIRE(again != nullptr);
    CHECK(again->message == 2);
    CHECK(again->text == map->text);
}

TEST_CASE("after compaction a folder's map rides the next message, once; trimmed, it is said",
          "[commands][attachments][map]") {
    Fixture fixture;
    fixture.write("proj/src/a.cpp", "int a;\n");
    fixture.write("proj/docs/x.md", "the docs\n");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("proj", fixture.work));
    attached.settle();
    REQUIRE(riding(attached.for_turn(0, "q", fixture.budget(), 4, {}), "proj (map)") != nullptr);

    attached.after_compaction();
    CHECK(fixture.heard("the attachments' maps ride your next message again"));
    CHECK_FALSE(fixture.session.attachments[0].map_at.has_value());
    const ChatAttachments::Turn next = attached.for_turn(6, "q", fixture.budget(), 4, {});
    REQUIRE(riding(next, "proj (map)") != nullptr);
    CHECK(riding(next, "proj (map)")->message == 6);
    // Once: the turn after, it still rides message 6, not the newest.
    const ChatAttachments::Turn after = attached.for_turn(8, "q", fixture.budget(), 4, {});
    REQUIRE(riding(after, "proj (map)") != nullptr);
    CHECK(riding(after, "proj (map)")->message == 6);

    attached.after_turn({"proj (map)"});
    CHECK(fixture.heard("the map of proj no longer fits the conversation -- it is left out"));
    CHECK(riding(attached.for_turn(10, "q", fixture.budget(), 4, {}), "proj (map)") == nullptr);
}

TEST_CASE("a map takes its place in the attachment share, and none is drawn on an unknown window",
          "[commands][attachments][map]") {
    // A folder of forty long-named folders: its text far past the share, its
    // map within it -- and big enough that a file which fits the share alone
    // no longer fits beside it.
    Fixture fixture;
    for (int i = 0; i < 40; ++i) {
        fixture.write("wide/" + std::string(60, static_cast<char>('a' + (i % 26))) +
                          std::to_string(10 + i) + "/f.txt",
                      std::string(400, 'w') + "\n");
    }
    std::string fill;
    for (int line = 0; line < 64; ++line) {
        fill += "the fill line " + std::to_string(100 + line) + " says nothing at all, at length\n";
    }
    fixture.write("fill.md", fill);
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("wide", fixture.work));
    attached.settle();
    CHECK(fixture.heard(", with a map of its folders -- its excerpts are retrieved each turn"));
    REQUIRE(attached.attach("fill.md", fixture.work));
    attached.settle();
    const auto line = [](const Fixture& where) {
        const auto it = std::ranges::find_if(where.said, [](const std::string& said) {
            return said.starts_with("attached fill.md:");
        });
        return it == where.said.end() ? std::string{} : *it;
    };
    CHECK(line(fixture).ends_with("-- its excerpts are retrieved each turn"));

    // Alone, the same file is inlined.
    Fixture alone;
    alone.write("fill.md", fill);
    ChatAttachments only{*alone.harness, alone.session, ChatAttachments::index_for("chat-1"),
                         alone.hooks()};
    REQUIRE(only.attach("fill.md", alone.work));
    only.settle();
    CHECK(line(alone).ends_with("-- inlined whole"));

    // A backend whose window nobody knows inlines nothing, and maps nothing.
    Fixture unknown;
    unknown.session.backend = "embedder";
    unknown.write("proj/a.md", "a\n");
    unknown.write("proj/b/c.md", "c\n");
    ChatAttachments blind{*unknown.harness, unknown.session, ChatAttachments::index_for("chat-1"),
                          unknown.hooks()};
    REQUIRE(blind.attach("proj", unknown.work));
    blind.settle();
    CHECK(unknown.heard(", no map: the model's window is unknown -- "));
    CHECK(riding(blind.for_turn(0, "q", unknown.budget(), 4, {}), "proj (map)") == nullptr);
}
