#include "modelstore/mlx_info.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"

/// The MLX directory reader (27b), over directories built in the test: what
/// a whole model says, and -- the dishonesty guards -- every way one cannot
/// load said as "cannot load" with its reason, never a guess filling the gap.
namespace {

using apogee::models::MlxInfo;
using apogee::models::read_mlx_info;
using apogee::testing::MlxModelSpec;
using apogee::testing::write_fixture_file;
using apogee::testing::write_mlx_model;

struct Dir {
    apogee::testing::TempDir root{"mlx-info-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = root.path() / "model";
};

}  // namespace

TEST_CASE("a whole quantized model reads its window, its quantization and its shards",
          "[models][mlx][info]") {
    const Dir dir;
    write_mlx_model(dir.model, MlxModelSpec{.shards = 2});
    const MlxInfo info = read_mlx_info(dir.model);
    CHECK(info.complete);
    CHECK(info.problem.empty());
    CHECK(info.mlx_format);
    CHECK(info.model_type == "llama");
    CHECK(info.architecture == "LlamaForCausalLM");
    CHECK(info.context_length == 131072);
    CHECK(info.context_key == "max_position_embeddings");
    CHECK(info.quantization.bits == 4);
    CHECK(info.quantization.group_size == 64);
    CHECK_FALSE(info.quantization.mixed);
    CHECK(info.quantization.dtype == "bf16");
    CHECK(info.quantization.describe() == "4-bit (affine, group 64)");
    CHECK(info.quantization.label() == "4bit");
    REQUIRE(info.shards.size() == 2);
    // The configuration alone: the same facts, no shard opened.
    const MlxInfo config = apogee::models::read_mlx_config(dir.model);
    CHECK(config.config_read);
    CHECK_FALSE(config.complete);
    CHECK(config.context_length == 131072);
    CHECK(config.quantization.bits == 4);
    CHECK(config.shards.empty());
    CHECK(info.shards.at(0).name == "model-00001-of-00002.safetensors");
    CHECK(info.shards.at(1).name == "model-00002-of-00002.safetensors");
    std::uintmax_t total = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir.model)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    CHECK(info.bytes == total);
    CHECK(apogee::models::is_mlx_model_dir(dir.model));
}

TEST_CASE("no config.json cannot load, and nothing is guessed in its place",
          "[models][mlx][info][honesty]") {
    const Dir dir;
    write_mlx_model(dir.model);
    std::filesystem::remove(dir.model / "config.json");
    const MlxInfo info = read_mlx_info(dir.model);
    CHECK_FALSE(info.complete);
    CHECK(info.problem == "cannot load: no config.json in " + dir.model.string());
    CHECK(info.context_length == 0);
    CHECK(info.model_type.empty());
    CHECK_FALSE(info.quantization.quantized());
    CHECK_FALSE(info.mlx_format);
    // What is on disk is still counted: a listing shows its size.
    CHECK(info.shards.size() == 1);
    CHECK(info.bytes > 0);
    CHECK_FALSE(apogee::models::is_mlx_model_dir(dir.model));

    CHECK(read_mlx_info(dir.root.path() / "absent").problem ==
          "cannot load: " + (dir.root.path() / "absent").string() + " is not a directory");
}

TEST_CASE("each file a load needs, missing or broken, is named as the reason",
          "[models][mlx][info][honesty]") {
    const Dir dir;
    SECTION("config.json that is not JSON") {
        write_mlx_model(dir.model);
        write_fixture_file(dir.model / "config.json", "{ not json");
        CHECK(read_mlx_info(dir.model).problem == "cannot load: config.json is not a JSON object");
    }
    SECTION("config.json replaced by a download record") {
        write_mlx_model(dir.model);
        write_fixture_file(dir.model / "config.json",
                           R"({"source_url": "x", "verification": {}, "file_digest": "y"})");
        CHECK(read_mlx_info(dir.model).problem.find("an Apogee download record") !=
              std::string::npos);
    }
    SECTION("a shard cut short: the header promises more than the file holds") {
        write_mlx_model(dir.model);
        const std::filesystem::path shard = dir.model / "model.safetensors";
        std::filesystem::resize_file(shard, std::filesystem::file_size(shard) - 10);
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK_FALSE(info.complete);
        CHECK(
            info.problem.starts_with("cannot load: model.safetensors is truncated: its header "
                                     "describes "));
    }
    SECTION("a shard cut inside its header") {
        write_mlx_model(dir.model);
        std::filesystem::resize_file(dir.model / "model.safetensors", 20);
        CHECK(read_mlx_info(dir.model).problem.find("model.safetensors is truncated") !=
              std::string::npos);
    }
    SECTION("a shard listing a tensor without its offsets") {
        write_mlx_model(dir.model);
        const std::string header = R"({"w":{"dtype":"F16","shape":[2]}})";
        std::string bytes;
        for (int i = 0; i < 8; ++i) {
            bytes += static_cast<char>(i == 0 ? header.size() : 0);
        }
        write_fixture_file(dir.model / "model.safetensors", bytes + header);
        CHECK(read_mlx_info(dir.model).problem ==
              "cannot load: model.safetensors lists tensor 'w' without its data offsets");
    }
    SECTION("a shard the index names, never arrived") {
        write_mlx_model(dir.model, MlxModelSpec{.shards = 3});
        std::filesystem::remove(dir.model / "model-00002-of-00003.safetensors");
        CHECK(read_mlx_info(dir.model).problem ==
              "cannot load: model-00002-of-00003.safetensors is missing "
              "(model.safetensors.index.json names it)");
    }
    SECTION("no weights at all") {
        write_mlx_model(dir.model);
        std::filesystem::remove(dir.model / "model.safetensors");
        CHECK(read_mlx_info(dir.model).problem ==
              "cannot load: no weights (*.safetensors) in " + dir.model.string());
    }
    SECTION("no tokenizer") {
        write_mlx_model(dir.model, MlxModelSpec{.tokenizer = false});
        CHECK(read_mlx_info(dir.model).problem.starts_with("cannot load: no tokenizer"));
    }
}

TEST_CASE("the window is read where a composite model declares it, and said when absent",
          "[models][mlx][info][window]") {
    const Dir dir;
    write_mlx_model(dir.model, MlxModelSpec{.window = 0});
    SECTION("nowhere") {
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.complete);  // a model without a stated window still loads
        CHECK(info.context_length == 0);
        CHECK(info.context_key.empty());
        // The default, as a GGUF with no trained length gets (26a).
        const apogee::models::MlxWindow window =
            apogee::models::mlx_window(info, apogee::harness::BackendConfig{});
        CHECK(window.window == 32768);
        CHECK(apogee::models::describe(window) == "32768-token window (the default)");
        // A configuration that cannot be read gives no window at all.
        const apogee::models::MlxWindow unknown =
            apogee::models::mlx_window(MlxInfo{}, apogee::harness::BackendConfig{});
        CHECK(unknown.window == 0);
        CHECK(apogee::models::describe(unknown) ==
              "window unknown -- config.json could not be read; set context_size");
    }
    SECTION("a vision-language model's text model") {
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "qwen3_vl", "text_config": {"model_type":
                              "qwen3_vl_text", "max_position_embeddings": 262144,
                              "dtype": "bfloat16"}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.context_length == 262144);
        CHECK(info.context_key == "text_config.max_position_embeddings");
        CHECK(info.text_model_type == "qwen3_vl_text");
        CHECK(info.quantization.describe() == "bf16");
    }
    SECTION("an omni model's thinker") {
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "qwen3_omni_moe", "thinker_config": {"text_config":
                              {"max_position_embeddings": 65536}}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.context_length == 65536);
        CHECK(info.context_key == "thinker_config.text_config.max_position_embeddings");
    }
    SECTION("the top level first") {
        write_fixture_file(dir.model / "config.json",
                           R"({"n_positions": 2048, "text_config": {"max_position_embeddings":
                              8192}})");
        CHECK(read_mlx_info(dir.model).context_length == 2048);
    }
}

TEST_CASE("quantization is said as mlx-lm wrote it: mixed, by mode, or not at all",
          "[models][mlx][info][quant]") {
    const Dir dir;
    SECTION("a mixed recipe's per-layer bits") {
        write_mlx_model(dir.model);
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "llama", "quantization": {"group_size": 64, "bits": 4,
               "model.layers.0.mlp.down_proj": {"group_size": 64, "bits": 6},
               "model.embed_tokens": false}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.quantization.mixed);
        CHECK(info.quantization.high_bits == 6);
        CHECK(info.quantization.describe() == "mixed 4/6-bit (group 64)");
        CHECK(info.quantization.label() == "mixed_4_6");
    }
    SECTION("a mode other than affine") {
        write_mlx_model(dir.model, MlxModelSpec{.bits = 4, .group_size = 32, .mode = "mxfp4"});
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.quantization.describe() == "mxfp4 (group 32)");
        CHECK(info.quantization.label() == "mxfp4");
    }
    SECTION("not quantized, saved by mlx-lm: the shards' metadata marks it") {
        write_mlx_model(dir.model, MlxModelSpec{.bits = 0});
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK(info.complete);
        CHECK(info.mlx_format);
        CHECK_FALSE(info.quantization.quantized());
        CHECK(info.quantization.describe() == "bf16");
        CHECK(info.quantization.label() == "bf16");
    }
    SECTION("a Hugging Face snapshot is not mlx-lm's format, whatever it publishes") {
        write_mlx_model(dir.model, MlxModelSpec{.bits = 0, .format = "pt"});
        CHECK_FALSE(read_mlx_info(dir.model).mlx_format);
        CHECK_FALSE(apogee::models::is_mlx_model_dir(dir.model));
        // gpt-oss: the publisher's own quantization, which is not mlx-lm's.
        write_fixture_file(dir.model / "config.json",
                           R"({"model_type": "gpt_oss", "max_position_embeddings": 131072,
                              "quantization_config": {"quant_method": "mxfp4"}})");
        const MlxInfo info = read_mlx_info(dir.model);
        CHECK_FALSE(info.mlx_format);
        CHECK(info.quantization.describe() == "mxfp4 (as published)");
        CHECK(info.quantization.label() == "mxfp4");
        CHECK(info.quantization.dtype.empty());
        CHECK(MlxInfo{}.quantization.describe() == "precision not stated");
    }
}

TEST_CASE("an mlx entry's window is 26a's: configured, else the default under the trained one",
          "[models][mlx][info][window]") {
    MlxInfo info;
    info.config_read = true;
    info.context_length = 131072;
    apogee::harness::BackendConfig backend;
    apogee::models::MlxWindow window = apogee::models::mlx_window(info, backend);
    CHECK(window.window == 32768);
    CHECK_FALSE(window.configured);
    CHECK(window.trained == 131072);
    CHECK(apogee::models::describe(window) ==
          "32768-token window (the default; trained for 131072)");

    info.context_length = 4096;  // trained for less than the default
    CHECK(apogee::models::mlx_window(info, backend).window == 4096);

    backend.context_size = 2048;
    window = apogee::models::mlx_window(info, backend);
    CHECK(window.window == 2048);
    CHECK(window.configured);
    CHECK(apogee::models::describe(window) == "2048-token window (context_size; trained for 4096)");
}

// ---- what kind of MLX directory (53e) --------------------------------------------------

TEST_CASE("an MLX directory's kind is its record's, else its files', else chat",
          "[models][mlx][kind]") {
    // One reader decides: the record a conversion wrote, else what the files
    // said before there was a record (a vision tower and its processor),
    // else a language model -- every directory before the record reads so.
    const Dir dir;
    using apogee::models::MlxKind;
    using apogee::models::MlxKindSource;
    using apogee::models::read_mlx_kind;

    SECTION("no record: a chat model, as every MLX directory was") {
        write_mlx_model(dir.model);
        const MlxKind kind = read_mlx_kind(dir.model);
        CHECK(kind.word == "chat");
        CHECK(kind.source == MlxKindSource::Default);
        CHECK(kind.known());
        CHECK(kind.backend());
    }
    SECTION("no record, and a vision tower with its processor: a vision model by its files") {
        write_mlx_model(dir.model, MlxModelSpec{.vision = true});
        CHECK(apogee::models::mlx_vision_files(dir.model));
        const MlxKind kind = read_mlx_kind(dir.model);
        CHECK(kind.word == "vision");
        CHECK(kind.source == MlxKindSource::Files);
        CHECK(kind.backend());
    }
    SECTION("a tower without its processor, or a processor without a tower, says nothing") {
        write_mlx_model(dir.model, MlxModelSpec{.vision = true});
        std::filesystem::remove(dir.model / "preprocessor_config.json");
        CHECK_FALSE(apogee::models::mlx_vision_files(dir.model));
        CHECK(read_mlx_kind(dir.model).word == "chat");
        // The processor's other spelling is a processor too.
        write_fixture_file(dir.model / "processor_config.json", "{}");
        CHECK(read_mlx_kind(dir.model).word == "vision");

        write_mlx_model(dir.root.path() / "plain");
        write_fixture_file(dir.root.path() / "plain" / "preprocessor_config.json", "{}");
        CHECK(read_mlx_kind(dir.root.path() / "plain").word == "chat");
    }
    SECTION("a pull's record names no kind: the files decide, as before") {
        write_mlx_model(dir.model, MlxModelSpec{.vision = true});
        apogee::models::Snapshot record;
        record.ref = "mlx-community/M-4bit";
        record.source = "huggingface";
        REQUIRE(apogee::models::write_snapshot(dir.model, record));
        const MlxKind kind = read_mlx_kind(dir.model);
        CHECK(kind.word == "vision");
        CHECK(kind.source == MlxKindSource::Files);
    }
    SECTION("each recorded word is the kind, whatever the files say") {
        for (const std::string word :
             {"chat", "vision", "embedding", "drafter", "video", "voice"}) {
            INFO(word);
            const std::filesystem::path at = dir.root.path() / word;
            // A vision tower under a record of `chat`: what mlx-lm made is a
            // language model, and its record says so.
            write_mlx_model(at, MlxModelSpec{.vision = true, .kind = word});
            const MlxKind kind = read_mlx_kind(at);
            CHECK(kind.word == word);
            CHECK(kind.source == MlxKindSource::Recorded);
            CHECK(kind.known());
            CHECK(kind.backend() == (word == "chat" || word == "vision"));
        }
    }
    SECTION("a word this build does not know is kept as written, and runs as nothing") {
        write_mlx_model(dir.model, MlxModelSpec{.kind = "Hologram"});
        const MlxKind kind = read_mlx_kind(dir.model);
        CHECK(kind.word == "Hologram");
        CHECK(kind.source == MlxKindSource::Recorded);
        CHECK_FALSE(kind.known());
        CHECK_FALSE(kind.backend());
    }
    SECTION("a kind that is not a word is no kind recorded, and the record still reads") {
        write_mlx_model(dir.model, MlxModelSpec{.vision = true});
        for (const char* kind : {R"("kind": 7)", R"("kind": "")", R"("kind": null)"}) {
            INFO(kind);
            write_fixture_file(
                dir.model / "apogee-snapshot.json",
                std::string{R"({"ref": "org/m", "source": "convert", "files": [], )"} + kind + "}");
            const std::optional<apogee::models::Snapshot> record =
                apogee::models::load_snapshot(dir.model);
            REQUIRE(record.has_value());
            CHECK(record->ref == "org/m");
            CHECK(record->kind.empty());
            CHECK(read_mlx_kind(dir.model).source == MlxKindSource::Files);
        }
    }
}

TEST_CASE("the kind is written beside the transform, only when there is one, and read as written",
          "[models][mlx][kind][record]") {
    const Dir dir;
    write_mlx_model(dir.model);
    apogee::models::Snapshot record;
    record.ref = "org--m/safetensors/aaaaaaaaaaaa";
    record.source = "convert";
    record.transform = "mlx_lm.convert 4bit";
    record.kind = "embedding";
    REQUIRE(apogee::models::write_snapshot(dir.model, record));
    const std::optional<apogee::models::Snapshot> read = apogee::models::load_snapshot(dir.model);
    REQUIRE(read.has_value());
    CHECK(read->transform == "mlx_lm.convert 4bit");
    CHECK(read->kind == "embedding");

    // A record with no kind -- a pull's, every one before this -- writes none.
    record.kind.clear();
    REQUIRE(apogee::models::write_snapshot(dir.model, record));
    std::ifstream in{apogee::models::snapshot_record_path(dir.model)};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(text.find("\"kind\"") == std::string::npos);
    CHECK(apogee::models::load_snapshot(dir.model)->kind.empty());
}

TEST_CASE("each kind is said in one wording, with where it came from",
          "[models][mlx][kind][wording]") {
    using apogee::models::describe_mlx_kind;
    CHECK(describe_mlx_kind("chat") == "a language model: an mlx backend chats with it");
    CHECK(describe_mlx_kind("vision") ==
          "a vision model: an mlx backend chats with it, and shows it images through mlx-vlm");
    CHECK(describe_mlx_kind("embedding") ==
          "an embedding model: the embedding role uses it, never a chat backend");
    CHECK(describe_mlx_kind("drafter") == "a drafter: it runs beside its base model, never alone");
    CHECK(describe_mlx_kind("video") ==
          "a video model: 'apogee video generate' runs it, never a chat backend");
    CHECK(describe_mlx_kind("voice") ==
          "a voice model: 'apogee voice say' speaks with it, never a chat backend");
    CHECK(describe_mlx_kind("Hologram") ==
          "kind 'Hologram', not one this apogee knows: nothing here runs it");

    using apogee::models::MlxKind;
    using apogee::models::MlxKindSource;
    CHECK(apogee::models::describe(MlxKind{}) ==
          "chat (no kind recorded) -- a language model: an mlx backend chats with it");
    CHECK(apogee::models::describe(MlxKind{.word = "vision", .source = MlxKindSource::Files}) ==
          "vision (inferred from its files: a vision_config and an image processor) -- a vision "
          "model: an mlx backend chats with it, and shows it images through mlx-vlm");
    CHECK(apogee::models::describe(MlxKind{.word = "voice", .source = MlxKindSource::Recorded}) ==
          "voice (recorded) -- a voice model: 'apogee voice say' speaks with it, never a chat "
          "backend");
}

TEST_CASE("a recorded kind survives a repair of the directory's files",
          "[models][mlx][kind][repair]") {
    // `models repair` re-fetches what its record lists and never writes the
    // record itself: the kind rides through.
    const Dir dir;
    const apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(dir.root.path());
    const std::filesystem::path staging =
        apogee::models::incoming_path(roots, apogee::models::kMlxFormat, "org--e");
    write_mlx_model(staging);
    apogee::models::Snapshot record;
    record.ref = "org--e/safetensors/aaaaaaaaaaaa";
    record.source = "convert";
    record.transform = "a converter";
    record.kind = "embedding";
    const apogee::models::StoredDirectory stored =
        apogee::models::commit_mlx(roots, "org--e", staging, record);
    REQUIRE(stored.error.empty());
    const std::string good = [&] {
        std::ifstream in{stored.dir / "tokenizer.json", std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }();
    write_fixture_file(stored.dir / "tokenizer.json", "damaged");

    const apogee::models::SnapshotDamage damage = apogee::models::find_snapshot_damage(stored.dir);
    REQUIRE(damage.refetch.size() == 1);
    const apogee::models::SnapshotRepair repair = apogee::models::repair_snapshot(
        stored.dir, damage,
        [&good](const apogee::models::SnapshotFile&, const std::filesystem::path& to) {
            write_fixture_file(to, good);
            return std::string{};
        });
    REQUIRE(repair.error.empty());
    CHECK(repair.fetched == std::vector<std::string>{"tokenizer.json"});
    const apogee::models::MlxKind kind = apogee::models::read_mlx_kind(stored.dir);
    CHECK(kind.word == "embedding");
    CHECK(kind.source == apogee::models::MlxKindSource::Recorded);
}
