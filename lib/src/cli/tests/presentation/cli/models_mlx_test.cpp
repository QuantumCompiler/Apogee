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
#include "support/mlx_model.h"
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
        std::string drop;
        /// This file is served with other bytes than its digest names.
        std::string corrupt;
        /// This file is served cut short -- its size and digest agreeing.
        std::string truncate;
        /// This file's download meets a Ctrl-C: the token is cancelled.
        std::string cancel;
        apogee::harness::CancellationToken token;
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
            n += url.find("/resolve/") != std::string::npos ? 1 : 0;
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
    for (const std::string& name : {"config.json", "model-00002-of-00002.safetensors"}) {
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
