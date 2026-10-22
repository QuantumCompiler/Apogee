#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/helpers.h"
#include "cli/models.h"
#include "cli/models_pull.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
#include "models/source_hf.h"
#include "modelstore/mlx_info.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "support/towered_configs.h"
#include "training/mlx_convert.h"
#include "transport/http_client.h"

/// MLX model operations (27b) end to end, in process.
///
/// **The pull against a local fixture "source"**: the Hugging Face API --
/// the model card, the tree with its sizes and LFS digests, each file's
/// bytes -- served from a directory built in the test, through the real
/// HTTP client and the pull's own code; only the transport is the test's.
/// No network: the cases that matter are the ones a live repository will not
/// give on demand (a connection dropped mid-shard, a shard not what its
/// digest says, files that are no model, a Ctrl-C mid-download).
///
/// **Convert** with `mlx_lm.convert` played in process, everywhere; and the
/// command line over the REAL shipped driver under the stub `mlx_lm`, on
/// Apple silicon with a python3 -- listed, checked, registered by name and
/// deleted whole. The real Ctrl-C (a SIGINT to the process) is
/// `cli.mlx_models_lifecycle`'s.
namespace {

using apogee::backends::HttpClient;
using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;
using apogee::models::StoreRoots;
using apogee::testing::CliHome;
using apogee::testing::MlxModelSpec;
using apogee::testing::write_mlx_model;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A Hugging Face repository served from a directory: the model card's
/// listing, the tree, and each file -- as `huggingface.co` answers them.
class FixtureSource final : public apogee::backends::HttpTransport {
public:
    struct Options {
        /// LFS digests for the shards, as Hugging Face publishes them.
        bool digests = true;
        std::string library = "mlx";
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

    FixtureSource(std::string repo, std::filesystem::path dir, Options options)
        : repo_{std::move(repo)}, dir_{std::move(dir)}, options_{std::move(options)} {}

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
        const std::string api = "https://huggingface.co/api/models/" + repo_;
        if (request.url == api) {
            reply(listing());
            return response;
        }
        if (request.url == api + "/tree/main?recursive=true") {
            reply(tree());
            return response;
        }
        const std::string prefix = "https://huggingface.co/" + repo_ + "/resolve/main/";
        if (request.url.starts_with(prefix)) {
            const std::string name = request.url.substr(prefix.size());
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

    [[nodiscard]] std::size_t downloads() const {
        std::size_t n = 0;
        for (const std::string& url : urls) {
            n += url.find("/resolve/") != std::string::npos ? 1U : 0U;
        }
        return n;
    }

    std::vector<std::string> urls;

private:
    [[nodiscard]] std::vector<std::filesystem::path> files() const {
        std::vector<std::filesystem::path> out;
        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            out.push_back(entry.path());
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
        for (const std::filesystem::path& file : files()) {
            siblings.push_back({{"rfilename", file.filename().string()}});
        }
        return nlohmann::json{{"id", repo_},
                              {"library_name", options_.library},
                              {"tags", {options_.library, "safetensors"}},
                              {"siblings", siblings}}
            .dump();
    }

    [[nodiscard]] std::string tree() const {
        nlohmann::json out = nlohmann::json::array();
        for (const std::filesystem::path& file : files()) {
            const std::string name = file.filename().string();
            const std::string bytes = served(name);
            nlohmann::json item{{"type", "file"}, {"path", name}, {"size", bytes.size()}};
            if (options_.digests && name.ends_with(".safetensors")) {
                // The digest of what SHOULD arrive: a corrupted serve misses it.
                item["lfs"] = {{"oid", apogee::models::sha256_hex(bytes)}, {"size", bytes.size()}};
                if (name == options_.corrupt) {
                    item["lfs"]["oid"] = apogee::models::sha256_hex(bytes + "x");
                }
            }
            out.push_back(item);
        }
        return out.dump();
    }

    std::string repo_;
    std::filesystem::path dir_;
    Options options_;
};

/// A store, a repository to pull from, and the client that reaches it.
struct Pull {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    std::filesystem::path repo = home.home() / "fixture" / "repo";
    StoreRoots roots = StoreRoots::at(home.models());
    FixtureSource* source = nullptr;
    std::unique_ptr<HttpClient> client;

    explicit Pull(FixtureSource::Options options = {}, const MlxModelSpec& spec = {.shards = 2}) {
        write_mlx_model(repo, spec);
        auto transport =
            std::make_unique<FixtureSource>("mlx-community/M-4bit", repo, std::move(options));
        source = transport.get();
        client = std::make_unique<HttpClient>(std::move(transport));
    }

    /// `pull_mlx`, its output captured; the exit code it failed with, or 0.
    int run(std::string* out, const apogee::harness::CancellationToken& cancellation = {}) const {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        int code = 0;
        try {
            (void)apogee::commands::pull_mlx("mlx-community/M-4bit", roots, *client, cancellation);
        } catch (const CLI::RuntimeError& e) {
            code = e.get_exit_code();
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str();
        return code;
    }

    /// Nothing under the model's mlx/ but committed ids: no staging, no
    /// owner marker, and nothing the doctor would call a leftover.
    void check_no_staging() const {
        const std::filesystem::path formats = roots.models / "mlx-community--M-4bit" / "mlx";
        if (std::filesystem::exists(formats)) {
            for (const auto& entry : std::filesystem::directory_iterator(formats)) {
                INFO(entry.path().string());
                CHECK_FALSE(entry.path().filename().string().starts_with(".incoming-"));
            }
        }
        CHECK(apogee::models::find_abandoned_staging(roots).empty());
    }
};

[[nodiscard]] std::string shard_id(const std::filesystem::path& repo, const MlxModelSpec& spec) {
    std::vector<apogee::models::SnapshotFile> shards;
    for (const std::string& name : apogee::testing::mlx_shard_names(spec)) {
        shards.push_back(
            {.path = name, .size = 0, .sha256 = apogee::models::file_sha256(repo / name)});
    }
    return apogee::models::snapshot_weight_id(shards);
}

}  // namespace

TEST_CASE("a pull of an MLX repository lands under mlx/ by its weights' hash, verified",
          "[commands][models][mlx][pull]") {
    const Pull pull;
    // What `models pull` decides on: the card says MLX, and there is no GGUF.
    const apogee::models::HfListing listing = apogee::models::list_gguf_files(
        *pull.client, *apogee::models::parse_hf_ref("mlx-community/M-4bit"), "", {});
    REQUIRE(listing.mlx());
    CHECK(listing.gguf_files.empty());

    std::string out;
    REQUIRE(pull.run(&out) == 0);
    INFO(out);
    const std::string id = shard_id(pull.repo, {.shards = 2});
    const std::filesystem::path dir = pull.roots.models / "mlx-community--M-4bit" / "mlx" / id;
    REQUIRE(std::filesystem::is_directory(dir));
    CHECK(apogee::models::read_mlx_info(dir).complete);
    for (const std::string name : {"config.json", "model-00002-of-00002.safetensors"}) {
        CHECK(read_file(dir / name) == read_file(pull.repo / name));
    }
    // Which checks ran, and which could not.
    CHECK(out.find("verified: 6 file(s), ") != std::string::npos);
    CHECK(out.find("-- 6 size(s) checked, 2 against a published sha256 (4 had none "
                   "published); config.json, a tokenizer and 2 shard header(s) read whole\n") !=
          std::string::npos);
    CHECK(out.find("mlx model: llama, 4-bit (affine, group 64), 32768-token window (the default; "
                   "trained for 131072), ") != std::string::npos);
    CHECK(out.find("apogee config add-backend M-4bit --type mlx --model-path " + dir.string()) !=
          std::string::npos);
    pull.check_no_staging();

    // Its record: the repository, and every file as it landed.
    const std::optional<apogee::models::Snapshot> record = apogee::models::load_snapshot(dir);
    REQUIRE(record.has_value());
    CHECK(record->ref == "mlx-community/M-4bit");
    CHECK(record->source == "huggingface");
    CHECK(record->revision == "main");
    REQUIRE(record->files.size() == 6);
    for (const apogee::models::SnapshotFile& file : record->files) {
        CHECK(file.sha256 == apogee::models::file_sha256(dir / file.path));
    }

    // Listed with its format, its quantization and its window.
    const apogee::harness::Config config = apogee::harness::load_config(pull.home.config_path());
    const std::vector<apogee::commands::ModelRow> rows =
        apogee::commands::build_model_rows(config, pull.home.models());
    const std::string handle = "mlx-community--M-4bit/mlx/" + id;
    const auto row = std::ranges::find(rows, handle, &apogee::commands::ModelRow::model);
    REQUIRE(row != rows.end());
    CHECK(row->state == "mlx");
    CHECK(row->provenance == "huggingface");
    CHECK(row->verified == "6 file(s) on record");
    CHECK(row->profile == "llama3");
    CHECK(row->note.starts_with("llama, 4-bit (affine, group 64), 32768-token window"));
    const std::string jsonl = apogee::commands::render_model_jsonl({*row});
    CHECK(jsonl.find(R"j("format":"mlx")j") != std::string::npos);
    CHECK(jsonl.find(R"j("quant":"4-bit (affine, group 64)")j") != std::string::npos);
    CHECK(jsonl.find(R"j("window":"32768-token window (the default; trained for 131072)")j") !=
          std::string::npos);
    CHECK(apogee::commands::render_model_table(rows).find("4-bit (affine, group 64)") !=
          std::string::npos);

    // And `info` on it, by its handle.
    REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find("format:       MLX -- runs on an mlx backend (Apple silicon)\n") !=
          std::string::npos);
    CHECK(out.find("lineage:      pulled from mlx-community/M-4bit (Hugging Face)\n") !=
          std::string::npos);
    CHECK(out.find("files:        whole -- config.json, a tokenizer and 2 shard(s), each holding "
                   "every byte its header lists\n") != std::string::npos);
    CHECK(out.find("quantization: 4-bit (affine, group 64)\n") != std::string::npos);
    CHECK(out.find("window:       32768 tokens (the default; trained for 131072)\n") !=
          std::string::npos);

    // Pulled again: found by the published digests before a byte moves.
    pull.source->urls.clear();
    REQUIRE(pull.run(&out) == 0);
    CHECK(out.find("already here -- these exact weights are at\n  " + dir.string()) !=
          std::string::npos);
    CHECK(pull.source->downloads() == 0);
}

TEST_CASE("a source that publishes no digests is checked by size, and says so",
          "[commands][models][mlx][pull]") {
    const Pull pull{{.digests = false}};
    std::string out;
    REQUIRE(pull.run(&out) == 0);
    INFO(out);
    CHECK(out.find("-- 6 size(s) checked, 0 against a published sha256 (none published); ") !=
          std::string::npos);
    // The id waits for the bytes: the same rule, over what landed.
    CHECK(std::filesystem::is_directory(pull.roots.models / "mlx-community--M-4bit" / "mlx" /
                                        shard_id(pull.repo, {.shards = 2})));
    pull.check_no_staging();
}

TEST_CASE("a pull that fails at any rung lands nothing and leaves no staging",
          "[commands][models][mlx][pull]") {
    std::string out;
    SECTION("the connection drops mid-shard") {
        const Pull pull{{.drop = "model-00002-of-00002.safetensors"}};
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("connection dropped mid-stream") != std::string::npos);
        CHECK(apogee::models::list_store_mlx(pull.roots).empty());
        pull.check_no_staging();
    }
    SECTION("a shard is not what its published digest says") {
        const Pull pull{{.corrupt = "model-00001-of-00002.safetensors"}};
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("digest mismatch") != std::string::npos);
        CHECK(apogee::models::list_store_mlx(pull.roots).empty());
        pull.check_no_staging();
    }
    SECTION("the files are no model mlx-lm can load, whatever their digests say") {
        const Pull pull{{.truncate = "model.safetensors"}, {.shards = 1}};
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("is not a loadable MLX model -- cannot load: model.safetensors is "
                       "truncated") != std::string::npos);
        CHECK(out.find("nothing was kept") != std::string::npos);
        CHECK(apogee::models::list_store_mlx(pull.roots).empty());
        pull.check_no_staging();
    }
    SECTION("Ctrl-C mid-download") {
        const apogee::harness::CancellationToken token =
            apogee::harness::CancellationToken::create();
        const Pull pull{{.cancel = "model-00001-of-00002.safetensors", .token = token}};
        CHECK(pull.run(&out, token) == apogee::commands::kCancelled);
        CHECK(out.find("cancelled -- nothing was written") != std::string::npos);
        CHECK(apogee::models::list_store_mlx(pull.roots).empty());
        pull.check_no_staging();
        CHECK_FALSE(std::filesystem::exists(pull.roots.models / "mlx-community--M-4bit"));
    }
    SECTION("no config.json: refused before a byte moves") {
        const Pull pull;
        std::filesystem::remove(pull.repo / "config.json");
        CHECK(pull.run(&out) == 1);
        CHECK(out.find("says it is an MLX model but holds no config.json") != std::string::npos);
        CHECK(pull.source->downloads() == 0);
        CHECK_FALSE(std::filesystem::exists(pull.roots.models / "mlx-community--M-4bit"));
    }
}

// ---- convert --mlx -------------------------------------------------------------------

namespace {

/// A full-weight snapshot in a store, and `mlx_lm.convert` played in process.
struct Convert {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    StoreRoots roots = StoreRoots::at(home.models());
    std::filesystem::path snapshot = home.models() / "org--m" / "safetensors" / "aaaaaaaaaaaa";
    int calls = 0;

    Convert() {
        write_mlx_model(snapshot, {.bits = 0, .format = "pt"});
    }

    [[nodiscard]] apogee::training::MlxConverter writes(int bits) {
        return [this, bits](const std::filesystem::path&, const std::filesystem::path& out,
                            const apogee::training::MessageSink&,
                            const apogee::harness::CancellationToken&) {
            ++calls;
            write_mlx_model(out, {.bits = bits});
            return std::string{};
        };
    }

    int run(const std::string& precision, const apogee::training::MlxConverter& converter,
            std::string* out) const {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        int code = 0;
        try {
            (void)apogee::commands::convert_model_to_mlx(
                roots, "org--m", "", *apogee::training::find_mlx_precision(precision), converter);
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

TEST_CASE("convert --mlx lands a runnable MLX model in the store, recorded and recognised",
          "[commands][models][mlx][convert]") {
    Convert convert;
    std::string out;
    REQUIRE(convert.run("4bit", convert.writes(4), &out) == 0);
    INFO(out);
    const std::vector<apogee::models::StoredMlx> stored =
        apogee::models::list_store_mlx(convert.roots, "org--m");
    REQUIRE(stored.size() == 1);
    CHECK(apogee::models::read_mlx_info(stored.front().dir).complete);
    CHECK(out.find("converting " + convert.snapshot.string() + " to MLX (4bit") !=
          std::string::npos);
    CHECK(out.find("hashing it (") != std::string::npos);
    CHECK(out.find("mlx model: llama, 4-bit (affine, group 64)") != std::string::npos);
    CHECK(out.find("apogee config add-backend m-4bit --type mlx --model-path " +
                   stored.front().dir.string()) != std::string::npos);
    const std::optional<apogee::models::Snapshot> record =
        apogee::models::load_snapshot(stored.front().dir);
    REQUIRE(record.has_value());
    CHECK(record->ref == "org--m/safetensors/aaaaaaaaaaaa");
    CHECK(record->source == "convert");
    CHECK(record->transform == "mlx_lm.convert 4bit");
    CHECK(apogee::models::find_abandoned_staging(convert.roots).empty());

    // The same set at the same precision is the same weights: nothing runs.
    REQUIRE(convert.run("4bit", convert.writes(4), &out) == 0);
    CHECK(out.find("already converted:\n  " + stored.front().dir.string()) != std::string::npos);
    CHECK(convert.calls == 1);

    // The snapshot it consumed folds from the listing, and says what was made.
    const apogee::harness::Config config = apogee::harness::load_config(convert.home.config_path());
    const std::vector<apogee::commands::ModelRow> rows =
        apogee::commands::build_model_rows(config, convert.home.models());
    const auto snapshot = std::ranges::find(rows, "org--m/safetensors/aaaaaaaaaaaa",
                                            &apogee::commands::ModelRow::model);
    REQUIRE(snapshot != rows.end());
    CHECK(snapshot->consumed);
    const std::string handle = "org--m/mlx/" + stored.front().id;
    const auto made = std::ranges::find(rows, handle, &apogee::commands::ModelRow::model);
    REQUIRE(made != rows.end());
    CHECK(made->provenance == "converted");
    REQUIRE(convert.home.run({"models", "info", "org--m/safetensors/aaaaaaaaaaaa", "-q"}, &out) ==
            0);
    CHECK(out.find("made from it: " + handle + " (recorded)\n") != std::string::npos);
    REQUIRE(convert.home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find("lineage:      converted from org--m/safetensors/aaaaaaaaaaaa (recorded, "
                   "mlx_lm.convert 4bit)\n") != std::string::npos);
}

TEST_CASE("Ctrl-C mid-convert, or a converter that fails, leaves the store untouched",
          "[commands][models][mlx][convert]") {
    Convert convert;
    std::string out;
    SECTION("Ctrl-C") {
        const apogee::training::MlxConverter interrupted =
            [](const std::filesystem::path&, const std::filesystem::path& output,
               const apogee::training::MessageSink&,
               const apogee::harness::CancellationToken& cancellation) {
                apogee::testing::write_fixture_file(output / "model-00001-of-00002.safetensors",
                                                    "half a shard");
                cancellation.cancel();  // what the Ctrl-C handler does
                return std::string{"cancelled"};
            };
        CHECK(convert.run("4bit", interrupted, &out) == apogee::commands::kCancelled);
        CHECK(out.find("cancelled -- nothing was written") != std::string::npos);
    }
    SECTION("the converter's own words") {
        const apogee::training::MlxConverter failing =
            [](const std::filesystem::path&, const std::filesystem::path& output,
               const apogee::training::MessageSink&, const apogee::harness::CancellationToken&) {
                apogee::testing::write_fixture_file(output / "config.json", "{}");
                return std::string{"ValueError: Model type gemma9 not supported."};
            };
        CHECK(convert.run("4bit", failing, &out) == 1);
        CHECK(out.find("ValueError: Model type gemma9 not supported.") != std::string::npos);
    }
    SECTION("asked for 8 bits, given 4") {
        CHECK(convert.run("8bit", convert.writes(4), &out) == 1);
        CHECK(out.find("did not quantize to 8 bits") != std::string::npos);
    }
    // Exactly the store there was: the snapshot, and no mlx/ at all.
    CHECK_FALSE(std::filesystem::exists(convert.home.models() / "org--m" / "mlx"));
    CHECK(apogee::models::read_mlx_info(convert.snapshot).complete);
    CHECK(apogee::models::find_abandoned_staging(convert.roots).empty());
}

TEST_CASE("convert --mlx refuses what is already an MLX model, before anything runs",
          "[commands][models][mlx][convert]") {
    Convert convert;
    std::string out;
    // An MLX model in the store is no SafeTensors set to convert.
    write_mlx_model(convert.home.models() / "org--m" / "mlx" / "bbbbbbbbbbbb");
    {
        const std::ostringstream captured;
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        CHECK_THROWS_AS(apogee::commands::convert_model_to_mlx(
                            convert.roots, "org--m/mlx/bbbbbbbbbbbb", "",
                            *apogee::training::find_mlx_precision("4bit"), convert.writes(4)),
                        CLI::RuntimeError);
        std::cerr.rdbuf(old_err);
        CHECK(captured.str().find("is an MLX model; this reads full-weight SafeTensors weights") !=
              std::string::npos);
    }
    // Nor is an mlx-community build pulled as a snapshot.
    write_mlx_model(convert.snapshot);
    CHECK(convert.run("4bit", convert.writes(4), &out) == 1);
    CHECK(out.find("is already an MLX model (4-bit (affine, group 64))") != std::string::npos);
    CHECK(convert.calls == 0);
}

TEST_CASE("an MLX model goes whole by its handle, its model's name or its backend's name",
          "[commands][models][mlx][delete]") {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    const StoreRoots roots = StoreRoots::at(home.models());
    const std::filesystem::path dir = home.models() / "org--m" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(dir, {.shards = 3});
    write_mlx_model(home.models() / "org--m" / "safetensors" / "aaaaaaaaaaaa",
                    {.bits = 0, .format = "pt"});

    const apogee::commands::DeletePlan by_handle =
        apogee::commands::plan_delete(roots, "org--m/mlx/bbbbbbbbbbbb");
    REQUIRE(by_handle.ok);
    CHECK(by_handle.removes == std::vector<std::filesystem::path>{dir});
    const apogee::commands::DeletePlan by_format =
        apogee::commands::plan_delete(roots, "org--m/mlx");
    CHECK(by_format.removes == std::vector<std::filesystem::path>{dir});
    CHECK(apogee::commands::plan_delete(roots, "org--m").removes.size() == 2);

    // A backend's name means the MLX model its model_path names (M7's rule).
    apogee::harness::Config config;
    apogee::harness::BackendConfig fast;
    fast.type = apogee::harness::BackendType::Mlx;
    fast.model_path = dir.string() + "/";
    config.backends.emplace("fast", fast);
    const apogee::commands::DeletePlan by_backend =
        apogee::commands::plan_delete(roots, config, "fast");
    REQUIRE(by_backend.ok);
    CHECK(by_backend.backend == "fast");
    CHECK(by_backend.removes == std::vector<std::filesystem::path>{dir});

    std::string out;
    REQUIRE(home.run({"models", "delete", "org--m/mlx/bbbbbbbbbbbb", "--yes"}, &out) == 0);
    CHECK_FALSE(std::filesystem::exists(dir));
    CHECK_FALSE(std::filesystem::exists(home.models() / "org--m" / "mlx"));
    CHECK(std::filesystem::exists(home.models() / "org--m" / "safetensors"));
}

TEST_CASE("a stored MLX model's name fills add-backend, as a stored GGUF's does",
          "[commands][models][mlx][config]") {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    const std::filesystem::path dir =
        home.models() / "mlx-community--Llama-3.2-1B-Instruct-4bit" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(dir);
    const std::filesystem::path converted = home.models() / "org--m" / "mlx" / "cccccccccccc";
    write_mlx_model(converted, {.bits = 8});
    CHECK(apogee::models::stored_mlx_name({.model = "mlx-community--Llama-3.2-1B-Instruct-4bit",
                                           .id = "bbbbbbbbbbbb",
                                           .dir = dir,
                                           .arrived = {}}) == "Llama-3.2-1B-Instruct-4bit");
    std::string out;
    REQUIRE(home.run({"config", "add-backend", "Llama-3.2-1B-Instruct-4bit"}, &out) == 0);
    INFO(out);
    CHECK(out.find("filled from the store: mlx-community--Llama-3.2-1B-Instruct-4bit/mlx/"
                   "bbbbbbbbbbbb\n  --type mlx --model-path " +
                   dir.string() + "\n") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    const apogee::harness::BackendConfig& entry = config.backends.at("Llama-3.2-1B-Instruct-4bit");
    CHECK(entry.type == apogee::harness::BackendType::Mlx);
    CHECK(entry.model_path == dir.string());

    // A conversion's name carries its precision.
    REQUIRE(home.run({"config", "add-backend", "m-8bit"}, &out) == 0);
    CHECK(apogee::harness::load_config(home.config_path()).backends.at("m-8bit").model_path ==
          converted.string());
}

TEST_CASE(
    "models info and status say whether an mlx entry reads images, from the same file facts the "
    "backend answers by",
    "[commands][models][mlx][vision]") {
    // 27c: a vision model and mlx-vlm in the environment, or why not.
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    const std::filesystem::path text = home.home() / "text-model";
    write_mlx_model(text);
    const std::filesystem::path sighted = home.home() / "vision-model";
    write_mlx_model(sighted, {.model_type = "qwen3_vl"});
    apogee::testing::write_fixture_file(
        sighted / "config.json",
        R"({"model_type": "qwen3_vl", "vision_config": {"depth": 27}, )"
        R"("text_config": {"model_type": "qwen3_vl_text", "max_position_embeddings": 262144}})");
    apogee::testing::write_fixture_file(sighted / "preprocessor_config.json", "{}");
    std::string out;
    REQUIRE(
        home.run({"config", "add-backend", "words", "--type", "mlx", "--model-path", text.string()},
                 &out) == 0);
    REQUIRE(home.run({"config", "add-backend", "eyes", "--type", "mlx", "--model-path",
                      sighted.string()},
                     &out) == 0);
    REQUIRE(home.run({"config", "set-default-vision", "eyes"}, &out) == 0);

    REQUIRE(home.run({"models", "info", "words", "-q"}, &out) == 0);
    CHECK(out.find("vision:       no -- it is not a vision model") != std::string::npos);
    REQUIRE(home.run({"models", "info", "eyes", "-q"}, &out) == 0);
    CHECK(out.find("vision:       no -- it is a vision model, but mlx-vlm is not installed") !=
          std::string::npos);
    CHECK(out.find("(apogee train setup --with mlx-vlm)") != std::string::npos);
    REQUIRE(home.run({"models", "status", "-q"}, &out) == 0);
    CHECK(out.find("vision: eyes   [mlx: cannot read images -- it is a vision model, but mlx-vlm "
                   "is not installed") != std::string::npos);

    const std::filesystem::path site =
        home.home() / "training" / "venv" / "lib" / "python3.14" / "site-packages";
    apogee::testing::write_fixture_file(site / "mlx_vlm" / "__init__.py", "");
    std::filesystem::create_directories(site / "mlx_vlm-0.3.9.dist-info");
    REQUIRE(home.run({"models", "info", "eyes", "-q"}, &out) == 0);
    CHECK(out.find("vision:       reads images as they are -- mlx-vlm 0.3.9") != std::string::npos);
    REQUIRE(home.run({"models", "status", "-q"}, &out) == 0);
    CHECK(out.find("vision: eyes   [mlx: reads images as they are]") != std::string::npos);
    // A text model stays one, whatever is installed.
    REQUIRE(home.run({"models", "info", "words", "-q"}, &out) == 0);
    CHECK(out.find("vision:       no -- it is not a vision model") != std::string::npos);
}

// ---- the command line, over the real driver ------------------------------------------

namespace {

/// The stub `mlx` packages laid out as an environment Apogee owns, under
/// `home` (what `mlx_fake_runtime.sh` does for the scripts): an interpreter
/// that is the host's python3 with them on its path.
void fake_mlx_runtime(const std::filesystem::path& home) {
    const std::filesystem::path stubs =
        std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts" / "stub_mlx";
    const std::filesystem::path site =
        home / "training" / "venv" / "lib" / "python3.99" / "site-packages";
    std::filesystem::create_directories(site);
    std::filesystem::copy(stubs, site, std::filesystem::copy_options::recursive);
    const std::filesystem::path python = home / "training" / "venv" / "bin" / "python";
    apogee::testing::write_fixture_file(python,
                                        "#!/bin/sh\nPYTHONPATH='" + site.string() +
                                            "' PYTHONDONTWRITEBYTECODE=1 exec python3 \"$@\"\n");
    std::filesystem::permissions(python, std::filesystem::perms::owner_all);
}

}  // namespace

TEST_CASE("models convert --mlx on the command line: the real driver, listed, checked, gone whole",
          "[commands][models][mlx][convert][driver]") {
    if (apogee::platform::host_target() != "macos-arm64" ||
        apogee::platform::find_on_path("python3").empty()) {
        SKIP("the mlx runtime is Apple silicon's, and the driver needs a python3");
    }
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    fake_mlx_runtime(home.home());
    const std::filesystem::path snapshot =
        home.models() / "org--m" / "safetensors" / "aaaaaaaaaaaa";
    write_mlx_model(snapshot, {.bits = 0, .format = "pt"});

    std::string out;
    REQUIRE(home.run({"models", "convert", "org--m", "--mlx"}, &out) == 0);
    INFO(out);
    const std::vector<apogee::models::StoredMlx> stored =
        apogee::models::list_store_mlx(StoreRoots::at(home.models()), "org--m");
    REQUIRE(stored.size() == 1);
    const std::string handle = "org--m/mlx/" + stored.front().id;
    CHECK(out.find("mlx model: llama, 4-bit (affine, group 64), 32768-token window") !=
          std::string::npos);
    CHECK(out.find("[INFO]") == std::string::npos);  // the library's print stays off

    // Precisions are each engine's own.
    CHECK(home.run({"models", "convert", "org--m", "--mlx", "--type", "q8_0"}, &out) != 0);
    CHECK(out.find("'q8_0' is a GGUF precision") != std::string::npos);
    CHECK(home.run({"models", "convert", "org--m", "--type", "4bit"}, &out) != 0);
    CHECK(out.find("add --mlx") != std::string::npos);

    REQUIRE(home.run({"models", "list", "--no-color", "-q"}, &out) == 0);
    CHECK(out.find(handle) != std::string::npos);
    CHECK(out.find("llama, 4-bit (affine, group 64), 32768-token window (the default; trained "
                   "for 131072)") != std::string::npos);

    REQUIRE(home.run({"config", "add-backend", "m-4bit"}, &out) == 0);
    CHECK(out.find("filled from the store: " + handle) != std::string::npos);

    // By the backend's name, whole; the backend is said to stop working.
    REQUIRE(home.run({"models", "delete", "m-4bit", "--yes"}, &out) == 0);
    CHECK(out.find("backend 'm-4bit' points into this") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(stored.front().dir));
}

TEST_CASE("MLX is offered only for a type the installed mlx-lm builds, and refused before it runs",
          "[commands][models][mlx][convert][driver][model_type]") {
    // The user's call (M21, 2026-10-21): never show a conversion that cannot
    // work. The mlx-lm installed says which types it builds, by its own
    // modules (M15's reader); with none installed, nothing is decided.
    if (apogee::platform::host_target() != "macos-arm64" ||
        apogee::platform::find_on_path("python3").empty()) {
        SKIP("the mlx runtime is Apple silicon's, and the driver needs a python3");
    }
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::string vl = "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019";
    write_mlx_model(home.models() / "Qwen3-VL-8B-Instruct" / "safetensors" / "63f2634a2019",
                    {.bits = 0, .model_type = "qwen3_vl", .format = "pt"});
    const std::string llama = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f";
    write_mlx_model(home.models() / "Llama3.2-1B-Instruct" / "safetensors" / "edaba290ce7f",
                    {.bits = 0, .format = "pt"});
    std::string out;
    std::string err;
    const auto conversions = [&](const std::string& handle) {
        REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &out, &err) == 0);
        return nlohmann::json::parse(out)["data"]["converts_to"];
    };
    // No mlx-lm installed to ask: both offered, as before.
    CHECK(conversions(vl) == nlohmann::json::array({"gguf", "mlx"}));

    fake_mlx_runtime(home.home());  // builds gemma3, gemma4, gemma4_text, llama
    CHECK(conversions(vl) == nlohmann::json::array({"gguf"}));
    CHECK(conversions(llama) == nlohmann::json::array({"gguf", "mlx"}));
    REQUIRE(home.run({"models", "info", vl}, &out) == 0);
    CHECK(out.find("makes a GGUF of it\n") != std::string::npos);

    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD",
                                           (home.home() / "calls.jsonl").string()};
    CHECK(home.run({"models", "convert", vl, "--mlx"}, &out) != 0);
    INFO(out);
    CHECK(out.find("'" + vl +
                   "' is a qwen3_vl model, which the mlx-lm in Apogee's Python environment builds "
                   "no model for -- convert it without --mlx") != std::string::npos);
    CHECK(out.find("--model-type <type>") != std::string::npos);
    CHECK(home.run({"models", "convert", vl, "--mlx", "--model-type", "nosuch"}, &out) != 0);
    CHECK(out.find("--model-type nosuch is no type the mlx-lm in Apogee's Python environment "
                   "builds") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(home.home() / "calls.jsonl"));  // the driver never ran
    CHECK(apogee::models::list_store_mlx(StoreRoots::at(home.models())).empty());
}

TEST_CASE("a vendor's model_type is refused with its door, then converted as the type named",
          "[commands][models][mlx][convert][driver][model_type]") {
    // M15: a vendor's spelling of a type mlx-lm has no class for, its door
    // and the nearest types now said; then converted as the base type through
    // a view, the snapshot untouched, the record telling. (The user's own
    // Assistant models turned out to be draft heads, refused before this
    // door: the next case. This fixture is a plain model under a vendor's
    // type name.)
    if (apogee::platform::host_target() != "macos-arm64" ||
        apogee::platform::find_on_path("python3").empty()) {
        SKIP("the mlx runtime is Apple silicon's, and the driver needs a python3");
    }
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    fake_mlx_runtime(home.home());
    const std::filesystem::path snapshot =
        home.models() / "google--gemma-4-12B-it-assistant" / "safetensors" / "aaaaaaaaaaaa";
    write_mlx_model(snapshot,
                    {.bits = 0, .model_type = "gemma4_unified_assistant", .format = "pt"});
    const std::string config_before = read_file(snapshot / "config.json");
    const apogee::testing::EnvGuard unsupported{"STUB_MLX_UNSUPPORTED", "gemma4_unified_assistant"};
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD",
                                           (home.home() / "calls.jsonl").string()};

    std::string out;
    CHECK(home.run(
              {"models", "convert", "--mlx", "google--gemma-4-12B-it-assistant", "--type", "4bit"},
              &out) != 0);
    // Refused by the installed mlx-lm's own modules before the driver starts
    // (M21, the user's call), M15's door kept.
    CHECK(out.find("'google--gemma-4-12B-it-assistant' is a gemma4_unified_assistant model, "
                   "which the mlx-lm in Apogee's Python environment builds no model for") !=
          std::string::npos);
    CHECK(out.find("convert them as it: --model-type <type> (nearest here: gemma4_unified, "
                   "gemma4_text, gemma4, gemma3)") != std::string::npos);
    const StoreRoots roots = StoreRoots::at(home.models());
    CHECK(apogee::models::list_store_mlx(roots, "google--gemma-4-12B-it-assistant").empty());

    REQUIRE(home.run({"models", "convert", "--mlx", "google--gemma-4-12B-it-assistant", "--type",
                      "4bit", "--model-type", "gemma4_unified"},
                     &out) == 0);
    INFO(out);
    CHECK(out.find("converting it as model_type gemma4_unified (its config.json says "
                   "gemma4_unified_assistant) -- through a view of it; the snapshot itself is "
                   "untouched") != std::string::npos);
    // The driver read the view: the type named, under the store's staging.
    std::ifstream calls{home.home() / "calls.jsonl"};
    std::vector<nlohmann::json> seen;
    for (std::string line; std::getline(calls, line);) {
        seen.push_back(nlohmann::json::parse(line));
    }
    REQUIRE(seen.size() == 1);  // the refused run never started the driver
    CHECK(seen.back()["model_type"] == "gemma4_unified");
    CHECK(seen.back()["hf_path"].get<std::string>().find(".incoming-") != std::string::npos);
    // The snapshot untouched, and no view left behind.
    CHECK(read_file(snapshot / "config.json") == config_before);
    CHECK(apogee::models::find_abandoned_staging(roots).empty());
    const std::vector<apogee::models::StoredMlx> stored =
        apogee::models::list_store_mlx(roots, "google--gemma-4-12B-it-assistant");
    REQUIRE(stored.size() == 1);
    const std::optional<apogee::models::Snapshot> made =
        apogee::models::load_snapshot(stored.front().dir);
    REQUIRE(made.has_value());
    CHECK(made->transform ==
          "mlx_lm.convert 4bit, converted as gemma4_unified (model_type was "
          "gemma4_unified_assistant)");
    REQUIRE(home.run({"models", "info", "google--gemma-4-12B-it-assistant/mlx/" + stored.front().id,
                      "-q"},
                     &out) == 0);
    CHECK(out.find("converted as gemma4_unified (model_type was gemma4_unified_assistant)") !=
          std::string::npos);

    // Without --mlx the flag means nothing, and is refused saying why.
    CHECK(home.run({"models", "convert", "google--gemma-4-12B-it-assistant", "--model-type",
                    "gemma4_unified"},
                   &out) != 0);
    CHECK(out.find("--model-type names the type mlx-lm converts these weights as -- it works "
                   "with --mlx") != std::string::npos);
    // The types complete from the installed mlx-lm's own lists.
    REQUIRE(home.run({"__complete", "models", "convert", "x", "--mlx", "--model-type", "gemma4"},
                     &out) == 0);
    CHECK(out == "gemma4\ngemma4_text\ngemma4_unified\n");
}

namespace {

/// The MLX chain (M16) over `convert`'s store, `mlx_lm.convert` played by
/// `make` -- one call a precision -- and its narration captured.
int chain(Convert& convert, const std::vector<std::string>& precisions, bool listed,
          const std::string& base_name,
          const std::function<std::string(const apogee::training::MlxPrecision&)>& fails,
          std::string* out) {
    const std::ostringstream captured;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
    int code = 0;
    try {
        apogee::commands::run_mlx_register_chain(
            apogee::commands::MlxRegisterRequest{.roots = convert.roots,
                                                 .config_path = convert.home.config_path(),
                                                 .snapshot = "org--m",
                                                 .precisions = precisions,
                                                 .base_name = base_name,
                                                 .listed = listed},
            [&convert, &fails](const apogee::training::MlxPrecision& precision) {
                return [&convert, &fails, precision](const std::filesystem::path&,
                                                     const std::filesystem::path& output,
                                                     const apogee::training::MessageSink&,
                                                     const apogee::harness::CancellationToken&) {
                    ++convert.calls;
                    if (std::string why = fails ? fails(precision) : std::string{}; !why.empty()) {
                        return why;
                    }
                    // Each precision's own weights, as a real conversion's are.
                    write_mlx_model(
                        output, {.bits = precision.bits, .shards = precision.bits >= 8 ? 2 : 1});
                    return std::string{};
                };
            });
    } catch (const CLI::RuntimeError& e) {
        code = e.get_exit_code();
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    *out = captured.str();
    return code;
}

[[nodiscard]] std::string config_text(const CliHome& home) {
    return read_file(home.config_path());
}

}  // namespace

TEST_CASE("a draft head is refused at every conversion door, its base named, nothing run",
          "[commands][models][mlx][convert][draft_head]") {
    // The user's run (2026-10-21): converting Gemma4-12B-Assistant to MLX ended
    // deep in mlx-lm, and M15's door offered an override that cannot work --
    // the weights are a draft head, which runs beside its base and never alone.
    // Now refused before anything runs, and framed wherever it is listed.
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::filesystem::path head =
        home.models() / "Gemma4-12B-Assistant" / "safetensors" / "43b42c6289a0";
    write_mlx_model(head, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(head / "config.json",
                                        R"({"architectures": ["Gemma4UnifiedAssistantForCausalLM"],
            "model_type": "gemma4_unified_assistant", "backbone_hidden_size": 3840,
            "text_config": {"model_type": "gemma4_unified_text", "hidden_size": 1024}})");
    const std::filesystem::path base =
        home.models() / "Gemma4-12B-Instruct" / "safetensors" / "920344fd2dac";
    write_mlx_model(base, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(
        base / "config.json",
        R"({"architectures": ["Gemma4UnifiedForConditionalGeneration"],
            "model_type": "gemma4_unified", "text_config": {"hidden_size": 3840}})");
    const std::string refused =
        "'Gemma4-12B-Assistant/safetensors/43b42c6289a0' is a draft head "
        "(Gemma4UnifiedAssistantForCausalLM): speculative decoding runs it beside its base model "
        "(Gemma4-12B-Instruct), reading that model's hidden states, and never alone -- nothing "
        "converted from it could answer a chat; convert its base model instead";

    std::string out;
    // The user's exact command, refused before its chain announces a step.
    CHECK(home.run({"models", "convert", "Gemma4-12B-Assistant/safetensors/43b42c6289a0", "--mlx",
                    "--register", "--type", "4bit", "--base-name", "Gemma4-12B-Assistant"},
                   &out) != 0);
    INFO(out);
    CHECK(out.find(refused) != std::string::npos);
    CHECK(out.find("[1/2]") == std::string::npos);
    CHECK(out.find("--model-type") == std::string::npos);
    // M15's override renames the type, never what the weights are; nor is a
    // GGUF of one a backend.
    CHECK(home.run({"models", "convert", "Gemma4-12B-Assistant", "--mlx", "--model-type",
                    "gemma4_unified"},
                   &out) != 0);
    CHECK(out.find("is a draft head (Gemma4UnifiedAssistantForCausalLM)") != std::string::npos);
    CHECK(home.run({"models", "convert", "Gemma4-12B-Assistant"}, &out) != 0);
    CHECK(out.find("is a draft head (Gemma4UnifiedAssistantForCausalLM)") != std::string::npos);
    const StoreRoots roots = StoreRoots::at(home.models());
    CHECK(apogee::models::list_store_mlx(roots).empty());
    CHECK(apogee::models::list_store_ggufs(roots).empty());
    CHECK(apogee::models::find_abandoned_staging(roots).empty());

    // Framed for what it is wherever it is listed.
    REQUIRE(home.run({"models", "list", "--no-color"}, &out) == 0);
    CHECK(out.find("draft head -- runs beside Gemma4-12B-Instruct, never alone") !=
          std::string::npos);
    REQUIRE(home.run({"models", "info", "Gemma4-12B-Assistant/safetensors/43b42c6289a0"}, &out) ==
            0);
    CHECK(out.find("format:       SafeTensors -- a draft head's weights, not runnable and not "
                   "convertible") != std::string::npos);
    CHECK(out.find("draft head:   a draft head (Gemma4UnifiedAssistantForCausalLM): speculative "
                   "decoding runs it beside its base model (Gemma4-12B-Instruct)") !=
          std::string::npos);
    REQUIRE(home.run({"config", "scan"}, &out) == 0);
    CHECK(out.find("1 SafeTensors snapshot(s) not listed -- weights, not runnable; each registers "
                   "when converted") != std::string::npos);
    CHECK(out.find("1 draft head(s) not listed -- each runs beside its base model, never alone: "
                   "Gemma4-12B-Assistant") != std::string::npos);
    (void)home.run({"check", "--no-color"}, &out);
    CHECK(out.find("SafeTensors weights, draft head -- runs beside Gemma4-12B-Instruct, never "
                   "alone") != std::string::npos);
}

TEST_CASE("a diffusion model is refused at every conversion door, and offered no conversion",
          "[commands][models][mlx][convert][diffusion]") {
    // The user's run (M20, 2026-10-21): an MLX conversion of
    // Wan-AI--Wan2.2-TI2V-5B ran three seconds and failed in mlx-lm's words.
    // Its weights are a diffusers transformer, which neither converter takes:
    // refused before anything runs, and `models info` offers no conversion.
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::string handle = "Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83";
    const std::filesystem::path wan =
        home.models() / "Wan-AI--Wan2.2-TI2V-5B" / "safetensors" / "6ef9b00b8b83";
    write_mlx_model(wan, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(wan / "config.json",
                                        R"({"_class_name": "WanModel", "_diffusers_version":
            "0.33.0", "model_type": "ti2v", "dim": 3072, "num_layers": 30})");
    const std::string refused =
        "'" + handle +
        "' is a diffusion model's weights (WanModel, a diffusers class): neither converter takes "
        "one -- llama.cpp's finds a model by its architectures, and mlx-lm's builds a language "
        "model";

    std::string out;
    // The user's command, and the chain the app now builds, refused alike.
    CHECK(home.run({"models", "convert", handle, "--mlx"}, &out) != 0);
    INFO(out);
    CHECK(out.find(refused) != std::string::npos);
    CHECK(home.run({"models", "convert", handle, "--mlx", "--register-with", "8bit,4bit"}, &out) !=
          0);
    CHECK(out.find(refused) != std::string::npos);
    CHECK(out.find("[1/") == std::string::npos);
    CHECK(home.run({"models", "convert", handle}, &out) != 0);
    CHECK(out.find(refused) != std::string::npos);
    const StoreRoots roots = StoreRoots::at(home.models());
    CHECK(apogee::models::list_store_mlx(roots).empty());
    CHECK(apogee::models::list_store_ggufs(roots).empty());
    CHECK(apogee::models::find_abandoned_staging(roots).empty());

    // `models info` says what it is and offers nothing; its data says none.
    REQUIRE(home.run({"models", "info", handle}, &out) == 0);
    CHECK(out.find("format:       SafeTensors -- a diffusion model's weights, not runnable and "
                   "not convertible") != std::string::npos);
    CHECK(out.find("made from it: nothing -- no conversion takes these weights") !=
          std::string::npos);
    CHECK(out.find("makes a GGUF of it") == std::string::npos);
    std::string err;
    REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["data"]["converts_to"] == nlohmann::json::array());

    // A draft head is offered none either; a language model's snapshot both,
    // its line as before.
    const std::filesystem::path head =
        home.models() / "Gemma4-12B-Assistant" / "safetensors" / "43b42c6289a0";
    write_mlx_model(head, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(head / "config.json",
                                        R"({"architectures": ["Gemma4UnifiedAssistantForCausalLM"],
            "model_type": "gemma4_unified_assistant", "backbone_hidden_size": 3840})");
    REQUIRE(home.run({"models", "info", "Gemma4-12B-Assistant/safetensors/43b42c6289a0"}, &out) ==
            0);
    CHECK(out.find("made from it: nothing -- no conversion takes these weights") !=
          std::string::npos);
    REQUIRE(home.run({"models", "info", "Gemma4-12B-Assistant/safetensors/43b42c6289a0",
                      "--output-format", "json"},
                     &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["data"]["converts_to"] == nlohmann::json::array());
    const std::filesystem::path llama =
        home.models() / "Llama3.2-1B-Instruct" / "safetensors" / "edaba290ce7f";
    write_mlx_model(llama, {.bits = 0, .format = "pt"});
    REQUIRE(home.run({"models", "info", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"}, &out) ==
            0);
    CHECK(out.find("made from it: nothing yet -- 'apogee models convert "
                   "Llama3.2-1B-Instruct/safetensors/edaba290ce7f' makes a GGUF of it (--mlx an "
                   "MLX model)") != std::string::npos);
    REQUIRE(home.run({"models", "info", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f",
                      "--output-format", "json"},
                     &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["data"]["converts_to"] ==
          nlohmann::json::array({"gguf", "mlx"}));
}

TEST_CASE("an embedding model is refused MLX before its chain starts, and offered its GGUF alone",
          "[commands][models][mlx][convert][embedding]") {
    // The user's run (M21, 2026-10-21): converting Gemma-Embedding-300M to
    // MLX announced [1/2] and stopped in mlx-lm's strict load, hundreds of
    // weight names long. It is a sentence-transformers embedding model.
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::string handle = "Gemma-Embedding-300M/safetensors/b6845e67fafb";
    const std::filesystem::path embed =
        home.models() / "Gemma-Embedding-300M" / "safetensors" / "b6845e67fafb";
    write_mlx_model(embed, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(
        embed / "config.json",
        R"({"architectures": ["Gemma3TextModel"], "model_type": "gemma3_text"})");
    apogee::testing::write_fixture_file(embed / "modules.json", R"([
        {"idx": 0, "path": "", "type": "sentence_transformers.models.Transformer"},
        {"idx": 1, "path": "1_Pooling", "type": "sentence_transformers.models.Pooling"},
        {"idx": 2, "path": "2_Dense", "type": "sentence_transformers.models.Dense"},
        {"idx": 3, "path": "3_Dense", "type": "sentence_transformers.models.Dense"},
        {"idx": 4, "path": "4_Normalize", "type": "sentence_transformers.models.Normalize"}])");
    apogee::testing::write_fixture_file(embed / "2_Dense" / "model.safetensors", "x");

    std::string out;
    // The user's exact command, refused before its chain announces a step.
    CHECK(home.run({"models", "convert", handle, "--mlx", "--register", "--type", "8bit",
                    "--base-name", "Gemma-Embed-300M"},
                   &out) != 0);
    INFO(out);
    CHECK(out.find("'" + handle +
                   "' is an embedding model (sentence-transformers: Transformer, Pooling, Dense, "
                   "Dense, Normalize)") != std::string::npos);
    CHECK(out.find("convert it without --mlx") != std::string::npos);
    CHECK(out.find("[1/") == std::string::npos);
    const StoreRoots roots = StoreRoots::at(home.models());
    CHECK(apogee::models::list_store_mlx(roots).empty());
    CHECK(apogee::models::find_abandoned_staging(roots).empty());

    // `models info` offers the GGUF alone, and its data says so.
    REQUIRE(home.run({"models", "info", handle}, &out) == 0);
    CHECK(out.find("embedding:    an embedding model (sentence-transformers:") !=
          std::string::npos);
    CHECK(out.find("makes a GGUF of it\n") != std::string::npos);
    CHECK(out.find("--mlx an MLX model") == std::string::npos);
    std::string err;
    REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["data"]["converts_to"] == nlohmann::json::array({"gguf"}));
}

TEST_CASE("convert --mlx --register converts once and registers what the hint would",
          "[commands][models][mlx][convert][chain]") {
    // M16: the printed `config add-backend … --type mlx` line, executed.
    Convert convert;
    std::string out;
    REQUIRE(chain(convert, {"4bit"}, false, "", {}, &out) == 0);
    INFO(out);
    CHECK(convert.calls == 1);
    const std::vector<apogee::models::StoredMlx> stored =
        apogee::models::list_store_mlx(convert.roots, "org--m");
    REQUIRE(stored.size() == 1);
    CHECK(out.find("[1/2] convert to MLX 4bit") != std::string::npos);
    CHECK(out.find("[2/2] register m-4bit") != std::string::npos);
    CHECK(out.find("registered m-4bit") != std::string::npos);
    CHECK(out.find("Use it by adding a backend") == std::string::npos);  // executed, not hinted

    // Byte for byte the hand-typed line's edit, in a twin home.
    CliHome twin{"backends:\n  m:\n    type: mock\n"};
    std::string said;
    REQUIRE(twin.run({"config", "add-backend", "m-4bit", "--type", "mlx", "--model-path",
                      stored.front().dir.string()},
                     &said) == 0);
    CHECK(config_text(convert.home) == config_text(twin));

    // Again: found, not made, and not registered twice.
    REQUIRE(chain(convert, {"4bit"}, false, "", {}, &out) == 0);
    CHECK(convert.calls == 1);
    CHECK(out.find("already converted:") != std::string::npos);
    CHECK(out.find("already registered: m-4bit") != std::string::npos);
}

TEST_CASE("convert --mlx --register-with converts once a precision, each its own backend",
          "[commands][models][mlx][convert][chain]") {
    Convert convert;
    std::string out;
    // A failure mid-chain: the first build and its entry stand, the resume
    // command is said.
    const auto eight_fails = [](const apogee::training::MlxPrecision& precision) {
        return precision.name == "8bit" ? std::string{"ValueError: out of memory"} : std::string{};
    };
    CHECK(chain(convert, {"4bit", "8bit"}, true, "Gem", eight_fails, &out) != 0);
    INFO(out);
    CHECK(out.find("registered Gem-4bit") != std::string::npos);
    CHECK(out.find("apogee models convert org--m/safetensors/aaaaaaaaaaaa --mlx "
                   "--register-with 4bit,8bit --base-name Gem") != std::string::npos);
    CHECK(apogee::models::list_store_mlx(convert.roots, "org--m").size() == 1);
    CHECK(apogee::harness::load_config(convert.home.config_path()).backends.contains("Gem-4bit"));
    CHECK(apogee::models::find_abandoned_staging(convert.roots).empty());

    // Resumed: nothing made twice, the rest made.
    convert.calls = 0;
    REQUIRE(chain(convert, {"4bit", "8bit"}, true, "Gem", {}, &out) == 0);
    CHECK(convert.calls == 1);  // 8bit alone
    CHECK(out.find("already registered: Gem-4bit") != std::string::npos);
    CHECK(out.find("registered Gem-8bit") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(convert.home.config_path());
    REQUIRE(config.backends.contains("Gem-8bit"));
    CHECK(config.backends.at("Gem-8bit").type == apogee::harness::BackendType::Mlx);
    CHECK(apogee::models::list_store_mlx(convert.roots, "org--m").size() == 2);
}

TEST_CASE("each engine's levels are its own, refused by name before anything runs",
          "[commands][models][mlx][convert][chain]") {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    write_mlx_model(home.models() / "org--m" / "safetensors" / "aaaaaaaaaaaa",
                    {.bits = 0, .format = "pt"});
    std::string out;
    std::string err;
    CHECK(home.run({"models", "convert", "org--m", "--mlx", "--register-with", "Q4_K_M"}, &out,
                   &err) != 0);
    CHECK(err.find("--register-with with --mlx takes the MLX precisions (") != std::string::npos);
    CHECK(err.find("'Q4_K_M' is a GGUF level") != std::string::npos);
    CHECK(home.run({"models", "convert", "org--m", "--register-with", "4bit"}, &out, &err) != 0);
    CHECK(err.find("'4bit' is an MLX precision -- add --mlx to register MLX builds") !=
          std::string::npos);
    CHECK(home.run(
              {"models", "convert", "org--m", "--mlx", "--type", "8bit", "--register-with", "4bit"},
              &out, &err) != 0);
    CHECK(err.find("--register-with names every precision to convert -- drop --type") !=
          std::string::npos);
    CHECK(apogee::models::list_store_mlx(StoreRoots::at(home.models()), "org--m").empty());
    // Completion answers per engine.
    REQUIRE(home.run({"__complete", "models", "convert", "org--m", "--mlx", "--register-with", ""},
                     &out) == 0);
    CHECK(out.find("4bit\n") != std::string::npos);
    CHECK(out.find("Q4_K_M") == std::string::npos);
    REQUIRE(home.run({"__complete", "models", "convert", "org--m", "--register-with", ""}, &out) ==
            0);
    CHECK(out.find("Q4_K_M") != std::string::npos);
    CHECK(out.find("4bit\n") == std::string::npos);
}

// ---- what kind of MLX directory (53e) --------------------------------------------------

namespace {

/// An MLX directory in a store, the kind its record names, and the default
/// name `config scan` registers it under.
struct KindDir {
    std::string model{};
    std::string kind{};
    std::string name{};

    [[nodiscard]] std::string handle() const {
        return model + "/mlx/bbbbbbbbbbbb";
    }

    /// An mlx backend chats with it.
    [[nodiscard]] bool backend() const {
        return kind == "chat" || kind == "vision";
    }
};

/// The line of `text` that `start` begins, followed by a space -- a scan's
/// row by its name -- or empty.
[[nodiscard]] std::string line_starting(const std::string& text, const std::string& start) {
    std::istringstream lines{text};
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with(start + " ")) {
            return line;
        }
    }
    return {};
}

/// `models list --output-format json`'s rows, by the `key` each carries.
[[nodiscard]] std::map<std::string, nlohmann::json> listed_by(const CliHome& home,
                                                              const std::string& key) {
    std::string out;
    std::string err;
    REQUIRE(home.run({"models", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    std::map<std::string, nlohmann::json> rows;
    for (const nlohmann::json& row : document.at("data")) {
        rows[row.at(key).get<std::string>()] = row;
    }
    return rows;
}

}  // namespace

TEST_CASE("an MLX directory recorded as each kind lists, informs and scans as that kind",
          "[commands][models][mlx][kind]") {
    // 53e: the record a conversion writes says what it made, and every
    // surface follows it -- only what an mlx backend chats with registers as
    // one; the rest are said apart with what uses them. A kind no build knows
    // yet is shown as written and registered as nothing.
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::vector<KindDir> dirs{
        {.model = "org--talk", .kind = "chat", .name = "talk-4bit"},
        {.model = "org--see", .kind = "vision", .name = "see-4bit"},
        {.model = "org--embed", .kind = "embedding", .name = "embed-4bit"},
        {.model = "org--draft", .kind = "drafter", .name = "draft-4bit"},
        {.model = "org--film", .kind = "video", .name = "film-4bit"},
        {.model = "org--speak", .kind = "voice", .name = "speak-4bit"},
        {.model = "org--future", .kind = "hologram", .name = "future-4bit"},
    };
    for (const KindDir& dir : dirs) {
        write_mlx_model(home.models() / dir.model / "mlx" / "bbbbbbbbbbbb", {.kind = dir.kind});
    }

    // Listed: every row's kind in the JSON; in the table, one no mlx backend
    // chats with says what it is in place of a backend's summary.
    const std::map<std::string, nlohmann::json> rows = listed_by(home, "model");
    for (const KindDir& dir : dirs) {
        INFO(dir.kind);
        REQUIRE(rows.contains(dir.handle()));
        const nlohmann::json& row = rows.at(dir.handle());
        CHECK(row.at("kind") == dir.kind);
        CHECK(row.at("format") == "mlx");
        CHECK(row.at("group") == "mlx");
        CHECK(row.at("state") == "mlx");
        const std::string note = row.at("note").get<std::string>();
        if (dir.backend()) {
            CHECK(note.starts_with("llama, 4-bit (affine, group 64), 32768-token window"));
        } else {
            CHECK(note == apogee::models::describe_mlx_kind(dir.kind));
        }
    }
    std::string out;
    REQUIRE(home.run({"models", "list", "--no-color"}, &out) == 0);
    CHECK(out.find("    an embedding model: the embedding role uses it, never a chat backend\n") !=
          std::string::npos);
    CHECK(out.find("    a drafter: it runs beside its base model, never alone\n") !=
          std::string::npos);
    CHECK(out.find("    kind 'hologram', not one this apogee knows: nothing here runs it\n") !=
          std::string::npos);

    // Informed: the kind line, where it came from, and what runs it; its
    // data says the word.
    for (const KindDir& dir : dirs) {
        INFO(dir.kind);
        REQUIRE(home.run({"models", "info", dir.handle(), "-q"}, &out) == 0);
        CHECK(out.find("kind:         " + dir.kind + " (recorded) -- " +
                       apogee::models::describe_mlx_kind(dir.kind) + "\n") != std::string::npos);
        if (dir.backend()) {
            CHECK(out.find("format:       MLX -- runs on an mlx backend (Apple silicon)\n") !=
                  std::string::npos);
            CHECK(out.find("backends:     none -- 'apogee config add-backend " + dir.name +
                           " --type mlx --model-path <the path below>'\n") != std::string::npos);
            CHECK(out.find("template:     ships with the model\n") != std::string::npos);
        } else {
            CHECK(out.find("format:       MLX (Apple silicon)\n") != std::string::npos);
            CHECK(out.find("backends:     none -- no chat backend runs it; the kind line says "
                           "what does\n") != std::string::npos);
            CHECK(out.find("template:") == std::string::npos);
            CHECK(out.find("add-backend") == std::string::npos);
        }
        std::string err;
        REQUIRE(home.run({"models", "info", dir.handle(), "--output-format", "json"}, &out, &err) ==
                0);
        CHECK(nlohmann::json::parse(out).at("data").at("kind") == dir.kind);
    }

    // Scanned: a row each, and only chat and vision registerable.
    REQUIRE(home.run({"config", "scan"}, &out) == 0);
    INFO(out);
    for (const KindDir& dir : dirs) {
        INFO(dir.kind);
        const std::string line = line_starting(out, dir.name);
        REQUIRE_FALSE(line.empty());
        CHECK(line.ends_with(dir.backend() ? "  not registered"
                                           : "  not registered -- " +
                                                 apogee::models::describe_mlx_kind(dir.kind)));
    }
    REQUIRE(home.run({"config", "scan", "--register"}, &out) == 0);
    CHECK(out.find("added backend 'talk-4bit'\n") != std::string::npos);
    CHECK(out.find("added backend 'see-4bit'\n") != std::string::npos);
    CHECK(out.find("registered 2 backend(s)") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    for (const KindDir& dir : dirs) {
        INFO(dir.kind);
        CHECK(config.backends.contains(dir.name) == dir.backend());
    }
    CHECK(config.backends.at("talk-4bit").type == apogee::harness::BackendType::Mlx);
    CHECK(config.backends.at("talk-4bit").model_path ==
          (home.models() / "org--talk" / "mlx" / "bbbbbbbbbbbb").lexically_normal().string());

    // Registered: the entries' rows and info carry their directory's kind;
    // the others stay said apart, and nothing is left to register.
    const std::map<std::string, nlohmann::json> backends = listed_by(home, "backend");
    CHECK(backends.at("talk-4bit").at("kind") == "chat");
    CHECK(backends.at("see-4bit").at("kind") == "vision");
    REQUIRE(home.run({"models", "info", "see-4bit", "-q"}, &out) == 0);
    CHECK(out.find("kind:         vision (recorded) -- a vision model: an mlx backend chats with "
                   "it, and shows it images through mlx-vlm\n") != std::string::npos);
    REQUIRE(home.run({"config", "scan", "--register"}, &out) == 0);
    CHECK(line_starting(out, "talk-4bit").ends_with("  backend: talk-4bit"));
    CHECK(line_starting(out, "embed-4bit")
              .ends_with("  not registered -- " + apogee::models::describe_mlx_kind("embedding")));
    CHECK(out.find("nothing to register") != std::string::npos);
}

TEST_CASE("an entry over a kind no mlx backend chats with is listed as needing attention",
          "[commands][models][mlx][kind]") {
    // Registered by hand (`config scan` never would): its row says what the
    // directory is, so the entry's mistake is seen; a chat entry beside it
    // reads as it always did.
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    // The runtime's files, where the mlx backend runs: the ladder's answer is
    // read from them, nothing started.
    const bool apple = apogee::platform::host_target() == "macos-arm64";
    if (apple) {
        fake_mlx_runtime(home.home());
    }
    const std::filesystem::path film = home.models() / "org--film" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(film, {.kind = "video"});
    const std::filesystem::path talk = home.models() / "org--talk" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(talk, {.kind = "chat"});
    std::string out;
    for (const auto& [name, dir] : {std::pair{"film", film}, std::pair{"talk", talk}}) {
        REQUIRE(
            home.run({"config", "add-backend", name, "--type", "mlx", "--model-path", dir.string()},
                     &out) == 0);
    }
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    const std::vector<apogee::commands::ModelRow> rows =
        apogee::commands::build_model_rows(config, home.models());
    const auto video = std::ranges::find(rows, "film", &apogee::commands::ModelRow::backend);
    REQUIRE(video != rows.end());
    CHECK(video->kind == "video");
    CHECK(apogee::commands::model_row_json(*video).at("kind") == "video");
    const auto chat = std::ranges::find(rows, "talk", &apogee::commands::ModelRow::backend);
    REQUIRE(chat != rows.end());
    CHECK(chat->kind == "chat");
    CHECK_FALSE(chat->attention);
    if (apple) {
        REQUIRE(video->state == "ready");
        CHECK(video->attention);
        CHECK(video->note == apogee::models::describe_mlx_kind("video"));
        REQUIRE(chat->state == "ready");
        CHECK(chat->note.starts_with("llama, 4-bit (affine, group 64), 32768-token window"));
    } else {
        // Elsewhere the runtime's refusal is what needs fixing first.
        CHECK(video->note == chat->note);
    }
    REQUIRE(home.run({"models", "info", "film", "-q"}, &out) == 0);
    CHECK(out.find("kind:         video (recorded) -- a video model: 'apogee video generate' runs "
                   "it, never a chat backend\n") != std::string::npos);
    REQUIRE(home.run({"models", "info", "talk", "-q"}, &out) == 0);
    CHECK(out.find("kind:         chat (recorded) -- a language model: an mlx backend chats with "
                   "it\n") != std::string::npos);

    // An entry whose directory is gone claims no kind for it.
    apogee::harness::Config gone;
    apogee::harness::BackendConfig missing;
    missing.type = apogee::harness::BackendType::Mlx;
    missing.model_path = (home.models() / "org--gone" / "mlx" / "bbbbbbbbbbbb").string();
    gone.backends.emplace("gone", missing);
    const std::vector<apogee::commands::ModelRow> lost_rows =
        apogee::commands::build_model_rows(gone, home.models());
    const auto lost = std::ranges::find(lost_rows, "gone", &apogee::commands::ModelRow::backend);
    REQUIRE(lost != lost_rows.end());
    CHECK(lost->kind.empty());
    CHECK_FALSE(apogee::commands::model_row_json(*lost).contains("kind"));
    CHECK(apogee::commands::render_model_info(gone, "gone").find("kind:") == std::string::npos);
}

TEST_CASE("an MLX directory with no kind recorded lists, informs and registers as it did",
          "[commands][models][mlx][kind]") {
    // Every directory made before the record, or pulled ready-made: a chat
    // model unless its files say vision (27c's tower and processor).
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::filesystem::path plain = home.models() / "org--plain" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(plain);
    const std::filesystem::path eyes = home.models() / "org--eyes" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(eyes, {.vision = true});
    // A pull's record says where it came from, never a kind.
    const std::filesystem::path pulled = home.models() / "org--pulled" / "mlx" / "bbbbbbbbbbbb";
    write_mlx_model(pulled);
    apogee::models::Snapshot record;
    record.ref = "mlx-community/pulled-4bit";
    record.source = "huggingface";
    REQUIRE(apogee::models::write_snapshot(pulled, record));

    const std::map<std::string, nlohmann::json> rows = listed_by(home, "model");
    CHECK(rows.at("org--plain/mlx/bbbbbbbbbbbb").at("kind") == "chat");
    CHECK(rows.at("org--eyes/mlx/bbbbbbbbbbbb").at("kind") == "vision");
    CHECK(rows.at("org--pulled/mlx/bbbbbbbbbbbb").at("kind") == "chat");
    for (const auto& [handle, row] : rows) {
        if (row.contains("kind")) {
            INFO(handle);
            CHECK(row.at("note").get<std::string>().starts_with("llama, 4-bit (affine, group 64)"));
        }
    }

    std::string out;
    std::string err;
    REQUIRE(home.run({"models", "info", "org--plain/mlx/bbbbbbbbbbbb", "-q"}, &out) == 0);
    CHECK(out.find("kind:         chat (no kind recorded) -- a language model: an mlx backend "
                   "chats with it\n") != std::string::npos);
    CHECK(out.find("format:       MLX -- runs on an mlx backend (Apple silicon)\n") !=
          std::string::npos);
    REQUIRE(home.run({"models", "info", "org--eyes/mlx/bbbbbbbbbbbb", "-q"}, &out) == 0);
    CHECK(out.find("kind:         vision (inferred from its files: a vision_config and an image "
                   "processor) -- a vision model") != std::string::npos);
    REQUIRE(home.run({"models", "info", "org--pulled/mlx/bbbbbbbbbbbb", "--output-format", "json"},
                     &out, &err) == 0);
    CHECK(nlohmann::json::parse(out).at("data").at("kind") == "chat");

    // Every one registers, as M12's scan always did.
    REQUIRE(home.run({"config", "scan", "--register"}, &out) == 0);
    INFO(out);
    CHECK(out.find("registered 3 backend(s)") != std::string::npos);
    // And the backend reads images by the same file facts the kind was
    // inferred from.
    REQUIRE(home.run({"models", "info", "eyes-4bit", "-q"}, &out) == 0);
    CHECK(out.find("vision:       no -- it is a vision model, but mlx-vlm is not installed") !=
          std::string::npos);
    REQUIRE(home.run({"models", "info", "plain-4bit", "-q"}, &out) == 0);
    CHECK(out.find("vision:       no -- it is not a vision model") != std::string::npos);
}

TEST_CASE("convert --mlx records chat beside its transform, and a kind survives repair and rename",
          "[commands][models][mlx][kind][convert]") {
    Convert convert;
    std::string out;
    // mlx-lm's converter, its output carrying a vision tower and processor
    // through: what it made is a language model all the same, and its record
    // says so over its files.
    const apogee::training::MlxConverter sighted =
        [&convert](const std::filesystem::path&, const std::filesystem::path& output,
                   const apogee::training::MessageSink&,
                   const apogee::harness::CancellationToken&) {
            ++convert.calls;
            write_mlx_model(output, {.bits = 4, .vision = true});
            return std::string{};
        };
    REQUIRE(convert.run("4bit", sighted, &out) == 0);
    INFO(out);
    std::vector<apogee::models::StoredMlx> stored =
        apogee::models::list_store_mlx(convert.roots, "org--m");
    REQUIRE(stored.size() == 1);
    const std::optional<apogee::models::Snapshot> record =
        apogee::models::load_snapshot(stored.front().dir);
    REQUIRE(record.has_value());
    CHECK(record->transform == "mlx_lm.convert 4bit");
    CHECK(record->kind == "chat");
    CHECK(apogee::models::mlx_vision_files(stored.front().dir));
    const apogee::models::MlxKind converted = apogee::models::read_mlx_kind(stored.front().dir);
    CHECK(converted.word == "chat");
    CHECK(converted.source == apogee::models::MlxKindSource::Recorded);

    // A recorded embedding model beside it, as its own converter will write
    // one.
    write_mlx_model(convert.home.models() / "org--embed" / "mlx" / "bbbbbbbbbbbb",
                    {.kind = "embedding"});

    // `models repair` re-verifies against the record and never rewrites it.
    for (const std::string& handle :
         {"org--m/mlx/" + stored.front().id, std::string{"org--embed/mlx/bbbbbbbbbbbb"}}) {
        INFO(handle);
        REQUIRE(convert.home.run({"models", "repair", handle}, &out) == 0);
        CHECK(out.find("Nothing to repair.") != std::string::npos);
    }
    CHECK(apogee::models::read_mlx_kind(stored.front().dir).source ==
          apogee::models::MlxKindSource::Recorded);
    CHECK(
        apogee::models::read_mlx_kind(convert.home.models() / "org--embed" / "mlx" / "bbbbbbbbbbbb")
            .word == "embedding");

    // `models rename` moves each directory whole, its record inside.
    REQUIRE(convert.home.run({"models", "rename", "org--m", "org--renamed"}, &out) == 0);
    REQUIRE(convert.home.run({"models", "rename", "org--embed", "org--embedder"}, &out) == 0);
    stored = apogee::models::list_store_mlx(convert.roots, "org--renamed");
    REQUIRE(stored.size() == 1);
    const apogee::models::MlxKind renamed = apogee::models::read_mlx_kind(stored.front().dir);
    CHECK(renamed.word == "chat");
    CHECK(renamed.source == apogee::models::MlxKindSource::Recorded);
    const std::vector<apogee::models::StoredMlx> embedder =
        apogee::models::list_store_mlx(convert.roots, "org--embedder");
    REQUIRE(embedder.size() == 1);
    const apogee::models::MlxKind moved = apogee::models::read_mlx_kind(embedder.front().dir);
    CHECK(moved.word == "embedding");
    CHECK(moved.source == apogee::models::MlxKindSource::Recorded);
    std::string err;
    REQUIRE(convert.home.run(
                {"models", "info", "org--embedder/mlx/bbbbbbbbbbbb", "--output-format", "json"},
                &out, &err) == 0);
    CHECK(nlohmann::json::parse(out).at("data").at("kind") == "embedding");
}

// ---- sets that see or hear (53f) ------------------------------------------------------

namespace {

/// The stub `mlx_vlm` laid beside the stub `mlx_lm` that `fake_mlx_runtime`
/// laid (what `train setup --with mlx-vlm` installs), its packages building
/// `gemma4`, `gemma4_unified`, `qwen3_5` and `qwen3_vl`. Its site-packages.
std::filesystem::path fake_mlx_vlm(const std::filesystem::path& home) {
    const std::filesystem::path stubs =
        std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts" / "stub_mlx_vlm";
    const std::filesystem::path site =
        home / "training" / "venv" / "lib" / "python3.99" / "site-packages";
    std::filesystem::copy(stubs, site,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing);
    return site;
}

/// A full-weight set at `dir` whose `config.json` is `config` -- one of the
/// user's (`support/towered_configs.h`) -- with the image processor a
/// release that sees ships beside it.
void towered_snapshot(const std::filesystem::path& dir, const char* config) {
    write_mlx_model(dir, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(dir / "config.json", config);
    apogee::testing::write_fixture_file(dir / "preprocessor_config.json", "{}");
}

[[nodiscard]] std::vector<nlohmann::json> converter_calls(const std::filesystem::path& file) {
    std::vector<nlohmann::json> out;
    std::ifstream in{file};
    for (std::string line; std::getline(in, line);) {
        out.push_back(nlohmann::json::parse(line));
    }
    return out;
}

/// The one MLX build of `model` made with `transform`, or an empty path.
[[nodiscard]] std::filesystem::path mlx_made_by(const StoreRoots& roots, const std::string& model,
                                                const std::string& transform) {
    for (const apogee::models::StoredMlx& stored : apogee::models::list_store_mlx(roots, model)) {
        const std::optional<apogee::models::Snapshot> record =
            apogee::models::load_snapshot(stored.dir);
        if (record.has_value() && record->transform == transform) {
            return stored.dir;
        }
    }
    return {};
}

}  // namespace

TEST_CASE(
    "a set with towers converts through mlx-vlm, kept and recorded vision; a language model as "
    "before; --text-only asks for mlx-lm's build",
    "[commands][models][mlx][convert][driver][towers]") {
    if (apogee::platform::host_target() != "macos-arm64" ||
        apogee::platform::find_on_path("python3").empty()) {
        SKIP("the mlx runtime is Apple silicon's, and the driver needs a python3");
    }
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    fake_mlx_runtime(home.home());
    (void)fake_mlx_vlm(home.home());
    const StoreRoots roots = StoreRoots::at(home.models());
    const std::string vl = "Qwen3-VL-8B-Instruct/safetensors/63f2634a2019";
    towered_snapshot(home.models() / vl, apogee::testing::kQwen3VlConfig);
    const std::string gemma = "Gemma4-E4B-Instruct/safetensors/aaaaaaaaaaaa";
    towered_snapshot(home.models() / gemma, apogee::testing::kGemma4Config);
    const std::string llama = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f";
    write_mlx_model(home.models() / llama, {.bits = 0, .format = "pt"});
    apogee::testing::write_fixture_file(home.models() / llama / "config.json",
                                        apogee::testing::kLlamaConfig);
    const std::filesystem::path calls_file = home.home() / "calls.jsonl";
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD", calls_file.string()};
    std::string out;

    // Qwen3-VL: mlx-vlm's converter, its tower kept, recorded vision.
    REQUIRE(home.run({"models", "convert", vl, "--mlx"}, &out) == 0);
    INFO(out);
    CHECK(out.find(") -- mlx-vlm's converter, its towers kept (vision_config), in Apogee's "
                   "Python environment\n") != std::string::npos);
    CHECK(out.find("kind: vision (recorded) -- a vision model: an mlx backend chats with it, "
                   "and shows it images through mlx-vlm\n") != std::string::npos);
    CHECK(out.find("[INFO]") == std::string::npos);  // the library's print stays off
    std::vector<nlohmann::json> calls = converter_calls(calls_file);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["converter"] == "mlx_vlm");
    CHECK(calls[0]["q_bits"] == 4);
    CHECK(calls[0]["q_group_size"] == 64);
    CHECK(calls[0]["trust_remote_code"] == false);
    const std::filesystem::path seeing =
        mlx_made_by(roots, "Qwen3-VL-8B-Instruct", "mlx_vlm.convert 4bit");
    REQUIRE_FALSE(seeing.empty());
    CHECK(apogee::models::load_snapshot(seeing)->kind == "vision");
    const apogee::models::MlxKind kind = apogee::models::read_mlx_kind(seeing);
    CHECK(kind.word == "vision");
    CHECK(kind.source == apogee::models::MlxKindSource::Recorded);
    CHECK(apogee::models::mlx_vision_files(seeing));

    // --text-only where the mlx-lm installed builds no qwen3_vl: refused
    // before Python, mlx-vlm's way named.
    CHECK(home.run({"models", "convert", vl, "--mlx", "--text-only"}, &out) != 0);
    CHECK(out.find("'" + vl +
                   "' is a qwen3_vl model, which the mlx-lm in Apogee's Python environment "
                   "builds no model for") != std::string::npos);
    CHECK(out.find(" -- without --text-only, mlx-vlm builds it with its towers kept") !=
          std::string::npos);
    CHECK(converter_calls(calls_file).size() == 1);

    // Gemma 4: its language model alone with --text-only, mlx-lm's and
    // recorded chat; its towers kept without -- two builds, each its own.
    REQUIRE(home.run({"models", "convert", gemma, "--mlx", "--text-only"}, &out) == 0);
    CHECK(out.find(") -- mlx-lm's converter, its language model alone (--text-only: "
                   "vision_config, audio_config left out), in Apogee's Python environment\n") !=
          std::string::npos);
    REQUIRE(home.run({"models", "convert", gemma, "--mlx"}, &out) == 0);
    calls = converter_calls(calls_file);
    REQUIRE(calls.size() == 3);
    CHECK_FALSE(calls[1].contains("converter"));  // the stub mlx-lm's
    CHECK(calls[1]["model_type"] == "gemma4");
    CHECK(calls[2]["converter"] == "mlx_vlm");
    const std::filesystem::path text =
        mlx_made_by(roots, "Gemma4-E4B-Instruct", "mlx_lm.convert 4bit");
    const std::filesystem::path whole =
        mlx_made_by(roots, "Gemma4-E4B-Instruct", "mlx_vlm.convert 4bit");
    REQUIRE_FALSE(text.empty());
    REQUIRE_FALSE(whole.empty());
    CHECK(apogee::models::read_mlx_kind(text).word == "chat");
    CHECK(apogee::models::read_mlx_kind(whole).word == "vision");
    // Each recognised as itself before anything runs.
    for (const std::vector<std::string>& again :
         {std::vector<std::string>{"models", "convert", gemma, "--mlx", "--text-only"},
          std::vector<std::string>{"models", "convert", gemma, "--mlx"}}) {
        REQUIRE(home.run(again, &out) == 0);
        CHECK(out.find("already converted:") != std::string::npos);
    }
    CHECK(converter_calls(calls_file).size() == 3);

    // A language model: mlx-lm's, as it always was.
    REQUIRE(home.run({"models", "convert", llama, "--mlx"}, &out) == 0);
    CHECK(out.find(") -- mlx-lm's converter, in Apogee's Python environment\n") !=
          std::string::npos);
    CHECK(out.find("kind:") == std::string::npos);
    calls = converter_calls(calls_file);
    REQUIRE(calls.size() == 4);
    CHECK_FALSE(calls[3].contains("converter"));
    CHECK(calls[3]["quantize"] == true);
    CHECK(calls[3]["q_bits"] == 4);
    CHECK(calls[3]["q_group_size"] == 64);
    CHECK(calls[3]["q_mode"] == "affine");
    const std::filesystem::path chat =
        mlx_made_by(roots, "Llama3.2-1B-Instruct", "mlx_lm.convert 4bit");
    REQUIRE_FALSE(chat.empty());
    CHECK(apogee::models::load_snapshot(chat)->kind == "chat");
    // --text-only leaves nothing out of it, and says so.
    REQUIRE(home.run({"models", "convert", llama, "--mlx", "--text-only"}, &out) == 0);
    CHECK(out.find("the snapshot declares no vision or audio tower -- --text-only changes "
                   "nothing; converting it as it is") != std::string::npos);
    CHECK(out.find("already converted:") != std::string::npos);

    // Without --mlx the flag means nothing, and is refused saying why.
    CHECK(home.run({"models", "convert", gemma, "--text-only"}, &out) != 0);
    CHECK(out.find("--text-only asks --mlx for mlx-lm's build of a model's language model "
                   "alone") != std::string::npos);

    // Registered, each build is the backend its kind is.
    REQUIRE(home.run({"models", "convert", gemma, "--mlx", "--text-only", "--register",
                      "--base-name", "gemma-text"},
                     &out) == 0);
    REQUIRE(home.run({"models", "convert", gemma, "--mlx", "--register", "--base-name", "gemma"},
                     &out) == 0);
    REQUIRE(home.run({"models", "info", "gemma-text-4bit", "-q"}, &out) == 0);
    CHECK(out.find("kind:         chat (recorded)") != std::string::npos);
    REQUIRE(home.run({"models", "info", "gemma-4bit", "-q"}, &out) == 0);
    CHECK(out.find("kind:         vision (recorded)") != std::string::npos);
    CHECK(converter_calls(calls_file).size() == 4);  // both found stored
}

TEST_CASE(
    "a set with towers is offered MLX by the installed mlx-vlm's own packages, else mlx-lm's "
    "text-only build, said -- the omni model refused before Python where neither builds it",
    "[commands][models][mlx][convert][driver][towers]") {
    if (apogee::platform::host_target() != "macos-arm64" ||
        apogee::platform::find_on_path("python3").empty()) {
        SKIP("the mlx runtime is Apple silicon's, and the driver needs a python3");
    }
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
    const std::string omni = "Qwen3-Omni-30B-A3B-Instruct/safetensors/bbbbbbbbbbbb";
    towered_snapshot(home.models() / omni, apogee::testing::kQwen3OmniConfig);
    const std::string gemma = "Gemma4-E4B-Instruct/safetensors/cccccccccccc";
    towered_snapshot(home.models() / gemma, apogee::testing::kGemma4Config);
    std::string out;
    std::string err;
    const auto data = [&](const std::string& handle) {
        REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &out, &err) == 0);
        return nlohmann::json::parse(out)["data"];
    };
    const auto route = [](const nlohmann::json& towers, const nlohmann::json& converter,
                          bool text_only) {
        return nlohmann::json{
            {"towers", towers}, {"converter", converter}, {"text_only", text_only}};
    };
    const nlohmann::json thinker = nlohmann::json::array({"thinker_config"});
    const nlohmann::json both = nlohmann::json::array({"vision_config", "audio_config"});

    // Nothing installed to ask: nothing decided (M21), its towers the default.
    CHECK(data(omni)["converts_to"] == nlohmann::json::array({"gguf", "mlx"}));
    CHECK(data(omni)["mlx_conversion"] == route(thinker, "mlx-vlm", true));

    // mlx-lm alone, which builds gemma4 and no qwen3_omni_moe.
    fake_mlx_runtime(home.home());
    CHECK(data(omni)["converts_to"] == nlohmann::json::array({"gguf"}));
    CHECK(data(omni)["mlx_conversion"].is_null());
    REQUIRE(home.run({"models", "info", omni}, &out) == 0);
    CHECK(out.find("towers:       thinker_config -- no MLX conversion: mlx-vlm is not installed "
                   "in Apogee's Python environment, and the mlx-lm there builds no "
                   "qwen3_omni_moe model\n") != std::string::npos);
    CHECK(out.find("makes a GGUF of it\n") != std::string::npos);
    const std::filesystem::path calls_file = home.home() / "calls.jsonl";
    const apogee::testing::EnvGuard record{"STUB_MLX_CONVERT_RECORD", calls_file.string()};
    CHECK(home.run({"models", "convert", omni, "--mlx"}, &out) != 0);
    CHECK(out.find("'" + omni +
                   "' declares towers (thinker_config): its MLX build keeps them through "
                   "mlx-vlm, which is not installed in Apogee's Python environment -- apogee "
                   "train setup --with mlx-vlm") != std::string::npos);
    CHECK(out.find("--text-only") == std::string::npos);  // mlx-lm cannot build it either
    // Gemma 4: its language model alone is offered, and said.
    CHECK(data(gemma)["converts_to"] == nlohmann::json::array({"gguf", "mlx"}));
    CHECK(data(gemma)["mlx_conversion"] == route(both, nullptr, true));
    REQUIRE(home.run({"models", "info", gemma}, &out) == 0);
    CHECK(out.find("towers:       vision_config, audio_config -- mlx-vlm is not installed in "
                   "Apogee's Python environment, so --mlx --text-only builds its language model "
                   "alone with mlx-lm; 'apogee train setup --with mlx-vlm' keeps them\n") !=
          std::string::npos);
    CHECK(out.find("makes a GGUF of it (--mlx --text-only an MLX model of its language model)\n") !=
          std::string::npos);
    CHECK(home.run({"models", "convert", gemma, "--mlx"}, &out) != 0);
    CHECK(out.find("which is not installed in Apogee's Python environment -- apogee train setup "
                   "--with mlx-vlm, or build its language model alone with mlx-lm: --text-only") !=
          std::string::npos);

    // mlx-vlm beside it, building gemma4 and no qwen3_omni_moe.
    const std::filesystem::path site = fake_mlx_vlm(home.home());
    CHECK(data(gemma)["mlx_conversion"] == route(both, "mlx-vlm", true));
    REQUIRE(home.run({"models", "info", gemma}, &out) == 0);
    CHECK(out.find("towers:       vision_config, audio_config -- --mlx builds it through mlx-vlm, "
                   "its towers kept; --mlx --text-only builds its language model alone with "
                   "mlx-lm\n") != std::string::npos);
    CHECK(data(omni)["converts_to"] == nlohmann::json::array({"gguf"}));
    CHECK(home.run({"models", "convert", omni, "--mlx"}, &out) != 0);
    CHECK(out.find("'" + omni +
                   "' is a qwen3_omni_moe model with towers (thinker_config), which the mlx-vlm "
                   "in Apogee's Python environment builds no model for -- convert it without "
                   "--mlx, or, if these weights are a type this mlx-vlm knows, convert them as "
                   "it: --model-type <type> (nearest here: qwen3_5, qwen3_vl, qwen3_vl_moe)") !=
          std::string::npos);
    CHECK(home.run({"models", "convert", omni, "--mlx", "--model-type", "nosuch"}, &out) != 0);
    CHECK(out.find("--model-type nosuch is no type the mlx-vlm in Apogee's Python environment "
                   "builds") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(calls_file));  // no converter ever started
    // --model-type completes from the converter the line's set routes to.
    REQUIRE(home.run({"__complete", "models", "convert", omni, "--mlx", "--model-type", "qwen3"},
                     &out) == 0);
    CHECK(out == "qwen3_5\nqwen3_vl\nqwen3_vl_moe\n");
    REQUIRE(home.run({"__complete", "models", "convert", gemma, "--mlx", "--text-only",
                      "--model-type", "gemma4"},
                     &out) == 0);
    CHECK(out == "gemma4\ngemma4_text\ngemma4_unified\n");

    // An mlx-vlm that builds qwen3_omni_moe: offered, and converted whole.
    apogee::testing::write_fixture_file(
        site / "mlx_vlm" / "models" / "qwen3_omni_moe" / "__init__.py",
        "from .qwen3_omni_moe import Model\n");
    CHECK(data(omni)["converts_to"] == nlohmann::json::array({"gguf", "mlx"}));
    CHECK(data(omni)["mlx_conversion"] == route(thinker, "mlx-vlm", false));
    REQUIRE(home.run({"models", "convert", omni, "--mlx"}, &out) == 0);
    INFO(out);
    const std::vector<nlohmann::json> calls = converter_calls(calls_file);
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["converter"] == "mlx_vlm");
    CHECK(calls[0]["model_type"] == "qwen3_omni_moe");
    const std::filesystem::path made = mlx_made_by(
        StoreRoots::at(home.models()), "Qwen3-Omni-30B-A3B-Instruct", "mlx_vlm.convert 4bit");
    REQUIRE_FALSE(made.empty());
    CHECK(apogee::models::read_mlx_kind(made).word == "vision");

    // A language model's document says its route too: mlx-lm's, no towers.
    const std::string llama = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f";
    write_mlx_model(home.models() / llama, {.bits = 0, .format = "pt"});
    CHECK(data(llama)["mlx_conversion"] == route(nlohmann::json::array(), "mlx-lm", false));
    // What conversions make has none.
    const std::vector<apogee::models::StoredMlx> stored = apogee::models::list_store_mlx(
        StoreRoots::at(home.models()), "Qwen3-Omni-30B-A3B-Instruct");
    REQUIRE(stored.size() == 1);
    CHECK(data("Qwen3-Omni-30B-A3B-Instruct/mlx/" + stored.front().id)["mlx_conversion"].is_null());
}
