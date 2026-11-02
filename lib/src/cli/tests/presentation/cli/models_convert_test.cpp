#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cli/helpers.h"
#include "cli/job_stream.h"
#include "cli/models_pull.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "models/quantize.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/sidecar.h"
#include "modelstore/store.h"
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
                      std::string_view level, const apogee::models::QuantizeProgress& progress,
                      const apogee::harness::CancellationToken& /*cancellation*/) {
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
           FakeQuantizer& quantizer, std::string* out, std::string base_name = {}) {
    const std::ostringstream captured;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
    bool ok = true;
    try {
        apogee::commands::run_register_chain(
            RegisterChainRequest{.roots = home.roots,
                                 .config_path = home.config_path,
                                 .pull_ref = {},
                                 .snapshot = snapshot,
                                 .snapshot_from = {},
                                 .levels = std::move(levels),
                                 .base_name = std::move(base_name)},
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
    // With no default set, the entry becomes it, said as add-backend says it (M33).
    CHECK(out.find("models.default set to 'vision-F16' (none was set)") != std::string::npos);
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
    CHECK(out.find("chat with one:  apogee chat -m vision-Q5KM") != std::string::npos);

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
                      {"vision-Q4KM", "--type", "llamacpp", "--model-path", q4.file.string(),
                       "--mmproj-path", q4.projector.string()},
                      {"vision-Q5KM", "--type", "llamacpp", "--model-path", q5.file.string(),
                       "--mmproj-path", q5.projector.string()}}));
}

TEST_CASE("what a chain makes says where it came from, and its snapshot folds out of the list",
          "[commands][models][convert][register][lineage]") {
    // M4 over M3: the chain's verbs write the records lineage reads -- nothing
    // extra -- and the command line shows them.
    const Home home;
    home.add_snapshot("org--vision", true);
    FakeQuantizer quantizer;
    std::string out;
    REQUIRE(chain(home, "org--vision", {"Q4_K_M"}, quantizer, &out));
    const auto f16 = stored_at(home, "org--vision", "vision-F16.gguf");
    const std::string f16_handle = "org--vision/gguf/" + f16.id;

    REQUIRE(home.run({"models", "info", "vision-Q4KM", "--quiet"}, &out) == 0);
    INFO(out);
    CHECK(out.find("lineage:      quantized to Q4_K_M from " + f16_handle +
                   " (recorded)\n"
                   "              converted from org--vision/safetensors/aaaaaaaaaaaa "
                   "(recorded)\n") != std::string::npos);
    REQUIRE(home.run({"models", "info", f16_handle, "--quiet"}, &out) == 0);
    CHECK(out.find("backends:     vision-F16\n") != std::string::npos);
    REQUIRE(home.run({"models", "info", "org--vision/safetensors/aaaaaaaaaaaa", "--quiet"}, &out) ==
            0);
    CHECK(out.find("made from it: " + f16_handle + " (recorded)\n") != std::string::npos);

    REQUIRE(home.run({"models", "list", "--quiet", "--no-color"}, &out) == 0);
    CHECK(out.find("org--vision/safetensors/aaaaaaaaaaaa") == std::string::npos);
    CHECK(out.find("1 snapshot consumed by a conversion is folded -- --all lists it") !=
          std::string::npos);
    REQUIRE(home.run({"models", "list", "--quiet", "--no-color", "--all"}, &out) == 0);
    CHECK(out.find("org--vision/safetensors/aaaaaaaaaaaa") != std::string::npos);

    // A whole model is not one thing to show: info names what it holds.
    CHECK(home.run({"models", "info", "org--vision", "--quiet"}, &out) == 1);
    CHECK(out.find("'org--vision' is a model -- name one set of its weights:") !=
          std::string::npos);
    CHECK(out.find("  " + f16_handle) != std::string::npos);
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
    CHECK(out.find("registered vision-Q5KM") != std::string::npos);
}

TEST_CASE("a base name begins every backend's name, and the resume command carries it",
          "[commands][models][convert][register]") {
    // The user's own names (2026-10-03): Gemma4-E4B-F16, Gemma4-E4B-Q4KM.
    const Home home;
    home.add_snapshot("org--vision", true);
    FakeQuantizer failing;
    failing.refuse = "Q5_K_M";
    std::string out;
    REQUIRE_FALSE(chain(home, "org--vision", {"Q4_K_M", "Q5_K_M"}, failing, &out, "Vision-7B"));
    INFO(out);
    const std::string resume =
        "apogee models convert org--vision/safetensors/aaaaaaaaaaaa --register-with Q4_K_M,Q5_K_M "
        "--base-name Vision-7B";
    CHECK(out.find("resume with:\n  " + resume + "\n") != std::string::npos);

    // Resumed as printed: every name begins with the base.
    FakeQuantizer working;
    REQUIRE(chain(home, "org--vision/safetensors/aaaaaaaaaaaa", {"Q4_K_M", "Q5_K_M"}, working, &out,
                  "Vision-7B"));
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path);
    CHECK(config.backends.contains("Vision-7B-F16"));
    CHECK(config.backends.contains("Vision-7B-Q4KM"));
    CHECK(config.backends.contains("Vision-7B-Q5KM"));
    CHECK_FALSE(config.backends.contains("vision-F16"));
    CHECK(out.find("chat with one:  apogee chat -m Vision-7B-Q5KM") != std::string::npos);
    // The file keeps the table's spelling.
    CHECK(stored_at(home, "org--vision", "vision-Q4_K_M.gguf").file.filename() ==
          "vision-Q4_K_M.gguf");

    // Through the command line, where no quantizer is needed.
    home.add_snapshot("org--text", false);
    REQUIRE(home.run({"models", "convert", "org--text", "--register", "--base-name", "Text-1B"},
                     &out) == 0);
    CHECK(out.find("registered Text-1B-F16") != std::string::npos);
    CHECK(apogee::harness::load_config(home.config_path).backends.contains("Text-1B-F16"));
}

TEST_CASE(
    "a base name the config cannot hold, or with nothing to name, is refused before anything "
    "runs",
    "[commands][models][convert][register]") {
    const Home home;
    home.add_snapshot("org--text", false);
    const std::string pristine = read_file(home.config_path);
    std::string out;

    struct Row {
        std::vector<std::string> args;
        std::string said;
    };

    for (const Row& row : {
             Row{.args = {"models", "convert", "org--text", "--register", "--base-name",
                          "My Model"},
                 .said = "is not one both config formats can edit"},
             Row{.args = {"models", "convert", "org--text", "--register", "--base-name", "a:b"},
                 .said = "a name is letters, digits, '_', '-', '.' and '/'"},
             Row{.args = {"models", "convert", "org--text", "--register", "--base-name", ""},
                 .said = "--base-name cannot be empty"},
             Row{.args = {"models", "convert", "org--text", "--base-name", "Text"},
                 .said = "--base-name names the backends --register makes"},
             // Refused before anything is fetched: no network in this test.
             Row{.args = {"models", "pull", "org/text", "--safetensors", "--base-name", "Text"},
                 .said = "--base-name names the backends --register makes"},
         }) {
        INFO(row.args.back());
        CHECK(home.run(row.args, &out) == 1);
        CHECK(out.find(row.said) != std::string::npos);
    }
    CHECK(home.converter_calls().empty());
    CHECK(read_file(home.config_path) == pristine);
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
    CHECK(out.find("text-Q4KM  (a base model") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path);
    CHECK(config.backends.contains("text-F16"));
    CHECK(config.backends.contains("text-Q4KM"));
}

TEST_CASE("an embedding model's chain ends on the embedding door, never on a chat",
          "[commands][models][convert][register][embedding]") {
    // The audit's finding (55d): the GGUF route's last line told the person to
    // chat with an entry that embeds, under a base model's note that does not
    // apply, and never named the embedding role.
    const Home home;
    const std::filesystem::path dir =
        home.roots.safetensors / "org--embed" / "safetensors" / "aaaaaaaaaaaa";
    write_file(dir / "config.json", R"({"architectures": ["Qwen3Model"], "model_type": "qwen3"})");
    write_file(dir / "model.safetensors", "weights");
    write_file(dir / "modules.json", R"([
        {"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"},
        {"idx": 1, "path": "1_Pooling", "type": "sentence_transformers.models.Pooling"},
        {"idx": 2, "path": "2_Normalize", "type": "sentence_transformers.models.Normalize"}])");
    FakeQuantizer quantizer;
    std::string out;
    REQUIRE(chain(home, "org--embed", {"Q4_K_M"}, quantizer, &out));
    INFO(out);
    const std::string door =
        "point the embedding role at it:  apogee config set-default-embedding embed-Q4KM";
    CHECK(out.ends_with("\n" + door + "\n"));
    CHECK(out.find("chat with one") == std::string::npos);
    CHECK(out.find("base model") == std::string::npos);
    CHECK(out.find("note: note:") == std::string::npos);
    CHECK(out.find("note: an embedding model -- the backend embeds and answers no chat; point "
                   "the embedding role at it: apogee config set-default-embedding embed-F16\n") !=
          std::string::npos);
    CHECK(out.find("  embed-F16\n  embed-Q4KM\n") != std::string::npos);  // no base model's aside
    // The door, as printed, on that state.
    REQUIRE(home.run({"config", "set-default-embedding", "embed-Q4KM"}, &out) == 0);
    CHECK(apogee::harness::load_config(home.config_path).models.default_embedding == "embed-Q4KM");

    // Converted alone, its hint is the embedding role's two commands.
    REQUIRE(home.run({"models", "convert", "org--embed"}, &out) == 0);
    CHECK(out.find("\nUse it as the embedding role by adding a backend:\n  apogee config "
                   "add-backend <name> --type llamacpp --model-path ") != std::string::npos);
    CHECK(out.find("\n  apogee config set-default-embedding <name>\n") != std::string::npos);
    CHECK(out.find("base model") == std::string::npos);
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

TEST_CASE("the whole chain, llama.cpp quantizing for real in a child of the built binary",
          "[commands][models][convert][register][llama]") {
    if (!apogee::models::quantize_supported()) {
        SKIP("this build has no llama.cpp to quantize with");
    }
    // The quantizer runs as a child of the binary's own (57i): here the
    // built `apogee`, since this test binary has no `__quantize` of its own.
    const std::filesystem::path binary{APOGEE_EXECUTABLE};
    std::error_code missing;
    if (!std::filesystem::is_regular_file(binary, missing)) {
        SKIP("this build made no apogee executable to quantize in");
    }
    // The converter's output is a model llama.cpp can quantize: 2 KiB, built
    // here -- the real quantizer, and nothing downloaded.
    const Home home;
    home.add_snapshot("org--text", false);
    write_file(home.home / "fixtures" / "model.gguf", apogee::testing::quantizable_gguf());
    const std::string pristine = read_file(home.config_path);
    std::string out;
    {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        bool ok = true;
        try {
            apogee::commands::run_register_chain(
                RegisterChainRequest{.roots = home.roots,
                                     .config_path = home.config_path,
                                     .pull_ref = {},
                                     .snapshot = "org--text",
                                     .snapshot_from = {},
                                     .levels = apogee::commands::register_levels({"q4_k_m"}),
                                     .base_name = {}},
                ChainTools{.quantize = apogee::commands::child_quantizer(binary)});
        } catch (...) {
            ok = false;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        out = captured.str();
        INFO(out);
        REQUIRE(ok);
    }
    INFO(out);
    CHECK(out.find("[2/3] quantize to Q4_K_M") != std::string::npos);
    CHECK(out.find("llama_model_loader") == std::string::npos);
    const auto f16 = stored_at(home, "org--text", "text-F16.gguf");
    const auto q4 = stored_at(home, "org--text", "text-Q4_K_M.gguf");
    CHECK(apogee::models::inspect_gguf(q4.file).is_quantized());
    CHECK(read_file(home.config_path) ==
          hand_typed(home, pristine,
                     {{"text-F16", "--type", "llamacpp", "--model-path", f16.file.string()},
                      {"text-Q4KM", "--type", "llamacpp", "--model-path", q4.file.string()}}));
}

// ---- a stop in any stage, the quantizer's above all (57i) --------------------

namespace {

using apogee::commands::JobStream;

/// A quantizer met by a Ctrl-C halfway through its tensors -- the SIGINT a
/// person's terminal or the GUI's Stop sends, raised in this process -- that
/// then honors its token as the child-run quantizer does: stopped, its
/// partial output left for the caller to remove.
[[nodiscard]] apogee::commands::Quantizer interrupted_quantizer() {
    return [](const std::filesystem::path& /*input*/, const std::filesystem::path& output,
              std::string_view /*level*/, const apogee::models::QuantizeProgress& progress,
              const apogee::harness::CancellationToken& cancellation) {
        write_file(output, "half a quantized model");
        if (progress) {
            progress(1, 2);
        }
        (void)std::raise(SIGINT);
        apogee::models::QuantizeResult result;
        if (cancellation.stop_requested()) {
            result.stopped = true;
            result.error = "stopped";
            return result;
        }
        result.ok = true;  // reached only if the SIGINT went unheard
        return result;
    };
}

/// A stdout that raises SIGINT the first time `trigger` is written to it:
/// a stop arriving inside a stage that runs no closure of the test's.
class RaisingBuffer final : public std::stringbuf {
public:
    explicit RaisingBuffer(std::string trigger) : trigger_{std::move(trigger)} {}

protected:
    std::streamsize xsputn(const char* text, std::streamsize count) override {
        const std::streamsize wrote = std::stringbuf::xsputn(text, count);
        check();
        return wrote;
    }

    int_type overflow(int_type c) override {
        const int_type wrote = std::stringbuf::overflow(c);
        check();
        return wrote;
    }

private:
    void check() {
        if (!raised_ && str().find(trigger_) != std::string::npos) {
            raised_ = true;
            (void)std::raise(SIGINT);
        }
    }

    std::string trigger_;
    bool raised_ = false;
};

/// What a chain ended with: its exit code, stdout and stderr.
struct ChainEnd {
    int code = 0;
    std::string out{};
    std::string err{};

    /// stdout's events, one a line, when the chain streamed.
    [[nodiscard]] std::vector<nlohmann::json> events() const {
        std::vector<nlohmann::json> found;
        std::istringstream in{out};
        for (std::string line; std::getline(in, line);) {
            const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
            INFO(line);
            REQUIRE(event.is_object());
            found.push_back(event);
        }
        return found;
    }

    /// The stream's notices, in order.
    [[nodiscard]] std::vector<std::string> notices() const {
        std::vector<std::string> said;
        for (const nlohmann::json& event : events()) {
            if (event.at("type") == "notice") {
                said.push_back(event.at("text").get<std::string>());
            }
        }
        return said;
    }
};

/// `convert <snapshot> --register-with <levels>` run with `quantizer`, its
/// stdout written to `out_buffer` -- under `models pull`'s stream when
/// `stream`, as `run_pull` frames a chain (52b).
[[nodiscard]] ChainEnd run_chain_on(const Home& home, const std::vector<std::string>& levels,
                                    const apogee::commands::Quantizer& quantizer, bool stream,
                                    std::stringbuf& out_buffer) {
    const std::ostringstream err;
    std::streambuf* const old_out = std::cout.rdbuf(&out_buffer);
    std::streambuf* const old_err = std::cerr.rdbuf(err.rdbuf());
    ChainEnd ended;
    const auto chain = [&] {
        apogee::commands::run_register_chain(RegisterChainRequest{.roots = home.roots,
                                                                  .config_path = home.config_path,
                                                                  .pull_ref = {},
                                                                  .snapshot = "org--text",
                                                                  .snapshot_from = {},
                                                                  .levels = levels,
                                                                  .base_name = {}},
                                             ChainTools{.quantize = quantizer});
    };
    try {
        if (stream) {
            JobStream job{"models pull"};
            try {
                chain();
            } catch (const CLI::RuntimeError&) {
                job.failed("the pull failed; stderr says why");
                throw;
            }
        } else {
            chain();
        }
    } catch (const CLI::RuntimeError& e) {
        ended.code = e.get_exit_code();
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    ended.out = out_buffer.str();
    ended.err = err.str();
    return ended;
}

/// Every `.incoming-*` the store holds: none, once a stop has cleaned up.
[[nodiscard]] std::vector<std::string> staging_left(const Home& home) {
    std::vector<std::string> out;
    std::error_code code;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(home.roots.models, code)) {
        if (entry.path().filename().string().starts_with(".incoming-")) {
            out.push_back(entry.path().string());
        }
    }
    return out;
}

}  // namespace

TEST_CASE("a chain Ctrl-C'd mid-quantize exits 130, leaves no staging, and its stream ends on it",
          "[commands][models][convert][register][interrupt][chain]") {
    // 57i, finding 1: the quantize stage had no scope, so the SIGINT took the
    // default action -- no `error`, no `stopped at`, the partial GGUF left
    // under `.incoming-*` for `check` to call abandoned.
    const Home home;
    home.add_snapshot("org--text", false);
    const std::string pristine = read_file(home.config_path);
    std::stringbuf buffer;
    const ChainEnd ended =
        run_chain_on(home, {"Q4_K_M"}, interrupted_quantizer(), /*stream=*/true, buffer);
    INFO(ended.out);
    INFO(ended.err);
    CHECK(ended.code == apogee::commands::kCancelled);
    CHECK(ended.err.find("apogee models: cancelled -- nothing was written") != std::string::npos);
    const std::vector<nlohmann::json> events = ended.events();
    REQUIRE(events.size() >= 2);
    CHECK(events.front().at("type") == "job");
    // The stream's last line is its `error`; where the chain stopped and the
    // command that resumes it are said before it.
    CHECK(events.back() ==
          nlohmann::json{{"type", "error"}, {"message", "cancelled -- nothing was written"}});
    const std::vector<std::string> notices = ended.notices();
    REQUIRE(notices.size() >= 3);
    CHECK(notices[notices.size() - 3] ==
          "stopped at [2/3] quantize to Q4_K_M -- what the stages before it made is kept.");
    CHECK(notices[notices.size() - 2] == "resume with:");
    CHECK(notices.back() ==
          "  apogee models convert org--text/safetensors/aaaaaaaaaaaa --register-with Q4_K_M");
    // The F16 the stage before it made is kept; nothing is staged, nothing
    // registered.
    (void)stored_at(home, "org--text", "text-F16.gguf");
    CHECK(staging_left(home).empty());
    CHECK(apogee::models::find_abandoned_staging(home.roots).empty());
    CHECK(read_file(home.config_path) == pristine);

    // As text: the same exit and words on a terminal.
    const Home text_home;
    text_home.add_snapshot("org--text", false);
    std::stringbuf text_buffer;
    const ChainEnd text =
        run_chain_on(text_home, {"Q4_K_M"}, interrupted_quantizer(), /*stream=*/false, text_buffer);
    INFO(text.out);
    CHECK(text.code == apogee::commands::kCancelled);
    CHECK(text.err == "apogee models: cancelled -- nothing was written\n");
    CHECK(text.out.find("stopped at [2/3] quantize to Q4_K_M -- what the stages before it made "
                        "is kept.\nresume with:\n  apogee models convert "
                        "org--text/safetensors/aaaaaaaaaaaa --register-with Q4_K_M\n") !=
          std::string::npos);
    CHECK(staging_left(text_home).empty());
}

TEST_CASE("a Ctrl-C inside the register stage stops it there, what it registered said",
          "[commands][models][convert][register][interrupt][chain]") {
    // The stop arrives as the first backend is registered: the second is
    // never added, the chain says where it stopped, and the stream ends on
    // the cancel. The resume registers the rest and nothing twice.
    const Home home;
    home.add_snapshot("org--text", false);
    FakeQuantizer quantizer;
    RaisingBuffer buffer{"registered text-F16"};
    const ChainEnd ended = run_chain_on(home, {"Q4_K_M"}, quantizer.get(), /*stream=*/true, buffer);
    INFO(ended.out);
    INFO(ended.err);
    CHECK(ended.code == apogee::commands::kCancelled);
    const std::string said = "cancelled -- registered text-F16, nothing more";
    CHECK(ended.err == "apogee models: " + said + "\n");
    CHECK(ended.events().back() == nlohmann::json{{"type", "error"}, {"message", said}});
    const std::vector<std::string> notices = ended.notices();
    REQUIRE(notices.size() >= 3);
    CHECK(notices[notices.size() - 3] ==
          "stopped at [3/3] register 2 backends -- what the stages before it made is kept.");
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path);
    CHECK(config.backends.contains("text-F16"));
    CHECK_FALSE(config.backends.contains("text-Q4KM"));
    CHECK(staging_left(home).empty());

    std::string out;
    REQUIRE(chain(home, "org--text/safetensors/aaaaaaaaaaaa", {"Q4_K_M"}, quantizer, &out));
    CHECK(out.find("already registered: text-F16") != std::string::npos);
    CHECK(out.find("registered text-Q4KM") != std::string::npos);
}

TEST_CASE("a Ctrl-C between two stages is the next stage's stop, never lost",
          "[commands][models][convert][register][interrupt][chain]") {
    // The gap the chain's one scope covers: the SIGINT lands as the register
    // stage is announced, before it runs. Nothing of it begins, and the stop
    // is said where it landed.
    const Home home;
    home.add_snapshot("org--text", false);
    const std::string pristine = read_file(home.config_path);
    FakeQuantizer quantizer;
    RaisingBuffer buffer{"[3/3] register 2 backends"};
    const ChainEnd ended =
        run_chain_on(home, {"Q4_K_M"}, quantizer.get(), /*stream=*/false, buffer);
    INFO(ended.out);
    INFO(ended.err);
    CHECK(ended.code == apogee::commands::kCancelled);
    CHECK(ended.err == "apogee models: cancelled -- nothing was written\n");
    CHECK(ended.out.find("stopped at [3/3] register 2 backends") != std::string::npos);
    CHECK(read_file(home.config_path) == pristine);
    (void)stored_at(home, "org--text", "text-Q4_K_M.gguf");

    // A SIGINT as the quantizer returns: the stage's hash sees it, and the
    // finished file is not committed.
    const Home late_home;
    late_home.add_snapshot("org--text", false);
    FakeQuantizer finishing;
    const apogee::commands::Quantizer inner = finishing.get();
    const apogee::commands::Quantizer late =
        [&inner](const std::filesystem::path& input, const std::filesystem::path& output,
                 std::string_view level, const apogee::models::QuantizeProgress& progress,
                 const apogee::harness::CancellationToken& cancellation) {
            apogee::models::QuantizeResult result =
                inner(input, output, level, progress, cancellation);
            (void)std::raise(SIGINT);
            return result;
        };
    std::stringbuf late_buffer;
    const ChainEnd stopped =
        run_chain_on(late_home, {"Q4_K_M"}, late, /*stream=*/false, late_buffer);
    INFO(stopped.out);
    CHECK(stopped.code == apogee::commands::kCancelled);
    CHECK(stopped.err == "apogee models: cancelled -- nothing was written\n");
    CHECK(stopped.out.find("stopped at [2/3] quantize to Q4_K_M") != std::string::npos);
    CHECK(staging_left(late_home).empty());
    CHECK(apogee::models::list_store_ggufs(late_home.roots, "org--text").size() == 1);
}

TEST_CASE("models quantize Ctrl-C'd mid-run exits 130 and leaves nothing staged",
          "[commands][models][quantize][interrupt]") {
    // The standalone verb reaches the same quantizer (finding 1's second
    // door): its run under one scope, the stop said as a stopped hash's is.
    const Home home;
    home.add_snapshot("org--text", false);
    FakeQuantizer quantizer;
    std::string out;
    REQUIRE(chain(home, "org--text", {}, quantizer, &out));  // the F16 alone
    const auto f16 = stored_at(home, "org--text", "text-F16.gguf");
    const std::ostringstream captured_out;
    const std::ostringstream captured_err;
    std::streambuf* const old_out = std::cout.rdbuf(captured_out.rdbuf());
    std::streambuf* const old_err = std::cerr.rdbuf(captured_err.rdbuf());
    int code = 0;
    try {
        apogee::commands::run_quantize({.roots = home.roots,
                                        .model = "org--text/gguf/" + f16.id,
                                        .from = {},
                                        .type = "Q4_K_M"},
                                       interrupted_quantizer());
    } catch (const CLI::RuntimeError& e) {
        code = e.get_exit_code();
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    INFO(captured_out.str());
    CHECK(code == apogee::commands::kCancelled);
    CHECK(captured_err.str() == "apogee models: cancelled -- nothing was written\n");
    CHECK(staging_left(home).empty());
    CHECK(apogee::models::list_store_ggufs(home.roots, "org--text").size() == 1);

    // Run again unstopped, the same verb makes it.
    std::streambuf* const again_out = std::cout.rdbuf(captured_out.rdbuf());
    std::streambuf* const again_err = std::cerr.rdbuf(captured_err.rdbuf());
    CHECK_NOTHROW(apogee::commands::run_quantize(
        {.roots = home.roots, .model = "org--text/gguf/" + f16.id, .from = {}, .type = "Q4_K_M"},
        quantizer.get()));
    std::cout.rdbuf(again_out);
    std::cerr.rdbuf(again_err);
    (void)stored_at(home, "org--text", "text-Q4_K_M.gguf");
}

#endif
