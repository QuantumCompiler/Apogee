#include "cli/models_migrate.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "agentloop/retriever.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
#include "embedstore/store.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "support/mlx_model.h"
#include "training/manifest.h"
#include "training/store.h"

/// `apogee models migrate` end to end, in process: the flat layout moved into
/// the store, and every thing that pointed into it -- a backend's model and
/// projector, a promotion ledger, a run's base model, a collection's recorded
/// embedding model -- pointed at the new places.
namespace {

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Home {
    apogee::testing::TempDir root{"migrate-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", root.path().string()};
    std::filesystem::path home = root.path();
    std::filesystem::path models = home / "models";
    std::filesystem::path config_path = home / "config" / "config.yaml";
    std::filesystem::path flat_gguf = models / "flat.gguf";
    std::filesystem::path flat_projector = models / "flat-mmproj.gguf";
    std::filesystem::path flat_snapshot = models / "Qwen--Qwen3-8B";
    std::filesystem::path promoted = home / "training" / "versions" / "tuned" / "v1.gguf";
    std::filesystem::path collection = home / "embeddings" / "notes.db";

    Home() {
        write_file(flat_gguf, apogee::testing::minimal_gguf("llava"));
        write_file(flat_projector, apogee::testing::minimal_gguf("clip"));
        write_file(flat_snapshot / "config.json", R"({"architectures": ["Qwen3ForCausalLM"]})");
        write_file(flat_snapshot / "model.safetensors", "weights");
        apogee::models::Snapshot record;
        record.ref = "Qwen/Qwen3-8B";
        record.source = "huggingface";
        record.files = {{"model.safetensors", 7, apogee::models::sha256_hex("weights")}};
        REQUIRE(apogee::models::write_snapshot(flat_snapshot, record));

        // A promoted version of a fine-tune of that snapshot.
        write_file(promoted, apogee::testing::minimal_gguf("qwen3"));
        apogee::training::RunManifest run;
        run.run_id = "20260923-120000";
        run.base_model = flat_snapshot.string();
        run.status = "complete";
        REQUIRE(
            apogee::training::write_manifest(home / "training" / "runs" / run.run_id, run).empty());
        apogee::training::VersionLedger ledger;
        ledger.backend = "tuned";
        ledger.active_version = 1;
        apogee::training::VersionEntry entry;
        entry.version = 1;
        entry.run_id = run.run_id;
        entry.gguf_path = promoted.string();
        entry.promoted_at = "2026-09-23T12:00:00Z";
        ledger.versions.push_back(entry);
        REQUIRE(apogee::training::save_ledger(home / "training" / "versions", ledger).empty());

        write_file(config_path,
                   "backends:\n"
                   "  local:\n"
                   "    type: llamacpp\n"
                   "    model_path: " +
                       flat_gguf.string() +
                       "   # the vision model\n"
                       "    mmproj_path: " +
                       flat_projector.string() +
                       "\n"
                       "  tuned:\n"
                       "    type: llamacpp\n"
                       "    model_path: " +
                       promoted.string() + "\n");

        // A collection this backend embedded: it recorded the model by path.
        apogee::embedstore::Store store{collection};
        store.set_embedding_model(flat_gguf.string(), 4);
    }

    int run(const std::vector<std::string>& args, std::string* out) const {
        std::ostringstream captured;
        std::ostringstream ignored;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(ignored.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand command{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = command.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str();
        return code;
    }
};

}  // namespace

TEST_CASE("migrate without --yes shows everything and changes nothing",
          "[commands][models][migrate]") {
    const Home home;
    const std::string config_before = read_file(home.config_path);
    std::string out;
    REQUIRE(home.run({"models", "migrate"}, &out) == 0);
    CHECK(out.find("will move into the model store") != std::string::npos);
    CHECK(out.find(home.flat_gguf.string()) != std::string::npos);
    CHECK(out.find("will move promoted training versions") != std::string::npos);
    CHECK(out.find("backends.local.model_path") != std::string::npos);
    CHECK(out.find("backends.local.mmproj_path") != std::string::npos);
    CHECK(out.find("backends.tuned.model_path") != std::string::npos);
    CHECK(out.find("rebound to the new path") != std::string::npos);
    CHECK(out.find("re-run with --yes") != std::string::npos);

    CHECK(std::filesystem::exists(home.flat_gguf));
    CHECK(std::filesystem::exists(home.promoted));
    CHECK(read_file(home.config_path) == config_before);
}

TEST_CASE("migrate --yes moves every model and repoints everything that named one",
          "[commands][models][migrate]") {
    const Home home;
    const std::string config_before = read_file(home.config_path);
    std::string out;
    REQUIRE(home.run({"models", "migrate", "--yes"}, &out) == 0);
    CHECK(out.find("migrated.") != std::string::npos);

    const apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(home.models);
    CHECK(apogee::models::find_legacy(roots).empty());

    // The GGUF, with its projector beside it.
    const auto flat = apogee::models::list_store_ggufs(roots, "flat");
    REQUIRE(flat.size() == 1);
    CHECK(flat.front().projector.filename() == "flat-mmproj.gguf");

    // The snapshot, into a model directory of its own name.
    const auto snapshot = apogee::models::newest_snapshot(roots, "Qwen--Qwen3-8B");
    REQUIRE(snapshot.has_value());

    // The promoted version, beside the model it was trained from, recorded.
    const auto trained = apogee::models::list_store_ggufs(roots, "Qwen--Qwen3-8B");
    REQUIRE(trained.size() == 1);
    CHECK(trained.front().file.filename() == "tuned-v1.gguf");
    const std::optional<apogee::models::Sidecar> record =
        apogee::models::load_sidecar(trained.front().file);
    REQUIRE(record.has_value());
    CHECK(record->source == "train");
    CHECK(record->ref == "tuned v1");
    CHECK_FALSE(std::filesystem::exists(home.promoted));

    // The ledger records the new place.
    const std::optional<apogee::training::VersionLedger> ledger =
        apogee::training::TrainingStore{home.home / "training"}.list_versions("tuned");
    REQUIRE(ledger.has_value());
    CHECK(ledger->find(1)->gguf_path == trained.front().file.string());

    // The run's base model is the snapshot's new home.
    std::string error;
    const std::optional<apogee::training::RunManifest> run =
        apogee::training::read_manifest(home.home / "training" / "runs" / "20260923-120000", error);
    REQUIRE(run.has_value());
    CHECK(run->base_model == snapshot->dir.string());

    // The config: the three paths, and not another byte -- the comment kept.
    std::string expected = config_before;
    const auto swap = [&expected](const std::string& from, const std::string& to) {
        const std::size_t at = expected.find(from);
        REQUIRE(at != std::string::npos);
        expected.replace(at, from.size(), apogee::harness::yaml_scalar(to));
    };
    swap(home.flat_gguf.string(), flat.front().file.string());
    swap(home.flat_projector.string(), flat.front().projector.string());
    swap(home.promoted.string(), trained.front().file.string());
    CHECK(read_file(home.config_path) == expected);
    CHECK(read_file(home.config_path).find("# the vision model") != std::string::npos);

    // The collection still names the model it was embedded with.
    const apogee::embedstore::Store store{home.collection};
    CHECK(store.embedding_model().model == flat.front().file.string());
    CHECK(store.embedding_model().dimension == 4);

    // And there is nothing left to do.
    REQUIRE(home.run({"models", "migrate"}, &out) == 0);
    CHECK(out.find("nothing to migrate") != std::string::npos);
}

TEST_CASE("migrate --yes renames a moved model in every source's record and a mixed store's names",
          "[commands][models][migrate][57s]") {
    const Home home;
    const std::string old_path = home.flat_gguf.string();
    // `notes` ingested under the backend that names its model by path: each
    // source recorded the path beside its folder.
    {
        apogee::embedstore::Store store{home.collection};
        store.ingest_source("one.md", "/corpus", old_path, {"one"}, {{1.0F, 0.0F, 0.0F, 0.0F}});
        store.ingest_source("two.md", "/corpus", old_path, {"two"}, {{0.0F, 1.0F, 0.0F, 0.0F}});
        store.set_embedding_model(old_path, 4);
    }
    // `mixed`: one source under the moved model, one under another -- bound
    // to neither, so a migration asking only the binding passed it by.
    const std::filesystem::path mixed_path = home.home / "embeddings" / "mixed.db";
    {
        apogee::embedstore::Store store{mixed_path};
        store.ingest_source("a.md", "/corpus", old_path, {"a"}, {{1.0F, 0.0F, 0.0F, 0.0F}});
        store.ingest_source("b.md", "/corpus", "other-embedder", {"b"}, {{0.0F, 1.0F, 0.0F, 0.0F}});
        store.mark_embedding_mixed({old_path, "other-embedder"}, 4);
    }

    std::string out;
    REQUIRE(home.run({"models", "migrate", "--yes"}, &out) == 0);
    const auto flat =
        apogee::models::list_store_ggufs(apogee::models::StoreRoots::at(home.models), "flat");
    REQUIRE(flat.size() == 1);
    const std::string moved = flat.front().file.string();
    CHECK(out.find("rebound  notes.db") != std::string::npos);
    CHECK(out.find("rebound  mixed.db") != std::string::npos);

    {
        apogee::embedstore::Store store{home.collection};
        CHECK(store.embedding_model().model == moved);
        const auto ingests = store.source_ingests();
        REQUIRE(ingests.size() == 2);
        CHECK(ingests.at("one.md").model == moved);
        CHECK(ingests.at("two.md").model == moved);
        CHECK(ingests.at("one.md").root == "/corpus");

        // The reviewer's sequence: one more file ingested under the moved
        // model, the binding decided over every vectored source's record as
        // `embed ingest` decides it -- one model, bound, no warning.
        store.ingest_source("three.md", "/corpus", moved, {"three"}, {{0.0F, 0.0F, 1.0F, 0.0F}});
        apogee::agentloop::IngestBindingFacts facts;
        facts.run_model = moved;
        facts.vectors_written = 1;
        const auto recorded = store.source_ingests();
        for (const std::string& source : store.vectored_sources()) {
            facts.source_models.push_back(recorded.at(source).model);
        }
        const apogee::agentloop::IngestBinding binding =
            apogee::agentloop::resolve_ingest_binding(facts);
        CHECK(binding.action == apogee::agentloop::BindingAction::Bind);
        CHECK(binding.model == moved);
        CHECK(binding.warning.empty());
    }
    {
        const apogee::embedstore::Store store{mixed_path};
        const apogee::embedstore::Store::EmbeddingBinding binding = store.embedding_model();
        CHECK_FALSE(binding.recorded());
        CHECK(binding.mixed == std::vector<std::string>{moved, "other-embedder"});
        CHECK(binding.dimension == 4);
        CHECK(store.source_ingests().at("a.md").model == moved);
        CHECK(store.source_ingests().at("b.md").model == "other-embedder");
    }
}

TEST_CASE("migrate moves a flat MLX model into mlx/ and repoints the backend naming it",
          "[commands][models][migrate][mlx]") {
    const Home home;
    const std::filesystem::path flat_mlx = home.models / "mlx-community--M-4bit";
    apogee::testing::write_mlx_model(flat_mlx);
    {
        std::ofstream config{home.config_path, std::ios::app};
        config << "  fast:\n    type: mlx\n    model_path: " << flat_mlx.string() << "/\n";
    }
    std::string out;
    REQUIRE(home.run({"models", "migrate", "--yes"}, &out) == 0);

    const apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(home.models);
    const std::vector<apogee::models::StoredMlx> stored =
        apogee::models::list_store_mlx(roots, "mlx-community--M-4bit");
    REQUIRE(stored.size() == 1);
    CHECK(apogee::models::list_store_snapshots(roots, "mlx-community--M-4bit").empty());
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path);
    CHECK(config.backends.at("fast").model_path == stored.front().dir.string());
}
