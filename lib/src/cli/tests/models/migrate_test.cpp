#include "models/migrate.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "models/sha256.h"
#include "models/sidecar.h"
#include "models/snapshot.h"
#include "models/store.h"
#include "support/env_guard.h"

namespace {

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

struct Flat {
    apogee::testing::TempDir root{"migrate-" + std::to_string(std::random_device{}())};
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(root.path() / "models");
};

}  // namespace

TEST_CASE("a flat GGUF moves with its record and its projector, under the id of its bytes",
          "[models][migrate]") {
    const Flat flat;
    const std::filesystem::path models = flat.roots.models;
    write_file(models / "llava.gguf", "model bytes");
    write_file(models / "llava-mmproj.gguf", "projector bytes");
    apogee::models::Sidecar record;
    record.ref = "llava:7b";
    record.source = "ollama";
    record.file_digest = apogee::models::sha256_hex("model bytes");
    REQUIRE(apogee::models::write_sidecar(models / "llava.gguf", record));

    const apogee::models::MigrationPlan plan = apogee::models::plan_migration(flat.roots);
    REQUIRE(plan.items.size() == 1);
    const apogee::models::MigrationItem& item = plan.items.front();
    CHECK(item.model == "llava-7b");  // from the record's ref, not the file name
    CHECK(item.id == apogee::models::weight_id_from_digest(record.file_digest));
    CHECK(item.moves.size() == 3);  // model, record, projector

    REQUIRE(apogee::models::apply_migration(item).empty());
    const std::filesystem::path home = models / "llava-7b" / "gguf" / item.id;
    CHECK(read_file(home / "llava.gguf") == "model bytes");
    CHECK(read_file(home / "llava-mmproj.gguf") == "projector bytes");
    CHECK(std::filesystem::exists(home / "llava.json"));
    CHECK_FALSE(std::filesystem::exists(models / "llava.gguf"));
    CHECK(apogee::models::find_legacy(flat.roots).empty());

    // Listed as the store lists it: one model, its projector beside it.
    const auto stored = apogee::models::list_store_ggufs(flat.roots);
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().projector.filename() == "llava-mmproj.gguf");
}

TEST_CASE("a GGUF with no record is hashed to name its directory", "[models][migrate]") {
    const Flat flat;
    write_file(flat.roots.models / "byhand.gguf", "hand placed");
    std::vector<std::filesystem::path> hashed;
    const apogee::models::MigrationPlan plan = apogee::models::plan_migration(
        flat.roots, [&hashed](const std::filesystem::path& file) { hashed.push_back(file); });
    REQUIRE(plan.items.size() == 1);
    CHECK(plan.items.front().id ==
          apogee::models::weight_id_from_digest(apogee::models::sha256_hex("hand placed")));
    CHECK(plan.items.front().model == "byhand");
    CHECK(hashed == std::vector<std::filesystem::path>{flat.roots.models / "byhand.gguf"});
}

TEST_CASE("a flat snapshot moves inside a model directory of its own name", "[models][migrate]") {
    // `<models>/Qwen--Qwen3-8B` becomes `<models>/Qwen--Qwen3-8B/safetensors/<id>`:
    // the destination is inside the source, which only works by moving it
    // aside first.
    const Flat flat;
    const std::filesystem::path old_dir = flat.roots.models / "Qwen--Qwen3-8B";
    write_file(old_dir / "config.json", R"({"architectures": ["Qwen3ForCausalLM"]})");
    write_file(old_dir / "model.safetensors", "weights");
    apogee::models::Snapshot record;
    record.ref = "Qwen/Qwen3-8B";
    record.source = "huggingface";
    record.files = {{"config.json", 40, ""},
                    {"model.safetensors", 7, apogee::models::sha256_hex("weights")}};
    REQUIRE(apogee::models::write_snapshot(old_dir, record));

    const apogee::models::MigrationPlan plan = apogee::models::plan_migration(flat.roots);
    REQUIRE(plan.items.size() == 1);
    const apogee::models::MigrationItem& item = plan.items.front();
    CHECK(item.kind == apogee::models::MigrationItem::Kind::Snapshot);
    CHECK(item.id == apogee::models::snapshot_weight_id(record.files));
    CHECK(item.destination == old_dir / "safetensors" / item.id);

    REQUIRE(apogee::models::apply_migration(item).empty());
    CHECK(read_file(item.destination / "model.safetensors") == "weights");
    CHECK(apogee::models::load_snapshot(item.destination).has_value());
    CHECK_FALSE(std::filesystem::exists(old_dir / "config.json"));
    // Nothing left behind under a hidden name.
    for (const auto& entry : std::filesystem::directory_iterator(flat.roots.models)) {
        CHECK_FALSE(entry.path().filename().string().starts_with("."));
    }
    const auto newest = apogee::models::newest_snapshot(flat.roots, "Qwen--Qwen3-8B");
    REQUIRE(newest.has_value());
    CHECK(newest->id == item.id);
}

TEST_CASE("snapshots move before GGUFs of the same model", "[models][migrate]") {
    // A GGUF pulled from the same repository lands in a model directory named
    // like the snapshot's old one; moving the GGUF first would put it INSIDE
    // the snapshot, which would then carry it off into safetensors/.
    const Flat flat;
    write_file(flat.roots.models / "org--repo" / "config.json", "{}");
    write_file(flat.roots.models / "org--repo" / "model.safetensors", "weights");
    write_file(flat.roots.models / "org-repo-q4.gguf", "gguf");
    apogee::models::Sidecar record;
    record.ref = "org/repo:q4.gguf";
    record.source = "huggingface";
    REQUIRE(apogee::models::write_sidecar(flat.roots.models / "org-repo-q4.gguf", record));

    const apogee::models::MigrationPlan plan = apogee::models::plan_migration(flat.roots);
    REQUIRE(plan.items.size() == 2);
    CHECK(plan.items.front().kind == apogee::models::MigrationItem::Kind::Snapshot);
    for (const apogee::models::MigrationItem& item : plan.items) {
        REQUIRE(apogee::models::apply_migration(item).empty());
    }
    CHECK(apogee::models::list_store_snapshots(flat.roots, "org--repo").size() == 1);
    CHECK(apogee::models::list_store_ggufs(flat.roots, "org--repo").size() == 1);
    CHECK(apogee::models::find_legacy(flat.roots).empty());
}

TEST_CASE("identical weights already stored are left alone, the old copy kept",
          "[models][migrate]") {
    const Flat flat;
    write_file(flat.roots.models / "dup.gguf", "same bytes");
    const std::string id =
        apogee::models::weight_id_from_digest(apogee::models::sha256_hex("same bytes"));
    write_file(flat.roots.models / "dup" / "gguf" / id / "dup.gguf", "same bytes");

    const apogee::models::MigrationPlan plan = apogee::models::plan_migration(flat.roots);
    REQUIRE(plan.items.size() == 1);
    CHECK(plan.items.front().duplicate);
    CHECK(apogee::models::apply_migration(plan.items.front()).empty());
    // Never deleted here: removing the user's copy is the user's call.
    CHECK(std::filesystem::exists(flat.roots.models / "dup.gguf"));
}

TEST_CASE("an already-migrated store has nothing to migrate", "[models][migrate]") {
    const Flat flat;
    write_file(flat.roots.models / "m" / "gguf" / "111111111111" / "m.gguf", "x");
    CHECK(apogee::models::plan_migration(flat.roots).items.empty());
}
