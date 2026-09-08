#include "models/source_hf.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

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

TEST_CASE("a repo with no gguf says what is wrong", "[models][hf]") {
    auto client =
        client_for({{.status = 200, .body = repo_json({"README.md", "model.safetensors"})}});

    HfRef ref{.owner = "owner", .repo = "repo"};
    std::string error;
    CHECK_FALSE(apogee::models::resolve_file(*client, ref, {}, {}, error));
    CHECK(error.find("no .gguf") != std::string::npos);
    CHECK(error.find("SafeTensors") != std::string::npos);
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
