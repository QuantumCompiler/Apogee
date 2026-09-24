#include "training/convert.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "harness/assets.h"
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
    std::filesystem::create_directories(venv / "bin");
    std::ofstream{venv / "bin" / "python"} << "";
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
        std::ofstream{path} << file.text;
    }
    std::filesystem::remove(package / "__init__.py");
    const std::string incomplete = converter_unavailable(PythonEnv{venv}, script);
    CHECK(incomplete.find((package / "__init__.py").string()) != std::string::npos);
    CHECK(incomplete.find("apogee check --fix") != std::string::npos);

    // Everything in place.
    std::ofstream{package / "__init__.py"} << "";
    CHECK(converter_unavailable(PythonEnv{venv}, script).empty());
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
