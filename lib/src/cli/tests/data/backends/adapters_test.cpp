#include "backends/adapters.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/sha256.h"
#include "modelstore/sidecar.h"
#include "modelstore/store.h"
#include "support/env_guard.h"

/// An entry's adapters against its base (53a): the hash an adapter is pinned
/// to its base by, read once per change of the weights; and the refusal
/// table a load asks first -- file facts, then the pin -- each naming the
/// adapter and its fix.
namespace {

namespace fs = std::filesystem;
using apogee::backends::adapter_files;
using apogee::backends::adapter_refusal;
using apogee::backends::AdapterCheck;
using apogee::backends::AdapterFiles;
using apogee::backends::pin_adapters;
using apogee::backends::spell_adapters;
using apogee::backends::WeightHashing;
using apogee::backends::weights_sha256;
using apogee::harness::AdapterConfig;
using Catch::Matchers::ContainsSubstring;

void write(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// A scratch directory, its base weights and a memo, and a hasher that
/// counts what it reads.
struct Scratch {
    apogee::testing::TempDir dir{"adapters-" + std::to_string(std::random_device{}())};
    fs::path base = dir.path() / "base.gguf";
    fs::path memo = dir.path() / "cache" / "weight-hashes.json";
    std::vector<fs::path> reads{};
    WeightHashing hashing{.memo = memo, .hasher = [this](const fs::path& file) {
                              reads.push_back(file);
                              return apogee::models::file_sha256(file);
                          }};

    Scratch() {
        write(base, "base weights");
    }

    [[nodiscard]] AdapterCheck check(bool directories = false) const {
        return AdapterCheck{
            .backend = "local", .base = base, .directories = directories, .hashing = hashing};
    }
};

}  // namespace

TEST_CASE("a base's pin is its file's sha256, read once per change of the file",
          "[backends][adapters][hash]") {
    Scratch scratch;
    const std::string expected = apogee::models::sha256_hex("base weights");
    CHECK(weights_sha256(scratch.base, scratch.hashing) == expected);
    CHECK(scratch.reads.size() == 1);
    REQUIRE(fs::exists(scratch.memo));
    // The same file, unchanged: the memo answers, and nothing is read.
    CHECK(weights_sha256(scratch.base, scratch.hashing) == expected);
    CHECK(scratch.reads.size() == 1);
    // Rewritten -- another size -- it is read again, and says so.
    write(scratch.base, "retrained base weights");
    CHECK(weights_sha256(scratch.base, scratch.hashing) ==
          apogee::models::sha256_hex("retrained base weights"));
    CHECK(scratch.reads.size() == 2);
    // Touched -- another time, the same size -- read again too.
    fs::last_write_time(scratch.base, fs::last_write_time(scratch.base) + std::chrono::seconds{5});
    (void)weights_sha256(scratch.base, scratch.hashing);
    CHECK(scratch.reads.size() == 3);
    // A memo that cannot be read is a memo with nothing in it, and a line in
    // it that is not one of ours is a line not remembered.
    write(scratch.memo, "not json");
    CHECK(weights_sha256(scratch.base, scratch.hashing) ==
          apogee::models::sha256_hex("retrained base weights"));
    CHECK(scratch.reads.size() == 4);
    write(scratch.memo, R"({"files": {")" + fs::absolute(scratch.base).lexically_normal().string() +
                            R"(": {"size": "big", "sha256": 7}}})");
    CHECK(weights_sha256(scratch.base, scratch.hashing) ==
          apogee::models::sha256_hex("retrained base weights"));
    CHECK(scratch.reads.size() == 5);
    // With no memo, every ask reads; nothing there, nothing to pin.
    WeightHashing bare{.hasher = scratch.hashing.hasher};
    (void)weights_sha256(scratch.base, bare);
    (void)weights_sha256(scratch.base, bare);
    CHECK(scratch.reads.size() == 7);
    CHECK(weights_sha256(scratch.dir.path() / "missing.gguf", scratch.hashing).empty());
}

TEST_CASE("an MLX model's pin digests its shards by the store's own id rule",
          "[backends][adapters][hash]") {
    Scratch scratch;
    const fs::path model = scratch.dir.path() / "mlx-model";
    write(model / "config.json", R"({"model_type":"llama"})");
    write(model / "model-00002-of-00002.safetensors", "shard two");
    write(model / "model-00001-of-00002.safetensors", "shard one");
    const std::string one = apogee::models::sha256_hex("shard one");
    const std::string two = apogee::models::sha256_hex("shard two");
    apogee::models::Sha256 digest;
    digest.update("model-00001-of-00002.safetensors\t" + one + "\n");
    digest.update("model-00002-of-00002.safetensors\t" + two + "\n");
    const std::string pin = weights_sha256(model, scratch.hashing);
    CHECK(pin == digest.hex_digest());
    // Its first 12 characters are the id the store files these weights by.
    CHECK(pin.substr(0, 12) == apogee::models::snapshot_weight_id(
                                   {{.path = "model-00001-of-00002.safetensors", .sha256 = one},
                                    {.path = "model-00002-of-00002.safetensors", .sha256 = two}}));
    CHECK(scratch.reads.size() == 2);
    // The configuration is not the weights; a shard is.
    write(model / "config.json", R"({"model_type":"llama","note":1})");
    CHECK(weights_sha256(model, scratch.hashing) == pin);
    CHECK(scratch.reads.size() == 2);
    write(model / "model-00002-of-00002.safetensors", "shard two, retrained");
    CHECK(weights_sha256(model, scratch.hashing) != pin);
    CHECK(scratch.reads.size() == 3);
    // A directory with no shard has no weights to pin to.
    fs::create_directories(scratch.dir.path() / "empty");
    CHECK(weights_sha256(scratch.dir.path() / "empty", scratch.hashing).empty());
}

TEST_CASE("the adapters' refusal table: file facts first, then the pin, each named with its fix",
          "[backends][adapters][refusal]") {
    Scratch scratch;
    const fs::path lora = scratch.dir.path() / "style.gguf";
    write(lora, "lora");
    const std::string base = apogee::models::sha256_hex("base weights");
    const AdapterConfig pinned{.path = lora.string(), .scale = 0.5, .base_sha256 = base};

    // None, or a set pinned to these weights: nothing to refuse.
    CHECK(adapter_refusal({}, scratch.check()).empty());
    CHECK(scratch.reads.empty());
    CHECK(adapter_refusal({pinned}, scratch.check()).empty());

    // An adapter not there: named, with the command that takes the set off.
    const std::string missing = adapter_refusal(
        {pinned, {.path = "/gone/tone.gguf", .base_sha256 = base}}, scratch.check());
    CHECK_THAT(missing, ContainsSubstring("the adapter /gone/tone.gguf is not there"));
    CHECK_THAT(missing, ContainsSubstring("apogee config set-backend local --adapters \"\""));
    // A directory where a llamacpp entry reads a GGUF file.
    CHECK_THAT(adapter_refusal({{.path = scratch.dir.path().string(), .base_sha256 = base}},
                               scratch.check()),
               ContainsSubstring("is not a file -- a llamacpp entry applies a GGUF LoRA file"));
    // A file, or a directory with no adapter_config.json, where mlx reads
    // the directory its training wrote.
    CHECK_THAT(adapter_refusal({pinned}, scratch.check(true)),
               ContainsSubstring("is not an mlx-lm adapter directory"));
    // Never pinned: the command that pins it names the whole set, so the fix
    // drops none of it.
    const std::string unpinned =
        adapter_refusal({{.path = lora.string(), .scale = 0.5}}, scratch.check());
    CHECK_THAT(unpinned, ContainsSubstring("the adapter " + lora.string() +
                                           " is pinned to no base -- apogee config set-backend "
                                           "local --adapters " +
                                           lora.string() + ":0.5 records the base"));
    // File facts are asked before any weights are read.
    CHECK(scratch.reads.size() == 1);

    // The base changed under it: both hashes named, and both ways out.
    write(scratch.base, "other base weights");
    const std::string changed = adapter_refusal({pinned}, scratch.check());
    CHECK_THAT(changed,
               ContainsSubstring(
                   "the adapter " + lora.string() + " was pinned to other base weights (sha256 " +
                   base.substr(0, 12) + ") than " + scratch.base.string() + " holds now (" +
                   apogee::models::sha256_hex("other base weights").substr(0, 12) + ")"));
    CHECK_THAT(changed, ContainsSubstring("point model_path back at them"));
    CHECK_THAT(changed, ContainsSubstring("apogee config set-backend local --adapters " +
                                          lora.string() + ":0.5"));
    // A base that cannot be read cannot be checked.
    fs::remove(scratch.base);
    CHECK_THAT(
        adapter_refusal({pinned}, scratch.check()),
        ContainsSubstring("the base weights at " + scratch.base.string() + " could not be read"));
}

TEST_CASE("an mlx adapter directory pins to the model directory's weights",
          "[backends][adapters][refusal]") {
    Scratch scratch;
    const fs::path model = scratch.dir.path() / "model";
    write(model / "model.safetensors", "weights");
    const fs::path adapter = scratch.dir.path() / "adapter";
    write(adapter / "adapter_config.json", "{}");
    write(adapter / "adapters.safetensors", "delta");
    AdapterCheck check = scratch.check(true);
    check.base = model;
    std::vector<AdapterConfig> adapters{{.path = adapter.string()}};
    REQUIRE(pin_adapters(adapters, check).empty());
    CHECK(adapters[0].base_sha256 == weights_sha256(model, scratch.hashing));
    CHECK(adapter_refusal(adapters, check).empty());
}

TEST_CASE("pinning makes each path absolute and records the base's hash on every one",
          "[backends][adapters][pin]") {
    Scratch scratch;
    write(scratch.dir.path() / "loras" / "a.gguf", "a");
    write(scratch.dir.path() / "loras" / "b.gguf", "b");
    // Relative to where the command runs, as a shell names them.
    const fs::path previous = fs::current_path();
    fs::current_path(scratch.dir.path());
    const fs::path here = fs::current_path();
    std::vector<AdapterConfig> adapters{{.path = "loras/a.gguf", .scale = 0.5},
                                        {.path = "./loras/../loras/b.gguf"}};
    const std::string refused = pin_adapters(adapters, scratch.check());
    fs::current_path(previous);
    REQUIRE(refused.empty());
    const std::string base = apogee::models::sha256_hex("base weights");
    CHECK(adapters ==
          std::vector<AdapterConfig>{
              {.path = (here / "loras" / "a.gguf").string(), .scale = 0.5, .base_sha256 = base},
              {.path = (here / "loras" / "b.gguf").string(), .base_sha256 = base}});
    // An adapter not there is refused before the base is read.
    std::vector<AdapterConfig> missing{{.path = "/gone/c.gguf"}};
    CHECK_THAT(pin_adapters(missing, scratch.check()),
               ContainsSubstring("the adapter /gone/c.gguf is not there"));
    // No base to read, nothing to pin to.
    fs::remove(scratch.base);
    std::vector<AdapterConfig> orphan{{.path = adapters[0].path}};
    CHECK_THAT(pin_adapters(orphan, scratch.check()),
               ContainsSubstring("could not be read to pin the adapters to"));
}

TEST_CASE("a read that does not remember answers from the memo and writes nothing back",
          "[backends][adapters][hash]") {
    // 53c: the doctor reads the hash a load remembered, and never writes the
    // cache -- `check` edits nothing on the machine.
    Scratch scratch;
    const std::string expected = apogee::models::sha256_hex("base weights");
    REQUIRE(weights_sha256(scratch.base, scratch.hashing) == expected);
    REQUIRE(scratch.reads.size() == 1);
    std::ifstream before_in{scratch.memo, std::ios::binary};
    const std::string before{std::istreambuf_iterator<char>{before_in}, {}};
    WeightHashing reading = scratch.hashing;
    reading.remember = false;
    // Held by the memo: answered with no read.
    CHECK(weights_sha256(scratch.base, reading) == expected);
    CHECK(scratch.reads.size() == 1);
    // Changed since: read again, and the memo left exactly as it was.
    write(scratch.base, "other base weights");
    CHECK(weights_sha256(scratch.base, reading) ==
          apogee::models::sha256_hex("other base weights"));
    CHECK(scratch.reads.size() == 2);
    std::ifstream after_in{scratch.memo, std::ios::binary};
    CHECK(std::string{std::istreambuf_iterator<char>{after_in}, {}} == before);
    // With no memo at all, nothing is created.
    fs::remove(scratch.memo);
    CHECK(weights_sha256(scratch.base, reading) ==
          apogee::models::sha256_hex("other base weights"));
    CHECK_FALSE(fs::exists(scratch.memo));
}

TEST_CASE("an adapter's own facts are read one by one, its base never",
          "[backends][adapters][refusal]") {
    // 53c: the doctor's row for each adapter, in the order a load refuses on.
    Scratch scratch;
    const fs::path lora = scratch.dir.path() / "style.gguf";
    write(lora, "lora");
    const fs::path directory = scratch.dir.path() / "tone";
    write(directory / "adapter_config.json", "{}");
    const std::string pin = apogee::models::sha256_hex("base weights");
    CHECK(adapter_files({.path = lora.string(), .base_sha256 = pin}, scratch.check()) ==
          AdapterFiles::Pinned);
    CHECK(adapter_files({.path = lora.string()}, scratch.check()) == AdapterFiles::Unpinned);
    CHECK(adapter_files({.path = "/gone/x.gguf", .base_sha256 = pin}, scratch.check()) ==
          AdapterFiles::Missing);
    // The kind each runtime reads: a GGUF LoRA file, an mlx-lm directory.
    CHECK(adapter_files({.path = directory.string(), .base_sha256 = pin}, scratch.check()) ==
          AdapterFiles::WrongKind);
    CHECK(adapter_files({.path = lora.string(), .base_sha256 = pin}, scratch.check(true)) ==
          AdapterFiles::WrongKind);
    CHECK(adapter_files({.path = directory.string(), .base_sha256 = pin}, scratch.check(true)) ==
          AdapterFiles::Pinned);
    CHECK(scratch.reads.empty());
}

TEST_CASE("a set is spelled as set-backend takes it back, an empty one as a quoted nothing",
          "[backends][adapters][pin]") {
    CHECK(spell_adapters({}) == "\"\"");
    CHECK(spell_adapters({{.path = "/l/a.gguf"}, {.path = "/l/b.gguf", .scale = 0.5}}) ==
          "/l/a.gguf,/l/b.gguf:0.5");
    CHECK(spell_adapters({{.path = "/my loras/a.gguf"}}) == "\"/my loras/a.gguf\"");
}
