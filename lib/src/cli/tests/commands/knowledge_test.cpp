#include "commands/knowledge.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/types.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "logger/session.h"
#include "support/env_guard.h"

/// `apogee knowledge capture` and chat's `/capture`, in process, on the
/// scripted mock as the clerk: what is stored, what is archived, what is
/// registered, what a dry run leaves behind (nothing), and the refusals.
namespace {

using apogee::knowledge::Record;
using apogee::knowledge::Store;

constexpr std::string_view kRecord =
    R"({"intent": "We dropped the cancel button because testers kept mistaking it for back.", "decision": "Remove the cancel button.", "status": "shipped", "discipline": "ux", "downstream_link": "", "provenance": {"source": "meeting", "attribution": "Ada Lovelace"}})";

std::string script_of(const std::vector<std::string>& texts) {
    nlohmann::json turns = nlohmann::json::array();
    for (const std::string& text : texts) {
        turns.push_back({{"text", text}});
    }
    return nlohmann::json{{"turns", turns}}.dump();
}

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// What a dry run must not change.
struct Footprint {
    bool collection = false;
    std::size_t raw_files = 0;
    std::string config;

    bool operator==(const Footprint&) const = default;
};

struct Fixture {
    apogee::testing::TempDir home{"knowledge-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    explicit Fixture(std::string_view extra_config = {}, bool embedder = false) {
        write(home.path() / "clerk.json", script_of({std::string{kRecord}}));
        write(home.path() / "prose.json", script_of({"I cannot produce JSON.", "still prose"}));
        write(home.path() / "chatty.json",
              script_of({"hi there", "A title", std::string{kRecord}}));
        std::string config = "models:\n  default: clerk\n";
        if (embedder) {
            config += "  default_embedding: embed\n";
        }
        config += "backends:\n";
        config +=
            "  clerk:\n    type: mock\n    model_path: " + (home.path() / "clerk.json").string() +
            "\n";
        config +=
            "  prose:\n    type: mock\n    model_path: " + (home.path() / "prose.json").string() +
            "\n";
        config +=
            "  chatty:\n    type: mock\n    model_path: " + (home.path() / "chatty.json").string() +
            "\n";
        config += "  vendor:\n    type: claude-cli\n";
        if (embedder) {
            config += "  embed:\n    type: mock\n    embedding_model: mock-space\n";
        }
        config += extra_config;
        write(config_path, config);
    }

    /// Runs the CLI with stdout, stderr and stdin redirected.
    int run(const std::vector<std::string>& args, std::string* out = nullptr,
            std::string* err = nullptr, std::string_view input = {}) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{std::string{input}};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            std::cin.rdbuf(old_in);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        if (out != nullptr) {
            *out = captured_out.str();
        }
        if (err != nullptr) {
            *err = captured_err.str();
        }
        return code;
    }

    [[nodiscard]] std::filesystem::path collection(std::string_view db = "knowledge") const {
        return home.path() / "embeddings" / (std::string{db} + ".db");
    }

    [[nodiscard]] std::filesystem::path raw_dir() const {
        return home.path() / "knowledge" / "raw";
    }

    [[nodiscard]] Store store(std::string_view db = "knowledge") const {
        return Store{collection(db), raw_dir()};
    }

    [[nodiscard]] Footprint footprint() const {
        Footprint out;
        std::error_code code;
        out.collection = std::filesystem::exists(collection(), code);
        if (std::filesystem::is_directory(raw_dir(), code)) {
            for (const auto& entry : std::filesystem::directory_iterator(raw_dir(), code)) {
                (void)entry;
                ++out.raw_files;
            }
        }
        out.config = bytes(config_path);
        return out;
    }
};

}  // namespace

TEST_CASE(
    "capture stores one record: a chunk keyed by its id, the conversation archived, the "
    "collection registered, and the name nowhere the index can reach",
    "[commands][knowledge][capture]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"knowledge", "capture", "Ada: drop it? Bob: yes, testers were confused"},
                        &out, &err) == 0);
    INFO(err);
    CHECK(out.starts_with("Captured kr-"));
    CHECK(out.find("Stored in \"knowledge\" (retriever: lexical)") != std::string::npos);
    CHECK(out.find("registered 'knowledge' in") != std::string::npos);
    CHECK(out.find("Attribution: Ada Lovelace") != std::string::npos);

    const Store store = fixture.store();
    const std::vector<Record> records = store.list();
    REQUIRE(records.size() == 1);
    const Record& record = records.front();
    CHECK(record.id.starts_with("kr-"));
    CHECK_FALSE(record.timestamp.empty());
    CHECK(record.intent.starts_with("We dropped the cancel button"));
    CHECK(record.status == "shipped");
    CHECK(record.discipline == "ux");
    CHECK(record.provenance.source == "meeting");
    CHECK(record.provenance.attribution == "Ada Lovelace");
    CHECK(record.raw_ref == (fixture.raw_dir() / (record.id + ".md")).string());
    CHECK(bytes(record.raw_ref) == "Ada: drop it? Bob: yes, testers were confused");
    CHECK(store.chunks().chunk_count() == 1);
    CHECK(store.chunks().search("cancel button testers", 5).size() == 1);
    CHECK(store.chunks().search("Lovelace", 5).empty());
    CHECK_FALSE(store.has_vectors());

    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    REQUIRE(config.find_embedding("knowledge") != nullptr);
    CHECK(config.find_embedding("knowledge")->description.find("apogee knowledge") !=
          std::string::npos);
    // The second capture finds the entry and says nothing about registering.
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "more"}, &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["registered"] == false);
    CHECK(fixture.store().list().size() == 2);
}

TEST_CASE("every flag overrides the clerk for its field, and --supersedes flips the earlier record",
          "[commands][knowledge][overrides]") {
    const Fixture fixture;
    std::string out;
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "first"}, &out) == 0);
    const std::string first = nlohmann::json::parse(out)["record"]["id"].get<std::string>();

    REQUIRE(fixture.run(
                {"knowledge", "capture", "--json", "--status", "abandoned", "--discipline", "eng",
                 "--source", "review", "--link", "PROJ-42", "--supersedes", first, "second"},
                &out) == 0);
    const nlohmann::json second = nlohmann::json::parse(out);
    CHECK(second["draft"] == false);
    CHECK(second["record"]["status"] == "rejected");
    CHECK(second["record"]["discipline"] == "eng");
    CHECK(second["record"]["provenance"]["source"] == "review");
    CHECK(second["record"]["downstream_link"] == "PROJ-42");
    CHECK(second["record"]["supersedes"] == first);
    CHECK(second["record"]["provenance"]["attribution"] == "Ada Lovelace");
    CHECK_FALSE(second.contains("notes"));

    const Store store = fixture.store();
    const std::optional<Record> old = store.get(first);
    REQUIRE(old.has_value());
    CHECK(old->status == "superseded");

    // A missing target is a note, never a lost record.
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "--supersedes", "kr-nope", "third"},
                        &out) == 0);
    CHECK(nlohmann::json::parse(out)["notes"][0].get<std::string>().find("no such record") !=
          std::string::npos);
    CHECK(store.list().size() == 3);
}

TEST_CASE("a dry run prints the record and the store decision and leaves zero footprint",
          "[commands][knowledge][dry-run]") {
    const Fixture fixture;
    const Footprint before = fixture.footprint();
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"knowledge", "capture", "--dry-run", "--status", "rejected", "raw"}, &out,
                        &err) == 0);
    CHECK(out.starts_with("Draft (not stored)\n"));
    CHECK(out.find("Status:      rejected") != std::string::npos);
    CHECK(out.find("Would store in \"knowledge\" (retriever: lexical)") != std::string::npos);
    CHECK(out.find("Re-run without --dry-run") != std::string::npos);
    CHECK(out.find("Captured") == std::string::npos);
    CHECK(fixture.footprint() == before);
    CHECK_FALSE(before.collection);
    CHECK(before.raw_files == 0);

    REQUIRE(fixture.run({"knowledge", "capture", "--dry-run", "--json", "raw"}, &out) == 0);
    const nlohmann::json json = nlohmann::json::parse(out);
    CHECK(json["draft"] == true);
    CHECK(json["db"] == "knowledge");
    CHECK(json["retriever"] == "lexical");
    CHECK(json["record"]["id"] == "");
    CHECK(json["record"]["timestamp"] == "");
    CHECK_FALSE(json["record"].contains("raw_ref"));
    CHECK(json["record"]["intent"].get<std::string>().starts_with("We dropped"));
    CHECK_FALSE(json.contains("registered"));
    CHECK(fixture.footprint() == before);

    // The decision a dry run prints is the one a real run makes.
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "raw"}, &out) == 0);
    CHECK(nlohmann::json::parse(out)["retriever"] == "lexical");
    CHECK(nlohmann::json::parse(out)["db"] == "knowledge");
}

TEST_CASE(
    "no conversation, a bad status, a vendor CLI, and a clerk that never conforms are "
    "refused, and nothing is stored",
    "[commands][knowledge][refusals]") {
    const Fixture fixture;
    std::string err;
    CHECK(fixture.run({"knowledge", "capture"}, nullptr, &err) == 1);
    CHECK(err.find("no conversation provided") != std::string::npos);
    CHECK(fixture.run({"knowledge", "capture", "--status", "maybe", "x"}, nullptr, &err) != 0);
    CHECK(fixture.run({"knowledge", "capture", "--retriever", "sideways", "x"}, nullptr, &err) !=
          0);
    CHECK(fixture.run({"knowledge", "capture", "--db", "../x", "x"}, nullptr, &err) == 1);
    CHECK(fixture.run({"knowledge", "capture", "-m", "vendor", "x"}, nullptr, &err) == 1);
    CHECK(err.find("vendor-CLI backend") != std::string::npos);
    CHECK(err.find("claude-cli") != std::string::npos);
    CHECK(fixture.run({"knowledge", "capture", "-m", "nope", "x"}, nullptr, &err) == 1);
    CHECK(err.find("no backend named 'nope'") != std::string::npos);
    CHECK(fixture.run({"knowledge", "capture", "-m", "prose", "x"}, nullptr, &err) == 2);
    CHECK(err.find("did not return a record") != std::string::npos);
    CHECK(err.find("after 2 attempts") != std::string::npos);
    CHECK(fixture.run({"knowledge", "capture", "--input", "/nonexistent/file.md"}, nullptr, &err) ==
          1);
    CHECK(fixture.run({"knowledge", "capture", "--from-chat", "no-such-chat"}, nullptr, &err) == 1);
    const Footprint after = fixture.footprint();
    CHECK_FALSE(after.collection);
    CHECK(after.raw_files == 0);
    // The alias is the command.
    CHECK(fixture.run({"kn", "capture", "--dry-run", "x"}) == 0);
    CHECK(fixture.run({"kn", "capture", "--dry-run", "--input",
                       (fixture.home.path() / "clerk.json").string()}) == 0);
}

TEST_CASE("piped stdin is the conversation when nothing else names one",
          "[commands][knowledge][stdin]") {
    const Fixture fixture;
    std::string out;
    REQUIRE(fixture.run({"knowledge", "capture", "--json"}, &out, nullptr,
                        "piped: we chose it because\n") == 0);
    const Record record = fixture.store().list().front();
    CHECK(bytes(record.raw_ref) == "piped: we chose it because");
}

TEST_CASE(
    "capture --from-chat renders the saved session and produces the record capture would, "
    "with the source chat",
    "[commands][knowledge][from-chat]") {
    const Fixture fixture;
    apogee::logger::Session session;
    session.chat_id = "20260913-120000-abcd";
    session.backend = "clerk";
    session.messages = {
        apogee::harness::ChatMessage::system("terse"),
        apogee::harness::ChatMessage::user("drop the cancel button?"),
        apogee::harness::ChatMessage::assistant("yes, testers mistook it for back")};
    session.turns = 1;
    apogee::logger::save(session);
    const std::string transcript = apogee::logger::transcript_text(session.messages);
    REQUIRE(transcript ==
            "User: drop the cancel button?\n\nAssistant: yes, testers mistook it for back");

    std::string out;
    std::string err;
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "--from-chat", "20260913-120000-abcd"},
                        &out, &err) == 0);
    const nlohmann::json from_chat = nlohmann::json::parse(out)["record"];
    CHECK(from_chat["provenance"]["source"] == "chat");
    CHECK(bytes(from_chat["raw_ref"].get<std::string>()) == transcript);

    // The same transcript through the argument: the same record, modulo what
    // the store mints -- and the source the user did not override.
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "--source", "chat", transcript}, &out) ==
            0);
    nlohmann::json direct = nlohmann::json::parse(out)["record"];
    for (const char* minted : {"id", "timestamp", "raw_ref"}) {
        direct.erase(minted);
    }
    nlohmann::json chatted = from_chat;
    for (const char* minted : {"id", "timestamp", "raw_ref"}) {
        chatted.erase(minted);
    }
    CHECK(direct == chatted);
    // An explicit --source still wins over the implied one.
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "--source", "review", "--from-chat",
                         "20260913-120000-abcd"},
                        &out) == 0);
    CHECK(nlohmann::json::parse(out)["record"]["provenance"]["source"] == "review");
}

TEST_CASE(
    "capture --db names the collection, and an unmetered embedder vectorises the record under "
    "auto or --retriever vector but never under lexical",
    "[commands][knowledge][vector]") {
    const Fixture fixture{{}, /*embedder=*/true};
    std::string out;
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "--db", "team", "one"}, &out) == 0);
    CHECK(nlohmann::json::parse(out)["db"] == "team");
    CHECK(nlohmann::json::parse(out)["retriever"] == "vector");
    {
        const Store team = fixture.store("team");
        CHECK(team.has_vectors());
        CHECK(team.chunks().embedding_model().model == "mock-space");
        CHECK(team.chunks().stats().dimension == 8);
        CHECK(team.chunks().chunk_vector(*team.chunk_id(team.list().front().id)).size() == 8);
    }
    CHECK_FALSE(std::filesystem::exists(fixture.collection("knowledge")));
    CHECK(apogee::harness::load_config(fixture.config_path).find_embedding("team") != nullptr);

    REQUIRE(fixture.run(
                {"knowledge", "capture", "--json", "--db", "team", "--retriever", "lexical", "two"},
                &out) == 0);
    CHECK(nlohmann::json::parse(out)["retriever"] == "lexical");
    const Store team = fixture.store("team");
    REQUIRE(team.list().size() == 2);
    CHECK(team.chunks().stats().lexical_only == 1);
    REQUIRE(fixture.run({"knowledge", "capture", "--json", "--db", "team", "--retriever", "vector",
                         "three"},
                        &out) == 0);
    CHECK(nlohmann::json::parse(out)["retriever"] == "vector");

    // The dry run says vector too, and writes no vector.
    const Footprint before = fixture.footprint();
    REQUIRE(fixture.run({"knowledge", "capture", "--dry-run", "--json", "--db", "team", "x"},
                        &out) == 0);
    CHECK(nlohmann::json::parse(out)["retriever"] == "vector");
    CHECK(fixture.footprint() == before);
    CHECK(fixture.store("team").list().size() == 3);
}

TEST_CASE("an explicit vector ask with no embedder is refused, and warned about by a dry run",
          "[commands][knowledge][vector][refusal]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    CHECK(fixture.run({"knowledge", "capture", "--retriever", "vector", "x"}, &out, &err) == 1);
    CHECK(err.find("vector ingest cannot run") != std::string::npos);
    CHECK_FALSE(fixture.footprint().collection);
    REQUIRE(
        fixture.run({"knowledge", "capture", "--dry-run", "--json", "--retriever", "vector", "x"},
                    &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["warning"].get<std::string>().find(
              "vector ingest cannot run") != std::string::npos);
}

TEST_CASE(
    "chat's /capture distils the live conversation with the loaded model, and an argument "
    "is a status when it is one and a link otherwise",
    "[commands][knowledge][chat]") {
    const Fixture fixture;
    std::string err;
    REQUIRE(fixture.run({"chat", "-m", "chatty"}, nullptr, &err,
                        "should we drop the cancel button?\n/capture rejected\n/exit\n") == 0);
    INFO(err);
    CHECK(err.find("captured kr-") != std::string::npos);
    CHECK(err.find("[rejected]") != std::string::npos);
    {
        const Store store = fixture.store();
        const std::vector<Record> records = store.list();
        REQUIRE(records.size() == 1);
        CHECK(records.front().status == "rejected");
        CHECK(records.front().provenance.source == "chat");
        CHECK(records.front().provenance.attribution == "Ada Lovelace");
        CHECK(bytes(records.front().raw_ref) ==
              "User: should we drop the cancel button?\n\nAssistant: hi there");
    }
    REQUIRE(fixture.run({"chat", "-m", "chatty"}, nullptr, &err, "again?\n/capture PROJ-7\n") == 0);
    const std::vector<Record> records = fixture.store().list();
    REQUIRE(records.size() == 2);
    CHECK(records.front().downstream_link == "PROJ-7");
    CHECK(records.front().status == "shipped");

    // Nothing said yet: nothing to capture, and no model call.
    REQUIRE(fixture.run({"chat", "-m", "chatty"}, nullptr, &err, "/capture\n/exit\n") == 0);
    CHECK(err.find("nothing to capture yet") != std::string::npos);
    CHECK(fixture.store().list().size() == 2);
}

TEST_CASE("auto_capture distils the session on a clean exit, and only when switched on",
          "[commands][knowledge][chat][auto]") {
    const Fixture quiet;
    REQUIRE(quiet.run({"chat", "-m", "chatty"}, nullptr, nullptr, "hello\n") == 0);
    CHECK_FALSE(std::filesystem::exists(quiet.collection()));

    const Fixture eager{"knowledge:\n  auto_capture: true\n"};
    std::string err;
    REQUIRE(eager.run({"chat", "-m", "chatty"}, nullptr, &err, "hello\n") == 0);
    INFO(err);
    REQUIRE(eager.store().list().size() == 1);
    CHECK(eager.store().list().front().provenance.source == "chat");
    REQUIRE(eager.run({"chat", "-m", "chatty"}, nullptr, &err, "hello again\n/quit\n") == 0);
    CHECK(eager.store().list().size() == 2);
    // An empty session has nothing to distil, and says so rather than
    // calling the model.
    REQUIRE(eager.run({"chat", "-m", "chatty"}, nullptr, &err, "") == 0);
    CHECK(eager.store().list().size() == 2);
}
