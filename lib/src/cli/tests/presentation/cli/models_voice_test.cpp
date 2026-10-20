#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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
#include "contracts/sha256.h"
#include "models/source_hf.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "modelstore/voice_info.h"
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
