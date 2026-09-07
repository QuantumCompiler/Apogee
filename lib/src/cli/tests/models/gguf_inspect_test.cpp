#include "models/gguf_inspect.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/gguf_builder.h"

/// The GGUF header reader.
///
/// **Fixtures are built here rather than committed.** A real GGUF is megabytes
/// even for a tiny model, and — decisively — the cases that matter are the
/// *malformed* ones: a truncated download, a length field that runs past the
/// end, a Git LFS pointer committed instead of the model. Those cannot be
/// obtained by downloading something; they have to be constructed. Building
/// them in the test also means the bytes are readable in the diff instead of
/// being an opaque blob.
///
/// The parser was separately verified against four real GGUFs on the
/// development machine (llama3.2-3b, qwen3.5-122b, qwen3.6-27b, stories260K);
/// that is characterization and is recorded in MILESTONES, not here, because a
/// test that needs a 71 GB file is not a test.
namespace {

using apogee::models::GgufInfo;
using apogee::models::inspect_gguf;
using Builder = apogee::testing::GgufBuilder;

/// Writes `bytes` to a scratch file and inspects it.
[[nodiscard]] GgufInfo inspect_bytes(const std::string& bytes, const std::string& name) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("apogee-gguf-" + name + ".gguf");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    const GgufInfo info = inspect_gguf(path);
    std::error_code code;
    std::filesystem::remove(path, code);
    return info;
}

/// A minimal well-formed GGUF: two metadata keys, three tensors.
[[nodiscard]] std::string well_formed() {
    Builder builder;
    builder.magic().u32(3).u64(3).u64(3);
    builder.string_kv("general.architecture", "llama");
    builder.string_kv("general.name", "a-test-model");
    builder.u32_kv("llama.block_count", 32);
    builder.tensor("token_embd.weight").tensor("blk.0.attn_q.weight").tensor("output_norm.weight");
    return builder.bytes();
}

}  // namespace

TEST_CASE("a well-formed header yields architecture and tensor counts", "[models][gguf]") {
    const GgufInfo info = inspect_bytes(well_formed(), "ok");

    REQUIRE(info.parsed);
    CHECK(info.parse_error.empty());
    CHECK(info.version == 3);
    CHECK(info.architecture == "llama");
    CHECK(info.name == "a-test-model");
    CHECK(info.tensors == 3);
    CHECK(info.text_tensors == 3);
    CHECK_FALSE(info.has_vision_tensors());
    CHECK(info.file_size > 0);
}

TEST_CASE("vision tensors are counted apart from the text model", "[models][gguf]") {
    // The signal that a file is one of Ollama's combined text+vision blobs,
    // which older llama.cpp could not load and which `models repair` strips.
    Builder builder;
    builder.magic().u32(3).u64(4).u64(1);
    builder.string_kv("general.architecture", "gemma3");
    builder.tensor("token_embd.weight")
        .tensor("blk.0.attn_q.weight")
        .tensor("v.blk.0.attn_q.weight")
        .tensor("mm.0.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "vision");

    REQUIRE(info.parsed);
    CHECK(info.tensors == 4);
    CHECK(info.text_tensors == 2);
    CHECK(info.has_vision_tensors());
}

TEST_CASE("a truncated download is reported, not accepted", "[models][gguf]") {
    // THE case this reader exists for, and the one a magic-bytes check gets
    // wrong: the first four bytes are a perfectly valid GGUF magic.
    const std::string whole = well_formed();
    const GgufInfo info = inspect_bytes(whole.substr(0, whole.size() / 2), "truncated");

    CHECK_FALSE(info.parsed);
    CHECK_FALSE(info.parse_error.empty());
    // A partial read must not leave partial results looking like findings.
    CHECK(info.tensors == 0);
    CHECK(info.architecture.empty());
}

TEST_CASE("a Git LFS pointer is reported as not a GGUF", "[models][gguf]") {
    // The other failure Ommi recorded: the pointer file gets committed and the
    // model never arrives.
    const GgufInfo info = inspect_bytes(
        "version https://git-lfs.github.com/spec/v1\noid sha256:abc\nsize 123\n", "lfs");

    CHECK_FALSE(info.parsed);
    CHECK(info.parse_error.find("GGUF magic") != std::string::npos);
}

TEST_CASE("an empty file is reported rather than crashing", "[models][gguf]") {
    const GgufInfo info = inspect_bytes({}, "empty");
    CHECK_FALSE(info.parsed);
    CHECK_FALSE(info.parse_error.empty());
}

TEST_CASE("a missing file reports why", "[models][gguf]") {
    const GgufInfo info =
        inspect_gguf(std::filesystem::temp_directory_path() / "apogee-no-such-model.gguf");
    CHECK_FALSE(info.parsed);
    CHECK_FALSE(info.parse_error.empty());
}

TEST_CASE("an implausible string length is refused instead of allocated", "[models][gguf]") {
    // Every length in a GGUF comes from the file. Without a cap this is a
    // request to allocate 16 exabytes on behalf of a corrupt file.
    Builder builder;
    builder.magic().u32(3).u64(0).u64(1);
    builder.u64(0xFFFFFFFFFFFFFFFFULL);  // key length

    const GgufInfo info = inspect_bytes(builder.bytes(), "huge-string");
    CHECK_FALSE(info.parsed);
    CHECK_FALSE(info.parse_error.empty());
}

TEST_CASE("an implausible tensor count is refused instead of looped over", "[models][gguf]") {
    Builder builder;
    builder.magic().u32(3).u64(0xFFFFFFFFFFFFFFFFULL).u64(0);

    const GgufInfo info = inspect_bytes(builder.bytes(), "huge-tensors");
    CHECK_FALSE(info.parsed);
    CHECK(info.parse_error.find("implausible") != std::string::npos);
}

TEST_CASE("a nested metadata array is refused rather than recursed on", "[models][gguf]") {
    // The spec permits it and no real model emits it; supporting it would mean
    // recursion depth driven by file content.
    Builder builder;
    builder.magic().u32(3).u64(0).u64(1);
    builder.text("weird");
    builder.u32(9);  // Array
    builder.u32(9);  // of Array
    builder.u64(1);

    const GgufInfo info = inspect_bytes(builder.bytes(), "nested-array");
    CHECK_FALSE(info.parsed);
    CHECK(info.parse_error.find("nested") != std::string::npos);
}

TEST_CASE("a string array is stepped over without being materialised", "[models][gguf]") {
    // The token vocabulary is exactly this shape and is megabytes in every real
    // model. Skipping it correctly is what keeps the read cheap -- and getting
    // the skip wrong desynchronises everything after it, which is why a tensor
    // is asserted on the far side.
    Builder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.text("tokenizer.ggml.tokens");
    builder.u32(9);  // Array
    builder.u32(8);  // of String
    builder.u64(3);
    builder.text("alpha").text("beta").text("gamma");
    builder.string_kv("general.architecture", "qwen35");
    builder.tensor("token_embd.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "string-array");

    REQUIRE(info.parsed);
    CHECK(info.architecture == "qwen35");
    CHECK(info.tensors == 1);
}

TEST_CASE("a header with no architecture key parses and says so", "[models][gguf]") {
    // Absent is not the same as unreadable, and reporting it as a failure would
    // send a user to re-download a file that is fine.
    Builder builder;
    builder.magic().u32(3).u64(1).u64(1);
    builder.u32_kv("general.file_type", 15);
    builder.tensor("token_embd.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "no-arch");

    REQUIRE(info.parsed);
    CHECK(info.architecture.empty());
    CHECK(info.tensors == 1);
}
