#include "models/sidecar.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

/// The provenance/integrity record.
///
/// The assertions that carry weight here are about **honesty of reporting**:
/// that "no digest was published" is distinguishable from "the digest did not
/// match", and that after a transform the record still knows which digest
/// describes the file on disk.
namespace {

using apogee::models::parse_sidecar;
using apogee::models::serialize;
using apogee::models::Sidecar;
using apogee::models::Verification;

}  // namespace

TEST_CASE("a sidecar round-trips through JSON", "[models][sidecar]") {
    Sidecar original;
    original.ref = "llama3.2:3b";
    original.source = "ollama";
    original.source_url = "/home/u/.ollama/models/blobs/sha256-abc";
    original.published_digest = "abc123";
    original.published_size = 1234;
    original.pulled_at = "2026-09-07T12:00:00Z";
    original.file = "llama3.2-3b.gguf";
    original.file_digest = "def456";
    original.file_size = 1234;
    original.template_hint = "{{ .Prompt }}";
    original.verification.size_checked = true;
    original.verification.size_matched = true;
    original.verification.digest_checked = true;
    original.verification.digest_matched = true;
    original.verification.header_checked = true;
    original.verification.header_parsed = true;

    const auto parsed = parse_sidecar(serialize(original));
    REQUIRE(parsed.has_value());

    CHECK(parsed->ref == original.ref);
    CHECK(parsed->source == original.source);
    CHECK(parsed->published_digest == original.published_digest);
    CHECK(parsed->file_digest == original.file_digest);
    CHECK(parsed->file_size == original.file_size);
    CHECK(parsed->template_hint == original.template_hint);
    CHECK(parsed->verification.digest_matched);
}

TEST_CASE("expected_digest prefers the on-disk value after a transform", "[models][sidecar]") {
    // The distinction Ommi had to retrofit: after a transform the file no
    // longer matches what the source published, and an integrity check that
    // compared against the pin would report every transformed model as corrupt.
    Sidecar sidecar;
    sidecar.published_digest = "pinned";
    sidecar.file_digest = "ondisk";
    CHECK(sidecar.expected_digest() == "ondisk");

    sidecar.file_digest.clear();
    CHECK(sidecar.expected_digest() == "pinned");
}

TEST_CASE("expected_size prefers the on-disk value", "[models][sidecar]") {
    Sidecar sidecar;
    sidecar.published_size = 100;
    sidecar.file_size = 90;
    CHECK(sidecar.expected_size() == 90);

    sidecar.file_size = 0;
    CHECK(sidecar.expected_size() == 100);
}

TEST_CASE("a verification with fewer checks can still be sound", "[models][sidecar]") {
    // Under an open-model policy most sources publish no digest. A file that
    // passed every check that was possible is sound; it simply had fewer.
    Verification verification;
    verification.size_checked = true;
    verification.size_matched = true;
    verification.header_checked = true;
    verification.header_parsed = true;

    CHECK(verification.sound());
    CHECK_FALSE(verification.digest_checked);
}

TEST_CASE("a failed check is never sound", "[models][sidecar]") {
    Verification verification;
    verification.digest_checked = true;
    verification.digest_matched = false;
    CHECK_FALSE(verification.sound());
}

TEST_CASE("the summary distinguishes an absent digest from a failed one", "[models][sidecar]") {
    // The whole reason this is a report rather than a boolean. Two very
    // different situations must not read alike.
    Verification absent;
    absent.size_checked = true;
    absent.size_matched = true;
    const std::string absent_text = absent.summary();

    Verification failed;
    failed.digest_checked = true;
    failed.digest_matched = false;
    const std::string failed_text = failed.summary();

    CHECK(absent_text.find("no digest published") != std::string::npos);
    CHECK(failed_text.find("DIGEST MISMATCH") != std::string::npos);
    CHECK(absent_text != failed_text);
    // Neither may claim the bare word "verified".
    CHECK(absent_text.find("verified") == std::string::npos);
}

TEST_CASE("nothing checked says so rather than looking like success", "[models][sidecar]") {
    const Verification nothing;
    CHECK(nothing.summary().find("no digest published") != std::string::npos);
}

TEST_CASE("a corrupt sidecar parses to nothing rather than throwing", "[models][sidecar]") {
    // A missing or broken record means "provenance unknown", which is a state
    // a listing must be able to report -- not an error that stops it.
    CHECK_FALSE(parse_sidecar("not json at all").has_value());
    CHECK_FALSE(parse_sidecar("[1,2,3]").has_value());
    CHECK_FALSE(parse_sidecar("").has_value());
}

TEST_CASE("a sidecar written by an older build still parses", "[models][sidecar]") {
    // Forward compatibility in the direction that actually happens: fields
    // added later must default rather than fail the whole record.
    const auto parsed = parse_sidecar(R"({"ref":"x","source":"ollama","file":"x.gguf"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->ref == "x");
    CHECK(parsed->file_size == 0);
    CHECK_FALSE(parsed->verification.digest_checked);
}

TEST_CASE("the published fields are always written, even when empty", "[models][sidecar]") {
    // So a reader can tell "the source published no digest" from "this record
    // predates the field" without guessing.
    const Sidecar empty;
    const std::string text = serialize(empty);
    CHECK(text.find("published_digest") != std::string::npos);
    CHECK(text.find("published_size") != std::string::npos);
}
