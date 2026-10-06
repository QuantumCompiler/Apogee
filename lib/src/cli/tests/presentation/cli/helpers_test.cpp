#include "cli/helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "agentloop/loop.h"
#include "backends/factory.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "embedstore/store.h"
#include "harness/roles.h"
#include "support/env_guard.h"
#include "tools/toolsets.h"

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

TEST_CASE("a helper runs on the conversation's backend, but only a named utility summarises",
          "[commands][helpers]") {
    // 26b: an unset helper runs on whatever the chat is on; a chore that only
    // pays when a different model does it asks for a NAMED utility model.
    apogee::harness::Config config;
    config.models.default_backend = "big";
    CHECK(apogee::commands::helper_backend(config, apogee::harness::ModelRole::Utility, "chat") ==
          "chat");
    CHECK(apogee::commands::helper_backend(config, apogee::harness::ModelRole::Utility, "") ==
          "big");
    CHECK(apogee::commands::named_utility(config).empty());

    config.models.default_utility = "small";
    CHECK(apogee::commands::helper_backend(config, apogee::harness::ModelRole::Utility, "chat") ==
          "small");
    CHECK(apogee::commands::named_utility(config) == "small");
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
using apogee::commands::describe_attachment_retrieval;
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

TEST_CASE(
    "retrieve_for_collection carries the collection's graph knobs from the config into "
    "the turn",
    "[commands][helpers][rag][graph]") {
    const apogee::testing::TempDir home{"helpers-graph-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    {
        apogee::embedstore::Store store{home.path() / "embeddings" / "notes.db"};
        store.replace_source("notes.md", {"the zarquon protocol requires seventeen widgets",
                                          "an unrelated second chunk"});
        const auto chunks = store.chunks_by_source("notes.md");
        const std::int64_t zarquon = store.upsert_node("Zarquon", "concept", "a protocol").id;
        const std::int64_t factory =
            store.upsert_node("Widget Factory", "organization", "makes the widgets").id;
        (void)store.add_mention(zarquon, chunks.front().id);
        (void)store.add_mention(factory, chunks.back().id);
        store.upsert_edge(zarquon, factory, "is supplied by", "");
    }
    apogee::harness::Config config;
    apogee::harness::EmbeddingConfig entry;
    entry.graph.enabled = true;
    entry.graph.max_entities = 1;
    config.embeddings.emplace("notes", entry);
    const apogee::harness::Harness harness{config};

    const apogee::agentloop::RagResult expanded = apogee::commands::retrieve_for_collection(
        harness, config, "notes", "zarquon protocol", 4, "", "", {});
    CHECK(expanded.chunks == 1);
    CHECK(expanded.graph_entities == 1);  // the cap travelled too
    CHECK(expanded.prefix.front().content.plain_text().find("[Knowledge graph: notes]") !=
          std::string::npos);

    config.embeddings.at("notes").graph.enabled = false;
    const apogee::agentloop::RagResult plain = apogee::commands::retrieve_for_collection(
        harness, config, "notes", "zarquon protocol", 4, "", "", {});
    CHECK(plain.chunks == 1);
    CHECK(plain.graph_entities == 0);
}

TEST_CASE("one renderer: each retriever's strength, raw score kept, and the floor said",
          "[commands][helpers][rag][floor]") {
    using apogee::agentloop::MatchStrength;
    const auto with = [](std::string retriever, double best, MatchStrength strength,
                         std::int64_t chunks) {
        apogee::agentloop::RagResult result;
        result.retriever = std::move(retriever);
        result.best_score = best;
        result.top_score = best;
        result.strength = std::move(strength);
        result.chunks = chunks;
        return result;
    };
    // A hybrid turn first in both lists: a strong match, not "top 0.032".
    const auto hybrid =
        with("hybrid", 2.0 / 61.0, {.band = "strong", .measure = "100% of RRF's ceiling"}, 2);
    CHECK(describe_attachment_retrieval(hybrid) ==
          "2 excerpts from the attachments, strong match (0.032 [hybrid], 100% of RRF's "
          "ceiling)");
    const auto cosine = with("vector", 0.452, {.band = "fair"}, 1);
    CHECK(describe_attachment_retrieval(cosine) ==
          "1 excerpt from the attachments, fair match (0.452 [vector])");
    // A question of stop words: no band to read, the raw facts alone.
    const auto unread = with("lexical", 0.950, {}, 1);
    CHECK(describe_attachment_retrieval(unread) ==
          "1 excerpt from the attachments (0.950 [lexical])");
    // Floored: nothing injected, and said -- never a quiet absence.
    const auto floored =
        with("lexical", 0.950,
             {.band = "weak", .floored = true, .measure = "none of the question's words"}, 0);
    CHECK(describe_attachment_retrieval(floored) ==
          "nothing relevant in the attachments -- the best match is under the floor (0.950 "
          "[lexical], none of the question's words)");
    const RagChoice notes{.collection = "notes", .source = RagSource::Config};
    CHECK(describe_retrieval(notes, floored) ==
          "nothing relevant in 'notes' -- the best match is under the floor (0.950 [lexical], none "
          "of the question's words) (auto_rag)");
}

TEST_CASE("an attachment turn's line counts the chat's graph entities, as a collection's does",
          "[commands][helpers][rag][attachments][graph]") {
    apogee::agentloop::RagResult result;
    result.chunks = 2;
    result.best_score = 0.869;
    result.top_score = 0.869;
    result.retriever = "lexical";
    result.strength = {.band = "strong", .floored = false, .measure = "2 of 2 question words"};
    result.graph_entities = 3;
    CHECK(describe_attachment_retrieval(result) ==
          "2 excerpts from the attachments, strong match (0.869 [lexical], 2 of 2 question "
          "words) +3 graph entities");
    // The section alone, its excerpts cut by the budget: still counted.
    result.chunks = 0;
    result.notes = {"0 of 2 excerpts fit the context budget"};
    CHECK(describe_attachment_retrieval(result) ==
          "nothing in the attachments matched [lexical] +3 graph entities -- 0 of 2 excerpts "
          "fit the context budget");
    // None injected, none said -- a chunk-only chat's line is as it was.
    result.chunks = 2;
    result.notes.clear();
    result.graph_entities = 0;
    CHECK(describe_attachment_retrieval(result) ==
          "2 excerpts from the attachments, strong match (0.869 [lexical], 2 of 2 question "
          "words)");
}

TEST_CASE("the retrieval line names chunks, score, retriever, and its origin",
          "[commands][helpers][rag]") {
    apogee::agentloop::RagResult result;
    result.chunks = 3;
    result.top_score = 0.869;
    result.best_score = 0.869;
    result.retriever = "lexical";
    result.strength = {.band = "strong", .floored = false, .measure = "3 of 4 question words"};

    const RagChoice from_flag{.collection = "notes", .source = RagSource::Flag};
    const std::string flagged = describe_retrieval(from_flag, result);
    // How strong first, the raw score and its retriever beside it (26s).
    CHECK(flagged ==
          "3 chunk(s) from 'notes', strong match (0.869 [lexical], 3 of 4 question words)");
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

    // The graph's contribution is always named, with or without chunks --
    // entities injected beside the chunks are context the user never saw
    // retrieved.
    apogee::agentloop::RagResult expanded = result;
    expanded.graph_entities = 4;
    CHECK(describe_retrieval(from_flag, expanded).find("question words) +4 graph entities") !=
          std::string::npos);
    apogee::agentloop::RagResult graph_only;
    graph_only.retriever = "lexical";
    graph_only.graph_entities = 2;
    CHECK(describe_retrieval(from_flag, graph_only).find("no matching context") !=
          std::string::npos);
    CHECK(describe_retrieval(from_flag, graph_only).find("+2 graph entities") != std::string::npos);
    CHECK(describe_retrieval(from_flag, result).find("graph entities") == std::string::npos);

    apogee::agentloop::RagResult broken;
    broken.error = "no collection at /x";
    CHECK(describe_retrieval(from_config, broken).find("retrieval unavailable") !=
          std::string::npos);
    CHECK(describe_retrieval(from_config, broken).find("(auto_rag)") != std::string::npos);
}

namespace {

/// A config whose suite pins `helper` to the fs and git toolsets (27d).
apogee::harness::Config pinned_config(bool active) {
    apogee::harness::Config config = apogee::harness::parse_config(R"(
models:
  default: root
backends:
  root:
    type: mock
  helper:
    type: mock
suites:
  research:
    members:
      chat:
        backend: helper
        toolset: [fs, git]
)",
                                                                   "<test>");
    config.models.default_suite = active ? "research" : "";
    return config;
}

class QuietReporter final : public apogee::agentloop::Reporter {};

}  // namespace

TEST_CASE("a suite's toolset pin narrows the offer to its toolsets, and nothing else does",
          "[commands][helpers][suites]") {
    using apogee::commands::apply_toolset;
    using apogee::commands::pin_toolset;
    const apogee::harness::Config active = pinned_config(true);
    const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &active});
    REQUIRE(registry.find("fetch_url") != nullptr);
    REQUIRE(registry.find("run_command") != nullptr);

    const apogee::agent::ToolRegistry pinned = pin_toolset(registry, active, "helper");
    REQUIRE_FALSE(pinned.empty());
    for (const std::string& name : pinned.names()) {
        INFO(name);
        const std::string toolset = apogee::tools::toolset_of(name);
        CHECK((toolset == "fs" || toolset == "git"));
    }
    CHECK(pinned.find("read_file") != nullptr);
    CHECK(pinned.find("git_diff") != nullptr);
    CHECK(pinned.find("run_command") == nullptr);
    CHECK(pinned.find("fetch_url") == nullptr);
    // The environment note rides along: a narrowed offer still knows the date.
    CHECK_FALSE(pinned.environment().empty());

    // A backend the suite does not pin, or no suite active: the whole offer.
    CHECK(pin_toolset(registry, active, "root").names() == registry.names());
    CHECK(pin_toolset(registry, pinned_config(false), "helper").names() == registry.names());
    // An empty pin offers nothing.
    CHECK(apply_toolset(registry, {}).empty());
}

TEST_CASE("a pinned member's request carries exactly its toolset", "[commands][helpers][suites]") {
    // 27d's guardrail at the wire: what the provider is handed is the pinned
    // offer, recorded by the provider itself.
    const apogee::harness::Config config = pinned_config(true);
    apogee::harness::Harness harness{config};
    std::vector<std::vector<std::string>> offered;
    apogee::backends::MockProvider::Options mock;
    mock.backend_name = "helper";
    mock.turns = {apogee::backends::MockTurn{.text = "done"}};
    mock.on_request = [&offered](const apogee::harness::ChatRequest& request) {
        std::vector<std::string> names;
        for (const apogee::harness::Tool& tool : request.tools) {
            names.push_back(tool.name);
        }
        offered.push_back(std::move(names));
    };
    harness.register_provider("helper",
                              std::make_shared<apogee::backends::MockProvider>(std::move(mock)));
    harness.use_default_router();

    const apogee::agent::ToolRegistry registry = apogee::commands::make_built_in_tools(
        apogee::commands::BuiltInToolOptions{.config = &config});
    const apogee::agent::ToolRegistry pinned =
        apogee::commands::pin_toolset(registry, config, "helper");
    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("list the files")};
    apogee::agentloop::Options options;
    options.model = "helper";
    options.tools = &pinned;
    QuietReporter reporter;
    (void)apogee::agentloop::run(harness, history, options, reporter);

    REQUIRE(offered.size() == 1);
    std::vector<std::string> expected = pinned.names();
    std::vector<std::string> sent = offered.front();
    std::ranges::sort(expected);
    std::ranges::sort(sent);
    CHECK(sent == expected);
    CHECK(std::ranges::find(sent, "run_command") == sent.end());
}

TEST_CASE("activating a suite rebuilds the backends it re-pins and only those",
          "[commands][helpers][suites]") {
    // 27d: `/suite` and `chat --suite` move the window a member's backend
    // runs at; one provider holds one window, so exactly the re-pinned ones
    // are built again -- the rest keep their loaded models.
    apogee::harness::Config config = apogee::harness::parse_config(R"(
models:
  default: root
backends:
  root:
    type: mock
  helper:
    type: mock
  other:
    type: mock
suites:
  small:
    members:
      utility:
        backend: helper
        context_size: 2048
  roomy:
    members:
      utility:
        backend: helper
        context_size: 8192
      chat: root
)",
                                                                   "<test>");
    apogee::harness::Harness harness{config};
    (void)apogee::backends::build_providers(harness);
    const auto identity = [&harness](std::string_view name) { return &harness.provider(name); };
    const auto* root = identity("root");
    const auto* helper = identity("helper");
    const auto* other = identity("other");

    CHECK(apogee::commands::activate_suite(harness, config, "small", {}).empty());
    CHECK(config.models.default_suite == "small");
    CHECK(harness.config().models.default_suite == "small");
    CHECK(harness.context_window_for_model("helper") == 2048);
    CHECK(identity("helper") != helper);
    CHECK(identity("root") == root);
    CHECK(identity("other") == other);

    // A switch that moves the pin rebuilds it again; one that names a backend
    // with no pin (root, the chat member) rebuilds nothing for it.
    helper = identity("helper");
    CHECK(apogee::commands::activate_suite(harness, config, "roomy", {}).empty());
    CHECK(harness.context_window_for_model("helper") == 8192);
    CHECK(identity("helper") != helper);
    CHECK(identity("root") == root);

    // Off: the entry's own window again, rebuilt to it.
    helper = identity("helper");
    CHECK(apogee::commands::activate_suite(harness, config, "", {}).empty());
    CHECK(config.models.default_suite.empty());
    CHECK(identity("helper") != helper);
    CHECK(identity("other") == other);
    // And a switch that moves nothing rebuilds nothing.
    helper = identity("helper");
    CHECK(apogee::commands::activate_suite(harness, config, "", {}).empty());
    CHECK(identity("helper") == helper);

    // 27e: with a session holding its suite, the one switch moves the hold
    // too -- members that leave are let go, the new ones held -- and the
    // hold ends with the session.
    {
        const apogee::harness::SessionHold hold{harness};
        CHECK(harness.held().empty());
        CHECK(apogee::commands::activate_suite(harness, config, "roomy", {}).empty());
        CHECK(harness.held() == std::vector<std::string>{"helper", "root"});
        CHECK(apogee::commands::activate_suite(harness, config, "small", {}).empty());
        CHECK(harness.held() == std::vector<std::string>{"helper"});
    }
    CHECK(harness.held().empty());
}
