#include "httpserver/admin_knowledge.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "backends/mock.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "events/bus.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/http_types.h"
#include "httpserver/jobs.h"
#include "httpserver/mux.h"
#include "knowledge/record.h"
#include "knowledge/refine.h"
#include "knowledge/store.h"
#include "support/env_guard.h"

/// The knowledge slice of the control plane: the capture twin on the
/// inference plane's own harness, the finished-record store, the honest
/// refusals, and the parity proof -- a record captured over HTTP is the
/// record the CLI captures from the same conversation.
namespace {

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::httpserver::AdminConfigContext;
using apogee::httpserver::Handler;
using apogee::httpserver::HandlerOptions;
using apogee::httpserver::HttpRequest;
using apogee::httpserver::HttpResponse;
using apogee::knowledge::Record;
using apogee::knowledge::Store;

constexpr std::string_view kRecord =
    R"({"intent": "We dropped the cancel button because testers kept mistaking it for back.", "decision": "Remove the cancel button.", "status": "shipped", "discipline": "ux", "downstream_link": "", "provenance": {"source": "meeting", "attribution": "Ada Lovelace"}})";

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

struct Fixture {
    apogee::testing::TempDir home{"admin-knowledge-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    apogee::harness::Config config;
    std::unique_ptr<apogee::harness::Harness> harness;
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<Handler> handler;

    std::shared_ptr<apogee::backends::MockEmbeddingProvider> embedder;

    explicit Fixture(std::vector<MockTurn> turns = {MockTurn{std::string{kRecord}}},
                     bool serve = true, bool with_embedder = false) {
        std::filesystem::create_directories(config_path.parent_path());
        // The CLI half of the parity proof builds ITS providers from this
        // file, so the mock entry carries the same script the in-memory
        // provider answers from.
        const std::filesystem::path script = home.path() / "clerk.json";
        std::ofstream{script, std::ios::binary}
            << nlohmann::json{{"turns", nlohmann::json::array({{{"text", std::string{kRecord}}}})}}
                   .dump();
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: mock\n"
            << (with_embedder ? "  default_embedding: embed\n" : "")
            << "backends:\n  mock:\n    type: mock\n    model_path: " << script.string()
            << "\n  vendor:\n    type: claude-cli\n"
            << (with_embedder ? "  embed:\n    type: mock\n    embedding_model: mock-space\n" : "");
        config = apogee::harness::load_config(config_path);
        harness = std::make_unique<apogee::harness::Harness>(config);
        MockProvider::Options options;
        options.backend_name = "mock";
        options.turns = std::move(turns);
        provider = std::make_shared<MockProvider>(std::move(options));
        harness->register_provider("mock", provider);
        if (with_embedder) {
            embedder = std::make_shared<apogee::backends::MockEmbeddingProvider>("embed");
            embedder->set_model_name("mock-space");
            harness->register_provider("embed", embedder);
        }
        harness->use_default_router();
        HandlerOptions served;
        if (serve) {
            served.served = {"mock"};
            served.default_backend = "mock";
        }
        handler = std::make_unique<Handler>(*harness, served, nullptr);
    }

    [[nodiscard]] AdminConfigContext context() const {
        return AdminConfigContext{.config_path = config_path, .startup = &config};
    }

    [[nodiscard]] static HttpRequest post(const nlohmann::json& body) {
        HttpRequest request;
        request.method = "POST";
        request.body = body.dump();
        request.remote_address = "127.0.0.1";
        return request;
    }

    [[nodiscard]] HttpResponse capture(const nlohmann::json& body) const {
        return apogee::httpserver::admin_capture_knowledge(context(), *handler, post(body));
    }

    [[nodiscard]] HttpResponse create(const nlohmann::json& body) const {
        return apogee::httpserver::admin_create_knowledge(context(), *handler, post(body));
    }

    [[nodiscard]] HttpResponse refine(const nlohmann::json& body) const {
        return apogee::httpserver::admin_refine_knowledge(context(), *handler, post(body));
    }

    [[nodiscard]] HttpResponse reindex(const nlohmann::json& body) const {
        return apogee::httpserver::admin_reindex_knowledge(context(), *handler, post(body));
    }

    [[nodiscard]] static HttpRequest get(const std::map<std::string, std::string>& query) {
        HttpRequest request;
        request.method = "GET";
        request.query = query;
        return request;
    }

    [[nodiscard]] HttpResponse list(const std::map<std::string, std::string>& query) const {
        return apogee::httpserver::admin_list_knowledge(context(), *handler, get(query));
    }

    [[nodiscard]] HttpResponse fetch(const std::string& id,
                                     const std::map<std::string, std::string>& query = {}) const {
        return apogee::httpserver::admin_get_knowledge(context(), id, get(query));
    }

    [[nodiscard]] HttpResponse patch(const std::string& id, const nlohmann::json& body) const {
        HttpRequest request = post(body);
        request.method = "PATCH";
        return apogee::httpserver::admin_patch_knowledge(context(), id, request);
    }

    [[nodiscard]] HttpResponse remove(const std::string& id,
                                      const std::map<std::string, std::string>& query = {}) const {
        HttpRequest request = get(query);
        request.method = "DELETE";
        return apogee::httpserver::admin_delete_knowledge(context(), id, request);
    }

    /// What a draft and a refine must not change.
    struct Footprint {
        bool collection = false;
        bool archive = false;
        std::string config;
        bool operator==(const Footprint&) const = default;
    };

    [[nodiscard]] Footprint footprint() const {
        std::error_code code;
        return Footprint{std::filesystem::exists(home.path() / "embeddings" / "knowledge.db", code),
                         std::filesystem::exists(home.path() / "knowledge" / "raw", code),
                         bytes(config_path)};
    }

    [[nodiscard]] Store store(std::string_view db = "knowledge") const {
        return Store{home.path() / "embeddings" / (std::string{db} + ".db"),
                     home.path() / "knowledge" / "raw"};
    }

    void cli(const std::vector<std::string>& args) const {
        std::ostringstream sink;
        std::streambuf* old_out = std::cout.rdbuf(sink.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(sink.rdbuf());
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", config_path.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        const int code = root.run(static_cast<int>(argv.size()), argv.data());
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        INFO(sink.str());
        REQUIRE(code == 0);
    }
};

nlohmann::json parsed(const HttpResponse& response) {
    const nlohmann::json body = nlohmann::json::parse(response.body, nullptr, false);
    REQUIRE_FALSE(body.is_discarded());
    return body;
}

}  // namespace

TEST_CASE(
    "POST /v1/admin/knowledge/capture runs the clerk on the served backend and stores the "
    "record: chunk, archive, registration",
    "[httpserver][admin][knowledge][capture]") {
    const Fixture fixture;
    const HttpResponse response = fixture.capture(
        {{"raw", "Ada: drop it? Bob: yes"}, {"link", "PROJ-1"}, {"status", "live"}});
    REQUIRE(response.status == 201);
    const nlohmann::json body = parsed(response);
    CHECK(body["db"] == "knowledge");
    CHECK(body["retriever"] == "lexical");
    CHECK(body["registered"] == true);
    const std::string id = body["record"]["id"].get<std::string>();
    CHECK(id.starts_with("kr-"));
    CHECK(body["record"]["status"] == "shipped");
    CHECK(body["record"]["downstream_link"] == "PROJ-1");
    CHECK(body["record"]["provenance"]["attribution"] == "Ada Lovelace");
    CHECK(body["record"]["raw_ref"].get<std::string>().ends_with(id + ".md"));

    const Store store = fixture.store();
    const std::optional<Record> stored = store.get(id);
    REQUIRE(stored.has_value());
    CHECK(bytes(stored->raw_ref) == "Ada: drop it? Bob: yes");
    CHECK(store.chunks().search("cancel button", 5).size() == 1);
    CHECK(apogee::harness::load_config(fixture.config_path).find_embedding("knowledge") != nullptr);
    // The clerk ran once, on the served backend, as a side request.
    REQUIRE(fixture.provider->requests().size() == 1);
    CHECK(fixture.provider->requests().front().transient.side_request);
    CHECK(fixture.provider->requests().front().model == "mock");

    // A second capture: no re-registration, and an explicit model resolves.
    const HttpResponse again = fixture.capture({{"raw", "more"}, {"model", "mock"}});
    REQUIRE(again.status == 201);
    CHECK(parsed(again)["registered"] == false);
    CHECK(store.list().size() == 2);
}

TEST_CASE("the capture twin refuses what the CLI refuses, with the status a client acts on",
          "[httpserver][admin][knowledge][refusals]") {
    const Fixture fixture;
    HttpRequest not_json;
    not_json.method = "POST";
    not_json.body = "[";
    CHECK(apogee::httpserver::admin_capture_knowledge(fixture.context(), *fixture.handler, not_json)
              .status == 400);
    CHECK(fixture.capture({{"link", "x"}}).status == 400);
    CHECK(parsed(fixture.capture({{"link", "x"}}))["error"]["message"].get<std::string>().find(
              "raw is required") != std::string::npos);
    CHECK(fixture.capture({{"raw", 7}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"status", "maybe"}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"retriever", "sideways"}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"db", "../x"}}).status == 400);
    CHECK(fixture.capture({{"raw", "x"}, {"model", "nope"}}).status == 400);
    const HttpResponse vendor = fixture.capture({{"raw", "x"}, {"model", "vendor"}});
    CHECK(vendor.status == 400);
    CHECK(parsed(vendor)["error"]["message"].get<std::string>().find("vendor CLI") !=
          std::string::npos);
    // An explicit vector ask with no embedder: the resolver's refusal.
    const HttpResponse vector = fixture.capture({{"raw", "x"}, {"retriever", "vector"}});
    CHECK(vector.status == 400);
    CHECK(parsed(vector)["error"]["message"].get<std::string>().find("vector ingest cannot run") !=
          std::string::npos);
    // Nothing was stored by any of those, and the clerk was never asked.
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "embeddings" / "knowledge.db"));
    CHECK(fixture.provider->requests().empty());

    // No generation backend served: 501, honestly, before any clerk call.
    const Fixture unserved{{MockTurn{std::string{kRecord}}}, /*serve=*/false};
    const HttpResponse none = unserved.capture({{"raw", "x"}});
    CHECK(none.status == 501);
    CHECK(parsed(none)["error"]["type"] == "backend_unavailable");
    CHECK(unserved.provider->requests().empty());

    // A clerk that never conforms: 502, and nothing stored.
    const Fixture stubborn{{MockTurn{"no"}, MockTurn{"still no"}}};
    const HttpResponse failed = stubborn.capture({{"raw", "x"}});
    CHECK(failed.status == 502);
    CHECK(parsed(failed)["error"]["type"] == "backend_error");
    CHECK(parsed(failed)["error"]["message"].get<std::string>().find("after 2 attempts") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(stubborn.home.path() / "embeddings" / "knowledge.db"));
}

TEST_CASE(
    "POST /v1/admin/knowledge stores a finished record without the clerk, minting what is "
    "missing and keeping what is given",
    "[httpserver][admin][knowledge][create]") {
    const Fixture fixture;
    const HttpResponse created =
        fixture.create({{"intent", "because the old one leaked"},
                        {"decision", "rotate the token"},
                        {"status", "Implemented"},
                        {"discipline", "eng"},
                        {"downstream_link", "SEC-9"},
                        {"provenance", {{"source", "review"}, {"attribution", "Bob"}}},
                        {"raw", "the review thread"}});
    REQUIRE(created.status == 201);
    const nlohmann::json body = parsed(created);
    const std::string id = body["record"]["id"].get<std::string>();
    CHECK(id.starts_with("kr-"));
    CHECK(body["record"]["status"] == "shipped");
    CHECK(body["record"]["timestamp"].get<std::string>().ends_with("Z"));
    CHECK(body["registered"] == true);
    CHECK(fixture.provider->requests().empty());
    const Store store = fixture.store();
    REQUIRE(store.get(id).has_value());
    CHECK(bytes(store.get(id)->raw_ref) == "the review thread");
    CHECK(store.get(id)->provenance.attribution == "Bob");

    // A given id is kept; a supersedes target is flipped; no raw, no archive.
    const HttpResponse second = fixture.create({{"id", "kr-20260913T120000Z-fixed1"},
                                                {"intent", "newer thinking"},
                                                {"status", "shipped"},
                                                {"supersedes", id}});
    REQUIRE(second.status == 201);
    CHECK(parsed(second)["record"]["id"] == "kr-20260913T120000Z-fixed1");
    CHECK_FALSE(parsed(second)["record"].contains("raw_ref"));
    CHECK(store.get(id)->status == "superseded");
    const std::optional<Record> fixed = store.get("kr-20260913T120000Z-fixed1");
    REQUIRE(fixed.has_value());
    CHECK(fixed->provenance.source == "manual");
    CHECK(fixed->timestamp.ends_with("Z"));

    // Validation is the record's: no intent, a bad status.
    CHECK(fixture.create({{"decision", "x"}, {"status", "shipped"}}).status == 400);
    CHECK(fixture.create({{"intent", "x"}, {"status", "perhaps"}}).status == 400);
    CHECK(fixture.create({{"intent", "x"}, {"retriever", "vector"}}).status == 400);
    CHECK(store.list().size() == 2);
}

TEST_CASE("a record captured over HTTP is the record the CLI captures from the same conversation",
          "[httpserver][admin][knowledge][parity]") {
    const Fixture fixture;
    const std::string raw = "Ada: drop the cancel button? Bob: yes, testers mistake it for back";
    const HttpResponse response = fixture.capture({{"raw", raw}, {"link", "PROJ-42"}});
    REQUIRE(response.status == 201);
    fixture.cli({"knowledge", "capture", "--link", "PROJ-42", raw});

    const Store store = fixture.store();
    const std::vector<Record> records = store.list();
    REQUIRE(records.size() == 2);
    const auto strip = [](Record record) {
        record.id.clear();
        record.timestamp.clear();
        record.raw_ref.clear();
        return nlohmann::json(record);
    };
    CHECK(strip(records[0]) == strip(records[1]));
    CHECK(bytes(records[0].raw_ref) == bytes(records[1].raw_ref));
    CHECK(store.chunks().chunk_by_id(*store.chunk_id(records[0].id))->text ==
          store.chunks().chunk_by_id(*store.chunk_id(records[1].id))->text);
}

TEST_CASE("the knowledge routes sit behind the gate and dispatch through the mux",
          "[httpserver][admin][knowledge][mux]") {
    const Fixture fixture;
    apogee::events::Bus bus;
    apogee::httpserver::JobRegistry jobs{bus};
    apogee::httpserver::AdminOptions admin_options;
    admin_options.config_path = fixture.config_path;
    admin_options.startup = fixture.config;
    apogee::httpserver::AdminHandler admin{admin_options, jobs, bus};
    const apogee::httpserver::Mux mux{*fixture.handler, admin, "token"};

    HttpRequest request = Fixture::post({{"raw", "x"}});
    request.path = "/v1/admin/knowledge/capture";
    CHECK(mux.dispatch(request).status == 401);
    request.headers["authorization"] = "Bearer token";
    CHECK(mux.dispatch(request).status == 201);
    request.path = "/v1/admin/knowledge";
    request.body = nlohmann::json{{"intent", "why"}, {"status", "shipped"}}.dump();
    const HttpResponse stored = mux.dispatch(request);
    REQUIRE(stored.status == 201);
    const std::string id = parsed(stored)["record"]["id"].get<std::string>();
    request.method = "GET";
    CHECK(mux.dispatch(request).status == 200);
    request.path = "/v1/admin/knowledge/" + id;
    CHECK(mux.dispatch(request).status == 200);
    request.method = "PATCH";
    request.body = nlohmann::json{{"link", "PROJ-1"}}.dump();
    CHECK(parsed(mux.dispatch(request))["downstream_link"] == "PROJ-1");
    request.headers.erase("authorization");
    CHECK(mux.dispatch(request).status == 401);
    request.headers["authorization"] = "Bearer token";
    request.method = "DELETE";
    CHECK(mux.dispatch(request).status == 200);
    request.method = "PUT";
    CHECK(mux.dispatch(request).status == 405);
    // The literal paths are never read as record ids.
    request.method = "POST";
    request.path = "/v1/admin/knowledge/reindex";
    request.body = "{}";
    CHECK(mux.dispatch(request).status == 200);
}

TEST_CASE(
    "GET /v1/admin/knowledge lists and queries: a missing collection is an empty list "
    "that creates nothing, filters apply, and an explicit vector ask with no embedder "
    "is a 501",
    "[httpserver][admin][knowledge][list]") {
    const Fixture fixture;
    HttpResponse response = fixture.list({});
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["object"] == "list");
    CHECK(parsed(response)["data"].empty());
    CHECK_FALSE(fixture.footprint().collection);
    CHECK(fixture.list({{"db", "../x"}}).status == 400);

    REQUIRE(fixture.capture({{"raw", "Ada: drop it? Bob: yes"}}).status == 201);
    REQUIRE(fixture.capture({{"raw", "second thoughts"}, {"status", "rejected"}}).status == 201);
    response = fixture.list({});
    REQUIRE(parsed(response)["data"].size() == 2);
    CHECK(parsed(response)["data"][0]["status"] == "rejected");  // newest first
    CHECK(parsed(response)["data"][0]["provenance"]["attribution"] == "Ada Lovelace");
    CHECK(parsed(fixture.list({{"status", "shipped"}}))["data"].size() == 1);
    CHECK(parsed(fixture.list({{"discipline", "eng"}}))["data"].empty());
    const nlohmann::json shared = parsed(fixture.list({{"anonymize", "true"}}))["data"];
    CHECK_FALSE(shared[0]["provenance"].contains("attribution"));
    CHECK_FALSE(shared[0].contains("raw_ref"));

    // A query: the envelope, the retriever, no default branch over HTTP.
    response = fixture.list({{"q", "cancel button"}});
    REQUIRE(response.status == 200);
    nlohmann::json body = parsed(response);
    CHECK(body["object"] == "list");
    CHECK(body["retriever"] == "lexical");
    CHECK(body["reranked"] == false);
    REQUIRE(body["data"].size() == 2);
    CHECK(body["data"][0]["record"]["id"].get<std::string>().starts_with("kr-"));
    CHECK(body["data"][0]["score"].is_number());
    CHECK(parsed(fixture.list({{"q", "cancel button"}, {"status", "shipped"}}))["data"].size() ==
          1);
    CHECK(parsed(fixture.list({{"q", "cancel button"}, {"limit", "1"}}))["data"].size() == 1);
    CHECK_FALSE(parsed(fixture.list(
        {{"q", "cancel"}, {"anonymize", "true"}}))["data"][0]["record"]["provenance"]
                    .contains("attribution"));
    CHECK(fixture.list({{"q", "x"}, {"limit", "many"}}).status == 400);
    CHECK(fixture.list({{"q", "x"}, {"retriever", "sideways"}}).status == 400);
    CHECK(fixture.list({{"q", "x"}, {"rerank", "nope"}}).status == 400);
    response = fixture.list({{"q", "cancel"}, {"retriever", "vector"}});
    CHECK(response.status == 501);
    CHECK(parsed(response)["error"]["message"].get<std::string>().find("?retriever=lexical") !=
          std::string::npos);
    CHECK(parsed(fixture.list({{"q", "cancel"}, {"retriever", "hybrid"}}))["retriever"] ==
          "lexical");
}

TEST_CASE(
    "GET, PATCH and DELETE by id: one edit per call, a metadata edit that never re-embeds, "
    "and honest 404s",
    "[httpserver][admin][knowledge][edit]") {
    const Fixture fixture{{MockTurn{std::string{kRecord}}}, true, /*with_embedder=*/true};
    CHECK(fixture.fetch("kr-x").status == 404);
    CHECK(fixture.remove("kr-x").status == 404);
    CHECK(fixture.patch("kr-x", {{"link", "x"}}).status == 404);
    REQUIRE(fixture.capture({{"raw", "Ada: drop it?"}, {"retriever", "vector"}}).status == 201);
    const Store store = fixture.store();
    const std::string id = store.list().front().id;
    const std::int64_t chunk = *store.chunk_id(id);
    const std::vector<float> vector = store.chunks().chunk_vector(chunk);
    REQUIRE(vector.size() == 8);

    HttpResponse response = fixture.fetch(id);
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["id"] == id);
    CHECK(parsed(response)["raw_ref"].get<std::string>().ends_with(".md"));
    CHECK(fixture.fetch("kr-nope").status == 404);
    CHECK(fixture.fetch(id, {{"db", "other"}}).status == 404);

    CHECK(fixture.patch(id, {{"link", "x"}, {"status", "shipped"}}).status == 400);
    CHECK(fixture.patch(id, {{"db", "knowledge"}}).status == 400);
    CHECK(fixture.patch(id, {{"status", "maybe"}}).status == 400);
    CHECK(fixture.patch(id, {{"link", 7}}).status == 400);
    response = fixture.patch(id, {{"link", "PROJ-9"}});
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["downstream_link"] == "PROJ-9");
    response = fixture.patch(id, {{"status", "Abandoned"}});
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["status"] == "rejected");
    CHECK(parsed(response)["downstream_link"] == "PROJ-9");
    CHECK(store.chunks().chunk_vector(chunk) == vector);
    CHECK(*store.chunk_id(id) == chunk);
    CHECK(fixture.patch("kr-nope", {{"status", "shipped"}}).status == 404);

    const std::string raw = store.get(id)->raw_ref;
    REQUIRE(std::filesystem::exists(raw));
    response = fixture.remove(id);
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["deleted"] == id);
    CHECK_FALSE(std::filesystem::exists(raw));
    CHECK(fixture.remove(id).status == 404);
}

TEST_CASE(
    "capture with draft: true runs the clerk and stores nothing; refine runs one bounded "
    "revision with its guards before the clerk and never stores",
    "[httpserver][admin][knowledge][refine]") {
    const Fixture fixture;
    const Fixture::Footprint before = fixture.footprint();
    HttpResponse response = fixture.capture({{"raw", "Ada: drop it?"}, {"draft", true}});
    REQUIRE(response.status == 200);
    nlohmann::json body = parsed(response);
    CHECK(body["draft"] == true);
    CHECK(body["record"]["id"] == "");
    CHECK(body["record"]["timestamp"] == "");
    CHECK_FALSE(body["record"].contains("raw_ref"));
    CHECK(body["db"] == "knowledge");
    CHECK(body["retriever"] == "lexical");
    CHECK_FALSE(body.contains("registered"));
    CHECK(fixture.footprint() == before);
    CHECK(fixture.provider->requests().size() == 1);
    CHECK(fixture.capture({{"raw", "x"}, {"draft", "yes"}}).status == 400);
    // A dry run warns where a real run would refuse.
    response = fixture.capture({{"raw", "x"}, {"draft", true}, {"retriever", "vector"}});
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["warning"].get<std::string>().find("vector ingest cannot run") !=
          std::string::npos);
    CHECK(fixture.footprint() == before);

    // Refine's guards, before any clerk call.
    const std::size_t calls = fixture.provider->requests().size();
    nlohmann::json draft = body["record"];
    CHECK(fixture.refine({{"instruction", "x"}}).status == 400);
    CHECK(fixture.refine({{"record", "not an object"}, {"instruction", "x"}}).status == 400);
    CHECK(fixture.refine({{"record", draft}, {"instruction", ""}}).status == 400);
    CHECK(fixture.refine({{"record", draft}, {"instruction", std::string(2001, 'x')}}).status ==
          400);
    CHECK(fixture.refine({{"record", draft}, {"instruction", "x"}, {"model", "vendor"}}).status ==
          400);
    CHECK(fixture.provider->requests().size() == calls);
    const Fixture unserved{{MockTurn{std::string{kRecord}}}, /*serve=*/false};
    CHECK(unserved.refine({{"record", draft}, {"instruction", "x"}}).status == 501);
    // The input guards come BEFORE the backend check: a bad request is a
    // 400 even on a server that could not have run the clerk.
    CHECK(unserved.refine({{"record", draft}, {"instruction", ""}}).status == 400);
    CHECK(unserved.refine({{"instruction", "x"}}).status == 400);
    CHECK(unserved.provider->requests().empty());

    // A revision: the instruction applied by the clerk, supersedes carried,
    // a dropped source kept -- and nothing stored.
    draft["supersedes"] = "kr-old";
    draft["provenance"]["source"] = "chat";
    const Fixture reviser{{MockTurn{
        R"({"intent": "We dropped the cancel button because testers kept mistaking it for back.", "decision": "Remove the cancel button.", "status": "rejected", "discipline": "ux", "downstream_link": "", "provenance": {"source": "", "attribution": "Ada Lovelace"}})"}}};
    response = reviser.refine(
        {{"record", draft}, {"instruction", "mark it rejected"}, {"raw", "Ada: drop it?"}});
    REQUIRE(response.status == 200);
    body = parsed(response);
    CHECK(body["draft"] == true);
    CHECK(body["record"]["status"] == "rejected");
    CHECK(body["record"]["supersedes"] == "kr-old");
    CHECK(body["record"]["provenance"]["source"] == "chat");
    CHECK(body["record"]["id"] == "");
    CHECK_FALSE(reviser.footprint().collection);
    CHECK_FALSE(reviser.footprint().archive);
    REQUIRE(reviser.provider->requests().size() == 1);
    const apogee::harness::ChatRequest& seen = reviser.provider->requests().front();
    CHECK(seen.messages.front().content.plain_text() == apogee::knowledge::refine_system_prompt());
    CHECK(seen.messages.back().content.plain_text().find(
              "REVIEWER INSTRUCTION\nmark it rejected") != std::string::npos);
    CHECK(seen.transient.side_request);

    // A revision that is not a record: 502, nothing stored.
    const Fixture stubborn{{MockTurn{"no"}, MockTurn{"still no"}}};
    response = stubborn.refine({{"record", draft}, {"instruction", "x"}});
    CHECK(response.status == 502);
    CHECK(parsed(response)["error"]["type"] == "backend_error");
    CHECK_FALSE(stubborn.footprint().collection);
}

TEST_CASE(
    "draft -> refine -> store lands the record a one-shot capture lands, with the clerk run "
    "exactly twice and nothing written before the store",
    "[httpserver][admin][knowledge][round-trip]") {
    const Fixture fixture;
    const std::string raw = "Ada: drop the cancel button? Bob: yes, testers mistake it for back";
    const Fixture::Footprint before = fixture.footprint();

    // 1. Draft.
    HttpResponse response = fixture.capture({{"raw", raw}, {"draft", true}, {"link", "PROJ-42"}});
    REQUIRE(response.status == 200);
    nlohmann::json draft = parsed(response)["record"];
    CHECK(fixture.footprint() == before);
    CHECK(fixture.provider->requests().size() == 1);

    // 2. Refine, with a no-op instruction the scripted clerk honours.
    response = fixture.refine(
        {{"record", draft}, {"instruction", "keep it exactly as it is"}, {"raw", raw}});
    REQUIRE(response.status == 200);
    nlohmann::json reviewed = parsed(response)["record"];
    reviewed["downstream_link"] = "PROJ-42";  // the reviewer's own touch survives the store
    CHECK(fixture.footprint() == before);
    CHECK(fixture.provider->requests().size() == 2);

    // 3. Store, through the ordinary finished-record route, raw attached.
    reviewed["raw"] = raw;
    response = fixture.create(reviewed);
    REQUIRE(response.status == 201);
    const std::string stored_id = parsed(response)["record"]["id"].get<std::string>();
    CHECK(fixture.footprint() != before);
    CHECK(fixture.provider->requests().size() == 2);

    // The one-shot capture of the same raw, for comparison.
    response = fixture.capture({{"raw", raw}, {"link", "PROJ-42"}});
    REQUIRE(response.status == 201);
    const std::string one_shot_id = parsed(response)["record"]["id"].get<std::string>();

    const Store store = fixture.store();
    const auto strip = [](Record record) {
        record.id.clear();
        record.timestamp.clear();
        record.raw_ref.clear();
        return nlohmann::json(record);
    };
    REQUIRE(store.get(stored_id).has_value());
    REQUIRE(store.get(one_shot_id).has_value());
    CHECK(strip(*store.get(stored_id)) == strip(*store.get(one_shot_id)));
    CHECK(bytes(store.get(stored_id)->raw_ref) == bytes(store.get(one_shot_id)->raw_ref));
    CHECK(store.chunks().chunk_by_id(*store.chunk_id(stored_id))->text ==
          store.chunks().chunk_by_id(*store.chunk_id(one_shot_id))->text);
}

TEST_CASE(
    "POST /v1/admin/knowledge/reindex: 404 for a missing collection, zero with a note for "
    "a lexical one, 501 for vectors with no embedder, and the rewrite with one",
    "[httpserver][admin][knowledge][reindex]") {
    const Fixture lexical;
    CHECK(lexical.reindex(nlohmann::json::object()).status == 404);
    REQUIRE(lexical.capture({{"raw", "one"}}).status == 201);
    HttpResponse response = lexical.reindex(nlohmann::json::object());
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["reindexed"] == 0);
    CHECK(parsed(response)["note"].get<std::string>().find("no embedded records") !=
          std::string::npos);
    // Vectors present, no embedder to rebuild them with: 501.
    {
        Store store = lexical.store();
        Record record = store.list().front();
        store.put(record, {0.1F, 0.2F}, "");
    }
    response = lexical.reindex(nlohmann::json::object());
    CHECK(response.status == 501);
    CHECK(parsed(response)["error"]["type"] == "backend_unavailable");
    HttpRequest empty_body;
    empty_body.method = "POST";
    CHECK(
        apogee::httpserver::admin_reindex_knowledge(lexical.context(), *lexical.handler, empty_body)
            .status == 501);

    const Fixture fixture{{MockTurn{std::string{kRecord}}}, true, /*with_embedder=*/true};
    REQUIRE(fixture.capture({{"raw", "one"}, {"retriever", "lexical"}}).status == 201);
    REQUIRE(fixture.capture({{"raw", "two"}, {"retriever", "vector"}}).status == 201);
    CHECK(fixture.store().vectorless_count() == 1);
    response = fixture.reindex(nlohmann::json::object());
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["reindexed"] == 2);
    CHECK(parsed(response)["note"].get<std::string>().find("1 of 2 record(s)") !=
          std::string::npos);
    CHECK(fixture.store().vectorless_count() == 0);
    CHECK(fixture.store().chunks().embedding_model().model == "mock-space");
    const std::string id = fixture.store().list().front().id;
    response = fixture.reindex({{"id", id}});
    REQUIRE(response.status == 200);
    CHECK(parsed(response)["reindexed"] == 1);
    CHECK_FALSE(parsed(response).contains("note"));
    CHECK(fixture.reindex({{"id", "kr-nope"}}).status == 404);
    CHECK(fixture.reindex({{"db", "../x"}}).status == 400);
    CHECK(fixture.reindex(nlohmann::json{}).status == 400);  // `null` is not an object
    CHECK(fixture.reindex({{"db", "other"}}).status == 404);
}
