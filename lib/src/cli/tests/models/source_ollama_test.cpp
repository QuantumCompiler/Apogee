#include "models/source_ollama.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "support/gguf_builder.h"

/// Reading the Ollama store.
///
/// Built against a **fixture store** rather than the developer's real one, for
/// two reasons. The first is the repo rule that tests are hermetic. The second
/// is specific and worth recording: the only model in this machine's Ollama
/// store is a **cloud** model, whose manifest carries an empty `layers` array
/// because the weights live on Ollama's servers. That case is covered below —
/// it is exactly the "manifest exists, nothing local" path — but it means the
/// happy path could not be characterized live here and is built to the layout
/// Ommi verified on its own host and documented in `store.go`.
namespace {

using apogee::models::find_in_store;
using apogee::models::list_store;
using apogee::models::manifest_path;
using apogee::models::parse_manifest;
using apogee::models::split_ref;

struct Store {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("apogee-ollama-" + std::to_string(counter()));

    Store() {
        std::error_code code;
        std::filesystem::create_directories(root / "blobs", code);
    }

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) = delete;
    Store& operator=(Store&&) = delete;

    ~Store() {
        std::error_code code;
        std::filesystem::remove_all(root, code);
    }

    void write(const std::filesystem::path& relative, std::string_view content) const {
        const std::filesystem::path path = root / relative;
        std::error_code code;
        std::filesystem::create_directories(path.parent_path(), code);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    /// Writes a manifest plus the blob it names, as a real pull would.
    [[nodiscard]] std::string add_model(std::string_view name, std::string_view tag,
                                        std::string_view architecture) const {
        const std::string gguf = apogee::testing::minimal_gguf(architecture);
        // A made-up but well-formed digest: the store is content-addressed by
        // name, and `acquire` is what checks the bytes against it.
        const std::string digest = "sha256:" + std::string(64, 'c');
        write(std::filesystem::path{"blobs"} / ("sha256-" + std::string(64, 'c')), gguf);
        write(std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" /
                  std::string{name} / std::string{tag},
              R"({"schemaVersion":2,"layers":[)"
              R"({"mediaType":"application/vnd.ollama.image.model","digest":")" +
                  digest + R"(","size":)" + std::to_string(gguf.size()) +
                  R"(})"
                  R"(]})");
        return gguf;
    }

private:
    static int counter() {
        static int next = 0;
        return ++next;
    }
};

}  // namespace

TEST_CASE("a ref splits into name and tag", "[models][ollama]") {
    CHECK(split_ref("llama3.2:3b") == std::pair<std::string, std::string>{"llama3.2", "3b"});
    // No tag means Ollama's implicit "latest".
    CHECK(split_ref("llama3.2") == std::pair<std::string, std::string>{"llama3.2", "latest"});
    CHECK(split_ref("user/model:v1") == std::pair<std::string, std::string>{"user/model", "v1"});
}

TEST_CASE("a colon inside a namespace is not a tag separator", "[models][ollama]") {
    // "host:port/model" -- the colon is part of the registry, not a tag. Only a
    // colon after the last slash separates a tag.
    CHECK(split_ref("host:5000/model") ==
          std::pair<std::string, std::string>{"host:5000/model", "latest"});
}

TEST_CASE("a library ref maps to the library namespace", "[models][ollama]") {
    const std::filesystem::path path = manifest_path("/store", "llama3.2:3b");
    CHECK(path.string().find("registry.ollama.ai/library/llama3.2/3b") != std::string::npos);
}

TEST_CASE("a namespaced ref maps under its own namespace", "[models][ollama]") {
    const std::filesystem::path path = manifest_path("/store", "someone/model:v2");
    CHECK(path.string().find("registry.ollama.ai/someone/model/v2") != std::string::npos);
    CHECK(path.string().find("library") == std::string::npos);
}

TEST_CASE("a manifest yields the model layer's blob and digest", "[models][ollama]") {
    Store store;
    const std::string gguf = store.add_model("llama3.2", "3b", "llama");

    const auto entry = find_in_store(store.root, "llama3.2:3b");
    REQUIRE(entry.has_value());

    CHECK(entry->ref == "llama3.2:3b");
    CHECK(entry->size == static_cast<std::int64_t>(gguf.size()));
    // Content-addressed, so this source really does publish a digest -- one of
    // the few that does.
    CHECK(entry->digest == std::string(64, 'c'));
    CHECK(std::filesystem::exists(entry->blob));
}

TEST_CASE("a cloud model reports as not local rather than as a broken manifest",
          "[models][ollama]") {
    // The only model in this machine's real store is of exactly this shape: an
    // empty `layers` array, because the weights are on Ollama's servers. It is
    // not broken, it is a different kind of model, and the message a user gets
    // has to reflect that.
    Store store;
    store.write(std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" / "gpt-oss" /
                    "20b-cloud",
                R"({"schemaVersion":2,"layers":[],"config":{"digest":"sha256:abc"}})");

    CHECK_FALSE(
        parse_manifest(store.root, "gpt-oss:20b-cloud", R"({"schemaVersion":2,"layers":[]})")
            .has_value());
    CHECK_FALSE(find_in_store(store.root, "gpt-oss:20b-cloud").has_value());
}

TEST_CASE("a cloud model is distinguishable from one never pulled", "[models][ollama]") {
    // Found by running it: both cases make find_in_store return nothing, but
    // they need opposite messages. Telling someone with a cloud model to run
    // `ollama pull` sends them to re-fetch what they already have.
    Store store;
    store.write(std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" / "gpt-oss" /
                    "20b-cloud",
                R"({"schemaVersion":2,"layers":[]})");

    CHECK(apogee::models::store_has_manifest(store.root, "gpt-oss:20b-cloud"));
    CHECK_FALSE(find_in_store(store.root, "gpt-oss:20b-cloud").has_value());

    // Never pulled at all: no manifest either.
    CHECK_FALSE(apogee::models::store_has_manifest(store.root, "never-pulled:latest"));
}

TEST_CASE("a manifest naming a blob that is gone reports absent", "[models][ollama]") {
    // A store the user pruned by hand. Returning the path would hand back
    // something that fails on open, much later.
    Store store;
    store.write(
        std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" / "ghost" / "latest",
        R"({"schemaVersion":2,"layers":[{"mediaType":"application/vnd.ollama.image.model",)"
        R"("digest":"sha256:)" +
            std::string(64, 'f') + R"(","size":10}]})");

    CHECK_FALSE(find_in_store(store.root, "ghost:latest").has_value());
}

TEST_CASE("a projector layer is read as its own file", "[models][ollama][vision]") {
    // Checked against the live registry before this was built: `llava` and
    // `moondream` both carry `application/vnd.ollama.image.projector` as a
    // SEPARATE layer beside the model. The plan inherited from Ommi was to
    // *extract* a projector out of a combined blob -- and the manifests say
    // there is nothing to extract, so this reads a second file instead.
    Store store;
    const std::string gguf = apogee::testing::minimal_gguf("llama");
    const std::string projector = apogee::testing::minimal_gguf("clip");
    const std::string model_digest = std::string(64, '1');
    const std::string proj_digest = std::string(64, '2');

    store.write(std::filesystem::path{"blobs"} / ("sha256-" + model_digest), gguf);
    store.write(std::filesystem::path{"blobs"} / ("sha256-" + proj_digest), projector);
    store.write(
        std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" / "llava" / "latest",
        R"({"schemaVersion":2,"layers":[)"
        R"({"mediaType":"application/vnd.ollama.image.model","digest":"sha256:)" +
            model_digest + R"(","size":)" + std::to_string(gguf.size()) +
            R"(},)"
            R"({"mediaType":"application/vnd.ollama.image.projector","digest":"sha256:)" +
            proj_digest + R"(","size":)" + std::to_string(projector.size()) +
            R"(})"
            R"(]})");

    const auto entry = find_in_store(store.root, "llava:latest");
    REQUIRE(entry.has_value());

    CHECK(entry->has_projector());
    CHECK(entry->projector_digest == proj_digest);
    CHECK(entry->projector_size == static_cast<std::int64_t>(projector.size()));
    CHECK(std::filesystem::exists(entry->projector_blob));

    // Its promise is its own: a separate file with its own digest goes through
    // the ladder separately and gets its own provenance record.
    const auto promise = apogee::models::projector_promise_for(*entry);
    CHECK(promise.digest == proj_digest);
    CHECK(promise.ref.find("projector") != std::string::npos);
}

TEST_CASE("a text-only model has no projector", "[models][ollama][vision]") {
    Store store;
    store.add_model("llama3.2", "3b", "llama");

    const auto entry = find_in_store(store.root, "llama3.2:3b");
    REQUIRE(entry.has_value());
    CHECK_FALSE(entry->has_projector());
}

TEST_CASE("a projector layer whose blob is gone does not break the model",
          "[models][ollama][vision]") {
    // The model still works for text. Failing the whole lookup because the
    // optional half is missing would take away what does work.
    Store store;
    const std::string gguf = apogee::testing::minimal_gguf("llama");
    const std::string model_digest = std::string(64, '3');

    store.write(std::filesystem::path{"blobs"} / ("sha256-" + model_digest), gguf);
    store.write(
        std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" / "half" / "latest",
        R"({"schemaVersion":2,"layers":[)"
        R"({"mediaType":"application/vnd.ollama.image.model","digest":"sha256:)" +
            model_digest + R"(","size":)" + std::to_string(gguf.size()) +
            R"(},)"
            R"({"mediaType":"application/vnd.ollama.image.projector","digest":"sha256:)" +
            std::string(64, '9') +
            R"(","size":10})"
            R"(]})");

    const auto entry = find_in_store(store.root, "half:latest");
    REQUIRE(entry.has_value());
    CHECK_FALSE(entry->has_projector());
    CHECK(std::filesystem::exists(entry->blob));
}

TEST_CASE("a template layer is recorded as a hint", "[models][ollama]") {
    // Recorded, never auto-applied: applying it would override the profile
    // layer's resolution ladder from outside the ladder.
    Store store;
    const std::string gguf = apogee::testing::minimal_gguf("llama");
    const std::string model_digest = std::string(64, 'a');
    const std::string template_digest = std::string(64, 'b');

    store.write(std::filesystem::path{"blobs"} / ("sha256-" + model_digest), gguf);
    store.write(std::filesystem::path{"blobs"} / ("sha256-" + template_digest), "{{ .Prompt }}");
    store.write(
        std::filesystem::path{"manifests"} / "registry.ollama.ai" / "library" / "m" / "latest",
        R"({"schemaVersion":2,"layers":[)"
        R"({"mediaType":"application/vnd.ollama.image.model","digest":"sha256:)" +
            model_digest + R"(","size":)" + std::to_string(gguf.size()) +
            R"(},)"
            R"({"mediaType":"application/vnd.ollama.image.template","digest":"sha256:)" +
            template_digest +
            R"("})"
            R"(]})");

    const auto entry = find_in_store(store.root, "m:latest");
    REQUIRE(entry.has_value());
    CHECK(entry->template_hint == "{{ .Prompt }}");
}

TEST_CASE("listing a store finds models with local weights", "[models][ollama]") {
    Store store;
    store.add_model("llama3.2", "3b", "llama");

    const auto entries = list_store(store.root);
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().ref == "llama3.2:3b");
}

TEST_CASE("listing an absent store is empty rather than an error", "[models][ollama]") {
    CHECK(list_store("/no/such/store").empty());
}

TEST_CASE("the store reader has no way to delete anything", "[models][ollama][safety]") {
    // Ollama's blobs are SHARED between models: sibling models reference
    // identical license, template, and sometimes weight layers. Removing one
    // because "its" model is going away silently corrupts every sibling.
    //
    // Apogee's rule, carried from Ommi, is that the only mutation of this store
    // is `ollama rm` -- the vendor's own reference-counting GC. That rule is
    // kept structurally rather than by review: this module exposes no delete,
    // so there is nothing here to misuse. This test is the record of why the
    // absence is deliberate; if a delete ever appears in the header, it should
    // fail review with this comment as the reason.
    Store store;
    const std::string gguf = store.add_model("shared", "latest", "llama");

    const auto entry = find_in_store(store.root, "shared:latest");
    REQUIRE(entry.has_value());

    // Reading does not disturb the store.
    std::string error;
    const auto source = apogee::models::blob_source(*entry);
    std::string copied;
    CHECK(source(
        [&copied](std::string_view bytes) {
            copied.append(bytes);
            return true;
        },
        error));
    CHECK(copied == gguf);
    CHECK(std::filesystem::exists(entry->blob));
}
