#include "models/source_hf.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "support/fake_transport.h"
#include "support/wan_original.h"

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

TEST_CASE("an MLX repository is the one its model card says it is, never its name",
          "[models][hf][mlx]") {
    // 27b: mlx-lm writes `library_name: mlx` and the `mlx` tag for every
    // model it converts; the listing reads both, and only with SafeTensors to
    // pull is it an MLX model to land whole.
    const auto listing_of = [](const std::string& body) {
        const auto client = client_for({{.status = 200, .body = body}});
        const auto ref = parse_hf_ref("mlx-community/Llama-3.2-1B-Instruct-4bit");
        REQUIRE(ref.has_value());
        return apogee::models::list_gguf_files(*client, *ref, "", {});
    };
    const apogee::models::HfListing by_library = listing_of(
        R"({"library_name":"mlx","tags":["transformers","safetensors"],"siblings":[
            {"rfilename":"config.json"},{"rfilename":"model.safetensors"}]})");
    REQUIRE(by_library.ok);
    CHECK(by_library.library_name == "mlx");
    CHECK(by_library.mlx());

    const apogee::models::HfListing by_tag = listing_of(
        R"({"library_name":"transformers","tags":["mlx","text-generation"],"siblings":[
            {"rfilename":"model.safetensors"}]})");
    CHECK(by_tag.tags == std::vector<std::string>{"mlx", "text-generation"});
    CHECK(by_tag.mlx());

    // A full-weight repository is not one, whatever its name says.
    CHECK_FALSE(listing_of(R"({"library_name":"transformers","tags":["safetensors"],"siblings":[
                              {"rfilename":"model.safetensors"}]})")
                    .mlx());
    // Nor a card saying mlx over nothing to pull.
    CHECK_FALSE(
        listing_of(R"({"library_name":"mlx","siblings":[{"rfilename":"README.md"}]})").mlx());
}

TEST_CASE("choosing a file from a listing in hand is resolve_file's decision",
          "[models][hf][mlx]") {
    apogee::models::HfListing listing;
    listing.ok = true;
    listing.gguf_files = {"m.Q4_K_M.gguf"};
    HfRef ref = *parse_hf_ref("owner/repo");
    std::string error;
    REQUIRE(apogee::models::choose_file(listing, ref, error));
    CHECK(ref.file == "m.Q4_K_M.gguf");

    listing.gguf_files = {"a.gguf", "b.gguf"};
    HfRef several = *parse_hf_ref("owner/repo");
    CHECK_FALSE(apogee::models::choose_file(listing, several, error));
    CHECK(error.find("contains 2 GGUF files") != std::string::npos);

    listing.ok = false;
    listing.error = "no such repository: 'owner/repo'";
    HfRef failed = *parse_hf_ref("owner/repo");
    CHECK_FALSE(apogee::models::choose_file(listing, failed, error));
    CHECK(error == listing.error);
}

TEST_CASE("a cancelled download is a failure the ladder cleans up after, never an escape",
          "[models][hf][cancel]") {
    // A Ctrl-C reaches a pull's transport as CancelledError (27b's pull is the
    // first to pass a live token); thrown past the ladder it would skip the
    // cleanup that removes the partial and the staging tree.
    const auto client = client_for({{.status = 200, .body = "bytes"}});
    const auto ref = parse_hf_ref("owner/repo");
    REQUIRE(ref.has_value());
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    apogee::models::SourcePromise promise;
    const apogee::models::ByteSource source = apogee::models::http_source(
        *client, *ref, apogee::models::HfRepoKind::Model,
        apogee::models::HfFile{.path = "model.safetensors"}, "", token, promise);
    std::string error;
    CHECK_FALSE(source([](std::string_view) { return true; }, error));
    CHECK(error == "cancelled");
}

TEST_CASE("a pipeline repository is the one with model_index.json at its root",
          "[models][hf][pipeline]") {
    // 49a: a file the repository holds, never its name or its tags.
    const auto listing_of = [](const std::vector<std::string>& files) {
        const auto client = client_for({{.status = 200, .body = repo_json(files)}});
        return apogee::models::list_gguf_files(*client, *parse_hf_ref("Wan-AI/Wan2.2"), "", {});
    };
    CHECK(listing_of({"model_index.json", "vae/diffusion_pytorch_model.safetensors"}).pipeline());
    // One deeper is a component's own file, not the pipeline's index.
    CHECK_FALSE(listing_of({"sub/model_index.json", "model.safetensors"}).pipeline());
    CHECK_FALSE(listing_of({"config.json", "model.safetensors"}).pipeline());
}

TEST_CASE("a pipeline pull takes the index and the components it names, never a copy",
          "[models][hf][pipeline]") {
    using apogee::models::HfFile;
    const std::vector<HfFile> tree{
        {.path = "model_index.json", .size = 1},
        {.path = "README.md", .size = 1},
        {.path = "svd_xt.safetensors", .size = 9},  // a single-file checkpoint beside it
        {.path = "assets/sample.json", .size = 1},  // a directory the index does not name
        {.path = "scheduler/scheduler_config.json", .size = 1},
        {.path = "unet/config.json", .size = 1},
        {.path = "unet/diffusion_pytorch_model.safetensors", .size = 4},
        {.path = "unet/diffusion_pytorch_model.fp16.safetensors", .size = 2},  // a copy
        {.path = "unet/diffusion_pytorch_model.bin", .size = 4},  // never PyTorch pickles
        {.path = "vae/config.json", .size = 1},
        {.path = "vae/diffusion_pytorch_model.fp16.safetensors", .size = 2},  // the only weights
        {.path = "image_encoder/model.safetensors", .size = 2},  // not named: not taken
    };
    std::vector<std::string> taken;
    for (const HfFile& file : apogee::models::pipeline_wanted(tree, {"scheduler", "unet", "vae"})) {
        taken.push_back(file.path);
    }
    CHECK(taken == std::vector<std::string>{
                       "model_index.json", "scheduler/scheduler_config.json", "unet/config.json",
                       "unet/diffusion_pytorch_model.safetensors", "vae/config.json",
                       "vae/diffusion_pytorch_model.fp16.safetensors"});
}

TEST_CASE("a repository's small file is read whole, refused when it is not small",
          "[models][hf][pipeline]") {
    const HfRef ref = *parse_hf_ref("org/video");
    const apogee::models::HfText text = apogee::models::fetch_repo_text(
        *client_for({{.status = 200, .body = R"({"_class_name": "WanPipeline"})"}}), ref,
        apogee::models::HfRepoKind::Model, "model_index.json", "", {});
    REQUIRE(text.ok);
    CHECK(text.text == R"({"_class_name": "WanPipeline"})");

    const apogee::models::HfText missing = apogee::models::fetch_repo_text(
        *client_for({{.status = 404, .body = "Entry not found"}}), ref,
        apogee::models::HfRepoKind::Model, "model_index.json", "", {});
    CHECK_FALSE(missing.ok);
    CHECK(missing.error ==
          "could not read model_index.json from 'org/video' (Hugging Face returned status 404)");

    const apogee::models::HfText large = apogee::models::fetch_repo_text(
        *client_for({{.status = 200, .body = std::string(64, 'x')}}), ref,
        apogee::models::HfRepoKind::Model, "model_index.json", "", {}, 16);
    CHECK_FALSE(large.ok);
    CHECK(large.error.ends_with("is larger than 16 bytes -- not the small file it should be"));
}

TEST_CASE("a voice model repository is the one laid out as Kokoro's, and only its weights move",
          "[models][hf][voice]") {
    // 50a: config.json and a checkpoint at the root, packs under voices/.
    const auto listing_of = [](const std::vector<std::string>& files) {
        const auto client = client_for({{.status = 200, .body = repo_json(files)}});
        return apogee::models::list_gguf_files(*client, *parse_hf_ref("hexgrad/Kokoro-82M"), "",
                                               {});
    };
    CHECK(listing_of({"config.json", "kokoro-v1_0.pth", "voices/af_heart.pt"}).voice());
    CHECK_FALSE(listing_of({"config.json", "kokoro-v1_0.pth"}).voice());
    CHECK_FALSE(listing_of({"config.json", "sub/model.pth", "voices/af_heart.pt"}).voice());
    CHECK_FALSE(listing_of({"config.json", "model.safetensors"}).voice());

    using apogee::models::HfFile;
    std::vector<std::string> taken;
    for (const HfFile& file : apogee::models::voice_wanted({
             {.path = "config.json", .size = 1},
             {.path = "kokoro-v1_0.pth", .size = 9},
             {.path = "README.md", .size = 1},
             {.path = "samples/HEARME.wav", .size = 5},
             {.path = "voices/af_heart.pt", .size = 2},
             {.path = "voices/nested/x.pt", .size = 2},
             {.path = "eval/ArtificialAnalysis.png", .size = 2},
         })) {
        taken.push_back(file.path);
    }
    CHECK(taken ==
          std::vector<std::string>{"config.json", "kokoro-v1_0.pth", "voices/af_heart.pt"});
}

TEST_CASE("a SafeTensors pull takes Wan's root checkpoints, and no other repository's",
          "[models][hf][snapshot][wan]") {
    // 53j: the user's repository as its tree lists it.
    using apogee::models::HfFile;
    std::vector<HfFile> wan;
    for (const std::string& path : apogee::testing::wan_repository_files()) {
        wan.push_back({.path = path, .size = 1, .sha256 = {}});
    }
    const auto paths = [](const std::vector<HfFile>& files) {
        std::vector<std::string> out;
        for (const HfFile& file : files) {
            out.push_back(file.path);
        }
        return out;
    };
    std::vector<std::string> ten{apogee::testing::kWanRecorded.begin(),
                                 apogee::testing::kWanRecorded.end()};
    // Not the layout: exactly the snapshot rule's ten, as before 53j.
    CHECK(paths(apogee::models::snapshot_files(wan, false)) == ten);
    // The layout: the ten and its two checkpoints, in the tree's order; never
    // the model card, the assets or the example image.
    std::vector<std::string> twelve = ten;
    twelve.insert(twelve.begin(), std::string{apogee::testing::kWanVae});
    twelve.emplace_back(apogee::testing::kWanTextEncoder);
    CHECK(paths(apogee::models::snapshot_files(wan, true)) == twelve);

    using apogee::models::root_checkpoint;
    CHECK(root_checkpoint("Wan2.2_VAE.pth"));
    CHECK_FALSE(root_checkpoint("original/consolidated.00.pth"));
    CHECK_FALSE(root_checkpoint("model.pt"));
    CHECK_FALSE(root_checkpoint("model.safetensors"));

    // What a stored set's record lacks of them: the two, in order.
    const std::vector<HfFile> gaps = apogee::models::snapshot_gaps(
        apogee::testing::wan_record(), apogee::models::snapshot_files(wan, true));
    CHECK(paths(gaps) == std::vector<std::string>{std::string{apogee::testing::kWanVae},
                                                  std::string{apogee::testing::kWanTextEncoder}});
    CHECK(apogee::models::snapshot_gaps(apogee::testing::wan_record(),
                                        apogee::models::snapshot_files(wan, false))
              .empty());
}

TEST_CASE("Wan's original layout is read from the repository's config, and only when it can be one",
          "[models][hf][snapshot][wan]") {
    using apogee::models::HfFile;
    const HfRef ref = *parse_hf_ref("Wan-AI/Wan2.2-TI2V-5B");
    const std::vector<HfFile> wan{{.path = "Wan2.2_VAE.pth", .size = 1, .sha256 = {}},
                                  {.path = "config.json", .size = 1, .sha256 = {}},
                                  {.path = "model.safetensors", .size = 1, .sha256 = {}}};
    const auto read = [&ref](const std::vector<HfFile>& tree, std::string config,
                             std::size_t* asked) {
        auto fake = std::make_unique<FakeTransport>(std::vector<FakeTransport::Reply>{
            FakeTransport::Reply{.status = 200, .body = std::move(config)}});
        const FakeTransport* transport = fake.get();
        HttpClient client{std::move(fake)};
        apogee::models::HfWanLayout layout =
            apogee::models::read_wan_layout(client, ref, tree, "", {});
        *asked = transport->attempts();
        if (*asked > 0) {
            CHECK(transport->requests()[0].url ==
                  "https://huggingface.co/Wan-AI/Wan2.2-TI2V-5B/resolve/main/config.json");
        }
        return layout;
    };

    std::size_t asked = 0;
    apogee::models::HfWanLayout layout =
        read(wan, std::string{apogee::testing::kWanTi2vConfig}, &asked);
    REQUIRE(layout.ok);
    CHECK(layout.layout());
    CHECK(layout.class_name == "WanModel");
    CHECK(asked == 1);

    // The config's class decides: a language model's, or diffusers' port of
    // Wan, beside a root checkpoint is not the layout.
    layout = read(wan, R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})", &asked);
    CHECK(layout.ok);
    CHECK_FALSE(layout.layout());
    layout = read(
        wan, R"({"_class_name": "WanTransformer3DModel", "_diffusers_version": "0.33.0"})", &asked);
    CHECK_FALSE(layout.layout());

    // No root checkpoint, or no root config: nothing more is asked.
    layout = read({{.path = "config.json", .size = 1, .sha256 = {}},
                   {.path = "original/consolidated.00.pth", .size = 1, .sha256 = {}},
                   {.path = "model.safetensors", .size = 1, .sha256 = {}}},
                  std::string{apogee::testing::kWanTi2vConfig}, &asked);
    CHECK(layout.ok);
    CHECK_FALSE(layout.layout());
    CHECK(asked == 0);
    layout = read({{.path = "Wan2.2_VAE.pth", .size = 1, .sha256 = {}},
                   {.path = "transformer/config.json", .size = 1, .sha256 = {}}},
                  std::string{apogee::testing::kWanTi2vConfig}, &asked);
    CHECK_FALSE(layout.layout());
    CHECK(asked == 0);

    // A config that cannot be read is the pull's failure, said.
    auto refused = client_for({FakeTransport::Reply{.status = 404, .body = ""}});
    layout = apogee::models::read_wan_layout(*refused, ref, wan, "", {});
    CHECK_FALSE(layout.ok);
    CHECK(layout.error.find("could not read config.json") != std::string::npos);
}
