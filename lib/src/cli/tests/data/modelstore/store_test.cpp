#include "modelstore/store.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/sha256.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "platform/platform.h"
#include "support/draft_head_configs.h"
#include "support/env_guard.h"
#include "support/file_time.h"
#include "support/mlx_model.h"
#include "support/pipeline_model.h"
#include "support/towered_configs.h"
#include "support/voice_model.h"
#include "support/wan_original.h"

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

TEST_CASE("weights are named for a sentence: a stored directory by its handle, a file by its name",
          "[models][store]") {
    // 53c: where the gate says it scored, and the doctor's rows.
    using apogee::models::weights_label;
    CHECK(weights_label("/h/models/org--m/mlx/0123456789ab") == "org--m/mlx/0123456789ab");
    CHECK(weights_label("/h/models/org--m/mlx/0123456789ab/") == "org--m/mlx/0123456789ab");
    CHECK(weights_label("/h/models/org--m/gguf/0123456789ab/m-Q4_K_M.gguf") == "m-Q4_K_M.gguf");
    CHECK(weights_label("/elsewhere/base.gguf") == "base.gguf");
    // An id under no format of the store is a name like any.
    CHECK(weights_label("/h/other/0123456789ab") == "0123456789ab");
    CHECK(weights_label("/h/models/org--m/mlx/not-an-id") == "not-an-id");
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

// ---- the pipeline/ row (49a) --------------------------------------------------------

TEST_CASE("a pipeline lands under its model's pipeline/ by id, named and removed whole",
          "[models][store][pipeline]") {
    // What video generation runs, so under the models directory -- never
    // under paths.hf_dir -- and no backend type runs one.
    const Store store;
    StoreRoots split = store.roots;
    split.safetensors = store.root.path() / "hf";
    const std::filesystem::path model = split.models / "Wan-AI--Wan2.2-TI2V-5B-Diffusers";
    const std::filesystem::path dir = apogee::models::weights_dir(
        split, apogee::models::kPipelineFormat, "Wan-AI--Wan2.2-TI2V-5B-Diffusers", "dddddddddddd");
    CHECK(dir == model / "pipeline" / "dddddddddddd");
    apogee::testing::write_pipeline_model(dir);
    write_file(model / "pipeline" / ".incoming-0123456789ab" / "model_index.json");
    write_file(model / "gguf" / "111111111111" / "m.gguf");

    const std::vector<apogee::models::StoredPipeline> stored =
        apogee::models::list_store_pipelines(split);
    REQUIRE(stored.size() == 1);  // work in progress is never listed
    CHECK(stored.front().model == "Wan-AI--Wan2.2-TI2V-5B-Diffusers");
    CHECK(stored.front().id == "dddddddddddd");
    CHECK(apogee::models::list_store_pipelines(split, "other").empty());
    CHECK(apogee::models::stored_pipeline_at(stored, std::filesystem::path{dir.string() + "/"})
              .has_value());
    CHECK(apogee::models::list_store_models(split) ==
          std::vector<std::string>{"Wan-AI--Wan2.2-TI2V-5B-Diffusers"});
    CHECK(apogee::models::backend_type_for_format(apogee::models::kPipelineFormat).empty());

    // The one list of formats a verb walks, and the one question.
    const std::span<const std::string_view> formats = apogee::models::store_formats();
    CHECK(std::vector<std::string_view>(formats.begin(), formats.end()) ==
          std::vector<std::string_view>{"gguf", "safetensors", "mlx", "pipeline", "voice"});
    CHECK(apogee::models::is_store_format("pipeline"));
    CHECK_FALSE(apogee::models::is_store_format("pipelines"));

    // Named like every other weights: by model, handle, bare id or path.
    using apogee::models::resolve_store_target;
    CHECK(resolve_store_target(split, "Wan-AI/Wan2.2-TI2V-5B-Diffusers").path == model);
    const StoreTarget by_id = resolve_store_target(split, "dddddddddddd");
    CHECK(by_id.format == "pipeline");
    CHECK(by_id.path == dir);
    const StoreTarget by_handle =
        resolve_store_target(split, "Wan-AI--Wan2.2-TI2V-5B-Diffusers/pipeline/dddddddddddd");
    CHECK(by_handle.format == "pipeline");
    CHECK(by_handle.id == "dddddddddddd");
    const StoreTarget by_path = resolve_store_target(split, (dir / "vae" / "config.json").string());
    CHECK(by_path.format == "pipeline");
    CHECK(by_path.id == "dddddddddddd");
    REQUIRE(apogee::models::parse_weights_handle(
                "Wan-AI--Wan2.2-TI2V-5B-Diffusers/pipeline/dddddddddddd")
                .has_value());

    // Removed whole, never a component alone; another format stays.
    REQUIRE(apogee::models::remove_weights(dir).empty());
    CHECK_FALSE(std::filesystem::exists(dir));
    CHECK(std::filesystem::exists(model / "gguf"));
}

TEST_CASE("an interrupted pipeline download is found as a leftover, by its owner",
          "[models][store][pipeline][staging]") {
    const Store store;
    const std::filesystem::path dead =
        store.roots.models / "org--video" / "pipeline" / ".incoming-222222222222";
    write_file(dead / "transformer" / "diffusion_pytorch_model.safetensors", std::string(100, 'x'));
    std::ofstream{apogee::models::staging_owner_path(dead)} << 999999999 << "\n";
    const std::vector<apogee::models::AbandonedStaging> found =
        apogee::models::find_abandoned_staging(store.roots);
    REQUIRE(found.size() == 1);
    CHECK(found.front().dir == dead);
    CHECK(found.front().bytes == 100);
}

TEST_CASE("a pipeline is committed by every component's shards, with every file on record",
          "[models][store][pipeline]") {
    const Store store;
    const auto stage = [&store]() {
        const std::filesystem::path staging =
            apogee::models::incoming_path(store.roots, apogee::models::kPipelineFormat, "org--v");
        apogee::testing::write_pipeline_model(staging);
        return staging;
    };
    apogee::models::Snapshot record;
    record.ref = "org/v";
    record.source = "huggingface";
    const apogee::models::StoredDirectory stored =
        apogee::models::commit_pipeline(store.roots, "org--v", stage(), record);
    REQUIRE(stored.error.empty());
    CHECK_FALSE(stored.existed);
    CHECK(stored.dir.parent_path() == store.roots.models / "org--v" / "pipeline");
    const std::optional<apogee::models::Snapshot> written =
        apogee::models::load_snapshot(stored.dir);
    REQUIRE(written.has_value());
    CHECK(written->ref == "org/v");
    // Every file of every component, by its path under the pipeline.
    std::vector<std::string> shards;
    for (const apogee::models::SnapshotFile& file : written->files) {
        CHECK(file.sha256 == apogee::models::file_sha256(stored.dir / file.path));
        if (file.path.ends_with(".safetensors")) {
            shards.push_back(file.path);
        }
    }
    CHECK(shards == apogee::testing::pipeline_shard_names({}));
    CHECK(written->files.size() == 12);
    CHECK(stored.dir.filename() == apogee::models::snapshot_weight_id(written->files));

    // The same weights again find the directory they occupy.
    const apogee::models::StoredDirectory twice =
        apogee::models::commit_pipeline(store.roots, "org--v", stage(), record);
    REQUIRE(twice.error.empty());
    CHECK(twice.existed);
    CHECK(twice.dir == stored.dir);
    CHECK(apogee::models::list_store_pipelines(store.roots).size() == 1);
}

// ---- the voice/ row (50a) -----------------------------------------------------------

TEST_CASE("a voice model lands under its model's voice/ by the digest over its weights",
          "[models][store][voice]") {
    const Store store;
    const std::filesystem::path staging =
        apogee::models::incoming_path(store.roots, apogee::models::kVoiceFormat, "hexgrad--K");
    apogee::testing::write_voice_model(staging);
    apogee::models::Snapshot record;
    record.ref = "hexgrad/K";
    record.source = "huggingface";
    const apogee::models::StoredDirectory stored =
        apogee::models::commit_voice(store.roots, "hexgrad--K", staging, record);
    REQUIRE(stored.error.empty());
    CHECK(stored.dir.parent_path() == store.roots.models / "hexgrad--K" / "voice");
    const std::optional<apogee::models::Snapshot> written =
        apogee::models::load_snapshot(stored.dir);
    REQUIRE(written.has_value());
    // The id is the checkpoint's and the voices' digest, never random.
    CHECK(stored.dir.filename() == apogee::models::voice_weight_id(written->files));
    CHECK(apogee::models::snapshot_weight_id(written->files).empty());

    const std::vector<apogee::models::StoredVoice> voices =
        apogee::models::list_store_voices(store.roots);
    REQUIRE(voices.size() == 1);
    CHECK(apogee::models::stored_voice_at(voices, stored.dir).has_value());
    const StoreTarget by_id =
        apogee::models::resolve_store_target(store.roots, stored.dir.filename().string());
    CHECK(by_id.format == "voice");
    CHECK(apogee::models::resolve_store_target(store.roots, "hexgrad/K").path ==
          store.roots.models / "hexgrad--K");
    CHECK(apogee::models::backend_type_for_format(apogee::models::kVoiceFormat).empty());
    REQUIRE(apogee::models::remove_weights(stored.dir).empty());
    CHECK(apogee::models::list_store_voices(store.roots).empty());
}

namespace {

/// The user's Gemma4-12B-Assistant, as its own config.json says it (2026-10-21):
/// four layers 1024 wide, reading a 3840-wide base through a projection --
/// with the other three, the user's four (53i, `draft_head_configs.h`).
constexpr const char* kAssistantConfig = apogee::testing::kGemma4Assistant12BConfig;

/// A SafeTensors set whose config.json is `config`.
void place_snapshot_with(const std::filesystem::path& dir, const std::string& config) {
    write_file(dir / "config.json", config);
    write_file(dir / "model.safetensors");
}

}  // namespace

TEST_CASE("a draft head is read from its config: the width it reads, or its architecture's name",
          "[models][store][draft_head]") {
    const Store store;
    const std::filesystem::path head = store.root.path() / "head";
    place_snapshot_with(head, kAssistantConfig);
    const std::optional<apogee::models::DraftHead> read = apogee::models::snapshot_draft_head(head);
    REQUIRE(read.has_value());
    CHECK(read->architecture == "Gemma4UnifiedAssistantForCausalLM");
    CHECK(read->backbone_hidden_size == 3840);
    CHECK(read->family == "gemma4");

    // Named by an architecture llama.cpp converts as one, its width unsaid.
    const std::filesystem::path named = store.root.path() / "named";
    place_snapshot_with(named, R"({"architectures": ["Gemma4AssistantForCausalLM"],
                                   "model_type": "gemma4_assistant"})");
    const std::optional<apogee::models::DraftHead> by_name =
        apogee::models::snapshot_draft_head(named);
    REQUIRE(by_name.has_value());
    CHECK(by_name->backbone_hidden_size == 0);

    // A model of its own is none -- nor is a vendor's type name alone, M15's case.
    const std::filesystem::path base = store.root.path() / "base";
    place_snapshot_with(base, R"({"architectures": ["Gemma4UnifiedForConditionalGeneration"],
                                  "model_type": "gemma4_unified",
                                  "text_config": {"hidden_size": 3840}})");
    CHECK_FALSE(apogee::models::snapshot_draft_head(base).has_value());
    const std::filesystem::path variant = store.root.path() / "variant";
    place_snapshot_with(variant, R"({"architectures": ["LlamaForCausalLM"],
                                     "model_type": "gemma4_unified_assistant"})");
    CHECK_FALSE(apogee::models::snapshot_draft_head(variant).has_value());
    CHECK_FALSE(apogee::models::snapshot_draft_head(store.root.path() / "absent").has_value());

    // A width is the config's own, or its text model's.
    CHECK(apogee::models::config_width(base).hidden_size == 3840);
    CHECK(apogee::models::config_width(base).family == "gemma4");
    const std::filesystem::path flat = store.root.path() / "flat";
    place_snapshot_with(flat, R"({"model_type": "llama", "hidden_size": 4096})");
    CHECK(apogee::models::config_width(flat).hidden_size == 4096);
    CHECK(apogee::models::config_width(flat).family == "llama");

    // One wording: the base named when stored, else its width, else unsaid.
    CHECK(apogee::models::describe_draft_head(*read, {"Gemma4-12B-Instruct"}) ==
          "a draft head (Gemma4UnifiedAssistantForCausalLM): speculative decoding runs it beside "
          "its base model (Gemma4-12B-Instruct), reading that model's hidden states, and never "
          "alone");
    CHECK(apogee::models::describe_draft_head(*read, {}).find("(a 3840-wide gemma4 model)") !=
          std::string::npos);
    CHECK(apogee::models::describe_draft_head(*by_name, {}).find("its base model, reading") !=
          std::string::npos);
    CHECK(apogee::models::draft_head_note(*read, {"Gemma4-12B-Instruct"}) ==
          "draft head -- runs beside Gemma4-12B-Instruct, never alone");
    CHECK(apogee::models::draft_head_refusal("'Gemma4-12B-Assistant'", *read, {})
              .ends_with("never alone -- nothing converted from it could answer a chat; convert "
                         "its base model instead"));
}

TEST_CASE("a draft head's base is the stored model of its family as wide as the width it reads",
          "[models][store][draft_head]") {
    const Store store;
    const std::filesystem::path models = store.roots.models;
    place_snapshot_with(models / "Gemma4-12B-Assistant" / "safetensors" / "aaaaaaaaaaaa",
                        kAssistantConfig);
    place_snapshot_with(models / "Gemma4-12B-Instruct" / "safetensors" / "bbbbbbbbbbbb",
                        R"({"architectures": ["Gemma4UnifiedForConditionalGeneration"],
                            "model_type": "gemma4_unified", "text_config": {"hidden_size": 3840}})");
    // As wide, but another family: never its base.
    place_snapshot_with(models / "Wide-Llama" / "safetensors" / "cccccccccccc",
                        R"({"model_type": "llama", "hidden_size": 3840})");
    // Another draft head declaring the same width is no base either.
    place_snapshot_with(models / "Other-Assistant" / "safetensors" / "dddddddddddd",
                        R"({"model_type": "gemma4_assistant", "backbone_hidden_size": 3840,
                            "hidden_size": 3840})");
    // The 26B: its base stored twice, as a snapshot and as an MLX model.
    place_snapshot_with(models / "Gemma4-26B-Instruct" / "safetensors" / "eeeeeeeeeeee",
                        R"({"model_type": "gemma4", "text_config": {"hidden_size": 2816}})");
    apogee::testing::write_mlx_model(models / "Gemma4-26B-Instruct" / "mlx" / "ffffffffffff");
    write_file(models / "Gemma4-26B-Instruct" / "mlx" / "ffffffffffff" / "config.json",
               R"({"model_type": "gemma4", "text_config": {"hidden_size": 2816},
                   "quantization": {"group_size": 64, "bits": 4}})");

    const std::optional<apogee::models::DraftHead> head = apogee::models::snapshot_draft_head(
        models / "Gemma4-12B-Assistant" / "safetensors" / "aaaaaaaaaaaa");
    REQUIRE(head.has_value());
    CHECK(apogee::models::draft_head_bases(store.roots, *head) ==
          std::vector<std::string>{"Gemma4-12B-Instruct"});

    apogee::models::DraftHead wide = *head;
    wide.backbone_hidden_size = 2816;
    CHECK(apogee::models::draft_head_bases(store.roots, wide) ==
          std::vector<std::string>{"Gemma4-26B-Instruct"});
    wide.backbone_hidden_size = 5376;
    CHECK(apogee::models::draft_head_bases(store.roots, wide).empty());
    wide.backbone_hidden_size = 0;
    CHECK(apogee::models::draft_head_bases(store.roots, wide).empty());
}

namespace {

/// Wan2.2-TI2V-5B's own config.json, as the user's store holds it: a
/// diffusers transformer (M20, 2026-10-21).
constexpr const char* kWanConfig = R"({
  "_class_name": "WanModel",
  "_diffusers_version": "0.33.0",
  "dim": 3072,
  "eps": 1e-06,
  "ffn_dim": 14336,
  "freq_dim": 256,
  "in_dim": 48,
  "model_type": "ti2v",
  "num_heads": 24,
  "num_layers": 30,
  "out_dim": 48,
  "text_len": 512
})";

}  // namespace

TEST_CASE("a diffusion model is read from its config: its diffusers class, and no architectures",
          "[models][store][diffusion]") {
    const Store store;
    const std::filesystem::path wan = store.root.path() / "wan";
    place_snapshot_with(wan, kWanConfig);
    const std::optional<apogee::models::DiffusionModel> read =
        apogee::models::snapshot_diffusion_model(wan);
    REQUIRE(read.has_value());
    CHECK(read->class_name == "WanModel");

    // A language model is none, and neither is a config that names its
    // architectures whatever else it says, or one diffusers did not write.
    const std::filesystem::path llama = store.root.path() / "llama";
    place_snapshot_with(llama, R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
    CHECK_FALSE(apogee::models::snapshot_diffusion_model(llama).has_value());
    const std::filesystem::path named = store.root.path() / "named";
    place_snapshot_with(named, R"({"_class_name": "WanModel", "_diffusers_version": "0.33.0",
                                   "architectures": ["LlamaForCausalLM"]})");
    CHECK_FALSE(apogee::models::snapshot_diffusion_model(named).has_value());
    const std::filesystem::path unwritten = store.root.path() / "unwritten";
    place_snapshot_with(unwritten, R"({"_class_name": "WanModel"})");
    CHECK_FALSE(apogee::models::snapshot_diffusion_model(unwritten).has_value());
    CHECK_FALSE(apogee::models::snapshot_diffusion_model(store.root.path() / "absent").has_value());

    // One wording, and the refusal built on it -- M20's for a diffusers
    // class that is no Wan transformer (Wan's is 53j's, below).
    const std::filesystem::path ltx = store.root.path() / "ltx";
    place_snapshot_with(ltx, R"({"_class_name": "LTXVideoTransformer3DModel",
                                 "_diffusers_version": "0.33.0"})");
    const std::optional<apogee::models::DiffusionModel> other =
        apogee::models::snapshot_diffusion_model(ltx);
    REQUIRE(other.has_value());
    CHECK_FALSE(other->wan.has_value());
    CHECK(apogee::models::describe_diffusion_model(*other) ==
          "a diffusion model's weights (LTXVideoTransformer3DModel, a diffusers class): neither "
          "converter takes one -- llama.cpp's finds a model by its architectures, and mlx-lm's "
          "builds a language model");
    CHECK(apogee::models::diffusion_model_refusal("'Lightricks--LTX-Video'", *other) ==
          "'Lightricks--LTX-Video' is " + apogee::models::describe_diffusion_model(*other));
}

TEST_CASE("a Wan transformer is known by its config's class, never diffusers' port of it",
          "[models][store][diffusion][wan]") {
    using apogee::models::is_wan_transformer_class;
    CHECK(is_wan_transformer_class("WanModel"));
    CHECK(is_wan_transformer_class("VaceWanModel"));
    CHECK(is_wan_transformer_class("WanModel_S2V"));
    CHECK_FALSE(is_wan_transformer_class("WanTransformer3DModel"));
    CHECK_FALSE(is_wan_transformer_class("WanVACETransformer3DModel"));
    CHECK_FALSE(is_wan_transformer_class("LTXVideoTransformer3DModel"));
    CHECK_FALSE(is_wan_transformer_class("WanPipeline"));
    CHECK_FALSE(is_wan_transformer_class(""));

    // A config's text, as the pull reads one before a byte of weights moves:
    // diffusers wrote it, it names no architectures, and its class is Wan's.
    using apogee::models::wan_transformer_class;
    CHECK(wan_transformer_class(apogee::testing::kWanTi2vConfig) == "WanModel");
    CHECK(wan_transformer_class(R"({"_class_name": "WanModel"})").empty());
    CHECK(wan_transformer_class(R"({"_class_name": "WanModel", "_diffusers_version": "0.33.0",
                                    "architectures": ["LlamaForCausalLM"]})")
              .empty());
    CHECK(wan_transformer_class(R"({"_class_name": "WanTransformer3DModel",
                                    "_diffusers_version": "0.33.0"})")
              .empty());
    CHECK(wan_transformer_class(R"({"architectures": ["LlamaForCausalLM"]})").empty());
    CHECK(wan_transformer_class("not json").empty());
}

TEST_CASE("the user's Wan set reads as Wan's original layout, missing its two checkpoints by name",
          "[models][store][diffusion][wan]") {
    // 53j: the user's record of ten files and config, as the pull before
    // 53j left them.
    const Store store;
    // The fixture is the user's: ten files on record, the config and the
    // configuration at the sizes the record lists.
    CHECK(apogee::testing::kWanTi2vConfig.size() == 251);
    CHECK(apogee::testing::kWanConfiguration.size() == 43);
    CHECK(apogee::testing::wan_record().files.size() == 10);
    const std::filesystem::path dir = apogee::testing::write_user_wan_set(store.roots);
    const std::string encoder{apogee::testing::kWanTextEncoder};
    const std::string vae{apogee::testing::kWanVae};

    std::optional<apogee::models::WanLayout> layout = apogee::models::snapshot_wan_layout(dir);
    REQUIRE(layout.has_value());
    CHECK(layout->class_name == "WanModel");
    REQUIRE(layout->components.size() == 2);
    CHECK(layout->components[0].role == "text encoder");
    CHECK(layout->components[1].role == "VAE");
    CHECK_FALSE(layout->whole());
    CHECK(layout->missing() == std::vector<std::string>{encoder, vae});

    // The refusal and `info`'s line say what it is, what it lacks with the
    // pull that fetches it, that neither converter takes it, and what runs
    // the model today.
    std::optional<apogee::models::DiffusionModel> model =
        apogee::models::snapshot_diffusion_model(dir);
    REQUIRE(model.has_value());
    CHECK(model->ref == "Wan-AI/Wan2.2-TI2V-5B");
    CHECK(apogee::models::diffusion_model_refusal(
              "'Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83'", *model) ==
          "'Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83' is Wan's original layout "
          "(WanModel), missing models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth -- 'apogee "
          "models pull Wan-AI/Wan2.2-TI2V-5B --safetensors' fetches them into this set; neither "
          "converter takes it -- llama.cpp's finds a model by its architectures, and mlx-lm's "
          "builds a language model; what runs this model today is its Diffusers pipeline in "
          "'apogee video generate', pulled whole with 'apogee models pull "
          "Wan-AI/Wan2.2-TI2V-5B-Diffusers'");

    // One there, one not: the one named.
    apogee::testing::write_fixture_file(dir / vae, apogee::testing::wan_bytes(vae));
    layout = apogee::models::snapshot_wan_layout(dir);
    REQUIRE(layout.has_value());
    CHECK(layout->missing() == std::vector<std::string>{encoder});
    model = apogee::models::snapshot_diffusion_model(dir);
    REQUIRE(model.has_value());
    CHECK(apogee::models::describe_diffusion_model(*model).starts_with(
        "Wan's original layout (WanModel), missing models_t5_umt5-xxl-enc-bf16.pth -- 'apogee "
        "models pull Wan-AI/Wan2.2-TI2V-5B --safetensors' fetches it into this set; "));

    // Both there: whole, and said complete.
    apogee::testing::write_fixture_file(dir / encoder, apogee::testing::wan_bytes(encoder));
    layout = apogee::models::snapshot_wan_layout(dir);
    REQUIRE(layout.has_value());
    CHECK(layout->whole());
    CHECK(layout->missing().empty());
    model = apogee::models::snapshot_diffusion_model(dir);
    REQUIRE(model.has_value());
    CHECK(apogee::models::describe_diffusion_model(*model) ==
          "Wan's original layout (WanModel), complete -- its transformer beside "
          "models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth; neither converter takes it -- "
          "llama.cpp's finds a model by its architectures, and mlx-lm's builds a language model; "
          "what runs this model today is its Diffusers pipeline in 'apogee video generate', "
          "pulled whole with 'apogee models pull Wan-AI/Wan2.2-TI2V-5B-Diffusers'");
    // Still no conversion: the original layout converts through mlx-video,
    // which is 53k's.
    CHECK(apogee::models::snapshot_conversions(dir).empty());
}

TEST_CASE(
    "a Wan set's components are named as it holds them, as its record lists them, or as Wan "
    "names them",
    "[models][store][diffusion][wan]") {
    const Store store;
    // A record that took the checkpoints, one since removed, and an image
    // encoder beside them: each a component, the lost one by its recorded name.
    const std::filesystem::path dir = store.root.path() / "i2v";
    place_snapshot_with(dir, R"({"_class_name": "WanModel", "_diffusers_version": "0.30.0",
                                 "model_type": "i2v", "in_dim": 36, "out_dim": 16})");
    apogee::models::Snapshot record;
    record.ref = "Wan-AI/Wan2.1-I2V-14B-480P";
    record.revision = "0123abcd";
    record.source = "huggingface";
    record.files = {{.path = "model.safetensors", .size = 1, .sha256 = std::string(64, 'a')},
                    {.path = "models_t5_umt5-xxl-enc-bf16.pth", .size = 1, .sha256 = {}},
                    {.path = "Wan2.1_VAE.pth", .size = 1, .sha256 = {}},
                    {.path = "models_clip_open-clip-xlm-roberta-large-vit-huge-14.pth",
                     .size = 1,
                     .sha256 = {}}};
    REQUIRE(apogee::models::write_snapshot(dir, record));
    write_file(dir / "models_t5_umt5-xxl-enc-bf16.pth");
    write_file(dir / "models_clip_open-clip-xlm-roberta-large-vit-huge-14.pth");
    const std::optional<apogee::models::WanLayout> layout =
        apogee::models::snapshot_wan_layout(dir);
    REQUIRE(layout.has_value());
    REQUIRE(layout->components.size() == 3);
    CHECK(layout->components[2].role == "checkpoint");
    CHECK(layout->components[2].present);
    CHECK(layout->missing() == std::vector<std::string>{"Wan2.1_VAE.pth"});
    // The way names the revision it was pulled at; the pipeline, the
    // repository alone.
    const std::optional<apogee::models::DiffusionModel> model =
        apogee::models::snapshot_diffusion_model(dir);
    REQUIRE(model.has_value());
    const std::string said = apogee::models::describe_diffusion_model(*model);
    CHECK(said.find("missing Wan2.1_VAE.pth -- 'apogee models pull "
                    "Wan-AI/Wan2.1-I2V-14B-480P@0123abcd --safetensors' fetches it") !=
          std::string::npos);
    CHECK(said.ends_with("'apogee models pull Wan-AI/Wan2.1-I2V-14B-480P-Diffusers'"));

    // No record and no checkpoints: the names Wan's releases give them -- the
    // VAE by the latent width the transformer declares -- and no repository
    // to name.
    const std::filesystem::path bare = store.root.path() / "bare";
    place_snapshot_with(bare, R"({"_class_name": "WanModel", "_diffusers_version": "0.30.0",
                                  "out_dim": 16})");
    const std::optional<apogee::models::DiffusionModel> unrecorded =
        apogee::models::snapshot_diffusion_model(bare);
    REQUIRE(unrecorded.has_value());
    REQUIRE(unrecorded->wan.has_value());
    CHECK(unrecorded->wan->missing() ==
          std::vector<std::string>{"models_t5_umt5-xxl-enc-bf16.pth", "Wan2.1_VAE.pth"});
    CHECK(apogee::models::describe_diffusion_model(*unrecorded) ==
          "Wan's original layout (WanModel), missing models_t5_umt5-xxl-enc-bf16.pth and "
          "Wan2.1_VAE.pth -- pulling its repository again with --safetensors fetches them into "
          "this set; neither converter takes it -- llama.cpp's finds a model by its "
          "architectures, and mlx-lm's builds a language model; what runs this model today is "
          "its Diffusers pipeline in 'apogee video generate', pulled whole (a repository with "
          "model_index.json at its root)");
    const std::filesystem::path unsized = store.root.path() / "unsized";
    place_snapshot_with(unsized, R"({"_class_name": "WanModel", "_diffusers_version": "0.30.0"})");
    CHECK(apogee::models::snapshot_wan_layout(unsized)->missing().back() == "*_VAE.pth");

    // Anything else is no Wan layout.
    const std::filesystem::path llama = store.root.path() / "llama";
    place_snapshot_with(llama, R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
    CHECK_FALSE(apogee::models::snapshot_wan_layout(llama).has_value());
    CHECK_FALSE(apogee::models::snapshot_wan_layout(store.root.path() / "absent").has_value());
}

TEST_CASE(
    "which conversions a set takes is one decision: a language model both, a draft head or "
    "a diffusion model none",
    "[models][store][diffusion][draft_head]") {
    const Store store;
    const std::filesystem::path wan = store.root.path() / "wan";
    place_snapshot_with(wan, kWanConfig);
    const std::filesystem::path head = store.root.path() / "head";
    place_snapshot_with(head, kAssistantConfig);
    const std::filesystem::path llama = store.root.path() / "llama";
    place_snapshot_with(llama, R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
    CHECK(apogee::models::snapshot_conversions(wan).empty());
    CHECK(apogee::models::snapshot_conversions(head).empty());
    CHECK(apogee::models::snapshot_conversions(llama) == std::vector<std::string>{"gguf", "mlx"});
}

TEST_CASE(
    "each of the user's four assistants converts to an MLX drafter alone, exactly where the "
    "installed mlx-vlm drafts its type -- the unified 12B's only where it drafts that",
    "[models][store][draft_head][drafter]") {
    // 53i: M21's rule carried to drafters, as 53f carried it to towers.
    const Store store;
    using Formats = std::vector<std::string>;
    // What mlx-vlm drafts as the test says it: Gemma 4's assistant, and the
    // unified one only when asked.
    bool unified = false;
    const apogee::models::DrafterBuilds drafts = [&unified](std::string_view type) {
        return type == "gemma4_assistant" || (unified && type == "gemma4_unified_assistant");
    };
    for (const apogee::testing::DraftHeadPair& pair : apogee::testing::kUserDraftHeads) {
        INFO(pair.head_model);
        const std::filesystem::path head = store.root.path() / pair.head_model;
        place_snapshot_with(head, pair.head_config);
        const std::optional<apogee::models::DraftHead> read =
            apogee::models::snapshot_draft_head(head);
        REQUIRE(read.has_value());
        CHECK(read->model_type == pair.drafter_type);
        CHECK(read->backbone_hidden_size == pair.width);
        CHECK(read->family == "gemma4");
        // No mlx-vlm to ask: the draft-heads change's answer stands.
        CHECK(apogee::models::snapshot_conversions(head).empty());
        CHECK(apogee::models::snapshot_conversions(head, {}, [](std::string_view) {
                  return false;
              }).empty());
        // Never a GGUF: its one conversion is the drafter.
        unified = false;
        const bool twelve = std::string_view{pair.drafter_type} == "gemma4_unified_assistant";
        CHECK(apogee::models::snapshot_conversions(head, {}, drafts) ==
              (twelve ? Formats{} : Formats{"mlx"}));
        unified = true;
        CHECK(apogee::models::snapshot_conversions(head, {}, drafts) == Formats{"mlx"});
    }
    // A drafter's closure decides nothing for anything else.
    const std::filesystem::path llama = store.root.path() / "llama";
    place_snapshot_with(llama, apogee::testing::kLlamaConfig);
    CHECK(apogee::models::snapshot_conversions(llama, {}, drafts) == Formats{"gguf", "mlx"});
}

TEST_CASE("each assistant's base is the one stored model of its family as wide as it reads",
          "[models][store][draft_head][drafter]") {
    // 53i's guardrail on the draft-heads change's finder: the user's four
    // heads and their four bases in one store, each head finding its own.
    const Store store;
    for (const apogee::testing::DraftHeadPair& pair : apogee::testing::kUserDraftHeads) {
        place_snapshot_with(store.roots.models / pair.head_model / "safetensors" / "aaaaaaaaaaaa",
                            pair.head_config);
        place_snapshot_with(store.roots.models / pair.base_model / "safetensors" / "bbbbbbbbbbbb",
                            pair.base_config);
    }
    for (const apogee::testing::DraftHeadPair& pair : apogee::testing::kUserDraftHeads) {
        INFO(pair.head_model);
        const std::optional<apogee::models::DraftHead> head = apogee::models::snapshot_draft_head(
            store.roots.models / pair.head_model / "safetensors" / "aaaaaaaaaaaa");
        REQUIRE(head.has_value());
        CHECK(apogee::models::draft_head_bases(store.roots, *head) ==
              std::vector<std::string>{pair.base_model});
    }
}

TEST_CASE("a drafter's record names its bases, written only when there are any, read as text",
          "[models][store][drafter]") {
    const Store store;
    const std::filesystem::path dir = store.root.path() / "drafter";
    std::filesystem::create_directories(dir);
    apogee::models::Snapshot record;
    record.ref = "Gemma4-E4B-Assistant/safetensors/aaaaaaaaaaaa";
    record.source = "convert";
    record.transform = "mlx_vlm.convert bf16";
    record.kind = "drafter";
    record.bases = {"Gemma4-E4B-Instruct"};
    REQUIRE(apogee::models::write_snapshot(dir, record));
    const std::optional<apogee::models::Snapshot> read = apogee::models::load_snapshot(dir);
    REQUIRE(read.has_value());
    CHECK(read->kind == "drafter");
    CHECK(read->bases == std::vector<std::string>{"Gemma4-E4B-Instruct"});

    // None: no key at all, as every record before 53i.
    record.bases.clear();
    REQUIRE(apogee::models::write_snapshot(dir, record));
    std::ifstream in{apogee::models::snapshot_record_path(dir)};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text.find("bases") == std::string::npos);
    CHECK(apogee::models::load_snapshot(dir)->bases.empty());

    // What is not a name is no base: read as no record of one.
    write_file(apogee::models::snapshot_record_path(dir),
               R"({"ref": "x", "source": "convert", "kind": "drafter",
                   "bases": ["Gemma4-E4B-Instruct", 7, "", "Gemma4-E4B-Instruct", "Other"]})");
    CHECK(apogee::models::load_snapshot(dir)->bases ==
          std::vector<std::string>{"Gemma4-E4B-Instruct", "Other"});
    write_file(apogee::models::snapshot_record_path(dir),
               R"({"ref": "x", "source": "convert", "kind": "drafter", "bases": "Gemma"})");
    CHECK(apogee::models::load_snapshot(dir)->bases.empty());
}

TEST_CASE(
    "a set's towers are what its config declares beside its language model: vision, audio, an "
    "omni model's thinker, never a name",
    "[models][store][towers]") {
    // 53f: the converter is chosen by these, so each of the user's sets is
    // read from its own configuration.
    const Store store;
    const auto towers = [&store](const std::string& name, const std::string& config) {
        const std::filesystem::path dir = store.root.path() / name;
        place_snapshot_with(dir, config);
        return apogee::models::snapshot_towers(dir);
    };
    using Towers = std::vector<std::string>;
    CHECK(towers("qwen3-vl", apogee::testing::kQwen3VlConfig) == Towers{"vision_config"});
    CHECK(towers("qwen3-omni", apogee::testing::kQwen3OmniConfig) == Towers{"thinker_config"});
    CHECK(towers("gemma4", apogee::testing::kGemma4Config) ==
          Towers{"vision_config", "audio_config"});
    CHECK(towers("gemma4-unified", apogee::testing::kGemma4UnifiedConfig) ==
          Towers{"vision_config", "audio_config"});
    CHECK(towers("qwen3.8", apogee::testing::kQwen35Config) == Towers{"vision_config"});
    CHECK(towers("llama", apogee::testing::kLlamaConfig).empty());
    // A key set to nothing declares nothing, and neither does a model that
    // says it is its language model alone -- as the GGUF path reads them.
    CHECK(towers("null", R"({"model_type": "gemma4", "audio_config": null,
                             "vision_config": {}})")
              .empty());
    CHECK(towers("only", R"({"model_type": "mistral3", "language_model_only": true,
                             "vision_config": {"hidden_size": 1024}})")
              .empty());
    // The name says nothing: a Llama called "VL" is a Llama.
    CHECK(towers("Qwen3-VL-named", apogee::testing::kLlamaConfig).empty());
    CHECK(apogee::models::snapshot_towers(store.root.path() / "absent").empty());
    CHECK(apogee::models::describe_towers({"vision_config", "audio_config"}) ==
          "vision_config, audio_config");
    // What a set converts to is unchanged by them: MLX stays a format, its
    // converter the route's.
    CHECK(apogee::models::snapshot_conversions(store.root.path() / "qwen3-omni") ==
          std::vector<std::string>{"gguf", "mlx"});
    CHECK(apogee::models::snapshot_conversions(store.root.path() / "gemma4") ==
          std::vector<std::string>{"gguf", "mlx"});
}

namespace {

/// The user's Gemma-Embedding-300M (M21, 2026-10-21): its `modules.json`
/// and its two dense layers' configs as stored, its `config.json` sliced to
/// what says what it is.
constexpr const char* kEmbeddingConfig =
    R"({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text",
        "hidden_size": 768, "use_bidirectional_attention": true})";
constexpr const char* kEmbeddingModules = R"([
  {"idx": 0, "name": "0", "path": "", "type": "sentence_transformers.models.Transformer"},
  {"idx": 1, "name": "1", "path": "1_Pooling", "type": "sentence_transformers.models.Pooling"},
  {"idx": 2, "name": "2", "path": "2_Dense", "type": "sentence_transformers.models.Dense"},
  {"idx": 3, "name": "3", "path": "3_Dense", "type": "sentence_transformers.models.Dense"},
  {"idx": 4, "name": "4", "path": "4_Normalize", "type": "sentence_transformers.models.Normalize"}
])";

/// The embedding model `dir` as the user's is stored, its dense layers'
/// weights there when `dense`.
void place_embedding_model(const std::filesystem::path& dir, bool dense) {
    place_snapshot_with(dir, kEmbeddingConfig);
    write_file(dir / "modules.json", kEmbeddingModules);
    write_file(dir / "2_Dense" / "config.json",
               R"({"in_features": 768, "out_features": 3072, "bias": false,
                   "activation_function": "torch.nn.modules.linear.Identity"})");
    write_file(dir / "3_Dense" / "config.json",
               R"({"in_features": 3072, "out_features": 768, "bias": false,
                   "activation_function": "torch.nn.modules.linear.Identity"})");
    if (dense) {
        write_file(dir / "2_Dense" / "model.safetensors");
        write_file(dir / "3_Dense" / "model.safetensors");
    }
}

}  // namespace

TEST_CASE("an embedding model is read from its modules.json: its pipeline, and its dense layers",
          "[models][store][embedding]") {
    const Store store;
    const std::filesystem::path embed = store.root.path() / "embed";
    place_embedding_model(embed, true);
    const std::optional<apogee::models::EmbeddingModel> read =
        apogee::models::snapshot_embedding_model(embed);
    REQUIRE(read.has_value());
    CHECK(read->modules ==
          std::vector<std::string>{"Transformer", "Pooling", "Dense", "Dense", "Normalize"});
    CHECK(read->dense);
    // Dense modules named but their weights absent: the converter takes none.
    const std::filesystem::path bare = store.root.path() / "bare";
    place_embedding_model(bare, false);
    CHECK_FALSE(apogee::models::snapshot_embedding_model(bare)->dense);
    // No Pooling module, no modules.json: none.
    const std::filesystem::path unpooled = store.root.path() / "unpooled";
    place_snapshot_with(unpooled, kEmbeddingConfig);
    write_file(unpooled / "modules.json",
               R"([{"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"}])");
    CHECK_FALSE(apogee::models::snapshot_embedding_model(unpooled).has_value());
    const std::filesystem::path llama = store.root.path() / "llama";
    place_snapshot_with(llama, R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
    CHECK_FALSE(apogee::models::snapshot_embedding_model(llama).has_value());

    // One wording, and the decision: a GGUF alone.
    CHECK(apogee::models::describe_embedding_model(*read) ==
          "an embedding model (sentence-transformers: Transformer, Pooling, Dense, Dense, "
          "Normalize): it converts to a GGUF a llamacpp backend embeds with -- mlx-lm's converter "
          "builds language models, and the mlx backend does not embed");
    CHECK(apogee::models::embedding_model_mlx_refusal("'Gemma-Embedding-300M'", *read)
              .ends_with("does not embed -- convert it without --mlx"));
    CHECK(apogee::models::snapshot_conversions(embed) == std::vector<std::string>{"gguf"});
}

TEST_CASE("an embedding model gains MLX exactly where the installed mlx-embeddings builds its type",
          "[models][store][embedding][mlx]") {
    // 53h: the one decision, handed what the environment's mlx-embeddings
    // builds -- MLX joins the GGUF, offered second, where it builds the set's
    // own model_type; anywhere else M21's answer stands.
    const Store store;
    const std::filesystem::path embed = store.root.path() / "embed";
    place_embedding_model(embed, true);
    const auto builds = [](std::vector<std::string> types) -> apogee::models::EmbeddingBuilds {
        return [types = std::move(types)](std::string_view type) {
            return std::ranges::find(types, type) != types.end();
        };
    };
    CHECK(apogee::models::snapshot_conversions(embed, builds({"bert", "gemma3_text"})) ==
          std::vector<std::string>{"gguf", "mlx"});
    // Its type unbuilt, or nothing installed to ask: as M21 left it.
    CHECK(apogee::models::snapshot_conversions(embed, builds({"bert", "qwen3"})) ==
          std::vector<std::string>{"gguf"});
    CHECK(apogee::models::snapshot_conversions(embed, builds({})) ==
          std::vector<std::string>{"gguf"});
    CHECK(apogee::models::snapshot_conversions(embed, {}) == std::vector<std::string>{"gguf"});
    // A language model's answer is not the embedder's to change.
    const std::filesystem::path llama = store.root.path() / "llama";
    place_snapshot_with(llama, R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
    CHECK(apogee::models::snapshot_conversions(llama, builds({})) ==
          std::vector<std::string>{"gguf", "mlx"});

    // Said both ways where it builds.
    const std::optional<apogee::models::EmbeddingModel> read =
        apogee::models::snapshot_embedding_model(embed);
    REQUIRE(read.has_value());
    CHECK(apogee::models::describe_embedding_model_both(*read) ==
          "an embedding model (sentence-transformers: Transformer, Pooling, Dense, Dense, "
          "Normalize): it converts to a GGUF a llamacpp backend embeds with, and through "
          "mlx-embeddings to an MLX model an mlx backend embeds with");
}
