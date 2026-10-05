#include "modelstore/store.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "contracts/sha256.h"
#include "modelstore/sidecar.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/file_time.h"
#include "support/mlx_model.h"

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

TEST_CASE("a handle is read without asking the disk, and only a handle is one", "[models][store]") {
    // What a conversion's or a quantization's record names as its parent (M4).
    const std::optional<apogee::models::WeightsHandle> handle =
        apogee::models::parse_weights_handle("org--m/gguf/0123456789ab");
    REQUIRE(handle.has_value());
    CHECK(handle->model == "org--m");
    CHECK(handle->format == "gguf");
    CHECK(handle->id == "0123456789ab");
    CHECK(apogee::models::weights_handle(handle->model, handle->format, handle->id) ==
          "org--m/gguf/0123456789ab");
    CHECK(apogee::models::parse_weights_handle("org--m/safetensors/0123456789ab").has_value());
    for (const char* not_one :
         {"org--m", "org--m/gguf", "org--m/gguf/", "org--m/onnx/0123456789ab",
          "org--m/gguf/0123456789AB", "org--m/gguf/0123456789ab/x.gguf", "/models/m/gguf/x",
          "../gguf/0123456789ab", "/gguf/0123456789ab", "org/m:file.gguf"}) {
        INFO(not_one);
        CHECK_FALSE(apogee::models::parse_weights_handle(not_one).has_value());
    }
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
    apogee::testing::set_modified_time(model / "safetensors" / "aaaaaaaaaaaa",
                                       now - std::chrono::hours{1});
    apogee::testing::set_modified_time(model / "safetensors" / "bbbbbbbbbbbb", now);

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

TEST_CASE("a projector made after its model joins the stored directory, never replacing one",
          "[models][store][projector]") {
    // The id is the model file's hash alone, so a projector converted later --
    // or an Ollama layer that failed the first time -- belongs in the
    // directory the model already occupies.
    const Store store;
    const std::filesystem::path stored = store.roots.models / "m" / "gguf" / "aaaaaaaaaaaa";
    write_file(stored / "m-F16.gguf", "model");

    const std::filesystem::path staged = store.roots.models / "m" / "gguf" / ".incoming-1";
    write_file(staged / "m-F16.gguf", "model");
    write_file(staged / "m-F16-mmproj.gguf", "projector");
    write_file(staged / "m-F16-mmproj.json", "{}");
    const apogee::models::Commit first = apogee::models::commit_weights(staged, stored);
    REQUIRE(first.error.empty());
    CHECK(first.existed);
    CHECK(first.projector_added);
    CHECK(std::filesystem::exists(stored / "m-F16-mmproj.gguf"));
    CHECK(std::filesystem::exists(stored / "m-F16-mmproj.json"));
    CHECK_FALSE(std::filesystem::exists(staged));

    // A second projector finds one there: the stored one stays.
    write_file(staged / "other-mmproj.gguf", "another projector");
    const apogee::models::Commit second = apogee::models::commit_weights(staged, stored);
    CHECK(second.existed);
    CHECK_FALSE(second.projector_added);
    CHECK_FALSE(std::filesystem::exists(stored / "other-mmproj.gguf"));
    CHECK_FALSE(std::filesystem::exists(staged));
}

TEST_CASE("a projector is shared into another directory with its record",
          "[models][store][projector]") {
    const Store store;
    const std::filesystem::path from = store.roots.models / "m" / "gguf" / "aaaaaaaaaaaa";
    write_file(from / "m-F16-mmproj.gguf", "projector");
    apogee::models::Sidecar record;
    record.ref = "m/safetensors/bbbbbbbbbbbb";
    record.source = "convert";
    REQUIRE(apogee::models::write_record(from / "m-F16-mmproj.gguf", record).empty());

    const std::filesystem::path into = store.roots.models / "m" / "gguf" / ".incoming-2";
    REQUIRE(apogee::models::share_projector(from / "m-F16-mmproj.gguf", into).empty());
    CHECK(std::filesystem::exists(into / "m-F16-mmproj.gguf"));
    // One set of bytes, two names: quantizing a model does not copy its
    // projector's gigabyte.
    CHECK(std::filesystem::hard_link_count(from / "m-F16-mmproj.gguf") == 2);
    const std::optional<apogee::models::Sidecar> copied =
        apogee::models::load_sidecar(into / "m-F16-mmproj.gguf");
    REQUIRE(copied.has_value());
    CHECK(copied->file_digest == apogee::models::sha256_hex("projector"));
    CHECK(copied->file_size == 9);

    // Never over a file already there.
    CHECK_FALSE(apogee::models::share_projector(from / "m-F16-mmproj.gguf", into).empty());
}

TEST_CASE("a model file's projector sits beside it under its stem", "[models][store][projector]") {
    CHECK(apogee::models::projector_path_for("/s/m/gguf/x/Qwen-F16.gguf") ==
          std::filesystem::path{"/s/m/gguf/x/Qwen-F16-mmproj.gguf"});
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

TEST_CASE("a staging directory is claimed by its process until it is committed or removed",
          "[models][store][staging]") {
    const Store store;
    const std::filesystem::path staging =
        apogee::models::make_incoming_dir(store.roots, "gguf", "org--repo");
    const std::filesystem::path owner = apogee::models::staging_owner_path(staging);
    REQUIRE(std::filesystem::exists(owner));
    long pid = 0;
    {
        // Closed before the commit: Windows refuses to remove a file that is
        // still open, and the commit must remove this one.
        std::ifstream in{owner};
        in >> pid;
    }
    CHECK(pid == apogee::platform::current_process_id());
    // Beside, not inside: the rename that commits it cannot carry the claim.
    CHECK(owner.parent_path() == staging.parent_path());

    write_file(staging / "m.gguf", "weights");
    const apogee::models::Commit commit = apogee::models::commit_weights(
        staging, apogee::models::weights_dir(store.roots, "gguf", "org--repo", "aaaaaaaaaaaa"));
    REQUIRE(commit.error.empty());
    CHECK_FALSE(std::filesystem::exists(owner));
    CHECK_FALSE(std::filesystem::exists(commit.dir / owner.filename()));

    const std::filesystem::path dropped =
        apogee::models::make_incoming_dir(store.roots, "gguf", "org--repo");
    REQUIRE(apogee::models::remove_weights(dropped).empty());
    CHECK_FALSE(std::filesystem::exists(apogee::models::staging_owner_path(dropped)));
}

TEST_CASE("abandoned staging is what no running process owns", "[models][store][staging]") {
    // A killed `models convert` left two 52 GB copies behind (2026-09-23).
    // What is live must never be listed: `check --fix` removes what is.
    const Store store;
    const auto staging = [&store](std::string_view format, std::string_view name) {
        const std::filesystem::path dir =
            store.roots.models / "org--repo" / std::string{format} / std::string{name};
        write_file(dir / "big.gguf", std::string(1000, 'x'));
        return dir;
    };
    const auto owned_by = [](const std::filesystem::path& dir, long pid) {
        std::ofstream{apogee::models::staging_owner_path(dir)} << pid << "\n";
    };

    const std::filesystem::path live = staging("gguf", ".incoming-111111111111");
    owned_by(live, apogee::platform::current_process_id());
    const std::filesystem::path dead = staging("gguf", ".incoming-222222222222");
    owned_by(dead, 999999999);
    const std::filesystem::path fresh = staging("safetensors", ".incoming-333333333333");
    const std::filesystem::path stale = staging("safetensors", ".incoming-444444444444");
    // Made before markers existed, untouched since yesterday.
    const auto yesterday = std::filesystem::file_time_type::clock::now() - std::chrono::hours{24};
    apogee::testing::set_modified_time(stale / "big.gguf", yesterday);
    apogee::testing::set_modified_time(stale, yesterday);

    std::vector<std::filesystem::path> found;
    for (const apogee::models::AbandonedStaging& leftover :
         apogee::models::find_abandoned_staging(store.roots)) {
        found.push_back(leftover.dir);
        CHECK(leftover.bytes == 1000);
    }
    std::ranges::sort(found);
    CHECK(found == std::vector<std::filesystem::path>{dead, stale});
}

TEST_CASE("a commit whose hash is stopped leaves its staging to the caller",
          "[models][store][staging]") {
    const Store store;
    const std::filesystem::path staging =
        apogee::models::make_incoming_dir(store.roots, "gguf", "org--repo");
    write_file(staging / "m.gguf", std::string(3 * 1024 * 1024, 'g'));
    const apogee::models::StoredFile stored =
        apogee::models::commit_gguf(store.roots, "org--repo", staging, staging / "m.gguf",
                                    apogee::models::Sidecar{}, [](std::int64_t) { return false; });
    CHECK(stored.error == apogee::models::kStopped);
    CHECK(std::filesystem::exists(staging / "m.gguf"));
    CHECK(apogee::models::list_store_ggufs(store.roots).empty());
}

// ---- the mlx/ row (27b) -------------------------------------------------------------

TEST_CASE("an MLX model lands under its model's mlx/ by id, beside the other formats",
          "[models][store][mlx]") {
    // Runnable like a GGUF, so under the models directory like one -- never
    // under paths.hf_dir, where full-weight sets go.
    const Store store;
    StoreRoots split = store.roots;
    split.safetensors = store.root.path() / "hf";
    const std::filesystem::path dir = apogee::models::weights_dir(split, apogee::models::kMlxFormat,
                                                                  "org--m-4bit", "cccccccccccc");
    CHECK(dir == split.models / "org--m-4bit" / "mlx" / "cccccccccccc");
    apogee::testing::write_mlx_model(dir);
    write_file(split.models / "org--m-4bit" / "mlx" / ".incoming-0123456789ab" / "config.json");

    const std::vector<apogee::models::StoredMlx> stored = apogee::models::list_store_mlx(split);
    REQUIRE(stored.size() == 1);  // work in progress is never listed
    CHECK(stored.front().model == "org--m-4bit");
    CHECK(stored.front().id == "cccccccccccc");
    CHECK(stored.front().dir == dir);
    CHECK(apogee::models::list_store_mlx(split, "other").empty());
    CHECK(apogee::models::list_store_models(split) == std::vector<std::string>{"org--m-4bit"});
    CHECK(apogee::models::backend_type_for_format(apogee::models::kMlxFormat) == "mlx");
    CHECK(apogee::models::backend_type_for_format(apogee::models::kGgufFormat) == "llamacpp");
    CHECK(apogee::models::backend_type_for_format(apogee::models::kSafetensorsFormat).empty());

    // A backend's model_path names the directory, with or without a slash.
    CHECK(apogee::models::stored_mlx_at(stored, dir).has_value());
    CHECK(apogee::models::stored_mlx_at(stored, std::filesystem::path{dir.string() + "/"})
              .has_value());
    CHECK_FALSE(apogee::models::stored_mlx_at(stored, dir.parent_path()).has_value());

    // Its handle parses like any other format's.
    const std::optional<apogee::models::WeightsHandle> handle =
        apogee::models::parse_weights_handle("org--m-4bit/mlx/cccccccccccc");
    REQUIRE(handle.has_value());
    CHECK(handle->format == "mlx");
}

TEST_CASE("an MLX model is named like every other weights: by model, handle, id or path",
          "[models][store][mlx]") {
    const Store store;
    const std::filesystem::path model = store.roots.models / "mlx-community--M-4bit";
    apogee::testing::write_mlx_model(model / "mlx" / "cccccccccccc");
    using apogee::models::resolve_store_target;

    const StoreTarget by_name = resolve_store_target(store.roots, "mlx-community/M-4bit");
    CHECK(by_name.error.empty());
    CHECK(by_name.model == "mlx-community--M-4bit");
    CHECK(by_name.path == model);

    const StoreTarget by_id = resolve_store_target(store.roots, "cccccccccccc");
    CHECK(by_id.format == "mlx");
    CHECK(by_id.path == model / "mlx" / "cccccccccccc");

    const StoreTarget by_handle =
        resolve_store_target(store.roots, "mlx-community--M-4bit/mlx/cccccccccccc");
    CHECK(by_handle.format == "mlx");
    CHECK(by_handle.id == "cccccccccccc");
    CHECK(resolve_store_target(store.roots, "mlx-community--M-4bit/mlx").format == "mlx");

    const StoreTarget by_path = resolve_store_target(
        store.roots, (model / "mlx" / "cccccccccccc" / "model.safetensors").string());
    CHECK(by_path.format == "mlx");
    CHECK(by_path.id == "cccccccccccc");
}

TEST_CASE("an MLX model is removed whole, never a shard alone", "[models][store][mlx]") {
    const Store store;
    const std::filesystem::path model = store.roots.models / "org--m";
    const std::filesystem::path dir = model / "mlx" / "cccccccccccc";
    apogee::testing::write_mlx_model(dir, apogee::testing::MlxModelSpec{.shards = 3});
    write_file(model / "gguf" / "111111111111" / "m.gguf");

    REQUIRE(apogee::models::remove_weights(dir).empty());
    CHECK_FALSE(std::filesystem::exists(dir));
    CHECK_FALSE(std::filesystem::exists(model / "mlx"));  // emptied: tidied
    CHECK(std::filesystem::exists(model / "gguf"));       // another format stays
}

TEST_CASE("an interrupted MLX download or conversion is found as leftovers, by its owner",
          "[models][store][mlx][staging]") {
    const Store store;
    const std::filesystem::path formats = store.roots.models / "org--m" / "mlx";
    // A conversion killed mid-run: its staging directory, its owner gone.
    const std::filesystem::path dead = formats / ".incoming-222222222222";
    write_file(dead / "model.safetensors", std::string(100, 'x'));
    std::ofstream{apogee::models::staging_owner_path(dead)} << 999999999 << "\n";
    // A download killed mid-tree: `acquire_tree` fills `<name>.staging`, and
    // the name's marker claims it -- this process is alive, so it is live.
    const std::filesystem::path name = formats / ".incoming-333333333333";
    const std::filesystem::path tree = formats / ".incoming-333333333333.staging";
    write_file(tree / "config.json");
    std::ofstream{apogee::models::staging_owner_path(name)}
        << apogee::platform::current_process_id() << "\n";

    std::vector<std::filesystem::path> found;
    for (const apogee::models::AbandonedStaging& leftover :
         apogee::models::find_abandoned_staging(store.roots)) {
        found.push_back(leftover.dir);
    }
    CHECK(found == std::vector<std::filesystem::path>{dead});

    // Once its owner is gone, the tree is a leftover too, and removing it
    // takes the marker that claimed it.
    std::ofstream{apogee::models::staging_owner_path(name)} << 999999999 << "\n";
    found.clear();
    for (const apogee::models::AbandonedStaging& leftover :
         apogee::models::find_abandoned_staging(store.roots)) {
        found.push_back(leftover.dir);
    }
    std::ranges::sort(found);
    CHECK(found == std::vector<std::filesystem::path>{dead, tree});
    REQUIRE(apogee::models::remove_weights(tree).empty());
    CHECK_FALSE(std::filesystem::exists(apogee::models::staging_owner_path(name)));
}

TEST_CASE("an MLX directory in the flat layout is refused, naming the migration",
          "[models][store][mlx][legacy]") {
    const Store store;
    apogee::testing::write_mlx_model(store.roots.models / "mlx-community--M-4bit");
    const apogee::models::LegacyLayout legacy = apogee::models::find_legacy(store.roots);
    REQUIRE(legacy.snapshots.size() == 1);
    CHECK(apogee::models::legacy_refusal(store.roots, "mlx-community/M-4bit")
              .find("run 'apogee models migrate'") != std::string::npos);
}

TEST_CASE("an MLX directory is committed by its shards' digests, with every file on record",
          "[models][store][mlx]") {
    const Store store;
    const auto stage = [&store]() {
        const std::filesystem::path staging =
            apogee::models::incoming_path(store.roots, apogee::models::kMlxFormat, "org--m");
        apogee::testing::write_mlx_model(staging, apogee::testing::MlxModelSpec{.shards = 2});
        return staging;
    };
    const std::filesystem::path staging = stage();
    // A download already verified one file: it is not hashed again.
    apogee::models::Snapshot record;
    record.ref = "org--m/safetensors/aaaaaaaaaaaa";
    record.source = "convert";
    record.transform = "mlx_lm.convert 4bit";
    const std::string config_digest = apogee::models::file_sha256(staging / "config.json");
    record.files.push_back(
        {"config.json",
         static_cast<std::int64_t>(std::filesystem::file_size(staging / "config.json")),
         "recorded-digest"});
    std::int64_t heard = 0;
    const apogee::models::StoredDirectory stored = apogee::models::commit_mlx(
        store.roots, "org--m", staging, record, [&heard](std::int64_t hashed) {
            heard = hashed;
            return true;
        });
    REQUIRE(stored.error.empty());
    CHECK_FALSE(stored.existed);
    CHECK_FALSE(std::filesystem::exists(staging));
    CHECK_FALSE(std::filesystem::exists(apogee::models::staging_owner_path(staging)));
    CHECK(stored.dir.parent_path() == store.roots.models / "org--m" / "mlx");
    CHECK(heard > 0);

    const std::optional<apogee::models::Snapshot> written =
        apogee::models::load_snapshot(stored.dir);
    REQUIRE(written.has_value());
    CHECK(written->ref == "org--m/safetensors/aaaaaaaaaaaa");
    CHECK(written->transform == "mlx_lm.convert 4bit");
    CHECK_FALSE(written->pulled_at.empty());
    std::vector<std::string> paths;
    for (const apogee::models::SnapshotFile& file : written->files) {
        paths.push_back(file.path);
        if (file.path == "config.json") {
            CHECK(file.sha256 == "recorded-digest");
        } else {
            CHECK(file.sha256 == apogee::models::file_sha256(stored.dir / file.path));
        }
    }
    CHECK(paths == std::vector<std::string>{"config.json", "model-00001-of-00002.safetensors",
                                            "model-00002-of-00002.safetensors",
                                            "model.safetensors.index.json", "tokenizer.json",
                                            "tokenizer_config.json"});
    CHECK(stored.dir.filename() == apogee::models::snapshot_weight_id(written->files));
    (void)config_digest;

    // The same weights again find the directory they occupy.
    const std::filesystem::path again = stage();
    const apogee::models::StoredDirectory twice =
        apogee::models::commit_mlx(store.roots, "org--m", again, apogee::models::Snapshot{});
    CHECK(twice.existed);
    CHECK(twice.dir == stored.dir);
    CHECK_FALSE(std::filesystem::exists(again));
    CHECK(apogee::models::list_store_mlx(store.roots).size() == 1);

    // A hash that is stopped leaves its staging to the caller.
    const std::filesystem::path stopped = stage();
    const apogee::models::StoredDirectory halted =
        apogee::models::commit_mlx(store.roots, "org--m", stopped, apogee::models::Snapshot{},
                                   [](std::int64_t) { return false; });
    CHECK(halted.error == apogee::models::kStopped);
    CHECK(std::filesystem::exists(stopped / "config.json"));
}
