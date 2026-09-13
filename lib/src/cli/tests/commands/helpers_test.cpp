#include "commands/helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "harness/config.h"
#include "support/env_guard.h"

using apogee::commands::base64_encode;
using apogee::commands::build_messages;
using apogee::commands::image_media_type;
using apogee::commands::load_image_part;
using apogee::commands::resolve_max_tokens;
using apogee::commands::resolve_system_prompt;
using apogee::commands::resolve_temperature;
using apogee::harness::ContentPart;
using apogee::harness::Role;
using apogee::testing::TempDir;

namespace {

apogee::harness::Config config_with_backend() {
    return apogee::harness::parse_config(R"(
backends:
  claude:
    type: mock
    temperature: 0.3
    max_tokens: 512
    system_prompt: "From config."
  bare:
    type: mock
)",
                                         "<test>");
}

}  // namespace

TEST_CASE("a flag beats the backend entry, which beats nothing", "[commands][helpers]") {
    const auto config = config_with_backend();

    // The flag is the most specific thing the user said, so it always wins.
    CHECK(resolve_temperature(0.9, config, "claude") == 0.9);
    CHECK(resolve_max_tokens(std::int64_t{100}, config, "claude") == 100);
    CHECK(resolve_system_prompt("From flag.", config, "claude") == "From flag.");

    // No flag: the backend entry's own value.
    CHECK(resolve_temperature(std::nullopt, config, "claude") == 0.3);
    CHECK(resolve_max_tokens(std::nullopt, config, "claude") == 512);
    CHECK(resolve_system_prompt("", config, "claude") == "From config.");

    // Neither: unset, NOT a hardcoded default -- an unset temperature must
    // leave the provider's own default alone rather than pin one here.
    CHECK_FALSE(resolve_temperature(std::nullopt, config, "bare").has_value());
    CHECK_FALSE(resolve_max_tokens(std::nullopt, config, "bare").has_value());
    CHECK(resolve_system_prompt("", config, "bare").empty());

    // An unknown backend resolves to unset rather than throwing.
    CHECK_FALSE(resolve_temperature(std::nullopt, config, "ghost").has_value());
    CHECK(resolve_system_prompt("", config, "ghost").empty());
}

TEST_CASE("base64 encodes with correct padding", "[commands][helpers]") {
    // The three residues are where a hand-rolled encoder goes wrong.
    CHECK(base64_encode("") == "");
    CHECK(base64_encode("f") == "Zg==");
    CHECK(base64_encode("fo") == "Zm8=");
    CHECK(base64_encode("foo") == "Zm9v");
    CHECK(base64_encode("foob") == "Zm9vYg==");
    CHECK(base64_encode("fooba") == "Zm9vYmE=");
    CHECK(base64_encode("foobar") == "Zm9vYmFy");

    // Bytes above 0x7F must not sign-extend -- a PNG is full of them.
    const std::string binary{"\x89\x50\x4E\x47", 4};
    CHECK(base64_encode(binary) == "iVBORw==");
}

TEST_CASE("image media types come from the extension, case-insensitively", "[commands][helpers]") {
    CHECK(image_media_type("a.png") == "image/png");
    CHECK(image_media_type("a.PNG") == "image/png");
    CHECK(image_media_type("a.jpg") == "image/jpeg");
    CHECK(image_media_type("a.jpeg") == "image/jpeg");
    CHECK(image_media_type("a.webp") == "image/webp");
    // Unrecognised is reported, never guessed: the wire format needs an
    // explicit type and sending the wrong one fails obscurely.
    CHECK(image_media_type("a.txt").empty());
    CHECK(image_media_type("a").empty());
}

TEST_CASE("an image file becomes a data-URI content part", "[commands][helpers]") {
    const TempDir dir{"image"};
    const std::filesystem::path path = dir.path() / "pic.png";
    {
        std::ofstream out(path, std::ios::binary);
        out << "foobar";
    }

    const ContentPart part = load_image_part(path);
    CHECK(part.kind == ContentPart::Kind::ImageUrl);
    CHECK(part.image_url == "data:image/png;base64,Zm9vYmFy");
}

TEST_CASE("a bad image path or type is a clear error", "[commands][helpers]") {
    const TempDir dir{"image-bad"};

    CHECK_THROWS_AS(load_image_part(dir.path() / "missing.png"), std::runtime_error);
    CHECK_THROWS_AS(load_image_part(dir.path() / "notes.txt"), std::runtime_error);

    const std::filesystem::path empty = dir.path() / "empty.png";
    {
        const std::ofstream out(empty, std::ios::binary);
    }
    CHECK_THROWS_AS(load_image_part(empty), std::runtime_error);
}

TEST_CASE("messages are built in a fixed order", "[commands][helpers]") {
    // System, then context, then the prompt. Context BEFORE the prompt because
    // a model weights the last message most, and the prompt is what it should
    // be answering -- not the reference material.
    const auto messages = build_messages("be brief", "reference material", "the question", {});

    REQUIRE(messages.size() == 3);
    CHECK(messages[0].role == Role::System);
    CHECK(messages[0].content.plain_text() == "be brief");
    CHECK(messages[1].content.plain_text() == "reference material");
    CHECK(messages[2].role == Role::User);
    CHECK(messages[2].content.plain_text() == "the question");
}

TEST_CASE("empty system and context produce no messages", "[commands][helpers]") {
    const auto messages = build_messages("", "", "just the prompt", {});
    REQUIRE(messages.size() == 1);
    CHECK(messages[0].role == Role::User);
}

TEST_CASE("attachments make the user turn multi-part, text first", "[commands][helpers]") {
    // Text first so the instruction is read before the images it refers to.
    const auto messages = build_messages("", "", "describe this",
                                         {ContentPart::from_image_url("data:image/png;base64,AA")});

    REQUIRE(messages.size() == 1);
    const auto& parts = messages.back().content.parts();
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].kind == ContentPart::Kind::Text);
    CHECK(parts[0].text == "describe this");
    CHECK(parts[1].kind == ContentPart::Kind::ImageUrl);
    CHECK(messages.back().content.is_rich());
}

TEST_CASE("an attachment with no prompt text still forms a valid turn", "[commands][helpers]") {
    const auto messages =
        build_messages("", "", "", {ContentPart::from_image_url("data:image/png;base64,AA")});
    REQUIRE(messages.size() == 1);
    REQUIRE(messages.back().content.parts().size() == 1);
    CHECK(messages.back().content.parts()[0].kind == ContentPart::Kind::ImageUrl);
}

// --- which collection a turn retrieves from -------------------------------------

namespace {

using apogee::commands::choose_rag_collection;
using apogee::commands::describe_retrieval;
using apogee::commands::RagChoice;
using apogee::commands::RagSource;

}  // namespace

TEST_CASE("the flag beats auto_rag, and an empty flag switches it off",
          "[commands][helpers][rag]") {
    // The whole contract of the shared decision, as a table. Every surface
    // calls this one function, so the precedence cannot differ between them.
    struct Row {
        bool flag_given;
        std::string_view flag_value;
        std::string_view auto_rag;
        std::string_view collection;
        RagSource source;
    };

    const Row rows[] = {
        // absent flag, no config: nothing
        {false, "", "", "", RagSource::None},
        // absent flag: the config decides, and says so
        {false, "", "notes", "notes", RagSource::Config},
        // a named flag wins over the config
        {true, "adrs", "notes", "adrs", RagSource::Flag},
        // an EMPTY flag is the off switch, not a fall-through to the config
        {true, "", "notes", "", RagSource::None},
        // a named flag with no config behind it
        {true, "adrs", "", "adrs", RagSource::Flag},
    };
    for (const Row& row : rows) {
        INFO("flag_given=" << row.flag_given << " flag='" << row.flag_value << "' auto_rag='"
                           << row.auto_rag << "'");
        const RagChoice choice =
            choose_rag_collection(row.flag_given, row.flag_value, row.auto_rag);
        CHECK(choice.collection == row.collection);
        CHECK(choice.source == row.source);
        CHECK(choice.active() == !row.collection.empty());
    }
}

TEST_CASE("the retrieval line names chunks, score, retriever, and its origin",
          "[commands][helpers][rag]") {
    apogee::agentloop::RagResult result;
    result.chunks = 3;
    result.top_score = 0.869;
    result.retriever = "lexical";

    const RagChoice from_flag{.collection = "notes", .source = RagSource::Flag};
    const std::string flagged = describe_retrieval(from_flag, result);
    CHECK(flagged.find("3 chunk(s)") != std::string::npos);
    CHECK(flagged.find("'notes'") != std::string::npos);
    CHECK(flagged.find("0.869") != std::string::npos);
    CHECK(flagged.find("[lexical]") != std::string::npos);
    CHECK(flagged.find("auto_rag") == std::string::npos);

    // Injection nobody typed a flag for is the one that must announce itself:
    // a user who does not know context was added cannot tell why an answer
    // went sideways.
    const RagChoice from_config{.collection = "notes", .source = RagSource::Config};
    const std::string automatic = describe_retrieval(from_config, result);
    CHECK(automatic.find("3 chunk(s)") != std::string::npos);
    CHECK(automatic.find("[lexical]") != std::string::npos);
    CHECK(automatic.find("(auto_rag)") != std::string::npos);

    // The same origin marker on the two non-injecting outcomes, so a user can
    // see that a key in their config is trying and failing.
    apogee::agentloop::RagResult nothing;
    nothing.retriever = "lexical";
    CHECK(describe_retrieval(from_config, nothing).find("no matching context") !=
          std::string::npos);
    CHECK(describe_retrieval(from_config, nothing).find("(auto_rag)") != std::string::npos);

    apogee::agentloop::RagResult broken;
    broken.error = "no collection at /x";
    CHECK(describe_retrieval(from_config, broken).find("retrieval unavailable") !=
          std::string::npos);
    CHECK(describe_retrieval(from_config, broken).find("(auto_rag)") != std::string::npos);
}
