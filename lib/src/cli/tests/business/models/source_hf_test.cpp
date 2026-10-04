#include "models/source_hf.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "support/fake_transport.h"

/// The Hugging Face source.
///
/// Hermetic: every case drives a `FakeTransport`, so no test here reaches the
/// network. The behaviour that matters most is the **refusal to guess**: a
/// repository holding a dozen quantisations gets a list and an error, not a
/// download of whichever one happened to be first.
namespace {

using apogee::backends::HttpClient;
using apogee::models::HfRef;
using apogee::models::parse_hf_ref;
using apogee::testing::FakeTransport;

[[nodiscard]] std::unique_ptr<HttpClient> client_for(std::vector<FakeTransport::Reply> replies) {
    return std::make_unique<HttpClient>(std::make_unique<FakeTransport>(std::move(replies)));
}

[[nodiscard]] std::string repo_json(const std::vector<std::string>& files) {
    std::string out = R"({"id":"owner/repo","siblings":[)";
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += R"({"rfilename":")" + files[i] + R"("})";
    }
    return out + "]}";
}

}  // namespace

TEST_CASE("a bare owner/repo ref parses", "[models][hf]") {
    const auto ref = parse_hf_ref("TheBloke/Llama-2-7B-GGUF");
    REQUIRE(ref.has_value());
    CHECK(ref->owner == "TheBloke");
    CHECK(ref->repo == "Llama-2-7B-GGUF");
    CHECK(ref->file.empty());
    CHECK(ref->revision.empty());
}

TEST_CASE("a ref naming a file parses", "[models][hf]") {
    const auto ref = parse_hf_ref("owner/repo:model.Q4_K_M.gguf");
    REQUIRE(ref.has_value());
    CHECK(ref->repo_id() == "owner/repo");
    CHECK(ref->file == "model.Q4_K_M.gguf");
}

TEST_CASE("a ref naming a revision parses", "[models][hf]") {
    const auto ref = parse_hf_ref("owner/repo@abc123:model.gguf");
    REQUIRE(ref.has_value());
    CHECK(ref->revision == "abc123");
    CHECK(ref->file == "model.gguf");
}

TEST_CASE("something that is not owner/repo is refused", "[models][hf]") {
    CHECK_FALSE(parse_hf_ref("llama3.2:3b").has_value());  // an Ollama ref
    CHECK_FALSE(parse_hf_ref("justaname").has_value());
    CHECK_FALSE(parse_hf_ref("a/b/c").has_value());
    CHECK_FALSE(parse_hf_ref("/repo").has_value());
}

TEST_CASE("the download url addresses the resolve endpoint", "[models][hf]") {
    HfRef ref;
    ref.owner = "owner";
    ref.repo = "repo";
    ref.file = "model.gguf";
    CHECK(apogee::models::hf_download_url(ref) ==
          "https://huggingface.co/owner/repo/resolve/main/model.gguf");

    ref.revision = "v2";
    CHECK(apogee::models::hf_download_url(ref) ==
          "https://huggingface.co/owner/repo/resolve/v2/model.gguf");
}

TEST_CASE("a repo with exactly one gguf resolves to it", "[models][hf]") {
    auto client = client_for({{.status = 200, .body = repo_json({"README.md", "model.gguf"})}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    REQUIRE(apogee::models::resolve_file(*client, ref, {}, {}, error));
    CHECK(ref.file == "model.gguf");
}

TEST_CASE("a repo with several gguf files refuses and lists them", "[models][hf]") {
    // THE behaviour this source exists to get right. A quantised upload holds a
    // dozen precisions differing by gigabytes and by quality; picking one for
    // the user spends their bandwidth on a file they did not choose.
    auto client = client_for(
        {{.status = 200,
          .body = repo_json({"model.Q2_K.gguf", "model.Q4_K_M.gguf", "model.Q8_0.gguf"})}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, {}, {}, error));

    CHECK(error.find("3 GGUF files") != std::string::npos);
    // Every candidate named, so the next command can be copied from the output.
    CHECK(error.find("model.Q2_K.gguf") != std::string::npos);
    CHECK(error.find("model.Q4_K_M.gguf") != std::string::npos);
    CHECK(error.find("model.Q8_0.gguf") != std::string::npos);
    // And it must not have silently picked one.
    CHECK(ref.file.empty());
}

TEST_CASE("a SafeTensors repo names the conversion path", "[models][hf]") {
    // SPEC lists SafeTensors in scope, so "no .gguf" alone reads as a product
    // limitation rather than as a step the user can take. The pull never
    // converts on its own -- the converter needs torch and transformers, which
    // a user installs on purpose -- but it names the command that does.
    auto client =
        client_for({{.status = 200, .body = repo_json({"README.md", "model.safetensors"})}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, {}, {}, error));

    CHECK(error.find("no .gguf") != std::string::npos);
    CHECK(error.find("SafeTensors") != std::string::npos);
    // The actual next step, not just a diagnosis.
    CHECK(error.find("apogee models convert owner/repo") != std::string::npos);
}

TEST_CASE("a repo with neither says so without mentioning SafeTensors", "[models][hf]") {
    // Naming a conversion path for a repository that has nothing to convert
    // would send the user after a file that is not there.
    auto client = client_for({{.status = 200, .body = repo_json({"README.md", "config.json"})}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, {}, {}, error));

    CHECK(error.find("no .gguf") != std::string::npos);
    CHECK(error.find("convert_hf_to_gguf.py") == std::string::npos);
}

TEST_CASE("a gated repo explains how to get access", "[models][hf]") {
    auto client = client_for({{.status = 403, .body = "{}"}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, {}, {}, error));
    // Names the likelier cause first: HF answers 401 for a nonexistent repo
    // too, so asserting "gated" would send a typo hunting for a licence.
    CHECK(error.find("Check the spelling") != std::string::npos);
    CHECK(error.find("gated") != std::string::npos);
    CHECK(error.find("HF_TOKEN") != std::string::npos);
}

TEST_CASE("a missing repo says so", "[models][hf]") {
    auto client = client_for({{.status = 404, .body = "{}"}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, {}, {}, error));
    CHECK(error.find("no such repository") != std::string::npos);
}

TEST_CASE("a ref that already names a file needs no listing", "[models][hf]") {
    // No reply is scripted: if this made a request, the transport would fail.
    auto client = client_for({});

    HfRef ref{.owner = "owner", .repo = "repo", .file = "model.gguf"};
    std::string error;
    CHECK(apogee::models::resolve_file(*client, ref, {}, {}, error));
    CHECK(ref.file == "model.gguf");
}

TEST_CASE("the token comes from config, then the environment, then nowhere",
          "[models][hf][secrets]") {
    // No OAuth flow and no credential store: the same line SPEC draws for the
    // vendor CLIs, applied to a plain HTTP source.
    {
        const apogee::testing::EnvGuard token{"HF_TOKEN", "from-env"};
        // An explicit value always wins; the environment is the fallback.
        CHECK(apogee::models::hf_token("from-config") == "from-config");
        CHECK(apogee::models::hf_token({}) == "from-env");
    }

    const apogee::testing::EnvUnsetGuard no_token{"HF_TOKEN"};
    const apogee::testing::EnvUnsetGuard no_hub_token{"HUGGING_FACE_HUB_TOKEN"};
    CHECK(apogee::models::hf_token({}).empty());
}

TEST_CASE("the tree listing reads sizes and LFS digests, for models and datasets",
          "[models][hf][tree]") {
    const std::string tree = R"([
        {"type": "file", "oid": "gitblob", "size": 12, "path": "config.json"},
        {"type": "directory", "oid": "x", "path": "images"},
        {"type": "file", "oid": "y", "size": 5, "path": "model.safetensors",
         "lfs": {"oid": "abc123", "size": 4096, "pointerSize": 134}}
    ])";
    auto fake = std::make_unique<FakeTransport>(
        std::vector<FakeTransport::Reply>{FakeTransport::Reply{.status = 200, .body = tree}});
    FakeTransport* transport_ptr = fake.get();
    HttpClient client{std::move(fake)};
    const HfRef ref = *parse_hf_ref("owner/repo@dev");
    const apogee::models::HfTree listed =
        apogee::models::list_repo_tree(client, ref, apogee::models::HfRepoKind::Model, "tok", {});
    REQUIRE(listed.ok);
    REQUIRE(listed.files.size() == 2);
    CHECK(listed.files[0].path == "config.json");
    CHECK(listed.files[0].size == 12);
    CHECK(listed.files[0].sha256.empty());  // a git blob hash is not a digest
    CHECK(listed.files[1].path == "model.safetensors");
    CHECK(listed.files[1].sha256 == "abc123");
    CHECK(listed.files[1].size == 4096);  // the LFS size wins
    const FakeTransport& transport = *transport_ptr;
    REQUIRE(transport.requests().size() == 1);
    CHECK(transport.requests()[0].url ==
          "https://huggingface.co/api/models/owner/repo/tree/dev?recursive=true");
    CHECK(transport.requests()[0].headers[0].value == "Bearer tok");

    auto datasets_fake = std::make_unique<FakeTransport>(
        std::vector<FakeTransport::Reply>{FakeTransport::Reply{.status = 200, .body = "[]"}});
    const FakeTransport* datasets_ptr = datasets_fake.get();
    HttpClient datasets{std::move(datasets_fake)};
    (void)apogee::models::list_repo_tree(datasets, *parse_hf_ref("org/data"),
                                         apogee::models::HfRepoKind::Dataset, "", {});
    CHECK(datasets_ptr->requests()[0].url ==
          "https://huggingface.co/api/datasets/org/data/tree/main?recursive=true");

    auto missing = client_for({FakeTransport::Reply{.status = 404, .body = ""}});
    const apogee::models::HfTree gone =
        apogee::models::list_repo_tree(*missing, ref, apogee::models::HfRepoKind::Model, "", {});
    CHECK_FALSE(gone.ok);
    CHECK(gone.error.find("no such repository or revision") != std::string::npos);
}

TEST_CASE("dataset files download from the datasets prefix with the tree's promise",
          "[models][hf][tree]") {
    HfRef ref = *parse_hf_ref("org/data");
    CHECK(apogee::models::hf_download_url(ref, apogee::models::HfRepoKind::Dataset)
              .rfind("https://huggingface.co/datasets/org/data/resolve/main/", 0) == 0);
    auto client = client_for({FakeTransport::Reply{.status = 200, .body = "bytes"}});
    apogee::models::SourcePromise promise;
    const apogee::models::HfFile file{"data/train.parquet", 5, "deadbeef"};
    const apogee::models::ByteSource source = apogee::models::http_source(
        *client, ref, apogee::models::HfRepoKind::Dataset, file, "", {}, promise);
    CHECK(promise.size == 5);
    CHECK(promise.digest == "deadbeef");
    CHECK(promise.source_url ==
          "https://huggingface.co/datasets/org/data/resolve/main/data/train.parquet");
    std::string got;
    std::string error;
    REQUIRE(source(
        [&got](std::string_view chunk) {
            got += chunk;
            return true;
        },
        error));
    CHECK(got == "bytes");
}

TEST_CASE("snapshot and dataset file filters, and the directory name", "[models][hf][snapshot]") {
    using apogee::models::dataset_file_wanted;
    using apogee::models::snapshot_wanted;
    CHECK(snapshot_wanted("model-00001-of-00002.safetensors"));
    CHECK(snapshot_wanted("config.json"));
    CHECK(snapshot_wanted("tokenizer.model"));
    CHECK(snapshot_wanted("merges.txt"));
    CHECK(snapshot_wanted("modeling_custom.py"));
    CHECK_FALSE(snapshot_wanted("README.md"));
    CHECK_FALSE(snapshot_wanted(".gitattributes"));
    CHECK_FALSE(snapshot_wanted("model.gguf"));
    CHECK_FALSE(snapshot_wanted("pytorch_model.bin"));
    CHECK_FALSE(snapshot_wanted("banner.png"));
    CHECK(dataset_file_wanted("data/train-00000.parquet"));
    CHECK(dataset_file_wanted("train.jsonl"));
    CHECK_FALSE(dataset_file_wanted("README.md"));
    CHECK_FALSE(dataset_file_wanted(".gitattributes"));
    CHECK(apogee::models::repo_directory_name(*parse_hf_ref("Owner/Repo-Name")) ==
          "Owner--Repo-Name");
}

TEST_CASE("a GGUF-less SafeTensors repository names the pull and the convert that follow",
          "[models][hf]") {
    auto client = client_for({FakeTransport::Reply{
        .status = 200, .body = repo_json({"config.json", "model.safetensors"})}});
    HfRef ref = *parse_hf_ref("owner/repo");
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, "", {}, error));
    CHECK(error.find("apogee models pull owner/repo --safetensors") != std::string::npos);
    // The next step is Apogee's own command, not a script to find and run by
    // hand: the vendored converter and its environment already ship.
    CHECK(error.find("apogee models convert owner/repo owner--repo.gguf") != std::string::npos);
    CHECK(error.find("train setup --with convert") != std::string::npos);
    const apogee::models::HfListing listing = apogee::models::list_gguf_files(
        *client_for({FakeTransport::Reply{.status = 200,
                                          .body = repo_json({"a.safetensors", "b.safetensors"})}}),
        ref, "", {});
    REQUIRE(listing.ok);
    CHECK(listing.safetensors_files == std::vector<std::string>{"a.safetensors", "b.safetensors"});
}
