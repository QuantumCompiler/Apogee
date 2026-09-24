#include "models/store.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "models/sha256.h"
#include "models/sidecar.h"
#include "support/env_guard.h"

using apogee::models::StoreRoots;
using apogee::models::StoreTarget;

namespace {

void write_file(const std::filesystem::path& path, const std::string& bytes = "x") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// A SafeTensors set by hand: enough for is_snapshot_dir.
void place_snapshot(const std::filesystem::path& dir) {
    write_file(dir / "config.json", R"({"architectures": ["LlamaForCausalLM"]})");
    write_file(dir / "model.safetensors");
}

struct Store {
    apogee::testing::TempDir root{"store-" + std::to_string(std::random_device{}())};
    StoreRoots roots = StoreRoots::at(root.path() / "models");
};

const std::string kDigestA(64, 'a');
const std::string kDigestB = std::string(12, 'b') + std::string(52, '0');

}  // namespace

TEST_CASE("a weight id is the first twelve hex of a sha256", "[models][store]") {
    using apogee::models::weight_id_from_digest;
    CHECK(weight_id_from_digest(kDigestA) == "aaaaaaaaaaaa");
    CHECK(weight_id_from_digest("sha256:" + kDigestB) == "bbbbbbbbbbbb");
    CHECK(weight_id_from_digest(std::string(64, 'A')) == "aaaaaaaaaaaa");  // any case
    CHECK(weight_id_from_digest("abc").empty());
    CHECK(weight_id_from_digest(std::string(64, 'z')).empty());

    CHECK(apogee::models::is_weight_id("0123456789ab"));
    CHECK_FALSE(apogee::models::is_weight_id("0123456789AB"));
    CHECK_FALSE(apogee::models::is_weight_id("0123456789a"));
    const std::string random = apogee::models::random_weight_id();
    CHECK(apogee::models::is_weight_id(random));
    CHECK(random != apogee::models::random_weight_id());
}

TEST_CASE("a SafeTensors set's id covers its shards and nothing else", "[models][store]") {
    using apogee::models::snapshot_weight_id;
    using apogee::models::SnapshotFile;
    const std::vector<SnapshotFile> set{{"model-2.safetensors", 1, kDigestB},
                                        {"config.json", 1, ""},
                                        {"model-1.safetensors", 1, kDigestA}};
    const std::string id = snapshot_weight_id(set);
    CHECK(apogee::models::is_weight_id(id));

    // Order-free, and blind to non-shard files: the same weights are the
    // same id however they were listed.
    const std::vector<SnapshotFile> reordered{{"model-1.safetensors", 1, kDigestA},
                                              {"model-2.safetensors", 1, kDigestB},
                                              {"tokenizer.json", 9, ""}};
    CHECK(snapshot_weight_id(reordered) == id);

    // One shard changed is another set of weights.
    const std::vector<SnapshotFile> changed{{"model-1.safetensors", 1, kDigestA},
                                            {"model-2.safetensors", 1, kDigestA}};
    CHECK(snapshot_weight_id(changed) != id);

    // Without a digest on every shard there is no content id to give.
    CHECK(snapshot_weight_id({{"model.safetensors", 1, ""}}).empty());
    CHECK(snapshot_weight_id({{"config.json", 1, ""}}).empty());
}

TEST_CASE("a model name keeps what a directory can hold", "[models][store]") {
    using apogee::models::safe_model_name;
    CHECK(safe_model_name("llama3.2:3b") == "llama3.2-3b");
    CHECK(safe_model_name("library/qwen3:8b") == "library-qwen3-8b");
    CHECK(safe_model_name("..hidden") == "hidden");
    CHECK(safe_model_name("") == "model");
}

TEST_CASE("the store lists each format's weights, newest snapshot by when it landed",
          "[models][store]") {
    const Store store;
    const std::filesystem::path model = store.roots.models / "org--repo";
    write_file(model / "gguf" / "111111111111" / "repo-Q4_K_M.gguf");
    write_file(model / "gguf" / "111111111111" / "repo-Q4_K_M-mmproj.gguf");
    write_file(model / "gguf" / "111111111111" / "repo-Q4_K_M.json", "{}");
    place_snapshot(model / "safetensors" / "aaaaaaaaaaaa");
    place_snapshot(model / "safetensors" / "bbbbbbbbbbbb");
    // Work in progress is never listed.
    place_snapshot(model / "safetensors" / ".incoming-0123456789ab");
    write_file(model / "gguf" / ".incoming-0123456789ab" / "half.gguf");

    // The older set landed an hour before the newer one.
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(model / "safetensors" / "aaaaaaaaaaaa",
                                     now - std::chrono::hours{1});
    std::filesystem::last_write_time(model / "safetensors" / "bbbbbbbbbbbb", now);

    const auto ggufs = apogee::models::list_store_ggufs(store.roots);
    REQUIRE(ggufs.size() == 1);
    CHECK(ggufs.front().model == "org--repo");
    CHECK(ggufs.front().id == "111111111111");
    CHECK(ggufs.front().file.filename() == "repo-Q4_K_M.gguf");
    CHECK(ggufs.front().projector.filename() == "repo-Q4_K_M-mmproj.gguf");

    CHECK(apogee::models::list_store_snapshots(store.roots).size() == 2);
    const auto newest = apogee::models::newest_snapshot(store.roots, "org--repo");
    REQUIRE(newest.has_value());
    CHECK(newest->id == "bbbbbbbbbbbb");
    CHECK_FALSE(apogee::models::newest_snapshot(store.roots, "other").has_value());
    CHECK(apogee::models::list_store_models(store.roots) == std::vector<std::string>{"org--repo"});
}

TEST_CASE("SafeTensors sets can live under their own root", "[models][store]") {
    // paths.hf_dir: the big full-weight sets on another disk, GGUFs at home.
    const Store store;
    StoreRoots split = store.roots;
    split.safetensors = store.root.path() / "hf";
    place_snapshot(split.safetensors / "org--repo" / "safetensors" / "aaaaaaaaaaaa");
    write_file(split.models / "org--repo" / "gguf" / "111111111111" / "m.gguf");

    CHECK(apogee::models::weights_dir(split, "safetensors", "org--repo", "aaaaaaaaaaaa") ==
          split.safetensors / "org--repo" / "safetensors" / "aaaaaaaaaaaa");
    CHECK(apogee::models::weights_dir(split, "gguf", "org--repo", "111111111111") ==
          split.models / "org--repo" / "gguf" / "111111111111");
    CHECK(apogee::models::list_store_snapshots(split).size() == 1);
    CHECK(apogee::models::list_store_ggufs(split).size() == 1);
    CHECK(apogee::models::list_store_models(split) == std::vector<std::string>{"org--repo"});
}

TEST_CASE("a target is read from a model name, a ref, an id, a store path or a path",
          "[models][store]") {
    const Store store;
    const std::filesystem::path model = store.roots.models / "Qwen--Qwen3-8B";
    write_file(model / "gguf" / "111111111111" / "q.gguf");
    place_snapshot(model / "safetensors" / "aaaaaaaaaaaa");
    using apogee::models::resolve_store_target;

    const StoreTarget by_name = resolve_store_target(store.roots, "Qwen--Qwen3-8B");
    CHECK(by_name.model == "Qwen--Qwen3-8B");
    CHECK(by_name.format.empty());
    CHECK(by_name.path == model);
    CHECK(resolve_store_target(store.roots, "Qwen/Qwen3-8B").model == "Qwen--Qwen3-8B");

    const StoreTarget by_id = resolve_store_target(store.roots, "aaaaaaaaaaaa");
    CHECK(by_id.format == "safetensors");
    CHECK(by_id.path == model / "safetensors" / "aaaaaaaaaaaa");

    const StoreTarget by_store_path =
        resolve_store_target(store.roots, "Qwen--Qwen3-8B/gguf/111111111111");
    CHECK(by_store_path.format == "gguf");
    CHECK(by_store_path.id == "111111111111");

    // A real path inside the store is read back into its parts -- a file
    // inside an id directory names that id.
    const StoreTarget by_path =
        resolve_store_target(store.roots, (model / "gguf" / "111111111111" / "q.gguf").string());
    CHECK(by_path.model == "Qwen--Qwen3-8B");
    CHECK(by_path.id == "111111111111");
    CHECK_FALSE(by_path.outside);

    // A path elsewhere is the caller's to take or refuse.
    write_file(store.root.path() / "elsewhere" / "m.gguf");
    const StoreTarget outside =
        resolve_store_target(store.roots, (store.root.path() / "elsewhere" / "m.gguf").string());
    CHECK(outside.outside);

    CHECK_FALSE(resolve_store_target(store.roots, "nothing-here").error.empty());
    CHECK_FALSE(resolve_store_target(store.roots, "../Qwen--Qwen3-8B/gguf").error.empty());

    // The same id under two models is named, never guessed.
    write_file(store.roots.models / "other" / "gguf" / "aaaaaaaaaaaa" / "o.gguf");
    const StoreTarget ambiguous = resolve_store_target(store.roots, "aaaaaaaaaaaa");
    CHECK(ambiguous.error.find("more than one place") != std::string::npos);
}

TEST_CASE("committing weights never replaces an id that is already there", "[models][store]") {
    const Store store;
    const std::filesystem::path staged =
        apogee::models::make_incoming_dir(store.roots, "gguf", "m");
    CHECK(staged.filename().string().starts_with(".incoming-"));
    write_file(staged / "m.gguf", "new");
    const std::filesystem::path final_dir =
        apogee::models::weights_dir(store.roots, "gguf", "m", "111111111111");

    const apogee::models::Commit first = apogee::models::commit_weights(staged, final_dir);
    CHECK(first.error.empty());
    CHECK_FALSE(first.existed);
    CHECK(std::filesystem::exists(final_dir / "m.gguf"));
    CHECK_FALSE(std::filesystem::exists(staged));

    // Identical weights again: the copy already there wins, the new one goes.
    const std::filesystem::path again = apogee::models::make_incoming_dir(store.roots, "gguf", "m");
    write_file(again / "m.gguf", "a second copy");
    const apogee::models::Commit second = apogee::models::commit_weights(again, final_dir);
    CHECK(second.existed);
    CHECK_FALSE(std::filesystem::exists(again));
    std::ifstream in{final_dir / "m.gguf"};
    CHECK(std::string{std::istreambuf_iterator<char>{in}, {}} == "new");
}

TEST_CASE("removing weights tidies emptied format and model directories only", "[models][store]") {
    const Store store;
    const std::filesystem::path model = store.roots.models / "m";
    write_file(model / "gguf" / "111111111111" / "a.gguf");
    write_file(model / "gguf" / "222222222222" / "b.gguf");
    place_snapshot(model / "safetensors" / "aaaaaaaaaaaa");

    CHECK(apogee::models::remove_weights(model / "gguf" / "111111111111").empty());
    CHECK_FALSE(std::filesystem::exists(model / "gguf" / "111111111111"));
    CHECK(std::filesystem::exists(model / "gguf"));  // another id still there

    CHECK(apogee::models::remove_weights(model / "gguf" / "222222222222").empty());
    CHECK_FALSE(std::filesystem::exists(model / "gguf"));
    CHECK(std::filesystem::exists(model));  // the SafeTensors set keeps it

    CHECK(apogee::models::remove_weights(model / "safetensors" / "aaaaaaaaaaaa").empty());
    CHECK_FALSE(std::filesystem::exists(model));
    CHECK(std::filesystem::exists(store.roots.models));
}

TEST_CASE("the flat layout is found, with records and projectors kept together",
          "[models][store][legacy]") {
    const Store store;
    const std::filesystem::path models = store.roots.models;
    write_file(models / "owner-repo-model.gguf");
    apogee::models::Sidecar record;
    record.ref = "owner/repo:model.gguf";
    record.source = "huggingface";
    REQUIRE(apogee::models::write_sidecar(models / "owner-repo-model.gguf", record));
    write_file(models / "llava.gguf");
    write_file(models / "llava-mmproj.gguf");
    write_file(models / "stray-mmproj.gguf");
    place_snapshot(models / "org--base");
    // Already in the new layout: not legacy.
    write_file(models / "new--model" / "gguf" / "111111111111" / "n.gguf");

    const apogee::models::LegacyLayout legacy = apogee::models::find_legacy(store.roots);
    REQUIRE(legacy.ggufs.size() == 3);
    CHECK(legacy.ggufs.at(0).model == "llava");
    CHECK(legacy.ggufs.at(0).projector == models / "llava-mmproj.gguf");
    CHECK(legacy.ggufs.at(1).model == "owner--repo");  // from its record's ref
    CHECK(legacy.ggufs.at(1).sidecar == models / "owner-repo-model.json");
    CHECK(legacy.ggufs.at(2).file == models / "stray-mmproj.gguf");  // no model to join
    REQUIRE(legacy.snapshots.size() == 1);
    CHECK(legacy.snapshots.front().model == "org--base");

    CHECK(apogee::models::legacy_refusal(store.roots, "org--base").find("models migrate") !=
          std::string::npos);
    CHECK(apogee::models::legacy_refusal(store.roots, "new--model").empty());
}

namespace {

/// A snapshot damaged exactly the way the old per-file records damaged one,
/// with the record that lets it be put right.
struct DamagedSnapshot {
    apogee::testing::TempDir root{"damaged-" + std::to_string(std::random_device{}())};
    std::filesystem::path dir = root.path() / "owner--repo" / "safetensors" / "aaaaaaaaaaaa";
    std::string config = R"({"architectures": ["LlamaForCausalLM"], "hidden_size": 16})";
    std::string tokenizer = R"({"model": {"type": "BPE"}})";

    DamagedSnapshot() {
        write_file(dir / "config.json", config);
        write_file(dir / "tokenizer.json", tokenizer);
        write_file(dir / "model.safetensors", "weights");
        apogee::models::Snapshot record;
        record.ref = "owner/repo";
        record.source = "huggingface";
        for (const char* name : {"config.json", "tokenizer.json", "model.safetensors"}) {
            record.files.push_back(
                {name, static_cast<std::int64_t>(std::filesystem::file_size(dir / name)),
                 apogee::models::file_sha256(dir / name)});
        }
        REQUIRE(apogee::models::write_snapshot(dir, record));

        // The damage: config.json is its own download record, a stray record
        // sits beside the shard, and the tokenizer is gone.
        const std::string download_record =
            R"({"file": "config.json", "file_digest": "ab", "source_url": "https://x",
                "verification": {"size_checked": true}})";
        write_file(dir / "config.json", download_record);
        write_file(dir / "model.json",
                   R"({"file": "model.safetensors", "file_digest": "cd", "source_url": "https://y",
                       "verification": {}})");
        std::filesystem::remove(dir / "tokenizer.json");
    }

    /// Serves the real bytes, as the repository would.
    [[nodiscard]] apogee::models::SnapshotFetchFn honest_fetch() const {
        return [this](const apogee::models::SnapshotFile& file, const std::filesystem::path& to) {
            write_file(to, file.path == "config.json" ? config : tokenizer);
            return std::string{};
        };
    }
};

}  // namespace

TEST_CASE("a damaged snapshot is judged against its own record", "[models][store][repair]") {
    const DamagedSnapshot snapshot;
    const apogee::models::SnapshotDamage damage =
        apogee::models::find_snapshot_damage(snapshot.dir);
    REQUIRE(damage.error.empty());
    std::vector<std::string> refetch;
    for (const apogee::models::SnapshotFile& file : damage.refetch) {
        refetch.push_back(file.path);
    }
    CHECK(refetch == std::vector<std::string>{"config.json", "tokenizer.json"});
    REQUIRE(damage.stray_records.size() == 1);
    CHECK(damage.stray_records.front().filename() == "model.json");
    // The shard was never touched and is not fetched again.
}

TEST_CASE("repairing a snapshot fetches only what is broken and verifies it",
          "[models][store][repair]") {
    const DamagedSnapshot snapshot;
    const apogee::models::SnapshotDamage damage =
        apogee::models::find_snapshot_damage(snapshot.dir);
    const apogee::models::SnapshotRepair repair =
        apogee::models::repair_snapshot(snapshot.dir, damage, snapshot.honest_fetch());
    INFO(repair.error);
    REQUIRE(repair.error.empty());
    CHECK(repair.fetched == std::vector<std::string>{"config.json", "tokenizer.json"});
    CHECK(repair.removed == std::vector<std::string>{"model.json"});
    CHECK_FALSE(apogee::models::config_is_download_record(snapshot.dir));
    CHECK(apogee::models::find_snapshot_damage(snapshot.dir).empty());
}

TEST_CASE("a fetched file that is not what the record says replaces nothing",
          "[models][store][repair]") {
    const DamagedSnapshot snapshot;
    const apogee::models::SnapshotDamage damage =
        apogee::models::find_snapshot_damage(snapshot.dir);
    const apogee::models::SnapshotFetchFn lying = [](const apogee::models::SnapshotFile&,
                                                     const std::filesystem::path& to) {
        write_file(to, "something else entirely");
        return std::string{};
    };
    const apogee::models::SnapshotRepair repair =
        apogee::models::repair_snapshot(snapshot.dir, damage, lying);
    CHECK(repair.error.find("config.json") != std::string::npos);
    CHECK(repair.fetched.empty());
    // Still the damaged file, and no half-fetched copy beside it.
    CHECK(apogee::models::config_is_download_record(snapshot.dir));
    CHECK_FALSE(std::filesystem::exists(snapshot.dir / "config.json.repair"));
}

TEST_CASE("a snapshot with no record cannot be judged, and says so", "[models][store][repair]") {
    const Store store;
    place_snapshot(store.roots.models / "m" / "safetensors" / "aaaaaaaaaaaa");
    const apogee::models::SnapshotDamage damage = apogee::models::find_snapshot_damage(
        store.roots.models / "m" / "safetensors" / "aaaaaaaaaaaa");
    CHECK(damage.error.find("nothing to judge") != std::string::npos);
    CHECK(damage.empty());
}
