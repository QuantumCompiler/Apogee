#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/models_pull.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "support/cli_home.h"
#include "support/wan_original.h"
#include "transport/http_client.h"

/// Wan's original layout pulled whole (53j), end to end in process: the
/// user's own set -- its record of ten files and its config, as the pull
/// before 53j left them -- in a store, and a fake Hugging Face serving the
/// repository with its two root checkpoints through the pull's own seam (the
/// client it is handed), the listing with its sizes and LFS digests and each
/// file. Then `models info`, the conversion's refusal and the pull itself
/// over them, and a Llama repository's pull beside them. Small fake bytes
/// stand in for the weights; no network.
namespace {

using apogee::backends::HttpClient;
using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;
using apogee::models::StoreRoots;
using apogee::testing::CliHome;
using apogee::testing::kWanTextEncoder;
using apogee::testing::kWanVae;

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A repository as Hugging Face serves it: its tree, recursive, each LFS
/// file with its sha256, and each file's bytes under `resolve/main/`.
struct Repository {
    std::string id;
    /// Every file in the tree's order, with its bytes.
    std::vector<std::pair<std::string, std::string>> files;
    /// Served for a file in place of its bytes, the listing unchanged.
    std::map<std::string, std::string> tampered{};
};

[[nodiscard]] bool lfs(const std::string& path) {
    return path.ends_with(".safetensors") || path.ends_with(".pth") ||
           path.ends_with("spiece.model") || path.ends_with("tokenizer.json");
}

class FixtureSource final : public apogee::backends::HttpTransport {
public:
    explicit FixtureSource(std::vector<Repository> repositories)
        : repositories_{std::move(repositories)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        cancellation.throw_if_cancelled();
        HttpResponse response;
        response.status = 404;
        const auto reply = [&](const std::string& body) {
            response.status = 200;
            if (sink) {
                (void)sink(body);
            } else {
                response.body = body;
            }
        };
        for (const Repository& repo : repositories_) {
            const std::string prefix = "https://huggingface.co/" + repo.id + "/resolve/main/";
            if (request.url ==
                "https://huggingface.co/api/models/" + repo.id + "/tree/main?recursive=true") {
                nlohmann::json tree = nlohmann::json::array();
                for (const auto& [path, bytes] : repo.files) {
                    nlohmann::json item{{"type", "file"}, {"path", path}, {"size", bytes.size()}};
                    if (lfs(path)) {
                        item["lfs"] = {{"oid", apogee::models::sha256_hex(bytes)},
                                       {"size", bytes.size()}};
                    }
                    tree.push_back(item);
                }
                reply(tree.dump());
            } else if (request.url.starts_with(prefix)) {
                const std::string path = request.url.substr(prefix.size());
                const auto file = std::ranges::find(repo.files, path,
                                                    &std::pair<std::string, std::string>::first);
                if (file != repo.files.end()) {
                    ++fetched[path];
                    const auto bad = repo.tampered.find(path);
                    reply(bad != repo.tampered.end() ? bad->second : file->second);
                }
            }
        }
        return response;
    }

    /// How often each file was fetched -- a download, or `config.json` read
    /// whole to know the layout.
    std::map<std::string, int> fetched;

    std::vector<Repository> repositories_;
};

/// The user's repository: its tree with the two checkpoints, every file's
/// bytes the fixture's.
[[nodiscard]] Repository wan_repository() {
    Repository repo{.id = std::string{apogee::testing::kWanRepo}, .files = {}};
    for (const std::string& path : apogee::testing::wan_repository_files()) {
        repo.files.emplace_back(path, apogee::testing::wan_bytes(path));
    }
    return repo;
}

/// Llama 3.2 1B Instruct's tree, `original/`'s checkpoint and all -- and,
/// with `root_pth`, a checkpoint at its root beside its config, which Wan's
/// layout has and a language model's does not decide by.
[[nodiscard]] Repository llama_repository(bool root_pth = false) {
    Repository repo{.id = "meta-llama/Llama-3.2-1B-Instruct", .files = {}};
    const auto add = [&repo](std::string path, std::string bytes) {
        repo.files.emplace_back(std::move(path), std::move(bytes));
    };
    add(".gitattributes", "*.safetensors filter=lfs\n");
    add("LICENSE.txt", "LLAMA 3.2 COMMUNITY LICENSE\n");
    add("README.md", "a model card\n");
    add("USE_POLICY.md", "a policy\n");
    add("config.json", R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
    if (root_pth) {
        add("consolidated.00.pth", "fake checkpoint bytes\n");
    }
    add("generation_config.json", R"({"do_sample": true})");
    add("model.safetensors", "fake shard bytes\n");
    add("original/consolidated.00.pth", "fake checkpoint bytes\n");
    add("original/params.json", R"({"dim": 2048})");
    add("original/tokenizer.model", "fake tokenizer bytes\n");
    add("special_tokens_map.json", "{}");
    add("tokenizer.json", R"({"model": {"type": "BPE"}})");
    add("tokenizer_config.json", "{}");
    return repo;
}

struct Pull {
    CliHome home{"backends:\n  m:\n    type: mock\n"};
    StoreRoots roots = StoreRoots::at(home.models());
    FixtureSource* source = nullptr;
    std::unique_ptr<HttpClient> client;

    explicit Pull(std::vector<Repository> repositories) {
        REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
        auto transport = std::make_unique<FixtureSource>(std::move(repositories));
        source = transport.get();
        client = std::make_unique<HttpClient>(std::move(transport));
    }

    /// `models pull <ref> --safetensors` over the fixture source: its exit
    /// code, what it printed into `out`, and what it pulled.
    int run(const std::string& ref, std::string* out,
            apogee::commands::PulledSnapshot* pulled = nullptr) const {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        int code = 0;
        try {
            apogee::commands::PulledSnapshot made =
                apogee::commands::pull_snapshot(ref, roots, *client, {});
            if (pulled != nullptr) {
                *pulled = std::move(made);
            }
        } catch (const CLI::RuntimeError& e) {
            code = e.get_exit_code();
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *out = captured.str();
        return code;
    }

    /// `models info <handle> --output-format json`'s `data`.
    [[nodiscard]] nlohmann::json data(const std::string& handle) const {
        std::string out;
        std::string err;
        REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &out, &err) == 0);
        return nlohmann::json::parse(out)["data"];
    }
};

/// The files under `dir` other than its record, sorted.
[[nodiscard]] std::vector<std::string> files_in(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path() != apogee::models::snapshot_record_path(dir)) {
            out.push_back(entry.path().lexically_relative(dir).generic_string());
        }
    }
    std::ranges::sort(out);
    return out;
}

[[nodiscard]] std::vector<std::string> recorded_paths(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    const std::optional<apogee::models::Snapshot> record = apogee::models::load_snapshot(dir);
    REQUIRE(record.has_value());
    for (const apogee::models::SnapshotFile& file : record->files) {
        out.push_back(file.path);
    }
    std::ranges::sort(out);
    return out;
}

[[nodiscard]] std::vector<std::string> sorted(std::vector<std::string> paths) {
    std::ranges::sort(paths);
    return paths;
}

[[nodiscard]] std::vector<std::string> user_ten() {
    return {apogee::testing::kWanRecorded.begin(), apogee::testing::kWanRecorded.end()};
}

[[nodiscard]] std::vector<std::string> user_twelve() {
    std::vector<std::string> twelve = user_ten();
    twelve.emplace_back(kWanTextEncoder);
    twelve.emplace_back(kWanVae);
    return sorted(twelve);
}

const std::string kMissingLine =
    "diffusion:    Wan's original layout (WanModel), missing models_t5_umt5-xxl-enc-bf16.pth and "
    "Wan2.2_VAE.pth -- 'apogee models pull Wan-AI/Wan2.2-TI2V-5B --safetensors' fetches them "
    "into this set; neither converter takes it -- llama.cpp's finds a model by its "
    "architectures, and mlx-lm's builds a language model; what runs this model today is its "
    "Diffusers pipeline in 'apogee video generate', pulled whole with 'apogee models pull "
    "Wan-AI/Wan2.2-TI2V-5B-Diffusers'\n";

const std::string kCompleteLine =
    "diffusion:    Wan's original layout (WanModel), complete -- its transformer beside "
    "models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth; neither converter takes it -- "
    "llama.cpp's finds a model by its architectures, and mlx-lm's builds a language model; what "
    "runs this model today is its Diffusers pipeline in 'apogee video generate', pulled whole "
    "with 'apogee models pull Wan-AI/Wan2.2-TI2V-5B-Diffusers'\n";

}  // namespace

TEST_CASE("the user's Wan set pulled again gains exactly its two checkpoints, in place, verified",
          "[commands][models][wan][pull]") {
    const Pull pull{{wan_repository()}};
    const std::filesystem::path dir = apogee::testing::write_user_wan_set(pull.roots);
    const std::string id = dir.filename().string();
    const std::string handle = "Wan-AI--Wan2.2-TI2V-5B/safetensors/" + id;

    // Before: `models info` names the two missing and the way, in its text
    // and its data.
    std::string out;
    REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &out) == 0);
    INFO(out);
    CHECK(out.find(kMissingLine) != std::string::npos);
    CHECK(out.find("record:       10 file(s) on record\n") != std::string::npos);
    nlohmann::json data = pull.data(handle);
    CHECK(data["wan_layout"] ==
          nlohmann::json{{"class", "WanModel"},
                         {"whole", false},
                         {"missing", {std::string{kWanTextEncoder}, std::string{kWanVae}}}});
    CHECK(data["converts_to"] == nlohmann::json::array());

    // Pulled again: only what its record lacks moves, into the same set.
    apogee::commands::PulledSnapshot pulled;
    REQUIRE(pull.run(std::string{apogee::testing::kWanRepo}, &out, &pulled) == 0);
    CHECK(pulled.dir == dir);
    CHECK(pulled.id == id);
    CHECK(pulled.completed ==
          std::vector<std::string>{std::string{kWanVae}, std::string{kWanTextEncoder}});
    CHECK(out.starts_with("already here -- these exact weights are at\n  " + dir.string() +
                          "\nits record lacks 2 file(s) this pull takes, "));
    CHECK(out.find("completed: Wan2.2_VAE.pth, models_t5_umt5-xxl-enc-bf16.pth\nverified: 2 "
                   "file(s), ") != std::string::npos);
    CHECK(out.find(", 2 checked against a published sha256 -- the set keeps its id\n") !=
          std::string::npos);
    CHECK(out.find("\nThe set is Wan's original layout (WanModel), complete -- ") !=
          std::string::npos);
    // The two downloaded once each, the config read once to know the layout,
    // and nothing else.
    CHECK(pull.source->fetched == std::map<std::string, int>{{"config.json", 1},
                                                             {std::string{kWanVae}, 1},
                                                             {std::string{kWanTextEncoder}, 1}});

    // The same set, under the same id: the ten as they were, the two beside
    // them byte for byte, all twelve on its record with their sizes and
    // sha256 -- what `models repair` judges by.
    const std::vector<apogee::models::StoredSnapshot> stored =
        apogee::models::list_store_snapshots(pull.roots, apogee::testing::kWanModel);
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().dir == dir);
    CHECK(files_in(dir) == user_twelve());
    CHECK(recorded_paths(dir) == user_twelve());
    const std::optional<apogee::models::Snapshot> record = apogee::models::load_snapshot(dir);
    REQUIRE(record.has_value());
    for (const std::string_view checkpoint : {kWanTextEncoder, kWanVae}) {
        const std::string bytes = apogee::testing::wan_bytes(checkpoint);
        CHECK(read_file(dir / checkpoint) == bytes);
        const auto entry = std::ranges::find(record->files, std::string{checkpoint},
                                             &apogee::models::SnapshotFile::path);
        REQUIRE(entry != record->files.end());
        CHECK(entry->size == static_cast<std::int64_t>(bytes.size()));
        CHECK(entry->sha256 == apogee::models::sha256_hex(bytes));
    }
    CHECK(apogee::models::find_snapshot_damage(dir).empty());
    CHECK(apogee::models::snapshot_weight_id(record->files) == id);

    // After: complete, said so in the text and the data.
    REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &out) == 0);
    CHECK(out.find(kCompleteLine) != std::string::npos);
    CHECK(out.find("record:       12 file(s) on record\n") != std::string::npos);
    data = pull.data(handle);
    CHECK(data["wan_layout"] == nlohmann::json{{"class", "WanModel"},
                                               {"whole", true},
                                               {"missing", nlohmann::json::array()}});

    // And pulled again: nothing to fetch.
    pull.source->fetched.clear();
    REQUIRE(pull.run(std::string{apogee::testing::kWanRepo}, &out, &pulled) == 0);
    CHECK(out == "already here -- these exact weights are at\n  " + dir.string() + "\n");
    CHECK(pulled.completed.empty());
    CHECK(pull.source->fetched == std::map<std::string, int>{{"config.json", 1}});
}

TEST_CASE("a fresh pull of Wan's original layout takes its two checkpoints from the start",
          "[commands][models][wan][pull]") {
    const Pull pull{{wan_repository()}};
    std::string out;
    apogee::commands::PulledSnapshot pulled;
    REQUIRE(pull.run(std::string{apogee::testing::kWanRepo}, &out, &pulled) == 0);
    INFO(out);
    CHECK(
        out.starts_with("Wan's original layout (WanModel): its root checkpoints come too -- "
                        "Wan2.2_VAE.pth, models_t5_umt5-xxl-enc-bf16.pth\ndownloading 12 "
                        "file(s), "));
    CHECK(pulled.completed.empty());
    // Its id is its shards' hash: the user's set's, checkpoints or not.
    CHECK(pulled.id == apogee::models::snapshot_weight_id(apogee::testing::wan_record().files));
    CHECK(files_in(pulled.dir) == user_twelve());
    CHECK(recorded_paths(pulled.dir) == user_twelve());
    CHECK(out.find("verified: 12 file(s), ") != std::string::npos);
    CHECK(out.find("\nThe set is Wan's original layout (WanModel), complete -- ") !=
          std::string::npos);
    // The make-a-GGUF hint is a language model's, never this set's.
    CHECK(out.find("make a GGUF of it") == std::string::npos);
    const std::optional<apogee::models::WanLayout> layout =
        apogee::models::snapshot_wan_layout(pulled.dir);
    REQUIRE(layout.has_value());
    CHECK(layout->whole());
    for (const std::string& path : user_twelve()) {
        INFO(path);
        CHECK(pull.source->fetched[path] == (path == "config.json" ? 2 : 1));
    }
}

TEST_CASE(
    "a checkpoint that is not what the repository published never lands; pulled again, "
    "only the rest moves",
    "[commands][models][wan][pull]") {
    Repository repo = wan_repository();
    repo.tampered[std::string{kWanTextEncoder}] =
        "fake bytes of models_t5_umt5-xxl-enc-bf16.pth, never weightz\n";
    const Pull pull{{repo}};
    const std::filesystem::path dir = apogee::testing::write_user_wan_set(pull.roots);
    std::string out;
    CHECK(pull.run(std::string{apogee::testing::kWanRepo}, &out) == 1);
    INFO(out);
    CHECK(out.find("apogee models: models_t5_umt5-xxl-enc-bf16.pth: digest mismatch") !=
          std::string::npos);
    CHECK(out.find("(Wan2.2_VAE.pth landed and stays on the set's record; pull again for the "
                   "rest)") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir / kWanTextEncoder));
    std::vector<std::string> eleven = user_ten();
    eleven.emplace_back(kWanVae);
    CHECK(files_in(dir) == sorted(eleven));
    CHECK(recorded_paths(dir) == sorted(eleven));

    // The repository mended: the next pull fetches the encoder alone.
    pull.source->repositories_.front().tampered.clear();
    pull.source->fetched.clear();
    apogee::commands::PulledSnapshot pulled;
    REQUIRE(pull.run(std::string{apogee::testing::kWanRepo}, &out, &pulled) == 0);
    CHECK(pulled.completed == std::vector<std::string>{std::string{kWanTextEncoder}});
    CHECK(pull.source->fetched ==
          std::map<std::string, int>{{"config.json", 1}, {std::string{kWanTextEncoder}, 1}});
    CHECK(recorded_paths(dir) == user_twelve());
    CHECK(apogee::models::snapshot_wan_layout(dir)->whole());
}

TEST_CASE(
    "a Llama repository's pull keeps its file list exactly, a root checkpoint beside its "
    "config or not",
    "[commands][models][wan][pull]") {
    // What the snapshot rule took before 53j, its licence text among them:
    // never a PyTorch checkpoint or the model card.
    const std::vector<std::string> expected{"LICENSE.txt",
                                            "config.json",
                                            "generation_config.json",
                                            "model.safetensors",
                                            "original/params.json",
                                            "original/tokenizer.model",
                                            "special_tokens_map.json",
                                            "tokenizer.json",
                                            "tokenizer_config.json"};
    for (const bool root_pth : {false, true}) {
        INFO("a root checkpoint: " << root_pth);
        const Pull pull{{llama_repository(root_pth)}};
        std::string out;
        apogee::commands::PulledSnapshot pulled;
        REQUIRE(pull.run("meta-llama/Llama-3.2-1B-Instruct", &out, &pulled) == 0);
        INFO(out);
        CHECK(files_in(pulled.dir) == expected);
        CHECK(recorded_paths(pulled.dir) == expected);
        CHECK(out.find("Wan's original layout") == std::string::npos);
        CHECK(out.find("make a GGUF of it") != std::string::npos);
        // Its config read to know the layout only where a root checkpoint
        // could make it one; nothing else asked of the repository.
        CHECK(pull.source->fetched["config.json"] == (root_pth ? 2 : 1));
        CHECK(pull.source->fetched.count("consolidated.00.pth") == 0);
        CHECK(pull.source->fetched.count("original/consolidated.00.pth") == 0);
        const std::string handle = "meta-llama--Llama-3.2-1B-Instruct/safetensors/" + pulled.id;
        CHECK(pull.data(handle)["wan_layout"].is_null());
    }
}

TEST_CASE(
    "a stored set of any other repository pulled again is left as it is, though its record "
    "lacks a file the listing now has",
    "[commands][models][wan][pull]") {
    // Completing a set in place is Wan's original layout's alone (53j): any
    // other set already stored is found and left exactly as before.
    for (const bool root_pth : {false, true}) {
        INFO("a root checkpoint: " << root_pth);
        const Pull pull{{llama_repository(root_pth)}};
        std::string out;
        apogee::commands::PulledSnapshot pulled;
        REQUIRE(pull.run("meta-llama/Llama-3.2-1B-Instruct", &out, &pulled) == 0);
        const std::filesystem::path dir = pulled.dir;
        const std::string handle = "meta-llama--Llama-3.2-1B-Instruct/safetensors/" + pulled.id;
        const std::string record = read_file(apogee::models::snapshot_record_path(dir));
        const std::vector<std::string> files = files_in(dir);
        std::string info;
        REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &info) == 0);
        const nlohmann::json data = pull.data(handle);

        // The repository gains a file the snapshot rule takes, its shards as
        // they were: the same set, its record lacking the file.
        pull.source->repositories_.front().files.emplace_back("chat_template.jinja",
                                                              "{{ messages }}");
        pull.source->fetched.clear();
        REQUIRE(pull.run("meta-llama/Llama-3.2-1B-Instruct", &out, &pulled) == 0);
        INFO(out);
        CHECK(out == "already here -- these exact weights are at\n  " + dir.string() + "\n");
        CHECK(pulled.dir == dir);
        CHECK(pulled.completed.empty());
        // Nothing fetched -- the config read only where a root checkpoint
        // could make the repository Wan's -- and nothing changed.
        CHECK(pull.source->fetched.count("chat_template.jinja") == 0);
        CHECK(pull.source->fetched == (root_pth ? std::map<std::string, int>{{"config.json", 1}}
                                                : std::map<std::string, int>{}));
        CHECK(read_file(apogee::models::snapshot_record_path(dir)) == record);
        CHECK(files_in(dir) == files);
        std::string again;
        REQUIRE(pull.home.run({"models", "info", handle, "-q"}, &again) == 0);
        CHECK(again == info);
        CHECK(pull.data(handle) == data);
    }
}

TEST_CASE(
    "the conversion's refusal of the user's Wan set says what it is, what it lacks and what "
    "runs it",
    "[commands][models][wan][convert][diffusion]") {
    const Pull pull{{wan_repository()}};
    const std::filesystem::path dir = apogee::testing::write_user_wan_set(pull.roots);
    const std::string handle = "Wan-AI--Wan2.2-TI2V-5B/safetensors/" + dir.filename().string();
    const std::string refused =
        "apogee models: '" + handle +
        "' is Wan's original layout (WanModel), missing models_t5_umt5-xxl-enc-bf16.pth and "
        "Wan2.2_VAE.pth -- 'apogee models pull Wan-AI/Wan2.2-TI2V-5B --safetensors' fetches them "
        "into this set; neither converter takes it -- llama.cpp's finds a model by its "
        "architectures, and mlx-lm's builds a language model; what runs this model today is its "
        "Diffusers pipeline in 'apogee video generate', pulled whole with 'apogee models pull "
        "Wan-AI/Wan2.2-TI2V-5B-Diffusers'\n";
    std::string out;
    // The user's command, and the GGUF form, refused alike before anything runs.
    CHECK(pull.home.run({"models", "convert", handle, "--mlx"}, &out) != 0);
    CHECK(out.find(refused) != std::string::npos);
    CHECK(pull.home.run({"models", "convert", handle}, &out) != 0);
    CHECK(out.find(refused) != std::string::npos);
    CHECK(apogee::models::list_store_mlx(pull.roots).empty());
    CHECK(apogee::models::list_store_ggufs(pull.roots).empty());

    // Completed, it says so -- and still no converter takes it (53k's).
    std::string pulled;
    REQUIRE(pull.run(std::string{apogee::testing::kWanRepo}, &pulled) == 0);
    CHECK(pull.home.run({"models", "convert", handle, "--mlx"}, &out) != 0);
    CHECK(out.find("' is Wan's original layout (WanModel), complete -- its transformer beside "
                   "models_t5_umt5-xxl-enc-bf16.pth and Wan2.2_VAE.pth; neither converter takes "
                   "it") != std::string::npos);
}
