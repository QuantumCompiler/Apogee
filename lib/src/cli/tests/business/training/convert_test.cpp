#include "training/convert.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/assets.h"
#include "contracts/sha256.h"
#include "platform/child_process.h"
#include "support/env_guard.h"

using apogee::training::converter_arguments;
using apogee::training::converter_out_types;
using apogee::training::converter_unavailable;
using apogee::training::explain_converter_failure;
using apogee::training::PythonEnv;

TEST_CASE("the converter is called with one argument shape", "[training][convert]") {
    // `train promote` and `models convert` both run the script through this;
    // a second spelling of its arguments would drift from upstream alone.
    CHECK(converter_arguments("q8_0", "/snap", "/out/m.gguf") ==
          std::vector<std::string>{"--outtype", "q8_0", "--outfile", "/out/m.gguf", "/snap"});
    // The projector is the same run with --mmproj: the script picks the
    // encoder's tensors itself.
    CHECK(converter_arguments("f16", "/snap", "/out/m-mmproj.gguf",
                              apogee::training::ConverterOutput::Projector) ==
          std::vector<std::string>{"--outtype", "f16", "--outfile", "/out/m-mmproj.gguf", "/snap",
                                   "--mmproj"});
}

TEST_CASE("an embedding model's dense layers are asked for; nothing else changes",
          "[training][convert][embedding]") {
    // M21: the converter leaves a sentence-transformers model's dense
    // modules out unless asked (its help names google/embeddinggemma-300m).
    CHECK(converter_arguments("f16", "/snap", "/out/m.gguf",
                              apogee::training::ConverterOutput::Model, true) ==
          std::vector<std::string>{"--outtype", "f16", "--outfile", "/out/m.gguf", "/snap",
                                   "--sentence-transformers-dense-modules"});
    // A projector's run is no place for them.
    CHECK(converter_arguments("f16", "/snap", "/out/m-mmproj.gguf",
                              apogee::training::ConverterOutput::Projector, true) ==
          std::vector<std::string>{"--outtype", "f16", "--outfile", "/out/m-mmproj.gguf", "/snap",
                                   "--mmproj"});
}

TEST_CASE("a failure listing hundreds of names reads in a line or so, its count and tail kept",
          "[training][convert][embedding]") {
    // The user's run (M21): mlx's strict weight load named every parameter
    // it could not place, one a line, the run's stderr tail on the last.
    std::string error = "ValueError: Received 314 parameters not in model: \n";
    for (int layer = 0; layer < 24; ++layer) {
        for (const char* weight : {"input_layernorm", "mlp.down_proj", "self_attn.q_proj"}) {
            error += "layers." + std::to_string(layer) + "." + weight + ".weight,\n";
        }
    }
    error += "norm.weight. -- stderr: [INFO] Loading";
    const std::string folded = apogee::training::fold_name_list(error);
    CHECK(folded.starts_with("ValueError: Received 314 parameters not in model: \nlayers.0."));
    CHECK(folded.find("... and 53 more") != std::string::npos);  // 73 names, 20 kept
    CHECK(folded.ends_with(" -- stderr: [INFO] Loading"));
    CHECK(std::ranges::count(folded, '\n') == 21);  // its line, 20 names, the count
    // An error that lists nothing, or few, is left as written.
    CHECK(apogee::training::fold_name_list("Traceback: it failed\nKeyError: 'x'") ==
          "Traceback: it failed\nKeyError: 'x'");
    const std::string few = "missing:\na.weight,\nb.weight.";
    CHECK(apogee::training::fold_name_list(few) == few);
}

TEST_CASE("the offered precisions are the script's own, less the ternary ones",
          "[training][convert]") {
    const std::vector<std::string>& types = converter_out_types();
    CHECK(types.front() == "f16");  // the default, and what promote writes
    for (const std::string_view ternary : {"tq1_0", "tq2_0"}) {
        CHECK(std::find(types.begin(), types.end(), ternary) == types.end());
    }
}

TEST_CASE("a converter that cannot run names the one command that fixes it",
          "[training][convert]") {
    const apogee::testing::TempDir root{"convert-env-" + std::to_string(std::random_device{}())};
    const std::filesystem::path venv = root.path() / "venv";
    const std::filesystem::path script = root.path() / "convert" / "convert_hf_to_gguf.py";

    // No environment at all.
    CHECK(converter_unavailable(PythonEnv{venv}, script).find("train setup --with convert") !=
          std::string::npos);

    // An environment without the convert set.
    // Where the platform's venv puts it: bin/python, or Scripts/python.exe.
    const std::filesystem::path python = PythonEnv{venv}.interpreter();
    std::filesystem::create_directories(python.parent_path());
    std::ofstream{python} << "";
    std::ofstream{venv / "apogee.json"} << R"({"sets": ["prepare"]})";
    CHECK(converter_unavailable(PythonEnv{venv}, script).find("'convert' requirement set") !=
          std::string::npos);

    // The set, but no seeded script.
    std::ofstream{venv / "apogee.json"} << R"({"sets": ["prepare", "convert"]})";
    CHECK(converter_unavailable(PythonEnv{venv}, script).find("apogee check --fix") !=
          std::string::npos);

    // The script, but not the package it imports: it would fall back to the
    // PyPI `gguf`, which lags the pin, so this is refused too.
    const std::filesystem::path package = root.path() / "convert" / "gguf-py" / "gguf";
    for (const apogee::harness::BundledScript& file : apogee::harness::bundled_converter_files()) {
        const std::filesystem::path path = root.path() / std::string{file.name};
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << file.text;
    }
    std::filesystem::remove(package / "__init__.py");
    const std::string incomplete = converter_unavailable(PythonEnv{venv}, script);
    CHECK(incomplete.find((package / "__init__.py").string()) != std::string::npos);
    CHECK(incomplete.find("apogee check --fix") != std::string::npos);

    // Everything in place.
    std::ofstream{package / "__init__.py"} << "";
    CHECK(converter_unavailable(PythonEnv{venv}, script).empty());
}

TEST_CASE("a converter an earlier Apogee seeded is refused, naming the update",
          "[training][convert]") {
    // After a pin bump the seeded copy keeps the old llama.cpp's converter --
    // skip-if-present -- and would refuse exactly the models the bump was for.
    const apogee::testing::TempDir root{"convert-stale-" + std::to_string(std::random_device{}())};
    const std::filesystem::path venv = root.path() / "venv";
    const std::filesystem::path script = root.path() / "convert" / "convert_hf_to_gguf.py";
    // Where the platform's venv puts it: bin/python, or Scripts/python.exe.
    const std::filesystem::path python = PythonEnv{venv}.interpreter();
    std::filesystem::create_directories(python.parent_path());
    std::ofstream{python} << "";
    std::ofstream{venv / "apogee.json"} << R"({"sets": ["convert"]})";
    for (const apogee::harness::BundledScript& file : apogee::harness::bundled_converter_files()) {
        const std::filesystem::path path = root.path() / std::string{file.name};
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << file.text;
    }
    CHECK(converter_unavailable(PythonEnv{venv}, script).empty());

    // The entry script as an earlier Apogee shipped it.
    // Binary: a text-mode write on Windows would store "\r\n", whose digest
    // is not the retired one.
    std::ofstream{script, std::ios::binary} << "# the old converter\n";
    const std::string entry =
        "convert/convert_hf_to_gguf.py " + apogee::models::sha256_hex("# the old converter\n");
    const std::array<std::string_view, 1> retired{entry};
    const std::string why = converter_unavailable(PythonEnv{venv}, script, retired);
    CHECK(why.find("from an earlier Apogee") != std::string::npos);
    CHECK(why.find("apogee check --fix") != std::string::npos);

    // The same bytes, not retired: an edit, which runs.
    CHECK(converter_unavailable(PythonEnv{venv}, script, {}).empty());
}

TEST_CASE("a failure the pinned converter cannot help is explained, anything else passed through",
          "[training][convert]") {
    const std::string unknown_arch = explain_converter_failure(
        "exited with code 1 -- stderr: Model FooForCausalLM is not supported");
    CHECK(unknown_arch.find("does not know this model's architecture") != std::string::npos);

    // The architecture is known, one of its tensors is not -- what a PyPI
    // `gguf` older than the pin produced for Qwen3.5's MTP layer.
    const std::string unknown_tensor = explain_converter_failure(
        "exited with code 1 -- stderr: ValueError: Can not map tensor "
        "'model.layers.64.eh_proj.weight'");
    CHECK(unknown_tensor.find("not every tensor this model carries") != std::string::npos);

    CHECK(explain_converter_failure("exited with code 1 -- stderr: out of memory") ==
          "exited with code 1 -- stderr: out of memory");
}

// ---------------------------------------------------------------------------
// The LoRA converter (53b): the vendored sibling, over the one script runner
// ---------------------------------------------------------------------------

TEST_CASE("the LoRA converter is called with one argument shape, its base always named",
          "[training][convert][adapter]") {
    // `--base` names the base's configuration on disk: without it the script
    // asks the Hub for the adapter's `base_model_name_or_path`.
    CHECK(apogee::training::lora_converter_arguments("f16", "/runs/r1/adapters", "/snap",
                                                     "/v/based/v1.lora.gguf") ==
          std::vector<std::string>{"--outtype", "f16", "--base", "/snap", "--outfile",
                                   "/v/based/v1.lora.gguf", "/runs/r1/adapters"});
    CHECK(apogee::training::lora_converter_script().filename() == "convert_lora_to_gguf.py");
    CHECK(apogee::training::lora_converter_script().parent_path() ==
          apogee::training::converter_script().parent_path());
}

TEST_CASE(
    "the LoRA converter runs under the environment's interpreter, offline, and a failure "
    "carries its stderr",
    "[training][convert][adapter]") {
    const std::string python = apogee::platform::find_on_path("python3");
    if (python.empty() || !apogee::platform::supports_child_processes()) {
        SKIP("no python3 to run a stand-in converter with");
    }
    const apogee::testing::TempDir root{"lora-run-" + std::to_string(std::random_device{}())};
    // A stand-in for the vendored script: it writes what it was handed where
    // the GGUF would go, and fails when the adapter directory says so.
    const std::filesystem::path script = root.path() / "convert_lora_to_gguf.py";
    std::ofstream{script, std::ios::binary} << R"(import json, os, sys
args = sys.argv[1:]
if os.path.exists(os.path.join(args[-1], "fail")):
    print("converting", flush=True)
    print("Model FooForCausalLM is not supported", file=sys.stderr)
    sys.exit(1)
out = args[args.index("--outfile") + 1]
with open(out, "w") as f:
    json.dump({"args": args, "offline": os.environ.get("HF_HUB_OFFLINE")}, f)
print("wrote " + out, flush=True)
)";
    const std::filesystem::path adapter = root.path() / "adapters";
    std::filesystem::create_directories(adapter);
    const std::filesystem::path out = root.path() / "v1.lora.gguf";
    const apogee::training::AdapterConverter convert =
        apogee::training::script_lora_converter(python, script);
    std::vector<std::string> messages;
    const std::string error =
        convert(adapter, root.path() / "snap", out,
                [&messages](std::string_view text) { messages.emplace_back(text); }, {});
    INFO(error);
    REQUIRE(error.empty());
    std::ifstream in{out, std::ios::binary};
    const std::string written{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(written.find(R"("offline": "1")") != std::string::npos);
    CHECK(written.find(R"("--outtype", "f16", "--base")") != std::string::npos);
    CHECK(std::ranges::any_of(
        messages, [](const std::string& message) { return message.starts_with("wrote "); }));

    // A failure: the script's stderr tail in the error, and the plain line
    // a known architecture-shaped failure earns.
    std::ofstream{adapter / "fail"} << "";
    const std::string failed = convert(adapter, root.path() / "snap", out, {}, {});
    CHECK(failed.find("Model FooForCausalLM is not supported") != std::string::npos);
    CHECK(failed.find("does not know this model's architecture") != std::string::npos);
}

TEST_CASE("the LoRA converter that cannot run names the command that fixes it, as its sibling",
          "[training][convert][adapter]") {
    // The same tree, so the same questions: the environment, the set, every
    // file seeded and none of them an earlier Apogee's.
    const apogee::testing::TempDir root{"lora-env-" + std::to_string(std::random_device{}())};
    const std::filesystem::path venv = root.path() / "venv";
    const std::filesystem::path script = root.path() / "convert" / "convert_lora_to_gguf.py";
    const std::filesystem::path python = PythonEnv{venv}.interpreter();
    std::filesystem::create_directories(python.parent_path());
    std::ofstream{python} << "";
    std::ofstream{venv / "apogee.json"} << R"({"sets": ["convert"]})";
    CHECK(converter_unavailable(PythonEnv{venv}, script).find("apogee check --fix") !=
          std::string::npos);
    bool seeded = false;
    for (const apogee::harness::BundledScript& file : apogee::harness::bundled_converter_files()) {
        const std::filesystem::path path = root.path() / std::string{file.name};
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << file.text;
        seeded = seeded || file.name == "convert/convert_lora_to_gguf.py";
    }
    REQUIRE(seeded);
    CHECK(converter_unavailable(PythonEnv{venv}, script).empty());
    // The full converter's run needs the LoRA script seeded too: one tree.
    std::filesystem::remove(script);
    const std::string incomplete =
        converter_unavailable(PythonEnv{venv}, root.path() / "convert" / "convert_hf_to_gguf.py");
    CHECK(incomplete.find("convert_lora_to_gguf.py") != std::string::npos);
}
