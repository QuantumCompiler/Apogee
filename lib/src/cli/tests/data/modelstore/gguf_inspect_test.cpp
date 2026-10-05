#include "modelstore/gguf_inspect.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>
#include <string>
#include <utility>
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

TEST_CASE("an all-vision file is a projector, not a combined blob", "[models][gguf]") {
    // Found by inspecting a real mmproj: it has 198 tensors and every one of
    // them is a vision tensor. The first version reported it as a "combined
    // text+vision blob", which tells a user something is wrong with a file that
    // is exactly what mmproj_path wants.
    Builder builder;
    builder.magic().u32(3).u64(3).u64(1);
    builder.string_kv("general.architecture", "clip");
    builder.tensor("v.blk.0.attn_q.weight").tensor("v.blk.1.attn_q.weight").tensor("mm.0.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "projector");

    REQUIRE(info.parsed);
    CHECK(info.tensors == 3);
    CHECK(info.text_tensors == 0);
    CHECK(info.is_projector());
    // The two are mutually exclusive: a projector is not "combined".
    CHECK_FALSE(info.has_vision_tensors());
}

TEST_CASE("text plus vision in one file is combined, not a projector", "[models][gguf]") {
    Builder builder;
    builder.magic().u32(3).u64(3).u64(1);
    builder.string_kv("general.architecture", "gemma3");
    builder.tensor("token_embd.weight")
        .tensor("blk.0.attn_q.weight")
        .tensor("v.blk.0.attn_q.weight");

    const GgufInfo info = inspect_bytes(builder.bytes(), "combined2");

    REQUIRE(info.parsed);
    CHECK(info.has_vision_tensors());
    CHECK_FALSE(info.is_projector());
}

TEST_CASE("a plain text model is neither", "[models][gguf]") {
    const GgufInfo info = inspect_bytes(well_formed(), "textonly");
    REQUIRE(info.parsed);
    CHECK_FALSE(info.is_projector());
    CHECK_FALSE(info.has_vision_tensors());
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
    // The other download failure: the pointer file gets committed and the
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

TEST_CASE("a chat template is noticed without being read", "[models][gguf]") {
    // Its presence is the fact that matters: a file without one is almost
    // always a base model. Stepping over it must leave the read in step.
    Builder with;
    with.magic().u32(3).u64(1).u64(2);
    with.string_kv("tokenizer.chat_template", "{% for m in messages %}{{ m.content }}{% endfor %}");
    with.string_kv("general.architecture", "llama");
    with.tensor("token_embd.weight");
    const GgufInfo templated = inspect_bytes(with.bytes(), "templated");
    REQUIRE(templated.parsed);
    CHECK(templated.has_chat_template);
    CHECK(templated.architecture == "llama");
    CHECK(templated.tensors == 1);

    const GgufInfo bare = inspect_bytes(well_formed(), "bare");
    REQUIRE(bare.parsed);
    CHECK_FALSE(bare.has_chat_template);
}

TEST_CASE("a template's reasoning is read: a switch, a think block, or neither",
          "[models][gguf][thinking]") {
    // 26i: what `models info` says a local model's thinking control can do.
    const auto read = [](std::string_view text) {
        Builder builder;
        builder.magic().u32(3).u64(1).u64(2);
        builder.string_kv("tokenizer.chat_template", text);
        builder.string_kv("general.architecture", "qwen3");
        builder.tensor("token_embd.weight");
        const GgufInfo info = inspect_bytes(builder.bytes(), "templated");
        REQUIRE(info.parsed);
        // The read stays in step past the template it now reads.
        CHECK(info.architecture == "qwen3");
        return info.template_thinking;
    };
    const auto qwen = read("{% if enable_thinking is false %}<think>\n\n</think>{% endif %}");
    CHECK(qwen.switchable);
    CHECK(qwen.reasons);
    const auto gpt_oss = read("{{ reasoning_effort | default('medium') }}");
    CHECK(gpt_oss.switchable);
    const auto always = read("{{ '<think>' }}{{ m.reasoning_content }}");
    CHECK_FALSE(always.switchable);
    CHECK(always.reasons);
    const auto llama = read("{% for m in messages %}{{ m.content }}{% endfor %}");
    CHECK_FALSE(llama.switchable);
    CHECK_FALSE(llama.reasons);
}

TEST_CASE("a projector's encoder flags are read", "[models][gguf]") {
    // What `check` and the audio capability answer from, without a load.
    const GgufInfo both = inspect_bytes(apogee::testing::projector_gguf(true, true), "both");
    REQUIRE(both.parsed);
    CHECK(both.projector_vision);
    CHECK(both.projector_audio);
    CHECK(both.is_projector());

    const GgufInfo seeing = inspect_bytes(apogee::testing::projector_gguf(true, false), "seeing");
    CHECK(seeing.projector_vision);
    CHECK_FALSE(seeing.projector_audio);

    const GgufInfo hearing = inspect_bytes(apogee::testing::projector_gguf(false, true), "hearing");
    CHECK_FALSE(hearing.projector_vision);
    CHECK(hearing.projector_audio);

    // A model, not a projector: neither.
    const GgufInfo model = inspect_bytes(well_formed(), "model");
    CHECK_FALSE(model.projector_vision);
    CHECK_FALSE(model.projector_audio);

    // A projector cut off after its flags: a partial read is not a finding.
    const std::string whole = apogee::testing::projector_gguf(true, true);
    const GgufInfo cut = inspect_bytes(whole.substr(0, whole.size() - 4), "cut");
    CHECK_FALSE(cut.parsed);
    CHECK_FALSE(cut.projector_vision);
    CHECK_FALSE(cut.projector_audio);
}

// ---- what a read costs (M2) -------------------------------------------------

namespace {

/// An in-memory file that counts the seeks made on it.
class CountingBuffer final : public std::stringbuf {
public:
    explicit CountingBuffer(const std::string& bytes) : std::stringbuf{bytes, std::ios::in} {}

    [[nodiscard]] int seeks() const noexcept {
        return seeks_;
    }

protected:
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                     std::ios_base::openmode which) override {
        ++seeks_;
        return std::stringbuf::seekoff(offset, direction, which);
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode which) override {
        ++seeks_;
        return std::stringbuf::seekpos(position, which);
    }

private:
    int seeks_ = 0;
};

/// A header shaped like a real model's: a vocabulary of `tokens` strings, its
/// scores (400 KB of f32), the architecture after both, and one tensor.
[[nodiscard]] std::string vocabulary_model(std::size_t tokens) {
    std::vector<std::string> vocabulary;
    vocabulary.reserve(tokens);
    for (std::size_t index = 0; index < tokens; ++index) {
        vocabulary.push_back("token" + std::to_string(index));
    }
    Builder builder;
    builder.magic().u32(3).u64(1).u64(3);
    builder.string_array_kv("tokenizer.ggml.tokens", vocabulary);
    builder.f32_array_kv("tokenizer.ggml.scores", 100000);
    builder.string_kv("general.architecture", "qwen3");
    builder.tensor("token_embd.weight");
    return builder.bytes();
}

}  // namespace

TEST_CASE("a vocabulary is stepped over through the buffer, not a seek per string",
          "[models][gguf][cost]") {
    // `models list` took 14 seconds on a 31-model store because every
    // vocabulary string was skipped with a seek, which throws the buffer away:
    // a system call per token. Small skips now read through the buffer; one
    // large enough to be worth it -- the scores array -- is still one seek.
    const std::string bytes = vocabulary_model(50000);
    CountingBuffer buffer{bytes};
    std::istream in{&buffer};

    const GgufInfo info = inspect_gguf(in, bytes.size());

    REQUIRE(info.parsed);
    CHECK(info.architecture == "qwen3");  // read past both arrays, in step
    CHECK(info.tensors == 1);
    CHECK(std::cmp_equal(info.file_size, bytes.size()));
    CHECK(buffer.seeks() == 1);
}

TEST_CASE("the stream and the file read the same header", "[models][gguf][cost]") {
    for (const std::string& bytes : {well_formed(), vocabulary_model(1000)}) {
        CountingBuffer buffer{bytes};
        std::istream in{&buffer};
        const GgufInfo streamed = inspect_gguf(in, bytes.size());
        const GgufInfo filed = inspect_bytes(bytes, "stream-vs-file");
        REQUIRE(streamed.parsed);
        REQUIRE(filed.parsed);
        CHECK(streamed.architecture == filed.architecture);
        CHECK(streamed.name == filed.name);
        CHECK(streamed.tensors == filed.tensors);
        CHECK(streamed.text_tensors == filed.text_tensors);
        CHECK(streamed.file_size == filed.file_size);
    }
}

TEST_CASE("a stream shorter than it claims fails inside a skip, as a reason",
          "[models][gguf][cost]") {
    // The bounds check trusts the size it is given; a file cut short after its
    // size was taken ends the skip early, and that is a parse error with a
    // reason, never a header read past the bytes there were.
    const std::string whole = vocabulary_model(2000);
    // 2,000 tokens are some 34 KB of strings: 20,000 bytes ends among them.
    const std::string cut = whole.substr(0, 20000);
    CountingBuffer buffer{cut};
    std::istream in{&buffer};

    const GgufInfo info = inspect_gguf(in, whole.size());

    CHECK_FALSE(info.parsed);
    CHECK(info.parse_error == "could not read the file");
    CHECK(info.architecture.empty());
    CHECK(info.tensors == 0);

    // Cut inside the very last field, a tensor's data offset: nothing is read
    // after it, so only the skip's own check can tell.
    CountingBuffer tail{whole.substr(0, whole.size() - 4)};
    std::istream tail_in{&tail};
    const GgufInfo short_tail = inspect_gguf(tail_in, whole.size());
    CHECK_FALSE(short_tail.parsed);
    CHECK(short_tail.parse_error == "could not read the file");
}

TEST_CASE("a file's own sampling recommendation is read, whatever its number types",
          "[models][gguf][sampling]") {
    // What a conversion writes from generation_config.json (26h): floats for
    // the temperature and the cuts, an integer for top-k -- and the read
    // stays in step with the keys around them.
    Builder builder;
    builder.magic().u32(3).u64(1).u64(7);
    builder.string_kv("general.architecture", "qwen3vl");
    builder.u32_kv("general.sampling.top_k", 20);
    builder.f32_kv("general.sampling.top_p", 0.8F);
    builder.f32_kv("general.sampling.temp", 0.7F);
    builder.f32_kv("general.sampling.min_p", 0.05F);
    builder.f32_kv("general.sampling.penalty_repeat", 1.1F);
    // A sampling key nothing here uses is stepped over.
    builder.f32_kv("general.sampling.mirostat_tau", 5.0F);
    builder.tensor("token_embd.weight");
    const GgufInfo info = inspect_bytes(builder.bytes(), "sampling");
    REQUIRE(info.parsed);
    CHECK(info.architecture == "qwen3vl");
    REQUIRE(info.sampling.temperature.has_value());
    CHECK(*info.sampling.temperature == static_cast<double>(0.7F));
    CHECK(info.sampling.top_p == static_cast<double>(0.8F));
    CHECK(info.sampling.top_k == 20);
    CHECK(info.sampling.min_p == static_cast<double>(0.05F));
    CHECK(info.sampling.repeat_penalty == static_cast<double>(1.1F));
    CHECK(info.tensors == 1);
}

TEST_CASE("a file that recommends no sampling leaves every value unset",
          "[models][gguf][sampling]") {
    const GgufInfo info = inspect_bytes(well_formed(), "no-sampling");
    REQUIRE(info.parsed);
    CHECK_FALSE(info.sampling.temperature.has_value());
    CHECK_FALSE(info.sampling.top_p.has_value());
    CHECK_FALSE(info.sampling.top_k.has_value());
    CHECK_FALSE(info.sampling.min_p.has_value());
    CHECK_FALSE(info.sampling.repeat_penalty.has_value());
}

TEST_CASE("a sampling key of the wrong type is stepped over, not misread",
          "[models][gguf][sampling]") {
    // A string where a number belongs reads as no recommendation; the read
    // continues past it.
    Builder builder;
    builder.magic().u32(3).u64(1).u64(3);
    builder.string_kv("general.sampling.temp", "warm");
    builder.f32_kv("general.sampling.top_p", 0.9F);
    builder.string_kv("general.architecture", "llama");
    builder.tensor("token_embd.weight");
    const GgufInfo info = inspect_bytes(builder.bytes(), "sampling-typed");
    REQUIRE(info.parsed);
    CHECK_FALSE(info.sampling.temperature.has_value());
    CHECK(info.sampling.top_p == static_cast<double>(0.9F));
    CHECK(info.architecture == "llama");
}

TEST_CASE("a header that fails to read carries no sampling", "[models][gguf][sampling]") {
    Builder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.f32_kv("general.sampling.temp", 0.7F);
    builder.u32(42);  // a key whose length runs past the end
    const GgufInfo info = inspect_bytes(builder.bytes(), "sampling-truncated");
    REQUIRE_FALSE(info.parsed);
    CHECK_FALSE(info.sampling.temperature.has_value());
}
