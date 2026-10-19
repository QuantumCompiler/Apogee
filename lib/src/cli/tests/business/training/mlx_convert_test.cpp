#include "training/mlx_convert.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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
