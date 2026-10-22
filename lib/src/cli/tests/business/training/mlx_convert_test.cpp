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

#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "support/towered_configs.h"

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
