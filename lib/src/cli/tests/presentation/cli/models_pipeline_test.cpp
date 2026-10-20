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
#include "modelstore/pipeline_info.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "operations/renames.h"
#include "support/cli_home.h"
#include "support/pipeline_model.h"
#include "transport/http_client.h"

/// Diffusion pipelines in the store (49a) end to end, in process: the pull
/// against a local fixture "source" -- the model card, the recursive tree
/// with its sizes and LFS digests, `model_index.json`, each component's
/// files, served from a directory built in the test in Wan 2.2 TI2V-5B's
/// layout through the real HTTP client and the pull's own code; only the
/// transport is the test's. No network, no real weights. Then the listing,
/// `info`'s component table, delete, repair and rename over what it stored.
namespace {

using apogee::backends::HttpClient;
using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;
using apogee::models::StoreRoots;
using apogee::testing::CliHome;
using apogee::testing::PipelineModelSpec;
using apogee::testing::write_pipeline_model;

constexpr std::string_view kRepo = "Wan-AI/Wan2.2-TI2V-5B-Diffusers";
constexpr std::string_view kModel = "Wan-AI--Wan2.2-TI2V-5B-Diffusers";

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A Hugging Face repository served from a directory, nested as a pipeline
/// is: the listing (`model_index.json` among its siblings), the recursive
/// tree, and each file -- as `huggingface.co` answers them.
class FixtureSource final : public apogee::backends::HttpTransport {
public:
    struct Options {
        bool digests = true;
        /// This file's download drops mid-body.
        std::string drop{};
        /// This file is served with other bytes than its digest names.
        std::string corrupt{};
        /// This file is served cut short -- its size and digest agreeing.
        std::string truncate{};
        /// This file's download meets a Ctrl-C: the token is cancelled.
        std::string cancel{};
        apogee::harness::CancellationToken token{};
    };

    FixtureSource(std::filesystem::path dir, Options options)
        : dir_{std::move(dir)}, options_{std::move(options)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        cancellation.throw_if_cancelled();
        urls.push_back(request.url);
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
        if (request.url == api) {
            reply(listing());
            return response;
        }
        if (request.url == api + "/tree/main?recursive=true") {
            reply(tree());
            return response;
        }
        const std::string prefix =
            "https://huggingface.co/" + std::string{kRepo} + "/resolve/main/";
        if (request.url.starts_with(prefix)) {
            const std::string name = request.url.substr(prefix.size());
            if (!std::filesystem::is_regular_file(dir_ / name)) {
                response.status = 404;
                response.body = "Entry not found";
                return response;
            }
            const std::string bytes = served(name);
            if (name == options_.cancel) {
                (void)sink(bytes.substr(0, bytes.size() / 2));
                options_.token.cancel();
                cancellation.throw_if_cancelled();
            }
            if (name == options_.drop) {
                (void)sink(bytes.substr(0, bytes.size() / 2));
                throw apogee::backends::HttpError("connection dropped mid-stream");
            }
            reply(bytes);
            return response;
        }
        response.status = 404;
        response.body = "not found";
        return response;
    }

    /// Files fetched beyond the index: what a pull spent on the pipeline.
    [[nodiscard]] std::vector<std::string> fetched() const {
        std::vector<std::string> out;
        const std::string prefix =
            "https://huggingface.co/" + std::string{kRepo} + "/resolve/main/";
        for (const std::string& url : urls) {
            if (url.starts_with(prefix) && !url.ends_with("/model_index.json")) {
                out.push_back(url.substr(prefix.size()));
            }
        }
        return out;
    }

    std::vector<std::string> urls;

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
        if (name == options_.truncate) {
            bytes.resize(bytes.size() - 16);
        }
        return bytes;
    }

    [[nodiscard]] std::string listing() const {
        nlohmann::json siblings = nlohmann::json::array();
        for (const std::string& file : files()) {
            siblings.push_back({{"rfilename", file}});
        }
        return nlohmann::json{{"id", kRepo},
                              {"library_name", "diffusers"},
                              {"tags", {"diffusers", "safetensors", "text-to-video"}},
                              {"siblings", siblings}}
            .dump();
    }

    [[nodiscard]] std::string tree() const {
        nlohmann::json out = nlohmann::json::array();
        for (const std::string& name : files()) {
            const std::string bytes = served(name);
            nlohmann::json item{{"type", "file"}, {"path", name}, {"size", bytes.size()}};
            if (options_.digests && name.ends_with(".safetensors")) {
                item["lfs"] = {{"oid", apogee::models::sha256_hex(bytes)}, {"size", bytes.size()}};
                if (name == options_.corrupt) {
                    item["lfs"]["oid"] = apogee::models::sha256_hex(bytes + "x");
                }
            }
            out.push_back(item);
        }
        return out.dump();
    }

    std::filesystem::path dir_;
    Options options_;
};

/// A store, a pipeline repository to pull from, and the client that reaches it.
struct Pull {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    std::filesystem::path repo = home.home() / "fixture" / "repo";
    StoreRoots roots = StoreRoots::at(home.models());
    FixtureSource* source = nullptr;
    std::unique_ptr<HttpClient> client;

    explicit Pull(FixtureSource::Options options = {}, const PipelineModelSpec& spec = {}) {
        write_pipeline_model(repo, spec);
        auto transport = std::make_unique<FixtureSource>(repo, std::move(options));
        source = transport.get();
        client = std::make_unique<HttpClient>(std::move(transport));
    }

    /// `pull_pipeline`, its output captured; the exit code it failed with, or 0.
    int run(std::string* out, const apogee::harness::CancellationToken& cancellation = {}) const {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        int code = 0;
        try {
            (void)apogee::commands::pull_pipeline(std::string{kRepo}, roots, *client, cancellation);
        } catch (const CLI::RuntimeError& e) {
            code = e.get_exit_code();
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str();
        return code;
    }

    /// Nothing under the model at all: no id, no staging, no owner marker.
    void check_nothing_landed() const {
        CHECK(apogee::models::list_store_pipelines(roots).empty());
        check_no_staging();
    }

    /// No staging and no owner marker: nothing the doctor would call a leftover.
    void check_no_staging() const {
        CHECK(apogee::models::find_abandoned_staging(roots).empty());
        const std::filesystem::path formats = roots.models / std::string{kModel} / "pipeline";
        if (std::filesystem::exists(formats)) {
            for (const auto& entry : std::filesystem::directory_iterator(formats)) {
                INFO(entry.path().string());
                CHECK_FALSE(entry.path().filename().string().starts_with(".incoming-"));
            }
        }
    }
};

[[nodiscard]] std::string shards_id(const std::filesystem::path& repo,
                                    const PipelineModelSpec& spec) {
    std::vector<apogee::models::SnapshotFile> shards;
    for (const std::string& name : apogee::testing::pipeline_shard_names(spec)) {
        shards.push_back(
            {.path = name, .size = 0, .sha256 = apogee::models::file_sha256(repo / name)});
    }
    return apogee::models::snapshot_weight_id(shards);
}

}  // namespace

TEST_CASE("a pull of a pipeline repository lands under pipeline/ by its shards' hash, verified",
          "[commands][models][pipeline][pull]") {
    const Pull pull;
    // What `models pull` decides on: model_index.json at the root.
    const apogee::models::HfListing listing = apogee::models::list_gguf_files(
        *pull.client, *apogee::models::parse_hf_ref(std::string{kRepo}), "", {});
    REQUIRE(listing.pipeline());

    std::string out;
    REQUIRE(pull.run(&out) == 0);
    INFO(out);
    const std::string id = shards_id(pull.repo, {});
    const std::filesystem::path dir = pull.roots.models / std::string{kModel} / "pipeline" / id;
    REQUIRE(std::filesystem::is_directory(dir));
    const apogee::models::PipelineInfo info = apogee::models::read_pipeline_info(dir);
    CHECK(info.complete);
    for (const std::string name :
         {"model_index.json", "transformer/diffusion_pytorch_model-00002-of-00002.safetensors",
          "tokenizer/tokenizer.json"}) {
        CHECK(read_file(dir / name) == read_file(pull.repo / name));
    }
    CHECK(out.starts_with("a diffusion pipeline (5 components): downloading 12 file(s), "));
    CHECK(out.find("-- 12 size(s) checked, 4 against a published sha256 (8 had none published); "
                   "model_index.json and 5 component(s) read whole, 4 shard header(s) among "
                   "them\n") != std::string::npos);
    CHECK(out.find("pipeline: WanPipeline, 5 components (scheduler, text_encoder, tokenizer, "
                   "transformer, vae), 4 shard(s), ") != std::string::npos);
    pull.check_no_staging();
    CHECK(apogee::models::list_store_pipelines(pull.roots).size() == 1);

    // Its record: the repository, and every file as it landed.
    const std::optional<apogee::models::Snapshot> record = apogee::models::load_snapshot(dir);
    REQUIRE(record.has_value());
    CHECK(record->ref == kRepo);
    CHECK(record->source == "huggingface");
    REQUIRE(record->files.size() == 12);
    for (const apogee::models::SnapshotFile& file : record->files) {
        CHECK(file.sha256 == apogee::models::file_sha256(dir / file.path));
    }

    // Listed with its kind, its class, its components and its size.
    const apogee::harness::Config config = apogee::harness::load_config(pull.home.config_path());
    const std::vector<apogee::commands::ModelRow> rows =
        apogee::commands::build_model_rows(config, pull.home.models());
    const std::string handle = std::string{kModel} + "/pipeline/" + id;
    const auto row = std::ranges::find(rows, handle, &apogee::commands::ModelRow::model);
    REQUIRE(row != rows.end());
    CHECK(row->state == "pipeline");
    CHECK(row->format == "pipeline");
    CHECK(row->architecture == "WanPipeline");
    CHECK(row->provenance == "huggingface");
    CHECK(row->verified == "12 file(s) on record");
    CHECK(row->components == 5);
    CHECK(row->bytes == info.bytes);
    CHECK(apogee::commands::model_group(*row).id == "pipeline");
    const nlohmann::json object = apogee::commands::model_row_json(*row);
    CHECK(object["group"] == "pipeline");
    CHECK(object["components"] == 5);
    CHECK(object["bytes"] == info.bytes);
    const std::string table = apogee::commands::render_model_table(rows);
    CHECK(table.find("Pipelines") != std::string::npos);
    CHECK(table.find("WanPipeline, 5 components") != std::string::npos);

    // `info` on it: the component table, each with its kind and its weight.
    REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find("format:       pipeline -- a diffusion pipeline, taken whole\n") !=
          std::string::npos);
    CHECK(out.find("lineage:      pulled from " + std::string{kRepo} + " (Hugging Face)\n") !=
          std::string::npos);
    CHECK(out.find("pipeline:     WanPipeline (diffusers 0.35.0)\n") != std::string::npos);
    CHECK(out.find("files:        whole -- model_index.json and 5 component(s), 4 shard(s), each "
                   "holding every byte its header lists\n") != std::string::npos);
    CHECK(out.find("components:   scheduler  scheduler  UniPCMultistepScheduler  1 file(s), ") !=
          std::string::npos);
    CHECK(out.find("              transformer  model  WanTransformer3DModel  4 file(s), 2 "
                   "shard(s), ") != std::string::npos);
    CHECK(out.find("not shipped:  transformer_2 (declared optional by the index)\n") !=
          std::string::npos);

    // Pulled again: found by the published digests before a byte of weights moves.
    pull.source->urls.clear();
    REQUIRE(pull.run(&out) == 0);
    CHECK(out.find("already here -- these exact weights are at\n  " + dir.string()) !=
          std::string::npos);
    CHECK(pull.source->fetched().empty());
}

TEST_CASE("a pipeline pull spends nothing on copies, checkpoints or directories it does not run",
          "[commands][models][pipeline][pull]") {
    const Pull pull{{}, {.fp16_copies = true}};
    std::filesystem::create_directories(pull.repo / "assets");
    std::ofstream{pull.repo / "assets" / "sample.json"} << "{}";
    std::ofstream{pull.repo / "wan_single_file.safetensors"} << "a checkpoint beside it";
    std::string out;
    REQUIRE(pull.run(&out) == 0);
    INFO(out);
    for (const std::string& file : pull.source->fetched()) {
        INFO(file);
        CHECK(apogee::models::weights_variant(std::filesystem::path{file}.filename().string())
                  .empty());
        CHECK_FALSE(file.starts_with("assets/"));
        CHECK(file != "wan_single_file.safetensors");
    }
    CHECK(pull.source->fetched().size() == 11);  // the index is read first, then the rest
}

TEST_CASE("a pipeline pull that fails at any rung lands nothing and leaves no staging",
          "[commands][models][pipeline][pull]") {
    std::string out;
    SECTION("the connection drops mid-shard") {
        const Pull pull{{.drop = "transformer/diffusion_pytorch_model-00002-of-00002.safetensors"}};
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("connection dropped mid-stream") != std::string::npos);
        pull.check_nothing_landed();
    }
    SECTION("a shard is not what its published digest says") {
        const Pull pull{{.corrupt = "vae/diffusion_pytorch_model.safetensors"}};
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("digest mismatch") != std::string::npos);
        pull.check_nothing_landed();
    }
    SECTION("the files are no pipeline diffusers can load, whatever their digests say") {
        const Pull pull{{.truncate = "vae/diffusion_pytorch_model.safetensors"}};
        CHECK(pull.run(&out) == 1);
        INFO(out);
        CHECK(out.find("is not a loadable pipeline -- cannot load: "
                       "vae/diffusion_pytorch_model.safetensors is truncated") !=
              std::string::npos);
        CHECK(out.find("nothing was kept") != std::string::npos);
        pull.check_nothing_landed();
    }
    SECTION("Ctrl-C mid-download") {
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        const Pull pull{{.cancel = "text_encoder/model.safetensors", .token = token}};
        CHECK(pull.run(&out, token) == apogee::commands::kCancelled);
        CHECK(out.find("cancelled -- nothing was written") != std::string::npos);
        pull.check_nothing_landed();
        CHECK_FALSE(std::filesystem::exists(pull.roots.models / std::string{kModel}));
    }
    SECTION("a component the index names, with nothing to pull: refused before a byte moves") {
        const Pull pull{{}, {.missing = "vae"}};
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("names the vae component in model_index.json but holds no vae/ files -- "
                       "nothing diffusers can load; nothing was downloaded") != std::string::npos);
        CHECK(pull.source->fetched().empty());
        CHECK_FALSE(std::filesystem::exists(pull.roots.models / std::string{kModel}));
    }
}

TEST_CASE("a pipeline goes whole: deleted, repaired and renamed as one directory",
          "[commands][models][pipeline][delete]") {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    const StoreRoots roots = StoreRoots::at(home.models());
    const std::filesystem::path dir = home.models() / "org--video" / "pipeline" / "dddddddddddd";
    write_pipeline_model(dir);
    apogee::models::Snapshot record;
    record.ref = "org/video";
    record.source = "huggingface";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            record.files.push_back({.path = entry.path().lexically_relative(dir).generic_string(),
                                    .size = static_cast<std::int64_t>(entry.file_size()),
                                    .sha256 = apogee::models::file_sha256(entry.path())});
        }
    }
    REQUIRE(apogee::models::write_snapshot(dir, record));

    // By its handle, its format or its model: the one directory.
    CHECK(apogee::commands::plan_delete(roots, "org--video/pipeline/dddddddddddd").removes ==
          std::vector<std::filesystem::path>{dir});
    CHECK(apogee::commands::plan_delete(roots, "org--video/pipeline").removes ==
          std::vector<std::filesystem::path>{dir});
    CHECK(apogee::commands::plan_delete(roots, "org--video").removes ==
          std::vector<std::filesystem::path>{dir});

    // Repaired against its own record: nothing damaged, read whole.
    std::string out;
    REQUIRE(home.run({"models", "repair", "org--video/pipeline/dddddddddddd"}, &out) == 0);
    CHECK(out.find("pipeline: org--video/pipeline/dddddddddddd\nNothing to repair.\n") !=
          std::string::npos);
    CHECK(out.find("files:    whole -- WanPipeline, 5 components") != std::string::npos);

    // Not a snapshot to convert, nor a GGUF to quantize: refused by name.
    CHECK(home.run({"models", "convert", "org--video/pipeline/dddddddddddd"}, &out) != 0);
    CHECK(out.find("is a diffusion pipeline; this reads a language model's full-weight "
                   "SafeTensors weights") != std::string::npos);

    // Renamed: the model's directory moves whole, the pipeline inside it.
    const apogee::commands::RenameOutcome renamed = apogee::commands::rename_stored_model(
        home.config_path(), roots, "org--video", "org--clips");
    INFO(renamed.refusal);
    REQUIRE(renamed.refusal.empty());
    const std::filesystem::path moved = home.models() / "org--clips" / "pipeline" / "dddddddddddd";
    CHECK(apogee::models::read_pipeline_info(moved).complete);
    CHECK_FALSE(std::filesystem::exists(home.models() / "org--video"));

    REQUIRE(home.run({"models", "delete", "org--clips/pipeline/dddddddddddd", "--yes"}, &out) == 0);
    CHECK_FALSE(std::filesystem::exists(moved));
}
