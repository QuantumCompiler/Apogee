#include "cli/chat_attachments.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "agentloop/media.h"
#include "backends/mlx_local.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "harness/harness.h"
#include "operations/graph_sources.h"
#include "operations/retrieval.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/fake_mlx_driver.h"
#include "support/media_fakes.h"
#include "tools/graph_nav.h"
#include "tools/toolsets.h"

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
    /// Calls so far, and from which one on a call is held until its
    /// cancellation -- Ctrl-C at the turn that waits on the attach (0: never).
    std::atomic<int> calls{0};
    std::atomic<int> hold_from{0};

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
        const apogee::harness::CancellationToken& cancellation) override {
        if (const int call = ++calls; hold_from > 0 && call >= hold_from) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{20};
            while (!cancellation.stop_requested() && std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds{5});
            }
            cancellation.throw_if_cancelled();
        }
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
    /// The lines said as warnings, again.
    std::vector<std::string> warned;

    /// `extra` is YAML at the config's top level.
    explicit Fixture(bool embedding_role = true, const std::string& extra = {}) {
        std::filesystem::create_directories(work);
        const std::string models = embedding_role ? "models:\n  default_embedding: embedder\n" : "";
        const apogee::harness::Config config = apogee::harness::parse_config(
            models +
                "backends:\n  chat:\n    type: mock\n    context_size: 8000\n  embedder:\n"
                "    type: mock\n" +
                extra,
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
            .say =
                [this](const std::string& line, bool warning) {
                    said.push_back(line);
                    if (warning) {
                        warned.push_back(line);
                    }
                },
            .progress = {},
            .confirm_large = {},
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code};
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
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code};
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
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code};
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
    // A folder of code is graphed too (27n): its line ends with the graph's.
    CHECK(attached.describe()[0].find(", inlined, with a map of its folders; graph: ") !=
          std::string::npos);

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

// ---- The attachment code graph (27n) ----------------------------------------

namespace {

/// The mini-repos 27k commits, one per vendored grammar.
[[nodiscard]] std::filesystem::path code_fixtures() {
    return std::filesystem::path{APOGEE_TEST_FIXTURES} / "code_graph";
}

/// A copy of `from` (under the fixtures' code_graph/, empty for all of it) at
/// `fixture.work / as`.
std::filesystem::path copy_code(const Fixture& fixture, const std::string& from,
                                const std::string& as) {
    const std::filesystem::path to = fixture.work / as;
    std::filesystem::create_directories(to);
    std::filesystem::copy(from.empty() ? code_fixtures() : code_fixtures() / from, to,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
    return to;
}

/// What `graph build --source` builds over `trees` -- 27k's entry, walking
/// each tree itself -- in a store of its own beside the chat's.
[[nodiscard]] std::string direct_dump(const Fixture& fixture,
                                      const std::vector<std::filesystem::path>& trees) {
    apogee::harness::NamedGraphConfig named;
    for (const std::filesystem::path& tree : trees) {
        named.sources.push_back(tree.generic_string());
    }
    const std::filesystem::path path =
        fixture.home.path() / ("direct-" + std::to_string(std::random_device{}()) + ".db");
    apogee::embedstore::Store store{path};
    (void)apogee::commands::build_graph_sources(store, named, {});
    return store.graph_dump();
}

[[nodiscard]] std::string chat_dump() {
    return apogee::embedstore::Store{ChatAttachments::index_for("chat-1")}.graph_dump();
}

[[nodiscard]] std::vector<std::string> chat_members() {
    return apogee::embedstore::Store{ChatAttachments::index_for("chat-1")}.code_members();
}

[[nodiscard]] bool said_graph(const Fixture& fixture) {
    return std::ranges::any_of(fixture.said,
                               [](const std::string& line) { return line.starts_with("graph:"); });
}

/// A harness with no backend at all: no chat model, no embedder, no key.
struct Bare {
    apogee::testing::TempDir home{"chat-attach-bare-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path work = home.path() / "work";
    std::unique_ptr<apogee::harness::Harness> harness;
    apogee::logger::Session session;
    std::vector<std::string> said;

    Bare() {
        std::filesystem::create_directories(work);
        harness = std::make_unique<apogee::harness::Harness>(
            apogee::harness::parse_config("# no backends\n", "<test>"));
        harness->use_default_router();
        session.chat_id = "chat-1";
    }
};

}  // namespace

TEST_CASE("a folder of code builds its graph in the chat's index, as graph build --source would",
          "[commands][attachments][graph]") {
    Fixture fixture;
    // Every vendored grammar's mini-repo, and the goldens no grammar parses.
    const std::filesystem::path tree = copy_code(fixture, "", "code");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("code", fixture.work));
    attached.settle();

    // Byte for byte the graph 27k's own entry builds over the same files.
    const std::string direct = direct_dump(fixture, {tree});
    REQUIRE_FALSE(direct.empty());
    CHECK(chat_dump() == direct);

    // One line, after the attach line, its counts the graph's own.
    const apogee::embedstore::GraphStats stats =
        apogee::embedstore::Store{ChatAttachments::index_for("chat-1")}.graph_stats();
    CHECK(stats.edges_inferred == 0);
    const auto graph_line = std::ranges::find_if(
        fixture.said, [](const std::string& line) { return line.starts_with("graph:"); });
    REQUIRE(graph_line != fixture.said.end());
    CHECK(std::prev(graph_line)->starts_with("attached code: "));
    CHECK(graph_line->starts_with("graph: " + std::to_string(stats.nodes) + " nodes, " +
                                  std::to_string(stats.edges) + " edges (supported: "));
    CHECK(graph_line->find("python 4") != std::string::npos);
    CHECK(graph_line->ends_with("; skipped: .golden 12)"));

    CHECK(std::ranges::find(fixture.warned, *graph_line) == fixture.warned.end());

    // Kept with the session, and repeated by /attachments.
    REQUIRE(fixture.session.attachments.size() == 1);
    REQUIRE(fixture.session.attachments[0].graph.has_value());
    CHECK(fixture.session.attachments[0].graph->label == "code");
    CHECK(fixture.session.attachments[0].graph->supported.size() == 12);
    const std::vector<std::string> described = attached.describe();
    REQUIRE(described.size() == 1);
    CHECK(described[0].ends_with("; " + *graph_line));

    // The chunks are indexed as ever: the graph is beside them.
    CHECK(fixture.embeddings->texts > 0);
}

TEST_CASE("with no backend and no embedder, attaching a folder of code gets the full graph",
          "[commands][attachments][graph]") {
    Bare bare;
    const std::filesystem::path tree = bare.work / "app";
    std::filesystem::copy(code_fixtures() / "python", tree,
                          std::filesystem::copy_options::recursive);
    ChatAttachments attached{
        *bare.harness, bare.session, ChatAttachments::index_for("chat-1"),
        ChatAttachments::Hooks{
            .say = [&bare](const std::string& line, bool) { bare.said.push_back(line); },
            .progress = {},
            .confirm_large = {},
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code}};
    // Named with a trailing separator: the same folder, the same label.
    REQUIRE(attached.attach("app/", bare.work));
    attached.settle();
    CHECK(std::ranges::any_of(bare.said, [](const std::string& line) {
        return line.find("searched by its words") != std::string::npos;
    }));
    CHECK(std::ranges::any_of(bare.said, [](const std::string& line) {
        return line.starts_with("graph: ") &&
               line.ends_with("(supported: python 4; skipped: none)");
    }));
    apogee::harness::NamedGraphConfig named;
    named.sources = {tree.generic_string()};
    apogee::embedstore::Store direct{bare.home.path() / "direct.db"};
    (void)apogee::commands::build_graph_sources(direct, named, {});
    CHECK(chat_dump() == direct.graph_dump());
}

TEST_CASE("a folder with no code, one file and a glob build no graph and say nothing of one",
          "[commands][attachments][graph]") {
    Fixture fixture;
    fixture.write("docs/a.md", "alpha\n");
    fixture.write("docs/b.txt", "beta\n");
    fixture.write("docs/third_party/vendored.py", "def v():\n    pass\n");
    copy_code(fixture, "python", "app");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("docs", fixture.work));
    REQUIRE(attached.attach("app/main.py", fixture.work));
    REQUIRE(attached.attach("app/pkg/*.py", fixture.work));
    attached.settle();
    REQUIRE(fixture.session.attachments.size() == 3);
    CHECK_FALSE(said_graph(fixture));
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());
    for (const apogee::logger::Attachment& attachment : fixture.session.attachments) {
        CHECK_FALSE(attachment.graph.has_value());
    }
    for (const std::string& line : attached.describe()) {
        CHECK(line.find("graph") == std::string::npos);
    }
}

TEST_CASE("re-attached, the graph converges on a fresh build; detached, its part alone goes",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path app = copy_code(fixture, "python", "app");
    const std::filesystem::path shapes = copy_code(fixture, "cpp", "shapes");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("app", fixture.work));
    REQUIRE(attached.attach("shapes", fixture.work));
    attached.settle();
    CHECK(chat_members() == std::vector<std::string>{"app", "shapes"});
    CHECK(chat_dump() == direct_dump(fixture, {app, shapes}));

    // An edit, re-attached: the same member, updated to what a fresh build
    // of the edited tree holds.
    std::ofstream{app / "pkg" / "service.py", std::ios::app}
        << "\n\ndef audit(user: User) -> str:\n    return user.greet()\n";
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(chat_members() == std::vector<std::string>{"app", "shapes"});
    CHECK(chat_dump() == direct_dump(fixture, {app, shapes}));
    REQUIRE(fixture.session.attachments.size() == 2);
    CHECK(fixture.session.attachments[1].name == "app");
    CHECK(fixture.session.attachments[1].graph->label == "app");

    // Each folder's line counts its own part.
    const std::vector<std::string> described = attached.describe();
    REQUIRE(described.size() == 2);
    CHECK(described[0].find("; graph: ") != std::string::npos);
    CHECK(described[0].find("(supported: cpp 4; skipped: none)") != std::string::npos);
    CHECK(described[1].find("(supported: python 4; skipped: none)") != std::string::npos);

    // Detached: its part goes, the other's re-linked as a build of it alone.
    REQUIRE(attached.detach("app"));
    CHECK(chat_members() == std::vector<std::string>{"shapes"});
    CHECK(chat_dump() == direct_dump(fixture, {shapes}));
    REQUIRE(attached.detach("shapes"));
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());
}

TEST_CASE("two folders of one name are two members; a resumed chat updates its own",
          "[commands][attachments][graph]") {
    Fixture fixture;
    copy_code(fixture, "python", "a/src");
    copy_code(fixture, "go", "b/src");
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("a/src", fixture.work));
        REQUIRE(attached.attach("b/src", fixture.work));
        attached.settle();
    }
    CHECK(chat_members() == std::vector<std::string>{"src", "src-2"});
    REQUIRE(fixture.session.attachments.size() == 2);
    CHECK(fixture.session.attachments[0].graph->label == "src");
    CHECK(fixture.session.attachments[1].graph->label == "src-2");

    // Resumed: re-attached under its name, the same member is updated, and a
    // third folder of that name takes the next number.
    copy_code(fixture, "ruby", "c/src");
    ChatAttachments resumed{*fixture.harness, fixture.session, ChatAttachments::index_for("chat-1"),
                            fixture.hooks()};
    REQUIRE(resumed.attach("b/src", fixture.work));
    REQUIRE(resumed.attach("c/src", fixture.work));
    resumed.settle();
    CHECK(chat_members() == std::vector<std::string>{"src", "src-2", "src-3"});
    CHECK(fixture.session.attachments[1].name == "b/src");
    CHECK(fixture.session.attachments[1].graph->label == "src-2");

    // With the first folder gone, a re-attach still updates its own member
    // rather than taking the freed name -- and a new folder takes it.
    REQUIRE(resumed.attach("a/src", fixture.work));
    resumed.settle();
    REQUIRE(resumed.detach("a/src"));
    REQUIRE(resumed.attach("b/src", fixture.work));
    resumed.settle();
    CHECK(chat_members() == std::vector<std::string>{"src-2", "src-3"});
    CHECK(fixture.session.attachments.back().name == "b/src");
    CHECK(fixture.session.attachments.back().graph->label == "src-2");
    copy_code(fixture, "java", "d/src");
    REQUIRE(resumed.attach("d/src", fixture.work));
    resumed.settle();
    CHECK(fixture.session.attachments.back().graph->label == "src");
    CHECK(chat_members() == std::vector<std::string>{"src", "src-2", "src-3"});
}

TEST_CASE("with the graph off nothing is built, and an earlier graph of the folder is forgotten",
          "[commands][attachments][graph]") {
    Fixture fixture;
    copy_code(fixture, "python", "app");
    ChatAttachments::Hooks off = fixture.hooks();
    off.built_in_graph = apogee::harness::AttachmentGraphMethod::Off;
    {
        // `complete`'s one-shot store: a folder of code, chunks only.
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), off};
        REQUIRE(attached.attach("app", fixture.work));
        attached.settle();
        CHECK(fixture.heard("attached app: 4 files"));
        CHECK_FALSE(said_graph(fixture));
        CHECK(chat_members().empty());
    }
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("app", fixture.work));
        attached.settle();
        CHECK(chat_members() == std::vector<std::string>{"app"});
    }
    fixture.said.clear();
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), off};
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK_FALSE(said_graph(fixture));
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());
    CHECK_FALSE(fixture.session.attachments[0].graph.has_value());
}

TEST_CASE("Ctrl-C keeps the chunks that are ready and leaves the graph absent, said",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path app = copy_code(fixture, "python", "app");
    // The second file's embedding is held until the attach is cancelled, and
    // Ctrl-C is pressed while the turn waits on it.
    fixture.embeddings->hold_from = 2;
    ChatAttachments::Hooks hooks = fixture.hooks();
    bool pressed = false;
    hooks.progress = [&pressed](const std::string& line) {
        if (!pressed && !line.empty() && line.find("(1 of 4)") == std::string::npos) {
            pressed = true;
            (void)std::raise(SIGINT);
        }
    };
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), hooks};
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(pressed);
    CHECK(fixture.heard(": cancelled"));
    CHECK(fixture.heard("graph: not built -- cancelled; attach it again to build it"));
    CHECK(std::ranges::find(fixture.warned,
                            "graph: not built -- cancelled; attach it again to build it") !=
          fixture.warned.end());

    // The index usable: what was ready is attached, and searched.
    REQUIRE(fixture.session.attachments.size() == 1);
    CHECK(fixture.session.attachments[0].files.size() == 1);  // main.py; the rest cancelled
    REQUIRE(fixture.session.attachments[0].graph.has_value());
    CHECK(fixture.session.attachments[0].graph->absent == "cancelled");
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());
    REQUIRE(attached.describe().size() == 1);
    CHECK(attached.describe()[0].ends_with(
        "; graph: not built -- cancelled; attach it again to build it"));

    // The next attach completes it.
    fixture.embeddings->hold_from = 0;
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(fixture.session.attachments[0].files.size() == 4);
    CHECK(fixture.session.attachments[0].graph->label == "app");
    CHECK(chat_dump() == direct_dump(fixture, {app}));
}

TEST_CASE("Ctrl-C on a re-attach forgets the folder's earlier graph rather than trust it",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path app = copy_code(fixture, "python", "app");
    ChatAttachments::Hooks hooks = fixture.hooks();
    bool armed = false;
    bool pressed = false;
    hooks.progress = [&](const std::string& line) {
        if (armed && !pressed && !line.empty() && line.find("(1 of 4)") == std::string::npos) {
            pressed = true;
            (void)std::raise(SIGINT);
        }
    };
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), hooks};
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    REQUIRE_FALSE(chat_dump().empty());

    // Edited, attached again, and interrupted while the second edit embeds:
    // the graph would be the old files', so it is forgotten, and said.
    std::ofstream{app / "main.py", std::ios::app} << "\n# edited\n";
    std::ofstream{app / "pkg" / "models.py", std::ios::app} << "\n# edited\n";
    fixture.embeddings->hold_from = fixture.embeddings->calls + 2;
    armed = true;
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    fixture.embeddings->hold_from = 0;
    CHECK(pressed);
    CHECK(fixture.heard("graph: not built -- cancelled; attach it again to build it"));
    REQUIRE(fixture.session.attachments.size() == 1);
    CHECK(fixture.session.attachments[0].graph->absent == "cancelled");
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());

    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(chat_dump() == direct_dump(fixture, {app}));
}

TEST_CASE("a re-attach that indexes nothing leaves the folder and its graph as they stood",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path app = copy_code(fixture, "python", "app");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    const std::string before = chat_dump();
    REQUIRE_FALSE(before.empty());
    // Every file emptied: nothing of it is indexed, so nothing is replaced.
    for (const char* file : {"main.py", "pkg/__init__.py", "pkg/models.py", "pkg/service.py"}) {
        std::ofstream{app / file, std::ios::trunc};
    }
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(fixture.heard("nothing attached from app"));
    CHECK(chat_dump() == before);
    REQUIRE(fixture.session.attachments.size() == 1);
    REQUIRE(fixture.session.attachments[0].graph.has_value());
    CHECK(fixture.session.attachments[0].graph->label == "app");
}

TEST_CASE("a quit mid-re-attach leaves the graph the saved session knows",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path app = copy_code(fixture, "python", "app");
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("app", fixture.work));
        attached.settle();
    }
    const std::string before = chat_dump();
    REQUIRE_FALSE(before.empty());
    // Two files edited, so both are embedded again; the second is held until
    // the process ends -- the destructor's cancel, with nothing recorded.
    std::ofstream{app / "main.py", std::ios::app} << "\n# edited\n";
    std::ofstream{app / "pkg" / "models.py", std::ios::app} << "\n# edited\n";
    fixture.embeddings->hold_from = fixture.embeddings->calls + 2;
    {
        ChatAttachments quitting{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(quitting.attach("app", fixture.work));
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (fixture.embeddings->calls < fixture.embeddings->hold_from &&
               std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        REQUIRE(fixture.embeddings->calls >= fixture.embeddings->hold_from);
    }
    fixture.embeddings->hold_from = 0;
    // The session still records the earlier attach and its graph, and the
    // index still holds that graph.
    REQUIRE(fixture.session.attachments.size() == 1);
    CHECK(fixture.session.attachments[0].graph->label == "app");
    CHECK(chat_members() == std::vector<std::string>{"app"});
    CHECK(chat_dump() == before);
}

TEST_CASE("after every settle the index's graph is exactly the recorded folders'",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path ghost = copy_code(fixture, "go", "ghost");
    fixture.write("notes.md", "plain notes\n");
    // A member no recorded attachment owns: a build a crash cut short.
    apogee::commands::AttachmentGraphJob job;
    job.name = "ghost";
    job.label = "ghost";
    job.root = ghost;
    job.files = apogee::commands::source_files_under(
        apogee::agentloop::find_attachment_files("ghost", fixture.work).files, ghost);
    REQUIRE(apogee::commands::build_attachment_graph(ChatAttachments::index_for("chat-1"), job, {})
                .state == apogee::commands::AttachmentGraphOutcome::State::Built);
    REQUIRE(chat_members() == std::vector<std::string>{"ghost"});

    // Attaching anything -- here no code at all -- settles into a graph that
    // holds only what the session records: nothing.
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("notes.md", fixture.work));
    attached.settle();
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());
}

TEST_CASE("a quit mid-attach leaves the graph whole or absent, never half; the next completes it",
          "[commands][attachments][graph]") {
    Fixture fixture;
    const std::filesystem::path tree = copy_code(fixture, "", "code");
    const std::string direct = direct_dump(fixture, {tree});
    {
        // The process ending: the destructor cancels whatever is in flight.
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("code", fixture.work));
    }
    if (std::filesystem::exists(ChatAttachments::index_for("chat-1"))) {
        const std::string after = chat_dump();
        CHECK((after.empty() || after == direct));
    }
    CHECK(fixture.session.attachments.empty());  // nothing settled, nothing recorded
    ChatAttachments again{*fixture.harness, fixture.session, ChatAttachments::index_for("chat-1"),
                          fixture.hooks()};
    REQUIRE(again.attach("code", fixture.work));
    again.settle();
    CHECK(chat_dump() == direct);
}

TEST_CASE("deleting the chat removes the graph with its index -- nothing survives it",
          "[commands][attachments][graph]") {
    Fixture fixture;
    copy_code(fixture, "python", "app");
    {
        ChatAttachments attached{*fixture.harness, fixture.session,
                                 ChatAttachments::index_for("chat-1"), fixture.hooks()};
        REQUIRE(attached.attach("app", fixture.work));
        attached.settle();
        REQUIRE_FALSE(chat_members().empty());
    }
    ChatAttachments::remove_index("chat-1");
    for (const auto& entry :
         std::filesystem::directory_iterator{apogee::harness::attachments_dir()}) {
        CHECK_FALSE(entry.path().filename().string().starts_with("chat-1"));
    }
}

// ---- The graph at work on turns (27o) -----------------------------------------

TEST_CASE("the chat's graph scope comes with its first graphed folder and goes with its last",
          "[commands][attachments][graph][scope]") {
    Fixture fixture;
    fixture.write("docs/a.md", "alpha\n");
    fixture.write("docs/b.txt", "beta\n");
    copy_code(fixture, "python", "app");
    copy_code(fixture, "cpp", "shapes");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    CHECK_FALSE(attached.graph_scope().has_value());

    // Chunks only: no graph, no scope.
    REQUIRE(attached.attach("docs", fixture.work));
    attached.settle();
    CHECK_FALSE(attached.graph_scope().has_value());

    REQUIRE(attached.attach("app", fixture.work));
    REQUIRE(attached.attach("shapes", fixture.work));
    attached.settle();
    std::optional<apogee::commands::AttachmentGraphScope> scope = attached.graph_scope();
    REQUIRE(scope.has_value());
    CHECK(scope->store == ChatAttachments::index_for("chat-1"));
    CHECK(scope->folders ==
          std::vector<apogee::commands::GraphedFolder>{{"app", "app"}, {"shapes", "shapes"}});
    // What the tools read and what their descriptions say.
    const apogee::graph::GraphTarget target = apogee::commands::attachment_graph_target(*scope);
    CHECK(target.name == "attachments");
    CHECK(target.store_path == ChatAttachments::index_for("chat-1"));
    CHECK(apogee::commands::attachment_graph_note(*scope) ==
          " Reads the code graph of the folders attached to this chat, app (member 'app') and "
          "shapes (member 'shapes'): where its functions and classes are defined and "
          "implemented, what calls what, and how one reaches another. A file is named relative "
          "to its member's folder.");

    // One graphed folder detached: the other's scope stands.
    REQUIRE(attached.detach("app"));
    scope = attached.graph_scope();
    REQUIRE(scope.has_value());
    CHECK(scope->folders == std::vector<apogee::commands::GraphedFolder>{{"shapes", "shapes"}});
    CHECK(apogee::commands::attachment_graph_note(*scope).starts_with(
        " Reads the code graph of the folder attached to this chat, shapes (member 'shapes'):"));
    // A folder whose graph is absent -- cancelled, failed -- is no part of it.
    fixture.session.attachments.push_back(apogee::logger::Attachment{
        .name = "cut",
        .files = {},
        .inline_at = {},
        .map_at = {},
        .graph = apogee::logger::AttachmentGraph{
            .label = {}, .supported = {}, .skipped = {}, .absent = "cancelled"}});
    REQUIRE(attached.graph_scope().has_value());
    CHECK(attached.graph_scope()->folders ==
          std::vector<apogee::commands::GraphedFolder>{{"shapes", "shapes"}});
    fixture.session.attachments.pop_back();
    // A label the index no longer holds a graph for walks nothing.
    apogee::commands::forget_attachment_graph(ChatAttachments::index_for("chat-1"), "shapes", {});
    CHECK_FALSE(attached.graph_scope().has_value());
    // Nor does an index that is gone -- and asking never makes one.
    ChatAttachments::remove_index("chat-1");
    CHECK_FALSE(attached.graph_scope().has_value());
    CHECK_FALSE(std::filesystem::exists(ChatAttachments::index_for("chat-1")));
}

TEST_CASE("the chat's graph scope goes with its last graphed folder, though chunks stay",
          "[commands][attachments][graph][scope]") {
    Fixture fixture;
    fixture.write("docs/a.md", "alpha\n");
    fixture.write("docs/b.txt", "beta\n");
    copy_code(fixture, "python", "app");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    REQUIRE(attached.attach("docs", fixture.work));
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    REQUIRE(attached.graph_scope().has_value());
    REQUIRE(attached.detach("app"));
    CHECK_FALSE(attached.graph_scope().has_value());
    CHECK(attached.names() == std::vector<std::string>{"docs"});
}

TEST_CASE("an attachment turn expands through the chat's graph and counts it; chunks alone do not",
          "[commands][attachments][graph]") {
    Bare bare;
    std::filesystem::copy(code_fixtures() / "python", bare.work / "app",
                          std::filesystem::copy_options::recursive);
    std::filesystem::create_directories(bare.work / "notes");
    std::ofstream{bare.work / "notes" / "make_user.txt"} << "make_user makes a user.\n";
    ChatAttachments attached{
        *bare.harness, bare.session, ChatAttachments::index_for("chat-1"),
        ChatAttachments::Hooks{
            .say = [&bare](const std::string& line, bool) { bare.said.push_back(line); },
            .progress = {},
            .confirm_large = {},
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code}};
    // Notes first: a chunk-only chat injects no graph.
    REQUIRE(attached.attach("notes", bare.work));
    attached.settle();
    ChatAttachments::Turn turn =
        attached.for_turn(0, "make_user", apogee::agentloop::TurnBudget{}, 4, {});
    REQUIRE(turn.retrieved.has_value());
    CHECK(turn.retrieved->graph_entities == 0);
    REQUIRE_FALSE(turn.retrieved->prefix.empty());
    CHECK(turn.retrieved->prefix.front().content.plain_text().find("[Knowledge graph") ==
          std::string::npos);

    // A folder of code: its excerpts' code is walked, after the excerpts, and
    // the line says how much -- with no embedder anywhere.
    REQUIRE(attached.attach("app", bare.work));
    attached.settle();
    turn = attached.for_turn(1, "make_user", apogee::agentloop::TurnBudget{}, 4, {});
    REQUIRE(turn.retrieved.has_value());
    CHECK(turn.retrieved->retriever == "lexical");
    CHECK(turn.retrieved->graph_entities > 0);
    REQUIRE(turn.retrieved->prefix.size() == 1);
    const std::string sent = turn.retrieved->prefix.front().content.plain_text();
    const std::size_t section = sent.find("[Knowledge graph: attachments]");
    REQUIRE(section != std::string::npos);
    CHECK(sent.find("--- app/pkg/service.py:") < section);
    CHECK(sent.find("(class, pkg/models.py:") > section);
    CHECK(apogee::commands::describe_attachment_retrieval(*turn.retrieved)
              .find(" +" + std::to_string(turn.retrieved->graph_entities) + " graph entities") !=
          std::string::npos);
}

TEST_CASE("a lexical-only chat walks its attachment graph through the scoped tools",
          "[commands][attachments][graph][scope]") {
    Bare bare;
    std::filesystem::copy(code_fixtures() / "python", bare.work / "app",
                          std::filesystem::copy_options::recursive);
    ChatAttachments attached{
        *bare.harness, bare.session, ChatAttachments::index_for("chat-1"),
        ChatAttachments::Hooks{
            .say = [&bare](const std::string& line, bool) { bare.said.push_back(line); },
            .progress = {},
            .confirm_large = {},
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code}};
    REQUIRE(attached.attach("app", bare.work));
    attached.settle();
    const std::optional<apogee::commands::AttachmentGraphScope> scope = attached.graph_scope();
    REQUIRE(scope.has_value());
    apogee::agent::ToolRegistry registry;
    apogee::tools::register_native_toolsets(registry, apogee::tools::ToolsetOptions{});
    const apogee::agent::ToolRegistry scoped =
        apogee::commands::attachment_graph_tools(registry, *scope);
    // Each says what it reads: the folder by its root, its member, and that a
    // file is named relative to it.
    for (const std::string_view name : apogee::tools::graph_tool_names()) {
        CHECK(scoped.find(name)->description.ends_with(
            apogee::commands::attachment_graph_note(*scope)));
    }
    CHECK(scoped.find("graph_explain")
              ->description.find("folder attached to this chat, app (member 'app')") !=
          std::string::npos);

    const apogee::agent::ToolOutcome card =
        scoped.find("graph_explain")->run(R"({"node":"make_user"})");
    REQUIRE_FALSE(card.is_error);
    const nlohmann::json json = nlohmann::json::parse(card.content);
    CHECK(json["graph"] == "attachments");
    CHECK(json["node"]["name"] == "pkg.service.make_user");
    CHECK(json["node"]["member"] == "app");
    CHECK(json["node"]["file"] == "pkg/service.py");

    const apogee::agent::ToolOutcome path =
        scoped.find("graph_path")
            ->run(R"({"from":"main","to":"make_user","directed":true,"relations":["calls"]})");
    REQUIRE_FALSE(path.is_error);
    CHECK(nlohmann::json::parse(path.content)["found"] == true);
}

TEST_CASE("a turn walks only a graph the chat records -- never one left in the index",
          "[commands][attachments][graph][scope]") {
    // What a quit mid-attach can leave until the next settle forgets it: code
    // in the index that no recorded folder owns. The turn reads the chat's
    // state, not the index's tables, so it walks none of it.
    Bare bare;
    std::filesystem::copy(code_fixtures() / "python", bare.work / "app",
                          std::filesystem::copy_options::recursive);
    ChatAttachments attached{
        *bare.harness, bare.session, ChatAttachments::index_for("chat-1"),
        ChatAttachments::Hooks{
            .say = [&bare](const std::string& line, bool) { bare.said.push_back(line); },
            .progress = {},
            .confirm_large = {},
            .save = false,
            .built_in_graph = apogee::harness::AttachmentGraphMethod::Code}};
    REQUIRE(attached.attach("app", bare.work));
    attached.settle();
    REQUIRE_FALSE(chat_members().empty());
    bare.session.attachments.front().graph.reset();
    CHECK_FALSE(attached.graph_scope().has_value());
    const ChatAttachments::Turn turn =
        attached.for_turn(0, "make_user", apogee::agentloop::TurnBudget{}, 4, {});
    REQUIRE(turn.retrieved.has_value());
    REQUIRE_FALSE(turn.retrieved->prefix.empty());
    CHECK(turn.retrieved->graph_entities == 0);
    CHECK(turn.retrieved->prefix.front().content.plain_text().find("[Knowledge graph") ==
          std::string::npos);
}

// ---- Attachment options (27p) -------------------------------------------------

TEST_CASE("an attach's method is a flag over the config over the surface's built-in",
          "[commands][attachments][options]") {
    using apogee::commands::graph_method_note;
    using apogee::commands::GraphMethod;
    using apogee::commands::GraphMethodSource;
    using apogee::commands::resolve_graph_method;
    using apogee::harness::AttachmentGraphMethod;
    constexpr AttachmentGraphMethod code = AttachmentGraphMethod::Code;
    constexpr AttachmentGraphMethod off = AttachmentGraphMethod::Off;

    struct Row {
        std::optional<AttachmentGraphMethod> flag;
        std::optional<AttachmentGraphMethod> config;
        AttachmentGraphMethod built_in;
        GraphMethod expected;
        std::string note;
    };

    const std::string from_config_code =
        "with its code graph (attachments.graph: code in the config)";
    const std::string from_config_off =
        "without its code graph (attachments.graph: off in the config)";
    // Every combination: a chat's built-in (code) and complete's (off), each
    // under no config, either config word, and no flag or either flag.
    const std::vector<Row> table{
        {{}, {}, code, {code, GraphMethodSource::BuiltIn}, ""},
        {{}, {}, off, {off, GraphMethodSource::BuiltIn}, ""},
        {{}, code, code, {code, GraphMethodSource::Config}, from_config_code},
        {{}, code, off, {code, GraphMethodSource::Config}, from_config_code},
        {{}, off, code, {off, GraphMethodSource::Config}, from_config_off},
        {{}, off, off, {off, GraphMethodSource::Config}, from_config_off},
        {code, {}, code, {code, GraphMethodSource::Flag}, "with its code graph (--graph=code)"},
        {code, {}, off, {code, GraphMethodSource::Flag}, "with its code graph (--graph=code)"},
        {code, off, code, {code, GraphMethodSource::Flag}, "with its code graph (--graph=code)"},
        {code, off, off, {code, GraphMethodSource::Flag}, "with its code graph (--graph=code)"},
        {code, code, off, {code, GraphMethodSource::Flag}, "with its code graph (--graph=code)"},
        {code, code, code, {code, GraphMethodSource::Flag}, "with its code graph (--graph=code)"},
        {off, {}, code, {off, GraphMethodSource::Flag}, "without its code graph (--graph=off)"},
        {off, {}, off, {off, GraphMethodSource::Flag}, "without its code graph (--graph=off)"},
        {off, code, code, {off, GraphMethodSource::Flag}, "without its code graph (--graph=off)"},
        {off, code, off, {off, GraphMethodSource::Flag}, "without its code graph (--graph=off)"},
        {off, off, code, {off, GraphMethodSource::Flag}, "without its code graph (--graph=off)"},
        {off, off, off, {off, GraphMethodSource::Flag}, "without its code graph (--graph=off)"},
    };
    for (const Row& row : table) {
        INFO("flag " << (row.flag ? apogee::harness::to_string(*row.flag) : "-") << ", config "
                     << (row.config ? apogee::harness::to_string(*row.config) : "-")
                     << ", built-in " << apogee::harness::to_string(row.built_in));
        const GraphMethod method = resolve_graph_method(row.flag, row.config, row.built_in);
        CHECK(method == row.expected);
        CHECK(graph_method_note(method) == row.note);
    }

    // The class asks the one function, with its own built-in and the config.
    Fixture fixture{true, "attachments:\n  graph: off\n"};
    ChatAttachments::Hooks hooks = fixture.hooks();
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), hooks};
    CHECK(attached.graph_method() == GraphMethod{off, GraphMethodSource::Config});
    CHECK(attached.graph_method(code) == GraphMethod{code, GraphMethodSource::Flag});
    Fixture plain;
    hooks = plain.hooks();
    hooks.built_in_graph = off;
    ChatAttachments one_shot{*plain.harness, plain.session, ChatAttachments::index_for("chat-1"),
                             hooks};
    CHECK(one_shot.graph_method() == GraphMethod{off, GraphMethodSource::BuiltIn});
}

TEST_CASE("/attach reads its path first, then its flags, and refuses the rest by name",
          "[commands][attachments][options]") {
    using apogee::commands::AttachArgument;
    using apogee::commands::parse_attach_argument;
    using apogee::harness::AttachmentGraphMethod;

    struct Row {
        std::string_view argument;
        std::string spec;
        std::optional<AttachmentGraphMethod> graph;
        std::string error;
    };

    const std::string shape{apogee::commands::kAttachShape};
    const std::vector<Row> table{
        // The path alone, as ever -- an unquoted one with a space included.
        {"src", "src", {}, ""},
        {"my notes/plan.md", "my notes/plan.md", {}, ""},
        {"\"my notes/plan.md\"", "my notes/plan.md", {}, ""},
        {"\"my notes/", "my notes/", {}, ""},  // completion's open folder quote
        {"src/**/*.cpp", "src/**/*.cpp", {}, ""},
        {"", "", {}, ""},
        // Flags after it, both spellings, any spacing.
        {"src --graph=off", "src", AttachmentGraphMethod::Off, ""},
        {"src --graph off", "src", AttachmentGraphMethod::Off, ""},
        {"src\t--graph=code", "src", AttachmentGraphMethod::Code, ""},
        {"src   --graph   code", "src", AttachmentGraphMethod::Code, ""},
        {"my notes --graph=off", "my notes", AttachmentGraphMethod::Off, ""},
        {"\"my notes\" --graph=off", "my notes", AttachmentGraphMethod::Off, ""},
        {"\"my notes/ --graph=off", "my notes/", AttachmentGraphMethod::Off, ""},
        {"\"has --dashes\" --graph=code", "has --dashes", AttachmentGraphMethod::Code, ""},
        // A `--` inside a word is the path's.
        {"a--b", "a--b", {}, ""},
        {"notes-2024 --graph=off", "notes-2024", AttachmentGraphMethod::Off, ""},
        // Refused, each naming the shape or the set.
        {"--graph=off src", "", {}, "the path comes first -- " + shape},
        {"--graph=off", "", {}, "the path comes first -- " + shape},
        {"src --depth=2", "", {}, "unknown flag '--depth' -- " + shape},
        {"src --", "", {}, "unknown flag '--' -- " + shape},
        {"src --graph=tree", "", {}, "--graph: unknown value 'tree' (accepted: code, off)"},
        {"src --graph=", "", {}, "--graph: unknown value '' (accepted: code, off)"},
        {"src --graph", "", {}, "--graph: unknown value '' (accepted: code, off)"},
        {"src --graph=Off", "", {}, "--graph: unknown value 'Off' (accepted: code, off)"},
        {"src --graph=off --graph=code", "", {}, "--graph is given twice -- " + shape},
        {"src --graph=off extra", "", {}, "'extra' after the path is not a flag -- " + shape},
        {"src --graph off extra", "", {}, "'extra' after the path is not a flag -- " + shape},
        {"\"a b\" extra", "", {}, "'extra' after the path is not a flag -- " + shape},
        {"\"a b\"c --graph=off",
         "",
         {},
         "nothing may follow the path's closing quote but a space -- " + shape},
    };
    for (const Row& row : table) {
        INFO("/attach " << row.argument);
        const AttachArgument read = parse_attach_argument(row.argument);
        CHECK(read.spec == row.spec);
        CHECK(read.graph == row.graph);
        CHECK(read.error == row.error);
    }
}

TEST_CASE("a folder of code attached with --graph=off is chunks alone; with code, graphed then",
          "[commands][attachments][options][graph]") {
    using apogee::harness::AttachmentGraphMethod;
    Fixture fixture;
    copy_code(fixture, "python", "app");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    // Off: the chunks as ever, nothing of a graph built, kept or said beyond
    // the method the attach line names.
    REQUIRE(
        attached.attach("app", fixture.work, attached.graph_method(AttachmentGraphMethod::Off)));
    attached.settle();
    CHECK(fixture.heard("attaching app (4 files, "));
    CHECK(fixture.heard(") -- without its code graph (--graph=off)"));
    CHECK(fixture.heard("attached app: 4 files"));
    CHECK_FALSE(said_graph(fixture));
    CHECK(chat_members().empty());
    CHECK_FALSE(fixture.session.attachments[0].graph.has_value());
    CHECK_FALSE(attached.graph_scope().has_value());
    REQUIRE(attached.describe().size() == 1);
    CHECK(attached.describe()[0].find("graph") == std::string::npos);

    // The same folder with code: built then, as a direct build would.
    fixture.said.clear();
    REQUIRE(
        attached.attach("app", fixture.work, attached.graph_method(AttachmentGraphMethod::Code)));
    attached.settle();
    CHECK(fixture.heard(") -- with its code graph (--graph=code)"));
    CHECK(said_graph(fixture));
    CHECK(chat_members() == std::vector<std::string>{"app"});
    CHECK(chat_dump() == direct_dump(fixture, {fixture.work / "app"}));
    REQUIRE(fixture.session.attachments[0].graph.has_value());
    CHECK(attached.graph_scope().has_value());

    // And off again forgets it -- honest, and silent about graphs.
    fixture.said.clear();
    REQUIRE(
        attached.attach("app", fixture.work, attached.graph_method(AttachmentGraphMethod::Off)));
    attached.settle();
    CHECK_FALSE(said_graph(fixture));
    CHECK(chat_members().empty());
    CHECK(chat_dump().empty());
}

TEST_CASE("the config's graph: off makes a bare attach chunks only; --graph=code overrides once",
          "[commands][attachments][options][graph]") {
    using apogee::harness::AttachmentGraphMethod;
    Fixture fixture{true, "attachments: { graph: off }\n"};
    copy_code(fixture, "python", "app");
    copy_code(fixture, "go", "svc");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    // Bare: the config's method, and the line says so.
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(fixture.heard(") -- without its code graph (attachments.graph: off in the config)"));
    CHECK_FALSE(said_graph(fixture));
    CHECK(chat_members().empty());

    // One attach overrides it, and says so; the next bare one is the
    // config's again.
    fixture.said.clear();
    REQUIRE(
        attached.attach("svc", fixture.work, attached.graph_method(AttachmentGraphMethod::Code)));
    attached.settle();
    CHECK(fixture.heard(") -- with its code graph (--graph=code)"));
    CHECK(said_graph(fixture));
    CHECK(chat_members() == std::vector<std::string>{"svc"});
    fixture.said.clear();
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK(fixture.heard("(attachments.graph: off in the config)"));
    CHECK(chat_members() == std::vector<std::string>{"svc"});
}

TEST_CASE("the method is named only where it matters: a folder of code",
          "[commands][attachments][options]") {
    using apogee::harness::AttachmentGraphMethod;
    Fixture fixture;
    fixture.write("docs/guide.md", "the guide\n");
    fixture.write("docs/faq.md", "the answers\n");
    fixture.write("notes.md", "a note\n");
    copy_code(fixture, "python", "app");
    ChatAttachments attached{*fixture.harness, fixture.session,
                             ChatAttachments::index_for("chat-1"), fixture.hooks()};
    const auto with = [&attached](AttachmentGraphMethod method) {
        return attached.graph_method(method);
    };
    // A folder with no code, one file and a glob: no method changes them,
    // so none is named -- and no graph is built, whatever was asked.
    REQUIRE(attached.attach("docs", fixture.work, with(AttachmentGraphMethod::Code)));
    REQUIRE(attached.attach("notes.md", fixture.work, with(AttachmentGraphMethod::Off)));
    REQUIRE(attached.attach("app/*.py", fixture.work, with(AttachmentGraphMethod::Code)));
    attached.settle();
    CHECK_FALSE(fixture.heard("code graph"));
    CHECK_FALSE(said_graph(fixture));
    CHECK(chat_members().empty());
    // The built-in's own method is never named.
    REQUIRE(attached.attach("app", fixture.work));
    attached.settle();
    CHECK_FALSE(fixture.heard("code graph"));
    CHECK(said_graph(fixture));
}

TEST_CASE("an @ mention stays a bare path: flags typed after it are prose",
          "[commands][attachments][options]") {
    // 27p adds no mention syntax: the words after a mention are the
    // message's, and the message is never changed.
    const std::string message = "look at @app --graph=off and @\"my notes\" --graph code";
    CHECK(mentioned_paths(message) == std::vector<std::string>{"app", "my notes"});
    CHECK(message == "look at @app --graph=off and @\"my notes\" --graph code");
}
