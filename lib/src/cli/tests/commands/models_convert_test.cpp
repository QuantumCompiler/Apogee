#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "commands/models_pull.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "models/gguf_inspect.h"
#include "models/quantize.h"
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
    /// Present: the converter fails the model itself (M3's resume test).
    std::filesystem::path refuse_model = home / "no-model";

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
                       "  if [ -f '" +
                       refuse_model.string() +
                       "' ]; then\n"
                       "    echo 'converter crashed' >&2\n"
                       "    exit 1\n"
                       "  fi\n"
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
    // The hash is announced, never a silent wait (2026-09-24), and nothing
    // left behind claims the staging name.
    CHECK(out.find("hashing it (") != std::string::npos);
    CHECK(out.find("\n\nand its projector") == std::string::npos);
    CHECK(apogee::models::find_abandoned_staging(home.roots).empty());
    for (const auto& entry :
         std::filesystem::directory_iterator(home.roots.models / "org--vision" / "gguf")) {
        CHECK_FALSE(entry.path().filename().string().starts_with(".incoming-"));
    }
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

// ---- one command to runnable: --register (M3) -------------------------------

namespace {

using apogee::commands::ChainTools;
using apogee::commands::RegisterChainRequest;

/// A quantizer that writes a small GGUF of its own for each level -- distinct
/// bytes, so each lands in its own place in the store -- or refuses the
/// level it is told to. No llama.cpp, so the chain runs in every build.
struct FakeQuantizer {
    std::vector<std::string> asked;
    std::string refuse;

    [[nodiscard]] apogee::commands::Quantizer get() {
        return [this](const std::filesystem::path& input, const std::filesystem::path& output,
                      std::string_view level, const apogee::models::QuantizeProgress& progress) {
            asked.emplace_back(level);
            apogee::models::QuantizeResult result;
            if (level == refuse) {
                result.error =
                    "llama.cpp could not quantize " + input.string() + " to " + std::string{level};
                return result;
            }
            if (progress) {
                progress(1, 1);
            }
            apogee::testing::GgufBuilder builder;
            builder.magic().u32(3).u64(1).u64(3);
            builder.string_kv("general.architecture", "qwen35");
            builder.u32_kv("general.file_type", 15);
            builder.string_kv("general.name", std::string{level});
            builder.tensor("token_embd.weight");
            write_file(output, builder.bytes());
            result.ok = true;
            result.input_bytes = static_cast<std::int64_t>(std::filesystem::file_size(input));
            result.output_bytes = static_cast<std::int64_t>(std::filesystem::file_size(output));
            return result;
        };
    }
};

/// Runs the chain on `home`, its narration captured; false when it threw.
bool chain(const Home& home, const std::string& snapshot, std::vector<std::string> levels,
           FakeQuantizer& quantizer, std::string* out) {
    const std::ostringstream captured;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
    bool ok = true;
    try {
        apogee::commands::run_register_chain(RegisterChainRequest{.roots = home.roots,
                                                                  .config_path = home.config_path,
                                                                  .pull_ref = {},
                                                                  .snapshot = snapshot,
                                                                  .snapshot_from = {},
                                                                  .levels = std::move(levels)},
                                             ChainTools{.quantize = quantizer.get()});
    } catch (...) {
        ok = false;
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    *out = captured.str();
    return ok;
}

/// The config `home`'s chain should leave, made by hand: the pristine one,
/// then `config add-backend` once per backend -- the comparison is bytes.
[[nodiscard]] std::string hand_typed(const Home& home, const std::string& pristine,
                                     const std::vector<std::vector<std::string>>& adds) {
    const std::string chained = read_file(home.config_path);
    write_file(home.config_path, pristine);
    std::string out;
    for (const std::vector<std::string>& add : adds) {
        std::vector<std::string> args{"config", "add-backend"};
        args.insert(args.end(), add.begin(), add.end());
        REQUIRE(home.run(args, &out) == 0);
    }
    std::string typed = read_file(home.config_path);
    write_file(home.config_path, chained);
    return typed;
}

[[nodiscard]] apogee::models::StoredGguf stored_at(const Home& home, const std::string& model,
                                                   const std::string& name) {
    for (const auto& stored : apogee::models::list_store_ggufs(home.roots, model)) {
        if (stored.file.filename() == name) {
            return stored;
        }
    }
    FAIL("no " << name << " stored for " << model);
    return {};
}

}  // namespace

TEST_CASE("convert --register makes the F16 and registers it, as add-backend would",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--vision", true);
    const std::string pristine = read_file(home.config_path);
    std::string out;
    REQUIRE(home.run({"models", "convert", "org--vision", "--register"}, &out) == 0);
    INFO(out);

    const auto f16 = stored_at(home, "org--vision", "vision-F16.gguf");
    REQUIRE_FALSE(f16.projector.empty());
    CHECK(out.find("[1/2] convert to F16") != std::string::npos);
    CHECK(out.find("[2/2] register vision-F16") != std::string::npos);
    CHECK(out.find("registered vision-F16") != std::string::npos);
    CHECK(out.find("chat with one:  apogee chat -m vision-F16") != std::string::npos);
    // The chain's own words replace each verb's what-to-do-next.
    CHECK(out.find("Use it by adding a backend") == std::string::npos);
    CHECK(out.find("Make it smaller") == std::string::npos);

    CHECK(read_file(home.config_path) ==
          hand_typed(home, pristine,
                     {{"vision-F16", "--type", "llamacpp", "--model-path", f16.file.string(),
                       "--mmproj-path", f16.projector.string()}}));
}

TEST_CASE("a two-level chain registers three backends, byte for byte the hand-typed config",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--vision", true);
    const std::string pristine = read_file(home.config_path);
    FakeQuantizer quantizer;
    std::string out;
    REQUIRE(chain(home, "org--vision", {"Q4_K_M", "Q5_K_M"}, quantizer, &out));
    INFO(out);
    CHECK(quantizer.asked == std::vector<std::string>{"Q4_K_M", "Q5_K_M"});
    CHECK(out.find("[2/4] quantize to Q4_K_M") != std::string::npos);
    CHECK(out.find("[4/4] register 3 backends") != std::string::npos);
    CHECK(out.find("chat with one:  apogee chat -m vision-Q5_K_M") != std::string::npos);

    const auto f16 = stored_at(home, "org--vision", "vision-F16.gguf");
    const auto q4 = stored_at(home, "org--vision", "vision-Q4_K_M.gguf");
    const auto q5 = stored_at(home, "org--vision", "vision-Q5_K_M.gguf");
    // The projector comes along to every quantization.
    REQUIRE_FALSE(q4.projector.empty());
    REQUIRE_FALSE(q5.projector.empty());

    CHECK(read_file(home.config_path) ==
          hand_typed(home, pristine,
                     {{"vision-F16", "--type", "llamacpp", "--model-path", f16.file.string(),
                       "--mmproj-path", f16.projector.string()},
                      {"vision-Q4_K_M", "--type", "llamacpp", "--model-path", q4.file.string(),
                       "--mmproj-path", q4.projector.string()},
                      {"vision-Q5_K_M", "--type", "llamacpp", "--model-path", q5.file.string(),
                       "--mmproj-path", q5.projector.string()}}));
}

TEST_CASE("a chain stopped at a quantize resumes with the command it printed, making nothing twice",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--vision", true);
    const std::string pristine = read_file(home.config_path);
    FakeQuantizer failing;
    failing.refuse = "Q5_K_M";
    std::string out;
    REQUIRE_FALSE(chain(home, "org--vision", {"Q4_K_M", "Q5_K_M"}, failing, &out));
    INFO(out);
    const std::string resume =
        "apogee models convert org--vision/safetensors/aaaaaaaaaaaa --register-with Q4_K_M,Q5_K_M";
    CHECK(out.find("stopped at [3/4] quantize to Q5_K_M") != std::string::npos);
    CHECK(out.find("resume with:\n  " + resume) != std::string::npos);
    // What the stages before it made is in the store; nothing is registered.
    (void)stored_at(home, "org--vision", "vision-F16.gguf");
    (void)stored_at(home, "org--vision", "vision-Q4_K_M.gguf");
    CHECK(read_file(home.config_path) == pristine);
    CHECK(apogee::models::find_abandoned_staging(home.roots).empty());

    // The printed command, run: the F16 and the Q4 are found, not made again.
    const std::size_t conversions = home.converter_calls().size();
    FakeQuantizer working;
    REQUIRE(
        chain(home, "org--vision/safetensors/aaaaaaaaaaaa", {"Q4_K_M", "Q5_K_M"}, working, &out));
    CHECK(home.converter_calls().size() == conversions);
    CHECK(working.asked == std::vector<std::string>{"Q5_K_M"});
    CHECK(out.find("already converted") != std::string::npos);
    CHECK(out.find("already quantized") != std::string::npos);
    CHECK(out.find("registered vision-Q5_K_M") != std::string::npos);
}

TEST_CASE("a chain stopped at the conversion is resumed by the very command it printed",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--text", false);
    write_file(home.refuse_model, "");
    std::string out;
    CHECK(home.run({"models", "convert", "org--text", "--register"}, &out) == 1);
    INFO(out);
    const std::string resume =
        "apogee models convert org--text/safetensors/aaaaaaaaaaaa --register";
    REQUIRE(out.find("stopped at [1/2] convert to F16") != std::string::npos);
    REQUIRE(out.find("resume with:\n  " + resume) != std::string::npos);

    std::filesystem::remove(home.refuse_model);
    std::vector<std::string> args;
    std::istringstream words{resume.substr(std::string{"apogee "}.size())};
    for (std::string word; words >> word;) {
        args.push_back(word);
    }
    REQUIRE(home.run(args, &out) == 0);
    CHECK(out.find("registered text-F16") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path);
    REQUIRE(config.backends.contains("text-F16"));
    CHECK(config.backends.at("text-F16").model_path ==
          stored_at(home, "org--text", "text-F16.gguf").file.string());
}

TEST_CASE("a name another backend holds refuses the chain before anything runs",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--text", false);
    std::string out;
    REQUIRE(home.run({"config", "add-backend", "text-F16", "--type", "llamacpp", "--model-path",
                      "/elsewhere/model.gguf"},
                     &out) == 0);
    const std::string before = read_file(home.config_path);
    CHECK(home.run({"models", "convert", "org--text", "--register"}, &out) == 1);
    CHECK(out.find("backend 'text-F16' already exists and points at /elsewhere/model.gguf") !=
          std::string::npos);
    CHECK(home.converter_calls().empty());
    CHECK(read_file(home.config_path) == before);
    // A name differing only in case is the same backend.
    REQUIRE(home.run({"config", "delete-backend", "text-F16"}, &out) == 0);
    REQUIRE(home.run({"config", "add-backend", "TEXT-f16", "--type", "mock"}, &out) == 0);
    CHECK(home.run({"models", "convert", "org--text", "--register"}, &out) == 1);
    CHECK(out.find("would collide with 'TEXT-f16'") != std::string::npos);
    CHECK(home.converter_calls().empty());
}

TEST_CASE("a chain with no config to register into refuses before anything runs",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--text", false);
    std::filesystem::remove(home.config_path);
    std::string out;
    CHECK(home.run({"models", "convert", "org--text", "--register"}, &out) == 1);
    CHECK(out.find("there is no config to register into") != std::string::npos);
    CHECK(out.find("run 'apogee config init' first") != std::string::npos);
    CHECK(home.converter_calls().empty());
}

TEST_CASE("a chain run again registers nothing twice", "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--text", false);
    std::string out;
    REQUIRE(home.run({"models", "convert", "org--text", "--register"}, &out) == 0);
    const std::string once = read_file(home.config_path);
    REQUIRE(home.run({"models", "convert", "org--text", "--register"}, &out) == 0);
    CHECK(out.find("already converted") != std::string::npos);
    CHECK(out.find("already registered: text-F16") != std::string::npos);
    CHECK(read_file(home.config_path) == once);
    CHECK(home.converter_calls().size() == 1);
}

TEST_CASE("a base model is registered, its note said once and beside its name",
          "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--text", false);  // no chat template
    FakeQuantizer quantizer;
    std::string out;
    REQUIRE(chain(home, "org--text", {"Q4_K_M"}, quantizer, &out));
    INFO(out);
    const std::string note = "note: this model has no chat template -- it is a base model";
    CHECK(out.find(note) != std::string::npos);
    CHECK(out.find(note) == out.rfind(note));
    CHECK(out.find("text-F16  (a base model") != std::string::npos);
    CHECK(out.find("text-Q4_K_M  (a base model") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path);
    CHECK(config.backends.contains("text-F16"));
    CHECK(config.backends.contains("text-Q4_K_M"));
}

TEST_CASE("the chain's flags are checked before anything is fetched or made",
          "[commands][models][register]") {
    const Home home;
    std::string out;
    // A GGUF pull is runnable as it lands; the chain is for full weights.
    CHECK(home.run({"models", "pull", "Qwen/Qwen3-8B", "--register"}, &out) == 1);
    CHECK(out.find("--register needs --safetensors") != std::string::npos);
    CHECK(home.run({"models", "pull", "Qwen/Qwen3-8B", "--safetensors", "--register-with",
                    "Q4_K_M,bogus"},
                   &out) == 1);
    CHECK(out.find("unknown quantization level 'bogus'") != std::string::npos);
    CHECK(home.run({"models", "pull", "not-a-repo", "--safetensors", "--register"}, &out) == 1);
    CHECK(out.find("is not one") != std::string::npos);
    CHECK(home.run({"models", "convert", "org--text", "-t", "q8_0", "--register"}, &out) == 1);
    CHECK(out.find("drop --type") != std::string::npos);
    // Spellings fold into the table's, each level once; F16 is always made.
    CHECK(apogee::commands::register_levels({"q4_k_m", "Q4_K_M", "f16", "Q8_0"}) ==
          std::vector<std::string>{"Q4_K_M", "Q8_0"});
    // With nothing quantizable in this build, a level is refused up front.
    if (!apogee::models::quantize_supported()) {
        home.add_snapshot("org--text", false);
        CHECK(home.run({"models", "convert", "org--text", "--register-with", "Q4_K_M"}, &out) == 1);
        CHECK(out.find("this build cannot quantize") != std::string::npos);
        CHECK(home.converter_calls().empty());
    }
}

TEST_CASE("the whole chain through the command line, llama.cpp quantizing for real",
          "[commands][models][convert][register][llama]") {
    if (!apogee::models::quantize_supported()) {
        SKIP("this build has no llama.cpp to quantize with");
    }
    // The converter's output is a model llama.cpp can quantize: 2 KiB, built
    // here -- the real quantizer, and nothing downloaded.
    const Home home;
    home.add_snapshot("org--text", false);
    write_file(home.home / "fixtures" / "model.gguf", apogee::testing::quantizable_gguf());
    const std::string pristine = read_file(home.config_path);
    std::string out;
    REQUIRE(home.run({"models", "convert", "org--text", "--register-with", "q4_k_m"}, &out) == 0);
    INFO(out);
    CHECK(out.find("[2/3] quantize to Q4_K_M") != std::string::npos);
    CHECK(out.find("llama_model_loader") == std::string::npos);
    const auto f16 = stored_at(home, "org--text", "text-F16.gguf");
    const auto q4 = stored_at(home, "org--text", "text-Q4_K_M.gguf");
    CHECK(apogee::models::inspect_gguf(q4.file).is_quantized());
    CHECK(read_file(home.config_path) ==
          hand_typed(home, pristine,
                     {{"text-F16", "--type", "llamacpp", "--model-path", f16.file.string()},
                      {"text-Q4_K_M", "--type", "llamacpp", "--model-path", q4.file.string()}}));
}
#endif
