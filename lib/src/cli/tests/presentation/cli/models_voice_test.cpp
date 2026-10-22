#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/helpers.h"
#include "cli/models.h"
#include "cli/models_pull.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
#include "models/source_hf.h"
#include "modelstore/mlx_info.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "modelstore/voice_info.h"
#include "platform/child_process.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "support/cli_home.h"
#include "support/voice_model.h"
#include "transport/http_client.h"

/// Voice models in the store (50a) end to end, in process: the pull against
/// a local fixture source -- the listing, the recursive tree with its sizes
/// and LFS digests, each file -- served from a directory built in the test
/// in Kokoro-82M's layout, through the real HTTP client and the pull's own
/// code. Then the listing, `info`'s voices by language, delete and the
/// convert refusal. No network, no weights.
namespace {

using apogee::backends::HttpClient;
using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;
using apogee::models::StoreRoots;
using apogee::testing::CliHome;

constexpr std::string_view kRepo = "hexgrad/Kokoro-82M";
constexpr std::string_view kModel = "hexgrad--Kokoro-82M";

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The repository served from a directory, as `huggingface.co` answers.
class FixtureSource final : public apogee::backends::HttpTransport {
public:
    explicit FixtureSource(std::filesystem::path dir, std::string truncate = {})
        : dir_{std::move(dir)}, truncate_{std::move(truncate)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        cancellation.throw_if_cancelled();
        HttpResponse response;
        response.status = 200;
        const auto reply = [&](const std::string& body) {
            if (sink) {
                (void)sink(body);
            } else {
                response.body = body;
            }
        };
        const std::string api = "https://huggingface.co/api/models/" + std::string{kRepo};
        const std::string prefix =
            "https://huggingface.co/" + std::string{kRepo} + "/resolve/main/";
        if (request.url == api) {
            nlohmann::json siblings = nlohmann::json::array();
            for (const std::string& file : files()) {
                siblings.push_back({{"rfilename", file}});
            }
            reply(nlohmann::json{{"id", kRepo}, {"siblings", siblings}}.dump());
        } else if (request.url == api + "/tree/main?recursive=true") {
            nlohmann::json out = nlohmann::json::array();
            for (const std::string& name : files()) {
                const std::string bytes = served(name);
                nlohmann::json item{{"type", "file"}, {"path", name}, {"size", bytes.size()}};
                if (name.ends_with(".pth") || name.ends_with(".pt")) {
                    item["lfs"] = {{"oid", apogee::models::sha256_hex(bytes)},
                                   {"size", bytes.size()}};
                }
                out.push_back(item);
            }
            reply(out.dump());
        } else if (request.url.starts_with(prefix)) {
            ++downloads;
            reply(served(request.url.substr(prefix.size())));
        } else {
            response.status = 404;
        }
        return response;
    }

    std::size_t downloads = 0;

private:
    [[nodiscard]] std::vector<std::string> files() const {
        std::vector<std::string> out;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_)) {
            if (entry.is_regular_file()) {
                out.push_back(entry.path().lexically_relative(dir_).generic_string());
            }
        }
        std::ranges::sort(out);
        return out;
    }

    [[nodiscard]] std::string served(const std::string& name) const {
        std::string bytes = read_file(dir_ / name);
        if (name == truncate_) {
            bytes.resize(bytes.size() - 26);  // the archive's end record, gone
        }
        return bytes;
    }

    std::filesystem::path dir_;
    std::string truncate_;
};

struct Pull {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    std::filesystem::path repo = home.home() / "fixture" / "repo";
    StoreRoots roots = StoreRoots::at(home.models());
    FixtureSource* source = nullptr;
    std::unique_ptr<HttpClient> client;

    explicit Pull(std::string truncate = {}) {
        apogee::testing::write_voice_model(repo);
        std::ofstream{repo / "README.md"} << "a model card";
        std::filesystem::create_directories(repo / "samples");
        std::ofstream{repo / "samples" / "HEARME.wav"} << "RIFF";
        auto transport = std::make_unique<FixtureSource>(repo, std::move(truncate));
        source = transport.get();
        client = std::make_unique<HttpClient>(std::move(transport));
    }

    int run(std::string* out) const {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        int code = 0;
        try {
            (void)apogee::commands::pull_voice(std::string{kRepo}, roots, *client, {});
        } catch (const CLI::RuntimeError& e) {
            code = e.get_exit_code();
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str();
        return code;
    }
};

}  // namespace

TEST_CASE("a pull of a voice model lands under voice/ by its weights' digest, verified",
          "[commands][models][voice][pull]") {
    const Pull pull;
    const apogee::models::HfListing listing = apogee::models::list_gguf_files(
        *pull.client, *apogee::models::parse_hf_ref(std::string{kRepo}), "", {});
    REQUIRE(listing.voice());

    std::string out;
    REQUIRE(pull.run(&out) == 0);
    INFO(out);
    // The samples and the card never move: the config, the checkpoint, 3 voices.
    CHECK(pull.source->downloads == 5);
    const std::vector<apogee::models::StoredVoice> stored =
        apogee::models::list_store_voices(pull.roots);
    REQUIRE(stored.size() == 1);
    const std::filesystem::path dir = stored.front().dir;
    CHECK(dir.parent_path() == pull.roots.models / std::string{kModel} / "voice");
    CHECK(apogee::models::read_voice_info(dir).complete);
    CHECK(read_file(dir / "voices" / "af_heart.pt") ==
          read_file(pull.repo / "voices" / "af_heart.pt"));
    CHECK(out.starts_with("a voice model (3 voices): downloading 5 file(s), "));
    CHECK(out.find("config.json, the checkpoint and 3 voice(s) read whole\n") != std::string::npos);
    CHECK(
        out.find("voice model: kokoro-v1_0.pth, 3 voices (American English, British English), ") !=
        std::string::npos);

    // Listed and shown.
    const apogee::harness::Config config = apogee::harness::load_config(pull.home.config_path());
    const std::vector<apogee::commands::ModelRow> rows =
        apogee::commands::build_model_rows(config, pull.home.models());
    const std::string handle = std::string{kModel} + "/voice/" + stored.front().id;
    const auto row = std::ranges::find(rows, handle, &apogee::commands::ModelRow::model);
    REQUIRE(row != rows.end());
    CHECK(row->state == "voice");
    CHECK(row->architecture == "kokoro-v1_0.pth");
    CHECK(apogee::commands::model_group(*row).id == "voice");
    CHECK(apogee::commands::model_row_json(*row)["bytes"] ==
          apogee::models::read_voice_info(dir).bytes);
    REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find("format:       voice -- a text-to-speech model, taken whole\n") !=
          std::string::npos);
    CHECK(out.find("voices:       American English: af_heart, am_adam\n") != std::string::npos);
    CHECK(out.find("              British English: bf_emma\n") != std::string::npos);

    // Pulled again: found by the published digests before a byte moves.
    pull.source->downloads = 0;
    REQUIRE(pull.run(&out) == 0);
    CHECK(out.find("already here") != std::string::npos);
    CHECK(pull.source->downloads == 0);

    // Not a snapshot to convert; gone whole.
    CHECK(pull.home.run({"models", "convert", handle}, &out) != 0);
    CHECK(out.find("is a voice model; this reads a language model's full-weight SafeTensors "
                   "weights") != std::string::npos);
    REQUIRE(pull.home.run({"models", "delete", handle, "--yes"}, &out) == 0);
    CHECK_FALSE(std::filesystem::exists(dir));
}

TEST_CASE("a voice model cut short lands nothing, naming the piece",
          "[commands][models][voice][pull]") {
    const Pull pull{"voices/am_adam.pt"};
    std::string out;
    CHECK(pull.run(&out) == 1);
    INFO(out);
    CHECK(out.find("is not a loadable voice model -- cannot load: voices/am_adam.pt is truncated "
                   "or not a PyTorch save (no archive end record); nothing was kept") !=
          std::string::npos);
    CHECK(apogee::models::list_store_voices(pull.roots).empty());
    CHECK(apogee::models::find_abandoned_staging(pull.roots).empty());
}

// ---- a voice model to MLX, through mlx-audio, and spoken there (53m) --------------------

namespace {

/// The stub mlx-audio -- its converter and Kokoro, the stub torch and misaki
/// beside them -- laid out with the stub mlx-lm as the environment Apogee
/// owns under `home`, as `train setup --with mlx-audio` would leave it: its
/// interpreter the host's python3 with them on its path. Without
/// `with_audio`, the environment alone, mlx-audio not installed.
void fake_audio_runtime(const std::filesystem::path& home, bool with_audio = true) {
    const std::filesystem::path stubs =
        std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts";
    const std::filesystem::path site =
        home / "training" / "venv" / "lib" / "python3.99" / "site-packages";
    std::filesystem::create_directories(site);
    std::filesystem::copy(
        stubs / "stub_mlx", site,
        std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
    if (with_audio) {
        std::filesystem::copy(stubs / "stub_mlx_audio", site,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::skip_existing);
    }
    const std::filesystem::path python = home / "training" / "venv" / "bin" / "python";
    apogee::testing::write_fixture_file(python,
                                        "#!/bin/sh\nPYTHONPATH='" + site.string() +
                                            "' PYTHONDONTWRITEBYTECODE=1 exec python3 \"$@\"\n");
    std::filesystem::permissions(python, std::filesystem::perms::owner_all);
}

/// Where the converter and the speech driver run: Apple silicon, and a
/// python3 for them.
[[nodiscard]] bool audio_driver_runs() {
    return apogee::platform::host_target() == "macos-arm64" &&
           apogee::platform::supports_child_processes() &&
           !apogee::platform::find_on_path("python3").empty();
}

/// A whole Kokoro stored as `models pull` stores it, its record the pull's.
[[nodiscard]] std::string store_kokoro(const CliHome& home) {
    const std::filesystem::path dir =
        home.models() / std::string{kModel} / "voice" / "aaaaaaaaaaaa";
    apogee::testing::write_voice_model(dir);
    apogee::models::Snapshot record;
    record.ref = std::string{kRepo};
    record.revision = "main";
    record.source = "huggingface";
    REQUIRE(apogee::models::write_snapshot(dir, record));
    return std::string{kModel} + "/voice/aaaaaaaaaaaa";
}

[[nodiscard]] nlohmann::json info_data(const CliHome& home, const std::string& handle) {
    std::string out;
    std::string err;
    REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &out, &err) == 0);
    return nlohmann::json::parse(out).at("data");
}

}  // namespace

TEST_CASE("a voice model is offered MLX only where mlx-audio is installed; else it reads as before",
          "[commands][models][voice][mlx_audio]") {
    const CliHome home{"backends:\n  m:\n    type: mock\n"};
    const std::string handle = store_kokoro(home);
    std::string out;
    std::string err;

    // No mlx-audio: nothing said of a conversion, the data's fields null, as
    // before 53m.
    REQUIRE(home.run({"models", "info", handle, "-q"}, &out) == 0);
    const std::string before = out;
    CHECK(before.find("format:       voice -- a text-to-speech model, taken whole\n") !=
          std::string::npos);
    CHECK(before.find("--mlx") == std::string::npos);
    CHECK(before.find("made from it") == std::string::npos);
    nlohmann::json data = info_data(home, handle);
    CHECK(data["converts_to"].is_null());
    CHECK(data["mlx_conversion"].is_null());
    // The environment alone, mlx-audio not in it: the same.
    fake_audio_runtime(home.home(), /*with_audio=*/false);
    REQUIRE(home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out == before);
    CHECK(info_data(home, handle)["converts_to"].is_null());

    // What the voice route refuses before anything, wherever it runs.
    CHECK(home.run({"models", "convert", handle, "--mlx", "--register"}, &out) != 0);
    CHECK(out.find("'" + handle +
                   "' is a voice model, which mlx-audio converts to an MLX voice model -- no "
                   "backend runs a voice model, so --register and --register-with do not apply") !=
          std::string::npos);
    CHECK(home.run({"models", "convert", handle, "--mlx", "--text-only"}, &out) != 0);
    CHECK(out.find("--text-only asks for mlx-lm's build of a language model, and a voice model "
                   "has none") != std::string::npos);
    CHECK(home.run({"models", "convert", handle, "--mlx", "--model-type", "kokoro"}, &out) != 0);
    CHECK(out.find("--model-type does not apply: mlx-audio converts it as Kokoro") !=
          std::string::npos);
    // The GGUF door's refusal is as it was.
    CHECK(home.run({"models", "convert", handle}, &out) != 0);
    CHECK(out.find("is a voice model; this reads a language model's full-weight SafeTensors "
                   "weights") != std::string::npos);
    // And --mlx names what is missing: Apple silicon, else mlx-audio.
    CHECK(home.run({"models", "convert", handle, "--mlx"}, &out) != 0);
    if (apogee::platform::host_target() == "macos-arm64") {
        CHECK(out.find("'" + handle +
                       "' is a voice model, which mlx-audio converts to an MLX voice model, and "
                       "mlx-audio is not installed in Apogee's Python environment") !=
              std::string::npos);
        CHECK(out.find("-- apogee train setup --with mlx-audio") != std::string::npos);
    } else {
        CHECK(out.find("--mlx builds '" + handle + "' through mlx-audio, an MLX voice model: ") !=
              std::string::npos);
    }
    CHECK_FALSE(std::filesystem::exists(home.models() / std::string{kModel} / "mlx"));

    // mlx-audio installed: offered MLX, its converter named.
    fake_audio_runtime(home.home());
    data = info_data(home, handle);
    CHECK(data["converts_to"] == nlohmann::json::array({"mlx"}));
    CHECK(data["mlx_conversion"] == nlohmann::json{{"towers", nlohmann::json::array()},
                                                   {"converter", "mlx-audio"},
                                                   {"text_only", false}});
    REQUIRE(home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find("format:       voice -- a text-to-speech model, taken whole; --mlx converts "
                   "it to an MLX voice model\n") != std::string::npos);
    CHECK(out.find("made from it: nothing yet -- 'apogee models convert " + handle +
                   " --mlx' makes an MLX voice model of it\n") != std::string::npos);
    // Completion offers what the door takes: the set with --mlx, every
    // precision, and no registration or type.
    REQUIRE(home.run({"__complete", "models", "convert", "--mlx", ""}, &out, &err) == 0);
    CHECK(out.find(handle + "\n") != std::string::npos);
    REQUIRE(home.run({"__complete", "models", "convert", "--type", ""}, &out, &err) == 0);
    CHECK(out.find(handle) == std::string::npos);
    REQUIRE(home.run({"__complete", "models", "convert", handle, "--mlx", "--type", ""}, &out,
                     &err) == 0);
    CHECK(out == "3bit\n4bit\n6bit\n8bit\nbf16\nf16\nmxfp4\n");
    (void)home.run({"__complete", "models", "convert", handle, "--mlx", "--register-with", ""},
                   &out, &err);
    CHECK(out.find("4bit") == std::string::npos);
    (void)home.run({"__complete", "models", "convert", handle, "--mlx", "--model-type", ""}, &out,
                   &err);
    CHECK(out.find("kokoro") == std::string::npos);

    // A voice model cut short: decided, and offered nothing.
    std::filesystem::remove_all(home.models() / std::string{kModel} / "voice" / "aaaaaaaaaaaa" /
                                "voices");
    CHECK(info_data(home, handle)["converts_to"] == nlohmann::json::array());
    CHECK(info_data(home, handle)["mlx_conversion"].is_null());
}

TEST_CASE("a stored Kokoro converts through mlx-audio to an MLX voice model, and speaks on MLX",
          "[commands][models][voice][mlx_audio][driver]") {
    if (!audio_driver_runs()) {
        SKIP("the converter and the speech run on Apple silicon, and their drivers need python3");
    }
    // 53m's acceptance in process, over the REAL seeded drivers and the stub
    // mlx-audio: the conversion recorded `voice`, said so by `models info`
    // with its voices, then `voice say` on it speaking on MLX -- its timings
    // the WAV's own, its record saying `engine: mlx` -- and neither driver's
    // library prints on either stream.
    const CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::string handle = store_kokoro(home);
    fake_audio_runtime(home.home());
    const std::string config_before = home.config_text();

    std::string out;
    std::string err;
    REQUIRE(home.run({"models", "convert", handle, "--mlx"}, &out, &err) == 0);
    INFO(out);
    INFO(err);
    const std::filesystem::path source =
        home.models() / std::string{kModel} / "voice" / "aaaaaaaaaaaa";
    CHECK(out.find("converting " + source.string() +
                   " to MLX (bf16) -- mlx-audio's converter, Kokoro's checkpoint and its 3 voices "
                   "built into an MLX voice model, in Apogee's Python environment\n") !=
          std::string::npos);
    CHECK(out.find("verified: config.json, the weights and its voices (voices/*.safetensors) -- 1 "
                   "shard header(s) and 3 voice(s) read whole\n") != std::string::npos);
    CHECK(out.find("kind: voice (recorded) -- a voice model: 'apogee voice say' speaks with it, "
                   "never a chat backend\n") != std::string::npos);
    for (const std::string& stream : {out, err}) {
        CHECK(stream.find("[INFO]") == std::string::npos);
        CHECK(stream.find("noise on stderr") == std::string::npos);
    }
    const std::vector<apogee::models::StoredMlx> made =
        apogee::models::list_store_mlx(apogee::models::StoreRoots::at(home.models()), kModel);
    REQUIRE(made.size() == 1);
    const std::string mlx = apogee::models::weights_handle(
        made.front().model, apogee::models::kMlxFormat, made.front().id);
    const std::optional<apogee::models::Snapshot> record =
        apogee::models::load_snapshot(made.front().dir);
    REQUIRE(record.has_value());
    CHECK(record->kind == "voice");
    CHECK(record->ref == handle);
    CHECK(record->transform == "mlx_audio.convert bf16");
    CHECK(out.find("apogee voice say \"<text>\" -o speech.wav --model " + mlx + "\n") !=
          std::string::npos);
    CHECK(home.config_text() == config_before);

    // Said for what it is, its voices the presets it carries.
    REQUIRE(home.run({"models", "info", mlx, "-q"}, &out) == 0);
    CHECK(out.find("kind:         voice (recorded) -- a voice model") != std::string::npos);
    CHECK(out.find("voices:       American English: af_heart, am_adam\n") != std::string::npos);
    CHECK(out.find("              British English: bf_emma\n") != std::string::npos);
    CHECK(out.find("files:        whole -- config.json, the weights and its voices") !=
          std::string::npos);
    CHECK(out.find("window:") == std::string::npos);
    CHECK(info_data(home, mlx)["kind"] == "voice");
    CHECK(info_data(home, mlx)["window"].is_null());
    REQUIRE(home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find("made from it: " + mlx + " (recorded)\n") != std::string::npos);
    // The same model at the same precision is recognised.
    REQUIRE(home.run({"models", "convert", handle, "--mlx"}, &out) == 0);
    CHECK(out.find("already converted:") != std::string::npos);
    // No backend runs it.
    (void)home.run({"config", "scan", "--register"}, &out);
    CHECK(out.find("not registered -- a voice model") != std::string::npos);
    CHECK(home.config_text() == config_before);

    // Spoken on MLX: the engine its directory says, the timings the WAV's.
    const std::filesystem::path wav = home.home() / "hello.wav";
    REQUIRE(home.run({"voice", "say", "Hello there. How are you?", "-o", wav.string(), "--model",
                      mlx, "--output-format", "json"},
                     &out, &err) == 0);
    INFO(err);
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["engine"] == "mlx");
    CHECK(document["status"] == "done");
    CHECK(document["voice"] == "af_heart");
    CHECK(document["model"]["name"] == mlx);
    CHECK(document["model"]["checkpoint"] == "model.safetensors");
    CHECK(document["model"]["ref"] == handle);
    CHECK(document["device"]["device"] == "gpu");
    CHECK(document["versions"]["mlx_audio"] == "0.0-audio-stub");
    CHECK(document["versions"]["torch"] == "");
    CHECK(document["versions"]["kokoro"] == "");
    std::string error;
    const std::optional<apogee::platform::WavAudio> audio = apogee::platform::read_wav(wav, error);
    REQUIRE(audio.has_value());
    CHECK(document["seconds"] == audio->seconds());
    CHECK(document["segments"][0]["seconds"] == audio->seconds());
    CHECK(audio->seconds() == 2400.0 / 24000.0);
    CHECK(document["segments"][0]["sentences"].size() == 2);
    for (const std::string& stream : {out, err}) {
        CHECK(stream.find("chatter") == std::string::npos);
        CHECK(stream.find("noise on stderr") == std::string::npos);
    }
    // The store holds two voice models now: the bare run is refused, naming both.
    CHECK(home.run({"voice", "say", "Hi.", "-o", (home.home() / "bare.wav").string()}, &out,
                   &err) != 0);
    CHECK(err.find("the store holds 2 voice models -- name one with --model:\n  " + mlx + "\n  " +
                   handle) != std::string::npos);
    // An engine the model does not speak on is refused before anything runs.
    CHECK(home.run({"voice", "say", "Hi.", "-o", (home.home() / "torch.wav").string(), "--model",
                    mlx, "--engine", "torch"},
                   &out, &err) != 0);
    CHECK(err.find("--engine torch: '" + mlx +
                   "' is an MLX voice model, which speaks on MLX (mlx-audio), not PyTorch") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(home.home() / "torch.wav"));
    // Completion offers it and its voices, and the engine's words.
    REQUIRE(home.run({"__complete", "voice", "say", "hi", "--model", ""}, &out, &err) == 0);
    CHECK(out.find(mlx + "\n") != std::string::npos);
    CHECK(out.find(handle + "\n") != std::string::npos);
    REQUIRE(home.run({"__complete", "voice", "say", "hi", "--engine", ""}, &out, &err) == 0);
    CHECK(out == "auto\ntorch\nmlx\n");
}
