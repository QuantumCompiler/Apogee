#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
#include "harness/layout.h"
#include "models/sidecar.h"
#include "models/store.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "training/python_env.h"

/// `apogee models convert` end to end, in process, with the Python converter
/// played by a shell script: the model, its projector beside it, a projector
/// the converter cannot make, and a second run that adds only what the first
/// could not. The ladder itself is `models/convert_test.cpp`'s; this is the
/// command around it.
///
/// POSIX only: the stand-in interpreter is a `#!/bin/sh` script.
#if !defined(_WIN32)
namespace {

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// What llama.cpp's converter writes for `--mmproj`: vision tensors only.
[[nodiscard]] std::string projector_gguf() {
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(1);
    builder.string_kv("general.architecture", "clip");
    builder.tensor("v.blk.0.attn_q.weight");
    return builder.bytes();
}

struct Home {
    apogee::testing::TempDir root{"convert-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", root.path().string()};
    std::filesystem::path home = root.path();
    std::filesystem::path config_path = home / "config" / "config.yaml";
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(home / "models");
    std::filesystem::path calls = home / "converter-calls.log";
    std::filesystem::path refuse_projector = home / "no-projector";

    Home() {
        REQUIRE(apogee::harness::seed_data_directory(home).ok());
        write_file(config_path, "backends:\n  local:\n    type: mock\n");

        // The environment, with the convert set recorded.
        const apogee::training::PythonEnv env{apogee::harness::training_venv_dir()};
        write_file(env.record_path(), R"({"base_python": "/usr/bin/python3", "created_at": "x",
                                         "sets": ["convert"]})");
        write_file(home / "fixtures" / "model.gguf", apogee::testing::minimal_gguf("qwen35"));
        write_file(home / "fixtures" / "projector.gguf", projector_gguf());
        write_file(env.interpreter(),
                   "#!/bin/sh\n"
                   "echo \"$*\" >> '" +
                       calls.string() +
                       "'\n"
                       "out=''; projector=0\n"
                       "while [ $# -gt 0 ]; do\n"
                       "  case \"$1\" in\n"
                       "    --outfile) out=\"$2\"; shift ;;\n"
                       "    --mmproj) projector=1 ;;\n"
                       "  esac\n"
                       "  shift\n"
                       "done\n"
                       "if [ $projector = 1 ]; then\n"
                       "  if [ -f '" +
                       refuse_projector.string() +
                       "' ]; then\n"
                       "    echo 'Model Qwen3_5ForConditionalGeneration is not supported' >&2\n"
                       "    exit 1\n"
                       "  fi\n"
                       "  cp '" +
                       (home / "fixtures" / "projector.gguf").string() +
                       "' \"$out\"\n"
                       "else\n"
                       "  cp '" +
                       (home / "fixtures" / "model.gguf").string() +
                       "' \"$out\"\n"
                       "fi\n");
        std::filesystem::permissions(env.interpreter(), std::filesystem::perms::owner_all);
    }

    /// A SafeTensors set in the store, declaring a vision encoder or not.
    void add_snapshot(const std::string& model, bool vision) const {
        const std::filesystem::path dir =
            roots.safetensors / model / "safetensors" / "aaaaaaaaaaaa";
        write_file(dir / "config.json",
                   vision ? R"({"architectures": ["Qwen3_5ForConditionalGeneration"],
                              "vision_config": {"depth": 27}})"
                          : R"({"architectures": ["Qwen3ForCausalLM"]})");
        write_file(dir / "model.safetensors", "weights");
        if (vision) {
            // An instruction-tuned release; the text-only one stands for a base model.
            write_file(dir / "tokenizer_config.json", R"({"chat_template": "{{ messages }}"})");
        }
    }

    [[nodiscard]] std::vector<std::string> converter_calls() const {
        std::vector<std::string> out;
        std::istringstream lines{read_file(calls)};
        for (std::string line; std::getline(lines, line);) {
            out.push_back(line);
        }
        return out;
    }

    int run(const std::vector<std::string>& args, std::string* out) const {
        std::ostringstream captured;
        std::ostringstream errors;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(errors.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand command{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = command.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str() + errors.str();
        return code;
    }
};

}  // namespace

TEST_CASE("convert makes a vision model's projector beside it", "[commands][models][convert]") {
    const Home home;
    home.add_snapshot("org--vision", true);
    std::string out;
    REQUIRE(home.run({"models", "convert", "org--vision"}, &out) == 0);

    const auto stored = apogee::models::list_store_ggufs(home.roots, "org--vision");
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().file.filename() == "vision-F16.gguf");
    CHECK(stored.front().projector.filename() == "vision-F16-mmproj.gguf");
    const std::optional<apogee::models::Sidecar> record =
        apogee::models::load_sidecar(stored.front().projector);
    REQUIRE(record.has_value());
    CHECK(record->source == "convert");
    CHECK(record->transform_note == "--mmproj --outtype f16");
    CHECK(record->ref == "org--vision/safetensors/aaaaaaaaaaaa");

    // Two runs of the converter, the second for the projector.
    const std::vector<std::string> calls = home.converter_calls();
    REQUIRE(calls.size() == 2);
    CHECK(calls.at(0).find("--mmproj") == std::string::npos);
    CHECK(calls.at(1).find("--mmproj") != std::string::npos);

    // The one command that uses both.
    CHECK(out.find("--mmproj-path " + stored.front().projector.string()) != std::string::npos);
    CHECK(out.find("no chat template") == std::string::npos);
}

TEST_CASE("a projector the converter cannot make leaves the model, and a later run adds it",
          "[commands][models][convert]") {
    const Home home;
    home.add_snapshot("org--vision", true);
    write_file(home.refuse_projector, "");
    std::string out;
    REQUIRE(home.run({"models", "convert", "org--vision"}, &out) == 0);
    CHECK(out.find("warning: its projector could not be made") != std::string::npos);
    CHECK(out.find("cannot make a projector for this model's architecture") != std::string::npos);
    CHECK(out.find("it cannot read images without one") != std::string::npos);
    auto stored = apogee::models::list_store_ggufs(home.roots, "org--vision");
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().projector.empty());
    const std::filesystem::path model_file = stored.front().file;

    // The converter learns the projector: the next run makes only that, into
    // the directory the model already has -- the model is not converted again.
    std::filesystem::remove(home.refuse_projector);
    REQUIRE(home.run({"models", "convert", "org--vision"}, &out) == 0);
    CHECK(out.find("making its projector") != std::string::npos);
    const std::vector<std::string> calls = home.converter_calls();
    REQUIRE(calls.size() == 3);
    CHECK(calls.at(2).find("--mmproj") != std::string::npos);
    stored = apogee::models::list_store_ggufs(home.roots, "org--vision");
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().file == model_file);
    CHECK(stored.front().projector.filename() == "vision-F16-mmproj.gguf");
    CHECK(std::filesystem::exists(apogee::models::sidecar_path_for(stored.front().projector)));

    // And with nothing left to make, nothing runs.
    REQUIRE(home.run({"models", "convert", "org--vision"}, &out) == 0);
    CHECK(out.find("already converted") != std::string::npos);
    CHECK(home.converter_calls().size() == 3);
}

TEST_CASE("a text-only model is converted once, with no projector attempted",
          "[commands][models][convert]") {
    const Home home;
    home.add_snapshot("org--text", false);
    std::string out;
    REQUIRE(home.run({"models", "convert", "org--text"}, &out) == 0);
    CHECK(home.converter_calls().size() == 1);
    const auto stored = apogee::models::list_store_ggufs(home.roots, "org--text");
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().projector.empty());
    CHECK(out.find("projector") == std::string::npos);
    CHECK(out.find("--mmproj-path") == std::string::npos);
    // No chat template: a base model, said so -- converted all the same.
    CHECK(out.find("no chat template -- it is a base model") != std::string::npos);
}
#endif
