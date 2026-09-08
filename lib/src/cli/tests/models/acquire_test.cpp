#include "models/acquire.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "models/sha256.h"
#include "models/sidecar.h"
#include "support/gguf_builder.h"

/// The acquisition ladder, and the promise that nothing partial ever lands.
///
/// **This is the failure-injection suite.** Each rung of the ladder is broken
/// in turn — a truncated stream, a wrong size, a wrong digest, an unreadable
/// header — and every case asserts the same three things: the transfer failed,
/// nothing is visible at the destination, and no `.partial` was left behind.
/// That third assertion is the one that matters most: a leftover partial is how
/// a later run "resumes" bytes of unknown provenance.
///
/// Every test is hermetic. The `ByteSource` seam means the Hugging Face path is
/// exercised with no network and the Ollama path with no store.
namespace {

using apogee::models::acquire;
using apogee::models::AcquireResult;
using apogee::models::ByteSource;
using apogee::models::SourcePromise;

/// A scratch directory that cleans itself up.
struct Scratch {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("apogee-acquire-" + std::to_string(counter()));

    Scratch() {
        std::error_code code;
        std::filesystem::create_directories(root, code);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        std::filesystem::remove_all(root, code);
    }

    [[nodiscard]] std::filesystem::path destination() const {
        return root / "model.gguf";
    }

    /// Whether a `.partial` survived. Must always be false after a failure.
    [[nodiscard]] bool has_partial() const {
        std::error_code code;
        for (const auto& entry : std::filesystem::directory_iterator(root, code)) {
            if (entry.path().extension() == ".partial") {
                return true;
            }
        }
        return false;
    }

private:
    static int counter() {
        static int next = 0;
        return ++next;
    }
};

/// A source that hands over `bytes` in one call.
[[nodiscard]] ByteSource bytes_source(std::string bytes) {
    return [bytes = std::move(bytes)](const std::function<bool(std::string_view)>& write,
                                      std::string&) { return write(bytes); };
}

/// A source that fails partway, having already written something.
[[nodiscard]] ByteSource failing_source(std::string prefix) {
    return [prefix = std::move(prefix)](const std::function<bool(std::string_view)>& write,
                                        std::string& error) {
        (void)write(prefix);
        error = "the connection dropped";
        return false;
    };
}

[[nodiscard]] std::string good_model() {
    return apogee::testing::minimal_gguf("llama");
}

}  // namespace

TEST_CASE("a clean transfer lands the model and its record", "[models][acquire]") {
    Scratch scratch;
    const std::string bytes = good_model();

    SourcePromise promise;
    promise.ref = "owner/repo:model.gguf";
    promise.source = "huggingface";
    promise.size = static_cast<std::int64_t>(bytes.size());

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    REQUIRE(result.ok);
    CHECK(std::filesystem::exists(scratch.destination()));
    CHECK_FALSE(scratch.has_partial());

    // The record landed beside it, and describes the file that is actually
    // there.
    const auto sidecar = apogee::models::load_sidecar(scratch.destination());
    REQUIRE(sidecar.has_value());
    CHECK(sidecar->file_size == static_cast<std::int64_t>(bytes.size()));
    CHECK(sidecar->file_digest == apogee::models::sha256_hex(bytes));
    CHECK(sidecar->ref == "owner/repo:model.gguf");
}

TEST_CASE("a transfer that fails partway lands nothing", "[models][acquire][failure]") {
    Scratch scratch;

    SourcePromise promise;
    promise.source = "huggingface";

    const AcquireResult result =
        acquire(scratch.destination(), promise, failing_source(good_model().substr(0, 40)));

    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
    CHECK_FALSE(std::filesystem::exists(scratch.destination()));
    // The one that matters: no partial for a later run to mistake for progress.
    CHECK_FALSE(scratch.has_partial());
}

TEST_CASE("a size mismatch lands nothing", "[models][acquire][failure]") {
    Scratch scratch;
    const std::string bytes = good_model();

    SourcePromise promise;
    promise.source = "ollama";
    promise.size = static_cast<std::int64_t>(bytes.size()) + 100;  // a lie

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("size mismatch") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(scratch.destination()));
    CHECK_FALSE(scratch.has_partial());
    CHECK_FALSE(std::filesystem::exists(apogee::models::sidecar_path_for(scratch.destination())));
}

TEST_CASE("a digest mismatch lands nothing", "[models][acquire][failure]") {
    Scratch scratch;
    const std::string bytes = good_model();

    SourcePromise promise;
    promise.source = "ollama";
    promise.digest = std::string(64, 'a');  // not the file's digest

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("digest mismatch") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(scratch.destination()));
    CHECK_FALSE(scratch.has_partial());
}

TEST_CASE("bytes that are not a GGUF land nothing", "[models][acquire][failure]") {
    // The rung that catches what size and digest cannot: a server that served
    // an HTML error page with a 200, which has a plausible size and no digest
    // to contradict it.
    Scratch scratch;

    SourcePromise promise;
    promise.source = "huggingface";

    const AcquireResult result =
        acquire(scratch.destination(), promise,
                bytes_source("<!DOCTYPE html><html><body>404 not found</body></html>"));

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("not a readable GGUF") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(scratch.destination()));
    CHECK_FALSE(scratch.has_partial());
}

TEST_CASE("a published digest that matches is recorded as checked and matched",
          "[models][acquire]") {
    Scratch scratch;
    const std::string bytes = good_model();

    SourcePromise promise;
    promise.source = "ollama";
    // Ollama's blobs are content-addressed, so this source really does publish
    // one -- with the "sha256:" prefix the manifest carries.
    promise.digest = "sha256:" + apogee::models::sha256_hex(bytes);
    promise.size = static_cast<std::int64_t>(bytes.size());

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    REQUIRE(result.ok);
    CHECK(result.sidecar.verification.digest_checked);
    CHECK(result.sidecar.verification.digest_matched);
    CHECK(result.sidecar.verification.size_checked);
    CHECK(result.sidecar.verification.sound());
}

TEST_CASE("no published digest is reported as such, not as a failure", "[models][acquire]") {
    // THE common case under an open-model policy, and the reason the report is
    // a set of flags rather than a boolean: most sources publish no digest, and
    // calling that "unverified" in the same words as a mismatch would teach a
    // user to ignore the word.
    Scratch scratch;
    const std::string bytes = good_model();

    SourcePromise promise;
    promise.source = "huggingface";
    promise.size = static_cast<std::int64_t>(bytes.size());

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    REQUIRE(result.ok);
    CHECK_FALSE(result.sidecar.verification.digest_checked);
    CHECK(result.sidecar.verification.sound());
    CHECK(result.sidecar.verification.summary().find("no digest published") != std::string::npos);
    // And it must not claim more than it did.
    CHECK(result.sidecar.verification.summary().find("digest ok") == std::string::npos);
}

TEST_CASE("an existing file is never silently overwritten", "[models][acquire]") {
    // The file may be the model a running session is using.
    Scratch scratch;
    {
        std::ofstream out(scratch.destination(), std::ios::binary);
        out << "an existing model";
    }

    SourcePromise promise;
    promise.source = "huggingface";
    const AcquireResult result =
        acquire(scratch.destination(), promise, bytes_source(good_model()));

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("already exists") != std::string::npos);

    std::ifstream in(scratch.destination(), std::ios::binary);
    const std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(content == "an existing model");
}

TEST_CASE("a stale partial from an earlier run is discarded, never resumed",
          "[models][acquire][failure]") {
    // Resuming belongs to a source that can make ranged requests. A partial of
    // unknown provenance is exactly the input this ladder exists to reject, so
    // it is cleared before the transfer rather than appended to.
    Scratch scratch;
    std::filesystem::path partial = scratch.destination();
    partial += ".partial";
    {
        std::ofstream out(partial, std::ios::binary);
        out << "bytes from who knows where";
    }

    const std::string bytes = good_model();
    SourcePromise promise;
    promise.source = "huggingface";
    promise.size = static_cast<std::int64_t>(bytes.size());

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    REQUIRE(result.ok);
    // The size check would have failed had the stale bytes been prepended.
    CHECK(result.sidecar.file_size == static_cast<std::int64_t>(bytes.size()));
    CHECK(result.sidecar.file_digest == apogee::models::sha256_hex(bytes));
}

TEST_CASE("reverify compares against the file on disk, not the published pin",
          "[models][acquire][repair]") {
    // The provenance/integrity split, doing the job it exists for: after a
    // transform the on-disk digest legitimately differs from what the source
    // published, and repair must not report that as corruption.
    Scratch scratch;
    const std::string bytes = good_model();

    apogee::models::Sidecar sidecar;
    sidecar.published_digest = std::string(64, 'b');  // what the source claimed
    sidecar.published_size = 999999;
    sidecar.file_digest = apogee::models::sha256_hex(bytes);
    sidecar.file_size = static_cast<std::int64_t>(bytes.size());
    sidecar.transform = "vision-stripped";

    {
        std::ofstream out(scratch.destination(), std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    const apogee::models::Verification verification =
        apogee::models::reverify(scratch.destination(), sidecar);

    CHECK(verification.sound());
    CHECK(verification.digest_matched);
    CHECK(verification.size_matched);
}

TEST_CASE("reverify catches a file that changed under its record", "[models][acquire][repair]") {
    Scratch scratch;
    const std::string bytes = good_model();

    apogee::models::Sidecar sidecar;
    sidecar.file_digest = apogee::models::sha256_hex(bytes);
    sidecar.file_size = static_cast<std::int64_t>(bytes.size());

    {
        std::ofstream out(scratch.destination(), std::ios::binary);
        out << bytes << "extra bytes appended later";
    }

    const apogee::models::Verification verification =
        apogee::models::reverify(scratch.destination(), sidecar);

    CHECK_FALSE(verification.sound());
    CHECK_FALSE(verification.size_matched);
}

TEST_CASE("the unrunnable-architecture list is advisory and never consulted by acquire",
          "[models][acquire][policy]") {
    // **No forbidden models.** The warning exists so a user is not surprised by
    // a multi-gigabyte download of something that will not run; it must not
    // become a refusal, so `acquire` never asks.
    CHECK(apogee::models::is_known_unrunnable("bert"));
    CHECK(apogee::models::is_known_unrunnable("T5"));  // case-insensitive
    CHECK_FALSE(apogee::models::is_known_unrunnable("llama"));

    Scratch scratch;
    // A GGUF declaring an architecture from the advisory list still lands.
    const std::string bytes = apogee::testing::minimal_gguf("bert");
    SourcePromise promise;
    promise.source = "huggingface";

    const AcquireResult result = acquire(scratch.destination(), promise, bytes_source(bytes));

    CHECK(result.ok);
    CHECK(std::filesystem::exists(scratch.destination()));
}
