#include "training/mlx_convert.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "modelstore/mlx_info.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "support/towered_configs.h"
#include "support/voice_model.h"
#include "support/wan_original.h"

/// `models convert --mlx`'s child (27b): the argument shape, the precisions,
/// and the shipped `assets/mlx/mlx_convert.py` -- the file, not a copy --
/// run on bare python3 under the stub `mlx_lm` (`data/backends/scripts/`),
/// whose `convert` writes a small MLX directory. Enough to hold the driver to
/// what the command relies on without Metal, weights or the network: the
/// precision reaches `mlx_lm.convert` as asked, a library's print never
/// reaches the protocol, a failure is the converter's own words, a missing
/// `mlx-lm` names its fix, and a cancel ends the child. Skipped by name on a
/// host with no python3.
namespace {

using apogee::training::find_mlx_precision;
using apogee::training::mlx_converter_arguments;
using apogee::training::MlxPrecision;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "mlx" / "mlx_convert.py";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts";

std::string python3() {
    return apogee::platform::find_on_path("python3");
}

/// A full-weight snapshot to convert, and somewhere to write.
struct Work {
    apogee::testing::TempDir root{"mlx-convert-" + std::to_string(std::random_device{}())};
    std::filesystem::path snapshot = root.path() / "snapshot";
    std::filesystem::path out = root.path() / "out";
    std::filesystem::path record = root.path() / "calls.jsonl";

    Work() {
        apogee::testing::write_mlx_model(snapshot,
                                         apogee::testing::MlxModelSpec{.bits = 0, .format = "pt"});
    }

    [[nodiscard]] std::vector<nlohmann::json> calls() const {
        std::vector<nlohmann::json> lines;
        std::ifstream in{record};
        for (std::string line; std::getline(in, line);) {
            lines.push_back(nlohmann::json::parse(line));
        }
        return lines;
    }
};

[[nodiscard]] std::string convert(const Work& work, const MlxPrecision& precision,
                                  std::vector<std::string>* messages = nullptr,
                                  const apogee::harness::CancellationToken& cancellation = {},
                                  const std::string& stubs = "stub_mlx") {
    const apogee::testing::EnvGuard path{"PYTHONPATH", (kStubs / stubs).string()};
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD", work.record.string()};
    const apogee::training::MlxConverter converter =
        apogee::training::script_mlx_converter(python3(), kDriver, precision);
    return converter(
        work.snapshot, work.out,
        [messages](std::string_view message) {
            if (messages != nullptr) {
                messages->emplace_back(message);
            }
        },
        cancellation);
}

/// The driver on the mlx-vlm route (53f), mlx-vlm laid beside mlx-lm --
/// or, with `stubs` naming mlx-lm's alone, missing.
[[nodiscard]] std::string convert_vlm(const Work& work, const MlxPrecision& precision,
                                      std::vector<std::string>* messages = nullptr,
                                      const std::string& stubs = "stub_mlx:stub_mlx_vlm",
                                      const std::filesystem::path& script = kDriver) {
    std::string path;
    for (std::size_t start = 0; start <= stubs.size();) {
        const std::size_t end = std::min(stubs.find(':', start), stubs.size());
        path += (path.empty() ? "" : ":") + (kStubs / stubs.substr(start, end - start)).string();
        start = end + 1;
    }
    const apogee::testing::EnvGuard python_path{"PYTHONPATH", path};
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD", work.record.string()};
    const apogee::training::MlxConverter converter = apogee::training::script_mlx_converter(
        python3(), script, precision, kStubs / "stub_mlx_vlm" / "mlx_vlm",
        apogee::training::MlxConverterKind::MlxVlm);
    return converter(work.snapshot, work.out,
                     [messages](std::string_view message) {
                         if (messages != nullptr) {
                             messages->emplace_back(message);
                         }
                     },
                     {});
}

#define REQUIRE_PYTHON()                                                          \
    do {                                                                          \
        if (python3().empty() || !apogee::platform::supports_child_processes()) { \
            SKIP("no python3 to run the driver with: the driver's cases skip");   \
        }                                                                         \
    } while (false)

}  // namespace

TEST_CASE("the precisions are mlx_lm.convert's own, named as community builds name them",
          "[training][mlx][convert]") {
    const auto precisions = apogee::training::mlx_precisions();
    REQUIRE_FALSE(precisions.empty());
    CHECK(precisions.front().name == "4bit");  // the default
    std::set<std::string_view> names;
    for (const MlxPrecision& precision : precisions) {
        CHECK(names.insert(precision.name).second);
        // Quantized, or a floating-point type: never both, never neither.
        CHECK((precision.bits > 0) == precision.dtype.empty());
        CHECK(find_mlx_precision(precision.name) == &precision);
    }
    CHECK(find_mlx_precision("Q4_K_M") == nullptr);
    CHECK(apogee::training::mlx_precision_names().size() == precisions.size());
}

TEST_CASE("the driver is called with one argument shape", "[training][mlx][convert]") {
    CHECK(mlx_converter_arguments(*find_mlx_precision("4bit"), "/snap", "/out") ==
          std::vector<std::string>{"--hf-path", "/snap", "--mlx-path", "/out", "--q-bits", "4",
                                   "--q-group-size", "64", "--q-mode", "affine"});
    // mxfp4 keeps its own group: mlx-lm's default for the mode.
    CHECK(mlx_converter_arguments(*find_mlx_precision("mxfp4"), "/snap", "/out") ==
          std::vector<std::string>{"--hf-path", "/snap", "--mlx-path", "/out", "--q-bits", "4",
                                   "--q-mode", "mxfp4"});
    CHECK(mlx_converter_arguments(*find_mlx_precision("bf16"), "/snap", "/out") ==
          std::vector<std::string>{"--hf-path", "/snap", "--mlx-path", "/out", "--dtype",
                                   "bfloat16"});
    CHECK(apogee::training::mlx_converter_script().filename() == "mlx_convert.py");
}

TEST_CASE("the converter is the set's towers' to choose, and mlx-lm's argv stays as it was",
          "[training][mlx][convert][towers]") {
    using apogee::training::choose_mlx_converter;
    using apogee::training::MlxConverterKind;
    // 53f: towers go to mlx-vlm, kept; --text-only asks for mlx-lm's build;
    // a language model is mlx-lm's whatever is asked.
    CHECK(choose_mlx_converter(true, false) == MlxConverterKind::MlxVlm);
    CHECK(choose_mlx_converter(true, true) == MlxConverterKind::MlxLm);
    CHECK(choose_mlx_converter(false, false) == MlxConverterKind::MlxLm);
    CHECK(choose_mlx_converter(false, true) == MlxConverterKind::MlxLm);
    CHECK(apogee::training::to_string(MlxConverterKind::MlxVlm) == "mlx-vlm");
    CHECK(apogee::training::mlx_converter_package(MlxConverterKind::MlxVlm) == "mlx_vlm");
    CHECK(apogee::training::mlx_converter_package(MlxConverterKind::MlxLm) == "mlx_lm");
    CHECK(apogee::training::mlx_converter_entry(MlxConverterKind::MlxVlm) == "mlx_vlm.convert");
    CHECK(apogee::training::mlx_converter_entry(MlxConverterKind::MlxLm) == "mlx_lm.convert");
    CHECK(apogee::training::mlx_converter_set(MlxConverterKind::MlxVlm) ==
          apogee::training::RequirementSet::MlxVlm);
    CHECK(apogee::training::mlx_converter_set(MlxConverterKind::MlxLm) ==
          apogee::training::RequirementSet::Mlx);

    // The one table, mapped for both: mlx-vlm's leads with its route; the
    // default and mlx-lm's named are byte for byte what they were.
    for (const MlxPrecision& precision : apogee::training::mlx_precisions()) {
        INFO(precision.name);
        const std::vector<std::string> lm = mlx_converter_arguments(precision, "/snap", "/out");
        CHECK(mlx_converter_arguments(precision, "/snap", "/out", MlxConverterKind::MlxLm) == lm);
        std::vector<std::string> vlm{"--with", "mlx-vlm"};
        vlm.insert(vlm.end(), lm.begin(), lm.end());
        CHECK(mlx_converter_arguments(precision, "/snap", "/out", MlxConverterKind::MlxVlm) == vlm);
    }
}

TEST_CASE("the shipped driver converts through mlx_lm.convert as asked, its prints kept off",
          "[training][mlx][convert][driver]") {
    REQUIRE_PYTHON();
    const Work work;
    std::vector<std::string> messages;
    REQUIRE(convert(work, *find_mlx_precision("4bit"), &messages).empty());
    const std::vector<nlohmann::json> calls = work.calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["hf_path"] == work.snapshot.string());
    CHECK(calls[0]["mlx_path"] == work.out.string());
    CHECK(calls[0]["quantize"] == true);
    CHECK(calls[0]["q_bits"] == 4);
    CHECK(calls[0]["q_group_size"] == 64);
    CHECK(std::filesystem::exists(work.out / "config.json"));
    // Its progress, and none of the library's print().
    CHECK(messages == std::vector<std::string>{"loading mlx-lm", "quantizing"});

    // Unquantized: the dtype, no quantize.
    const Work plain;
    REQUIRE(convert(plain, *find_mlx_precision("bf16")).empty());
    const std::vector<nlohmann::json> plain_calls = plain.calls();
    REQUIRE(plain_calls.size() == 1);
    CHECK(plain_calls[0]["quantize"] == false);
    CHECK(plain_calls[0]["dtype"] == "bfloat16");
}

TEST_CASE("the driver's failures are the converter's own words, and its refusals come first",
          "[training][mlx][convert][driver]") {
    REQUIRE_PYTHON();
    const Work work;
    SECTION("the converter raises") {
        const apogee::testing::EnvGuard error{"STUB_MLX_CONVERT_ERROR",
                                              "Model type gemma9 not supported."};
        CHECK(convert(work, *find_mlx_precision("4bit"))
                  .find("ValueError: Model type gemma9 not supported.") != std::string::npos);
    }
    SECTION("an output that exists is never written over") {
        std::filesystem::create_directories(work.out);
        CHECK(convert(work, *find_mlx_precision("4bit"))
                  .find("already exists -- a conversion never writes over one") !=
              std::string::npos);
        CHECK(work.calls().empty());
    }
    SECTION("mlx-lm missing from the environment names its fix") {
        CHECK(convert(work, *find_mlx_precision("4bit"), nullptr, {}, "stub_mlx_missing")
                  .find("apogee train setup --with mlx") != std::string::npos);
    }
}

TEST_CASE("a cancel ends the driver mid-conversion", "[training][mlx][convert][driver]") {
    REQUIRE_PYTHON();
    const Work work;
    const apogee::testing::EnvGuard hang{"STUB_MLX_CONVERT_HANG", "1"};
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    std::thread canceller{[&work, &token] {
        // Once the converter has started writing, as a Ctrl-C would find it.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (!std::filesystem::exists(work.out / "model-00001-of-00002.safetensors") &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
        token.cancel();
    }};
    const auto started = std::chrono::steady_clock::now();
    const std::string result = convert(work, *find_mlx_precision("4bit"), nullptr, token);
    canceller.join();
    CHECK(result == "cancelled");
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{15});
}

TEST_CASE("the shipped driver converts through mlx_vlm.convert as its command line would",
          "[training][mlx][convert][driver][towers]") {
    // 53f: the whole model, its towers kept -- the precision on mlx-vlm's own
    // flags, remote code never trusted, the library's print kept off.
    REQUIRE_PYTHON();
    Work work;
    apogee::testing::write_fixture_file(work.snapshot / "config.json",
                                        apogee::testing::kQwen3VlConfig);
    apogee::testing::write_fixture_file(work.snapshot / "preprocessor_config.json", "{}");
    std::vector<std::string> messages;
    REQUIRE(convert_vlm(work, *find_mlx_precision("4bit"), &messages).empty());
    std::vector<nlohmann::json> calls = work.calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["converter"] == "mlx_vlm");
    CHECK(calls[0]["hf_path"] == work.snapshot.string());
    CHECK(calls[0]["mlx_path"] == work.out.string());
    CHECK(calls[0]["quantize"] == true);
    CHECK(calls[0]["q_bits"] == 4);
    CHECK(calls[0]["q_group_size"] == 64);
    CHECK(calls[0]["q_mode"] == "affine");
    CHECK(calls[0]["trust_remote_code"] == false);
    CHECK(messages == std::vector<std::string>{"loading mlx-vlm", "quantizing"});
    // Its towers and the processor that feeds one, kept.
    CHECK(std::filesystem::exists(work.out / "preprocessor_config.json"));

    // mxfp4: the mode's own group, as mlx-vlm's command line leaves it.
    Work fp4;
    REQUIRE(convert_vlm(fp4, *find_mlx_precision("mxfp4")).empty());
    calls = fp4.calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["q_group_size"].is_null());
    CHECK(calls[0]["q_mode"] == "mxfp4");
    // Unquantized: the dtype, no quantize.
    Work plain;
    REQUIRE(convert_vlm(plain, *find_mlx_precision("bf16")).empty());
    calls = plain.calls();
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["quantize"] == false);
    CHECK(calls[0]["dtype"] == "bfloat16");
}

TEST_CASE("the mlx-vlm route names its fix, its own door, and a driver too old for it",
          "[training][mlx][convert][driver][towers]") {
    REQUIRE_PYTHON();
    const Work work;
    SECTION("mlx-vlm missing from the environment") {
        const std::string error =
            convert_vlm(work, *find_mlx_precision("4bit"), nullptr, "stub_mlx:stub_mlx_missing");
        CHECK(error.find("mlx-vlm is not installed in Apogee's Python environment") !=
              std::string::npos);
        CHECK(error.find("apogee train setup --with mlx-vlm") != std::string::npos);
        CHECK(work.calls().empty());
    }
    SECTION("a type mlx-vlm has no package for: its door, its own nearest types") {
        apogee::testing::write_fixture_file(work.snapshot / "config.json",
                                            apogee::testing::kQwen3OmniConfig);
        const apogee::testing::EnvGuard unsupported{"STUB_MLX_UNSUPPORTED", "qwen3_omni_moe"};
        const std::string error = convert_vlm(work, *find_mlx_precision("4bit"));
        CHECK(error.find("ValueError: Model type qwen3_omni_moe not supported.") !=
              std::string::npos);
        CHECK(error.ends_with(
            "if these weights are a type this mlx-vlm knows, convert them as it: --model-type "
            "<type> (nearest here: qwen3_5, qwen3_vl, qwen3_vl_moe)"));
    }
    SECTION("a seeded driver from before the route") {
        const std::filesystem::path older = work.root.path() / "mlx_convert.py";
        apogee::testing::write_fixture_file(
            older,
            "import argparse\n"
            "parser = argparse.ArgumentParser()\n"
            "for flag in ('--hf-path', '--mlx-path', '--q-bits', '--q-group-size', '--q-mode',\n"
            "             '--dtype'):\n"
            "    parser.add_argument(flag)\n"
            "parser.parse_args()\n");
        const std::string error =
            convert_vlm(work, *find_mlx_precision("4bit"), nullptr, "stub_mlx", older);
        CHECK(error.find("unrecognized arguments: --with mlx-vlm") != std::string::npos);
        CHECK(error.find("the seeded converter at " + older.string() +
                         " is older than this build: 'apogee check --fix'") != std::string::npos);
    }
}

TEST_CASE("the types an installed mlx-vlm builds are read from its own packages",
          "[training][mlx][convert][model_type][towers]") {
    // 53f: a package under models/ whose __init__ names its Model -- what
    // mlx-vlm's loader takes -- and its remap table's keys, never imported.
    CHECK(apogee::training::mlx_model_types(kStubs / "stub_mlx_vlm" / "mlx_vlm") ==
          std::vector<std::string>{"gemma4", "gemma4_unified", "qwen3_5", "qwen3_vl",
                                   "qwen3_vl_moe"});
    // A package naming only a longer name, or nothing, is no type; nor is a
    // private one.
    const apogee::testing::TempDir dir{"mlx-vlm-types-" + std::to_string(std::random_device{}())};
    const std::filesystem::path models = dir.path() / "mlx_vlm" / "models";
    apogee::testing::write_fixture_file(
        models / "qwen3_omni_moe" / "__init__.py",
        "from .qwen3_omni_moe import LanguageModel, Model, VisionModel\n");
    apogee::testing::write_fixture_file(models / "helpers" / "__init__.py",
                                        "from .output import LanguageModelOutput\n");
    apogee::testing::write_fixture_file(models / "empty" / "__init__.py", "");
    apogee::testing::write_fixture_file(models / "_private" / "__init__.py", "Model = None\n");
    CHECK(apogee::training::mlx_model_types(dir.path() / "mlx_vlm") ==
          std::vector<std::string>{"qwen3_omni_moe"});
}

TEST_CASE("the types an installed mlx-lm converts are read from its own files",
          "[training][mlx][convert][model_type]") {
    // M15: its model modules -- each defining a Model, never a helper like
    // `cache` -- and MODEL_REMAPPING's keys, read and never imported.
    CHECK(apogee::training::mlx_model_types(kStubs / "stub_mlx" / "mlx_lm") ==
          std::vector<std::string>{"gemma3", "gemma4", "gemma4_text", "gemma4_unified", "llama",
                                   "mistral"});
    CHECK(apogee::training::mlx_model_types(kStubs / "nowhere").empty());
    CHECK(apogee::training::mlx_model_types({}).empty());
}

TEST_CASE("the nearest types share the longest prefix, the shorter first, never a stranger",
          "[training][mlx][convert][model_type]") {
    using apogee::training::nearest_model_types;
    const std::vector<std::string> types{"gemma3", "gemma4",  "gemma4_text", "gemma4_unified",
                                         "llama",  "mistral", "gemma"};
    CHECK(nearest_model_types("gemma4_unified_assistant", types) ==
          std::vector<std::string>{"gemma4_unified", "gemma4_text", "gemma4", "gemma", "gemma3"});
    CHECK(nearest_model_types("gemma4_unified_assistant", types, 2) ==
          std::vector<std::string>{"gemma4_unified", "gemma4_text"});
    CHECK(nearest_model_types("qwen3", types).empty());
    // The type itself is no suggestion.
    CHECK(nearest_model_types("llama", {"llama", "llama4"}) == std::vector<std::string>{"llama4"});
    CHECK(apogee::training::unsupported_model_type(
              "ValueError: Model type gemma4_unified_assistant not supported. -- stderr: x") ==
          "gemma4_unified_assistant");
    CHECK(apogee::training::unsupported_model_type("ValueError: out of memory").empty());
}

TEST_CASE("mlx-lm refusing a model_type is folded with its door and the nearest types",
          "[training][mlx][convert][model_type][driver]") {
    // The transcript's bytes, over the stub: the vendor's variant refused,
    // now carrying the way out.
    REQUIRE_PYTHON();
    Work work;
    apogee::testing::write_mlx_model(
        work.snapshot, apogee::testing::MlxModelSpec{
                           .bits = 0, .model_type = "gemma4_unified_assistant", .format = "pt"});
    const apogee::testing::EnvGuard path{"PYTHONPATH", (kStubs / "stub_mlx").string()};
    const apogee::testing::EnvGuard unsupported{"STUB_MLX_UNSUPPORTED", "gemma4_unified_assistant"};
    const apogee::training::MlxConverter converter = apogee::training::script_mlx_converter(
        python3(), kDriver, *find_mlx_precision("4bit"), kStubs / "stub_mlx" / "mlx_lm");
    const std::string error = converter(work.snapshot, work.out, {}, {});
    CHECK(error ==
          "ValueError: Model type gemma4_unified_assistant not supported. -- stderr: [INFO] "
          "Loading -- if these weights are a type this mlx-lm knows, convert them as it: "
          "--model-type <type> (nearest here: gemma4_unified, gemma4_text, gemma4, gemma3)");
    // With nowhere to read the lists from, the door alone.
    const std::string bare = apogee::training::script_mlx_converter(
        python3(), kDriver, *find_mlx_precision("4bit"))(work.snapshot, work.out, {}, {});
    CHECK(bare.ends_with("convert them as it: --model-type <type>"));
    // Any other failure stays the converter's own words.
    CHECK(apogee::training::fold_model_type_failure("ValueError: out of memory", {"llama"}) ==
          "ValueError: out of memory");
}

namespace {

/// The driver on the mlx-embeddings route (53h), mlx-embeddings laid beside
/// mlx-lm -- or, with `stubs` naming another set, missing -- over an
/// embedding model's snapshot.
[[nodiscard]] std::string convert_embeddings(
    const Work& work, const MlxPrecision& precision, std::vector<std::string>* messages = nullptr,
    const std::string& stubs = "stub_mlx:stub_mlx_embeddings",
    const std::filesystem::path& script = kDriver) {
    std::string path;
    for (std::size_t start = 0; start <= stubs.size();) {
        const std::size_t end = std::min(stubs.find(':', start), stubs.size());
        path += (path.empty() ? "" : ":") + (kStubs / stubs.substr(start, end - start)).string();
        start = end + 1;
    }
    const apogee::testing::EnvGuard python_path{"PYTHONPATH", path};
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD", work.record.string()};
    const apogee::training::MlxConverter converter = apogee::training::script_mlx_converter(
        python3(), script, precision, kStubs / "stub_mlx_embeddings" / "mlx_embeddings",
        apogee::training::MlxConverterKind::MlxEmbeddings);
    return converter(work.snapshot, work.out,
                     [messages](std::string_view message) {
                         if (messages != nullptr) {
                             messages->emplace_back(message);
                         }
                     },
                     {});
}

/// The user's Gemma-Embedding-300M as M21's fixture has it: a bare
/// `Gemma3TextModel` and its sentence-transformers pipeline.
void as_embedding_model(const Work& work) {
    apogee::testing::write_fixture_file(
        work.snapshot / "config.json",
        R"({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text", "hidden_size": 768})");
    apogee::testing::write_fixture_file(work.snapshot / "modules.json", R"([
        {"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"},
        {"idx": 1, "path": "1_Pooling", "type": "sentence_transformers.models.Pooling"},
        {"idx": 2, "path": "2_Dense", "type": "sentence_transformers.models.Dense"},
        {"idx": 3, "path": "3_Dense", "type": "sentence_transformers.models.Dense"},
        {"idx": 4, "path": "4_Normalize", "type": "sentence_transformers.models.Normalize"}])");
    apogee::testing::write_fixture_file(work.snapshot / "2_Dense" / "model.safetensors", "x");
}

}  // namespace

TEST_CASE("an embedding model is mlx-embeddings' to convert, its argv the one table's",
          "[training][mlx][convert][embedding]") {
    using apogee::training::choose_mlx_converter;
    using apogee::training::MlxConverterKind;
    // 53h: an embedding model goes to mlx-embeddings whatever else is asked
    // -- towers or --text-only change nothing of one -- and nothing else does.
    CHECK(choose_mlx_converter(false, false, true) == MlxConverterKind::MlxEmbeddings);
    CHECK(choose_mlx_converter(true, false, true) == MlxConverterKind::MlxEmbeddings);
    CHECK(choose_mlx_converter(false, true, true) == MlxConverterKind::MlxEmbeddings);
    CHECK(choose_mlx_converter(true, false, false) == MlxConverterKind::MlxVlm);
    CHECK(choose_mlx_converter(false, false, false) == MlxConverterKind::MlxLm);
    CHECK(apogee::training::to_string(MlxConverterKind::MlxEmbeddings) == "mlx-embeddings");
    CHECK(apogee::training::mlx_converter_package(MlxConverterKind::MlxEmbeddings) ==
          "mlx_embeddings");
    CHECK(apogee::training::mlx_converter_entry(MlxConverterKind::MlxEmbeddings) ==
          "mlx_embeddings.convert");
    CHECK(apogee::training::mlx_converter_set(MlxConverterKind::MlxEmbeddings) ==
          apogee::training::RequirementSet::MlxEmbeddings);
    for (const MlxPrecision& precision : apogee::training::mlx_precisions()) {
        INFO(precision.name);
        const std::vector<std::string> lm = mlx_converter_arguments(precision, "/snap", "/out");
        std::vector<std::string> embeddings{"--with", "mlx-embeddings"};
        embeddings.insert(embeddings.end(), lm.begin(), lm.end());
        CHECK(mlx_converter_arguments(precision, "/snap", "/out",
                                      MlxConverterKind::MlxEmbeddings) == embeddings);
    }
    // Its types, read from its own modules as mlx-lm's are: a module defining
    // its Model, never a helper.
    CHECK(apogee::training::mlx_model_types(kStubs / "stub_mlx_embeddings" / "mlx_embeddings") ==
          std::vector<std::string>{"bert", "gemma3_text"});
}

TEST_CASE("the shipped driver converts an embedding model through mlx_embeddings.convert",
          "[training][mlx][convert][embedding][driver]") {
    REQUIRE_PYTHON();
    const Work work;
    as_embedding_model(work);
    SECTION("each precision as its own flags take it, the library's print kept off") {
        std::vector<std::string> messages;
        REQUIRE(convert_embeddings(work, *find_mlx_precision("8bit"), &messages).empty());
        const std::vector<nlohmann::json> calls = work.calls();
        REQUIRE(calls.size() == 1);
        CHECK(calls[0]["converter"] == "mlx_embeddings");
        CHECK(calls[0]["hf_path"] == work.snapshot.string());
        CHECK(calls[0]["mlx_path"] == work.out.string());
        CHECK(calls[0]["quantize"] == true);
        CHECK(calls[0]["q_bits"] == 8);
        CHECK(calls[0]["q_group_size"] == 64);
        CHECK(calls[0]["model_type"] == "gemma3_text");
        CHECK(messages == std::vector<std::string>{"loading mlx-embeddings", "quantizing"});
        // The top level's files carried over, its pipeline among them.
        CHECK(std::filesystem::exists(work.out / "modules.json"));
        CHECK(std::filesystem::exists(work.out / "model.safetensors"));
    }
    SECTION("mxfp4's group left to the mode, a float type unquantized") {
        REQUIRE(convert_embeddings(work, *find_mlx_precision("mxfp4")).empty());
        std::filesystem::remove_all(work.out);
        REQUIRE(convert_embeddings(work, *find_mlx_precision("bf16")).empty());
        const std::vector<nlohmann::json> calls = work.calls();
        REQUIRE(calls.size() == 2);
        CHECK(calls[0]["q_mode"] == "mxfp4");
        CHECK(calls[0]["q_group_size"].is_null());
        CHECK(calls[1]["quantize"] == false);
        CHECK(calls[1]["dtype"] == "bfloat16");
    }
    SECTION("no mlx-embeddings: its fix named, nothing converted") {
        const std::string error =
            convert_embeddings(work, *find_mlx_precision("4bit"), nullptr, "stub_mlx");
        CHECK(error.find("mlx-embeddings is not installed in Apogee's Python environment") !=
              std::string::npos);
        CHECK(error.find("apogee train setup --with mlx-embeddings") != std::string::npos);
        CHECK(work.calls().empty());
    }
    SECTION("a type it has no module for: its own words, and its door with its own types") {
        apogee::testing::write_fixture_file(
            work.snapshot / "config.json",
            R"({"architectures": ["Qwen3Model"], "model_type": "qwen3"})");
        const apogee::testing::EnvGuard unsupported{"STUB_MLX_UNSUPPORTED", "qwen3"};
        const std::string error = convert_embeddings(work, *find_mlx_precision("4bit"));
        CHECK(error.find("ValueError: Model type qwen3 not supported.") != std::string::npos);
        CHECK(error.find("if these weights are a type this mlx-embeddings knows") !=
              std::string::npos);
    }
    SECTION("a seeded driver from before the route") {
        // 53f's converter knows `--with`, and mlx-vlm alone beside mlx-lm.
        const std::filesystem::path older = work.root.path() / "mlx_convert.py";
        apogee::testing::write_fixture_file(
            older,
            "import argparse\n"
            "parser = argparse.ArgumentParser()\n"
            "parser.add_argument('--with', choices=['mlx-lm', 'mlx-vlm'])\n"
            "for flag in ('--hf-path', '--mlx-path', '--q-bits', '--q-group-size', '--q-mode',\n"
            "             '--dtype'):\n"
            "    parser.add_argument(flag)\n"
            "parser.parse_args()\n");
        const std::string error = convert_embeddings(work, *find_mlx_precision("4bit"), nullptr,
                                                     "stub_mlx:stub_mlx_embeddings", older);
        CHECK(error.find("argument --with: invalid choice") != std::string::npos);
        CHECK(error.find("the seeded converter at " + older.string() +
                         " is older than this build: 'apogee check --fix'") != std::string::npos);
    }
}

TEST_CASE("a draft head is mlx-vlm's to convert, as a drafter, whatever else is asked",
          "[training][mlx][convert][drafter]") {
    using apogee::training::choose_mlx_converter;
    using apogee::training::MlxConverterKind;
    // 53i: mlx-lm has no network for a draft head, so --text-only and the
    // towers change nothing of one; its argv is mlx-vlm's route, unchanged.
    for (const bool towers : {false, true}) {
        for (const bool text_only : {false, true}) {
            CHECK(choose_mlx_converter(towers, text_only, false, true) == MlxConverterKind::MlxVlm);
        }
    }
    CHECK(choose_mlx_converter(false, false, false, false) == MlxConverterKind::MlxLm);
    const apogee::training::MlxPrecision* bf16 = apogee::training::find_mlx_precision("bf16");
    REQUIRE(bf16 != nullptr);
    CHECK(
        apogee::training::mlx_converter_arguments(*bf16, "/in", "/out", MlxConverterKind::MlxVlm) ==
        std::vector<std::string>{"--with", "mlx-vlm", "--hf-path", "/in", "--mlx-path", "/out",
                                 "--dtype", "bfloat16"});
}

namespace {

/// The driver on the mlx-video route (53k), the stub mlx-video (and its stub
/// torch) laid beside mlx-lm -- or, with `stubs` naming another set, missing.
[[nodiscard]] std::string convert_video(const Work& work, const MlxPrecision& precision,
                                        std::vector<std::string>* messages = nullptr,
                                        const std::string& stubs = "stub_mlx:stub_mlx_video",
                                        const std::filesystem::path& script = kDriver) {
    std::string path;
    for (std::size_t start = 0; start <= stubs.size();) {
        const std::size_t end = std::min(stubs.find(':', start), stubs.size());
        path += (path.empty() ? "" : ":") + (kStubs / stubs.substr(start, end - start)).string();
        start = end + 1;
    }
    const apogee::testing::EnvGuard python_path{"PYTHONPATH", path};
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD", work.record.string()};
    const apogee::training::MlxConverter converter = apogee::training::script_mlx_converter(
        python3(), script, precision, kStubs / "stub_mlx_video" / "mlx_video",
        apogee::training::MlxConverterKind::MlxVideo);
    return converter(work.snapshot, work.out,
                     [messages](std::string_view message) {
                         if (messages != nullptr) {
                             messages->emplace_back(message);
                         }
                     },
                     {});
}

/// The user's Wan2.2-TI2V-5B, whole: its own config, a transformer shard,
/// and the two root checkpoints 53j completes it with -- small fake bytes.
void as_wan_layout(const Work& work) {
    apogee::testing::write_fixture_file(work.snapshot / "config.json",
                                        std::string{apogee::testing::kWanTi2vConfig});
    for (const std::string_view file :
         {apogee::testing::kWanTextEncoder, apogee::testing::kWanVae}) {
        apogee::testing::write_fixture_file(work.snapshot / file, "fake checkpoint bytes\n");
    }
}

}  // namespace

TEST_CASE("Wan's original layout is mlx-video's to convert, at the precisions it writes",
          "[training][mlx][convert][mlx_video]") {
    using apogee::training::choose_mlx_converter;
    using apogee::training::MlxConverterKind;
    // 53k: the layout goes to mlx-video whatever else its files might say,
    // and nothing else does.
    for (const bool towers : {false, true}) {
        for (const bool text_only : {false, true}) {
            CHECK(choose_mlx_converter(towers, text_only, false, false, true) ==
                  MlxConverterKind::MlxVideo);
        }
    }
    CHECK(choose_mlx_converter(false, false, false, false, false) == MlxConverterKind::MlxLm);
    CHECK(apogee::training::to_string(MlxConverterKind::MlxVideo) == "mlx-video");
    CHECK(apogee::training::mlx_converter_package(MlxConverterKind::MlxVideo) == "mlx_video");
    // The module the pinned commit holds its converter in -- neither README's.
    CHECK(apogee::training::mlx_converter_entry(MlxConverterKind::MlxVideo) ==
          "mlx_video.models.wan_2.convert");
    // The package its record's transform begins with is the one the store
    // knows an MLX video model made from a set by (M24): one word, both ends.
    CHECK(apogee::training::mlx_converter_package(MlxConverterKind::MlxVideo) ==
          apogee::models::kMlxVideoConverterPackage);
    CHECK(
        std::string{apogee::training::mlx_converter_entry(MlxConverterKind::MlxVideo)}.starts_with(
            std::string{apogee::models::kMlxVideoConverterPackage} + "."));
    CHECK(apogee::training::mlx_converter_set(MlxConverterKind::MlxVideo) ==
          apogee::training::RequirementSet::MlxVideo);
    // Its argv is the one table's, led by its route.
    const MlxPrecision& four = *find_mlx_precision("4bit");
    CHECK(mlx_converter_arguments(four, "/wan", "/out", MlxConverterKind::MlxVideo) ==
          std::vector<std::string>{"--with", "mlx-video", "--hf-path", "/wan", "--mlx-path", "/out",
                                   "--q-bits", "4", "--q-group-size", "64", "--q-mode", "affine"});
    // What its converter's command line takes: --bits 4 or 8 in affine
    // groups, or a --dtype; never 3 or 6 bits, nor mxfp4. Every other
    // converter writes them all.
    CHECK(apogee::training::mlx_precision_names(MlxConverterKind::MlxVideo) ==
          std::vector<std::string>{"4bit", "8bit", "bf16", "f16"});
    for (const MlxConverterKind other :
         {MlxConverterKind::MlxLm, MlxConverterKind::MlxVlm, MlxConverterKind::MlxEmbeddings}) {
        CHECK(apogee::training::mlx_precision_names(other) ==
              apogee::training::mlx_precision_names());
    }
}

TEST_CASE("mlx-video is installed when its Wan converter and torch are there, read from files",
          "[training][mlx][convert][mlx_video]") {
    const apogee::testing::TempDir root{"mlx-video-" + std::to_string(std::random_device{}())};
    const std::filesystem::path venv = root.path() / "venv";
    const std::filesystem::path site = venv / "lib" / "python3.99" / "site-packages";
    CHECK(apogee::training::find_mlx_video_converter(venv).empty());
    std::filesystem::create_directories(site);
    std::filesystem::copy(kStubs / "stub_mlx_video", site,
                          std::filesystem::copy_options::recursive);
    CHECK(apogee::training::find_mlx_video_converter(venv) ==
          site / "mlx_video" / "models" / "wan_2" / "convert.py");
    // Without the torch its converter loads checkpoints through: not
    // installed, whatever mlx-video has.
    std::filesystem::remove_all(site / "torch");
    CHECK(apogee::training::find_mlx_video_converter(venv).empty());
    std::filesystem::copy(kStubs / "stub_mlx_video" / "torch", site / "torch",
                          std::filesystem::copy_options::recursive);
    // A converter only where a README says it -- `mlx_video/wan2/` -- is none.
    std::filesystem::remove_all(site / "mlx_video" / "models" / "wan_2");
    apogee::testing::write_fixture_file(site / "mlx_video" / "wan2" / "convert.py", "\n");
    CHECK(apogee::training::find_mlx_video_converter(venv).empty());
}

TEST_CASE("the shipped driver converts Wan's original layout through mlx-video's converter",
          "[training][mlx][convert][mlx_video][driver]") {
    REQUIRE_PYTHON();
    const Work work;
    as_wan_layout(work);
    SECTION("4bit as its command line takes it, the converter's prints kept off") {
        std::vector<std::string> messages;
        REQUIRE(convert_video(work, *find_mlx_precision("4bit"), &messages).empty());
        const std::vector<nlohmann::json> calls = work.calls();
        REQUIRE(calls.size() == 1);
        CHECK(calls[0] == nlohmann::json{{"converter", "mlx_video"},
                                         {"checkpoint_dir", work.snapshot.string()},
                                         {"output_dir", work.out.string()},
                                         {"dtype", "bfloat16"},
                                         {"model_version", "auto"},
                                         {"quantize", true},
                                         {"bits", 4},
                                         {"group_size", 64}});
        // Its progress, and none of the converter's print().
        CHECK(messages == std::vector<std::string>{"loading mlx-video", "quantizing"});
        // What it writes: the documented output.
        for (const char* file :
             {"config.json", "t5_encoder.safetensors", "vae.safetensors", "model.safetensors"}) {
            INFO(file);
            CHECK(std::filesystem::is_regular_file(work.out / file));
        }
    }
    SECTION("8bit, and a float type unquantized") {
        REQUIRE(convert_video(work, *find_mlx_precision("8bit")).empty());
        std::filesystem::remove_all(work.out);
        REQUIRE(convert_video(work, *find_mlx_precision("f16")).empty());
        const std::vector<nlohmann::json> calls = work.calls();
        REQUIRE(calls.size() == 2);
        CHECK(calls[0]["bits"] == 8);
        CHECK(calls[0]["quantize"] == true);
        CHECK(calls[1]["quantize"] == false);
        CHECK(calls[1]["dtype"] == "float16");
    }
    SECTION("a precision its converter does not write, refused before it is loaded") {
        const std::string error = convert_video(work, *find_mlx_precision("3bit"));
        CHECK(error.find("mlx-video's converter quantizes 4 or 8 bits in groups of 32, 64 or "
                         "128, never 3 bits") != std::string::npos);
        CHECK(work.calls().empty());
    }
    SECTION("no mlx-video: its setup named, nothing converted") {
        const std::string error =
            convert_video(work, *find_mlx_precision("4bit"), nullptr, "stub_mlx:stub_mlx_missing");
        CHECK(error.find("mlx-video is not installed in Apogee's Python environment") !=
              std::string::npos);
        CHECK(error.find("apogee train setup --with mlx-video") != std::string::npos);
        CHECK(work.calls().empty());
    }
    SECTION("the converter's failure, in its own words") {
        const apogee::testing::EnvGuard error{"STUB_MLX_CONVERT_ERROR", "out of memory"};
        CHECK(convert_video(work, *find_mlx_precision("4bit")).find("ValueError: out of memory") !=
              std::string::npos);
    }
    SECTION("a seeded driver from before the route") {
        // 53h's converter knows mlx-embeddings, and no mlx-video.
        const std::filesystem::path older = work.root.path() / "mlx_convert.py";
        apogee::testing::write_fixture_file(
            older,
            "import argparse\n"
            "parser = argparse.ArgumentParser()\n"
            "parser.add_argument('--with', choices=['mlx-lm', 'mlx-vlm', 'mlx-embeddings'])\n"
            "for flag in ('--hf-path', '--mlx-path', '--q-bits', '--q-group-size', '--q-mode',\n"
            "             '--dtype'):\n"
            "    parser.add_argument(flag)\n"
            "parser.parse_args()\n");
        const std::string error = convert_video(work, *find_mlx_precision("4bit"), nullptr,
                                                "stub_mlx:stub_mlx_video", older);
        CHECK(error.find("argument --with: invalid choice") != std::string::npos);
        CHECK(error.find("the seeded converter at " + older.string() +
                         " is older than this build: 'apogee check --fix'") != std::string::npos);
    }
}

namespace {

/// The driver on the mlx-audio route (53m), the stub mlx-audio (and its stub
/// torch and misaki) laid beside mlx-lm -- or, with `stubs` naming another
/// set, missing -- over a voice model as Kokoro publishes it.
struct VoiceWork {
    apogee::testing::TempDir root{"mlx-voice-convert-" + std::to_string(std::random_device{}())};
    std::filesystem::path model = root.path() / "kokoro";
    std::filesystem::path out = root.path() / "out";
    std::filesystem::path record = root.path() / "calls.jsonl";
    std::filesystem::path loads = root.path() / "loads.jsonl";

    VoiceWork() {
        apogee::testing::write_voice_model(
            model, {.voices = {"af_heart", "bm_george"}, .checkpoint = "kokoro-v1_0.pth"});
    }

    [[nodiscard]] std::vector<nlohmann::json> lines(const std::filesystem::path& file) const {
        std::vector<nlohmann::json> out_lines;
        std::ifstream in{file};
        for (std::string line; std::getline(in, line);) {
            out_lines.push_back(nlohmann::json::parse(line));
        }
        return out_lines;
    }

    [[nodiscard]] std::string convert(const MlxPrecision& precision,
                                      std::vector<std::string>* messages = nullptr,
                                      const std::string& stubs = "stub_mlx:stub_mlx_audio",
                                      const std::filesystem::path& script = kDriver) const {
        std::string path;
        for (std::size_t start = 0; start <= stubs.size();) {
            const std::size_t end = std::min(stubs.find(':', start), stubs.size());
            path +=
                (path.empty() ? "" : ":") + (kStubs / stubs.substr(start, end - start)).string();
            start = end + 1;
        }
        const apogee::testing::EnvGuard python_path{"PYTHONPATH", path};
        const apogee::testing::EnvGuard calls{"STUB_MLX_CONVERT_RECORD", record.string()};
        const apogee::testing::EnvGuard torch{"STUB_MLX_AUDIO_RECORD", loads.string()};
        const apogee::training::MlxConverter converter = apogee::training::script_mlx_converter(
            python3(), script, precision, kStubs / "stub_mlx_audio" / "mlx_audio",
            apogee::training::MlxConverterKind::MlxAudio);
        return converter(model, out,
                         [messages](std::string_view message) {
                             if (messages != nullptr) {
                                 messages->emplace_back(message);
                             }
                         },
                         {});
    }
};

}  // namespace

TEST_CASE("a voice model is mlx-audio's to convert, at every precision the table maps",
          "[training][mlx][convert][mlx_audio]") {
    using apogee::training::choose_mlx_converter;
    using apogee::training::MlxConverterKind;
    // 53m: a voice model goes to mlx-audio whatever else is asked, and
    // nothing else does.
    for (const bool towers : {false, true}) {
        for (const bool text_only : {false, true}) {
            CHECK(choose_mlx_converter(towers, text_only, false, false, false, true) ==
                  MlxConverterKind::MlxAudio);
        }
    }
    CHECK(choose_mlx_converter(false, false, false, false, false, false) ==
          MlxConverterKind::MlxLm);
    CHECK(apogee::training::to_string(MlxConverterKind::MlxAudio) == "mlx-audio");
    CHECK(apogee::training::mlx_converter_package(MlxConverterKind::MlxAudio) == "mlx_audio");
    CHECK(apogee::training::mlx_converter_entry(MlxConverterKind::MlxAudio) == "mlx_audio.convert");
    CHECK(apogee::training::mlx_converter_set(MlxConverterKind::MlxAudio) ==
          apogee::training::RequirementSet::MlxAudio);
    const MlxPrecision& bf16 = *find_mlx_precision("bf16");
    CHECK(mlx_converter_arguments(bf16, "/kokoro", "/out", MlxConverterKind::MlxAudio) ==
          std::vector<std::string>{"--with", "mlx-audio", "--hf-path", "/kokoro", "--mlx-path",
                                   "/out", "--dtype", "bfloat16"});
    // Its converter takes affine bits, mxfp4 and a float type: all of them.
    CHECK(apogee::training::mlx_precision_names(MlxConverterKind::MlxAudio) ==
          apogee::training::mlx_precision_names());
}

TEST_CASE("mlx-audio builds Kokoro when its converter, Kokoro and torch are there, read from files",
          "[training][mlx][convert][mlx_audio]") {
    const apogee::testing::TempDir root{"mlx-audio-" + std::to_string(std::random_device{}())};
    const std::filesystem::path venv = root.path() / "venv";
    const std::filesystem::path site = venv / "lib" / "python3.99" / "site-packages";
    CHECK(apogee::training::find_mlx_audio_converter(venv).empty());
    CHECK(apogee::training::find_mlx_audio_kokoro(venv).empty());
    std::filesystem::create_directories(site);
    std::filesystem::copy(kStubs / "stub_mlx_audio", site,
                          std::filesystem::copy_options::recursive);
    CHECK(apogee::training::find_mlx_audio_converter(venv) == site / "mlx_audio" / "convert.py");
    CHECK(apogee::training::find_mlx_audio_kokoro(venv) ==
          site / "mlx_audio" / "tts" / "models" / "kokoro");
    // Without the torch the checkpoint is read through: no conversion; the
    // speech needs none.
    std::filesystem::remove_all(site / "torch");
    CHECK(apogee::training::find_mlx_audio_converter(venv).empty());
    CHECK_FALSE(apogee::training::find_mlx_audio_kokoro(venv).empty());
    // Without misaki, Kokoro cannot read text: no speech.
    std::filesystem::remove_all(site / "misaki");
    CHECK(apogee::training::find_mlx_audio_kokoro(venv).empty());
    std::filesystem::copy(kStubs / "stub_mlx_audio" / "misaki", site / "misaki",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy(kStubs / "stub_mlx_audio" / "torch", site / "torch",
                          std::filesystem::copy_options::recursive);
    // An mlx-audio that builds no Kokoro is none.
    std::filesystem::remove_all(site / "mlx_audio" / "tts" / "models" / "kokoro");
    CHECK(apogee::training::find_mlx_audio_converter(venv).empty());
    CHECK(apogee::training::find_mlx_audio_kokoro(venv).empty());
}

TEST_CASE("the shipped driver converts a voice model through mlx_audio.convert, read through torch",
          "[training][mlx][convert][mlx_audio][driver]") {
    REQUIRE_PYTHON();
    const VoiceWork work;
    SECTION(
        "bf16: the checkpoint flattened, the packs as voices, the converter's prints kept off") {
        std::vector<std::string> messages;
        REQUIRE(work.convert(*find_mlx_precision("bf16"), &messages).empty());
        const std::vector<nlohmann::json> calls = work.lines(work.record);
        REQUIRE(calls.size() == 1);
        // The source mlx-audio read: SafeTensors written from the PyTorch
        // release inside the output, Kokoro named, its modules under their
        // own names with DataParallel's prefix gone, each pack a voice.
        CHECK(calls[0]["hf_path"] == (work.out / ".apogee-source").string());
        CHECK(calls[0]["mlx_path"] == work.out.string());
        CHECK(calls[0]["model_type"] == "kokoro");
        CHECK(calls[0]["model_domain"] == "tts");
        CHECK(calls[0]["quantize"] == false);
        CHECK(calls[0]["dtype"] == "bfloat16");
        CHECK(calls[0]["weights"] ==
              nlohmann::json::array(
                  {"bert.embeddings.position_ids", "bert.embeddings.word_embeddings.weight",
                   "decoder.generator.conv_post.weight_v", "predictor.lstm.weight_ih_l0"}));
        CHECK(calls[0]["voices"] == nlohmann::json::array({"af_heart", "bm_george"}));
        // Read through torch, weights only.
        const std::vector<nlohmann::json> loads = work.lines(work.loads);
        REQUIRE(loads.size() == 3);
        for (const nlohmann::json& load : loads) {
            CHECK(load["weights_only"] == true);
            CHECK(load["map_location"] == "cpu");
        }
        CHECK(messages == std::vector<std::string>{"loading mlx-audio",
                                                   "reading Kokoro's PyTorch checkpoint and voices",
                                                   "converting"});
        // What it wrote: an MLX voice model, its source gone.
        CHECK(std::filesystem::is_regular_file(work.out / "config.json"));
        CHECK(std::filesystem::is_regular_file(work.out / "model.safetensors"));
        CHECK(std::filesystem::is_regular_file(work.out / "voices" / "af_heart.safetensors"));
        CHECK(std::filesystem::is_regular_file(work.out / "voices" / "bm_george.safetensors"));
        CHECK_FALSE(std::filesystem::exists(work.out / ".apogee-source"));
        const apogee::models::MlxInfo info =
            apogee::models::read_mlx_info(work.out, apogee::models::kMlxVoice);
        INFO(info.problem);
        CHECK(info.complete);
    }
    SECTION("4bit, and mxfp4 with its own group") {
        REQUIRE(work.convert(*find_mlx_precision("4bit")).empty());
        std::filesystem::remove_all(work.out);
        REQUIRE(work.convert(*find_mlx_precision("mxfp4")).empty());
        const std::vector<nlohmann::json> calls = work.lines(work.record);
        REQUIRE(calls.size() == 2);
        CHECK(calls[0]["quantize"] == true);
        CHECK(calls[0]["q_bits"] == 4);
        CHECK(calls[0]["q_group_size"] == 64);
        CHECK(calls[0]["q_mode"] == "affine");
        CHECK(calls[1]["q_mode"] == "mxfp4");
        CHECK(calls[1]["q_group_size"].is_null());
    }
    SECTION("no mlx-audio: its setup named, nothing read or converted") {
        const std::string error =
            work.convert(*find_mlx_precision("bf16"), nullptr, "stub_mlx:stub_mlx_missing");
        CHECK(error.find("mlx-audio is not installed in Apogee's Python environment") !=
              std::string::npos);
        CHECK(error.find("apogee train setup --with mlx-audio") != std::string::npos);
        CHECK(work.lines(work.record).empty());
        CHECK_FALSE(std::filesystem::exists(work.out));
    }
    SECTION("a checkpoint torch cannot read, in its words, nothing converted") {
        const apogee::testing::EnvGuard error{"STUB_TORCH_LOAD_ERROR", "not a zip archive"};
        CHECK(work.convert(*find_mlx_precision("bf16")) ==
              "could not read the voice model: RuntimeError: not a zip archive");
        CHECK(work.lines(work.record).empty());
    }
    SECTION("the converter's failure, in its own words, its source gone") {
        const apogee::testing::EnvGuard error{"STUB_MLX_CONVERT_ERROR", "out of memory"};
        CHECK(work.convert(*find_mlx_precision("bf16")).find("ValueError: out of memory") !=
              std::string::npos);
        CHECK_FALSE(std::filesystem::exists(work.out / ".apogee-source"));
    }
    SECTION("not a voice model: refused before anything is imported") {
        std::filesystem::remove_all(work.model / "voices");
        CHECK(work.convert(*find_mlx_precision("bf16")).find("is not a voice model") !=
              std::string::npos);
        CHECK(work.lines(work.loads).empty());
    }
    SECTION("a seeded driver from before the route") {
        // 53k's converter knows mlx-video, and no mlx-audio.
        const std::filesystem::path older = work.root.path() / "mlx_convert.py";
        apogee::testing::write_fixture_file(
            older,
            "import argparse\n"
            "parser = argparse.ArgumentParser()\n"
            "parser.add_argument('--with', choices=['mlx-lm', 'mlx-vlm', 'mlx-embeddings',\n"
            "                                       'mlx-video'])\n"
            "for flag in ('--hf-path', '--mlx-path', '--q-bits', '--q-group-size', '--q-mode',\n"
            "             '--dtype'):\n"
            "    parser.add_argument(flag)\n"
            "parser.parse_args()\n");
        const std::string error =
            work.convert(*find_mlx_precision("bf16"), nullptr, "stub_mlx:stub_mlx_audio", older);
        CHECK(error.find("argument --with: invalid choice") != std::string::npos);
        CHECK(error.find("the seeded converter at " + older.string() +
                         " is older than this build: 'apogee check --fix'") != std::string::npos);
    }
}
