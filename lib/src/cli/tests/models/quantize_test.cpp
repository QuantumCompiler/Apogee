#include "models/quantize.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include "backends/llama_runtime.h"
#include "models/gguf_inspect.h"
#include "support/gguf_builder.h"

/// Quantization, and the refusal that must exist in every build.
///
/// **The refusal is the part this file can always assert.** `llama_model_quantize`
/// needs llama.cpp, which is off by default and absent from the merge-blocking
/// CI target — so on that target every test here exercises the `#else` branch.
/// That is not a gap: the branch exists precisely so a build without llama.cpp
/// says "this build cannot, here is the flag" rather than making the subcommand
/// vanish, and a vanished subcommand reads as "Apogee cannot do this".
///
/// The tests below are written to pass in **both** builds by keying on
/// `llama_available()` where the answer differs, so the file means something on
/// either target rather than being skipped on one.
namespace {

using apogee::models::quantize;
using apogee::models::QuantizeResult;

struct Scratch {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("apogee-quant-" + std::to_string(counter()));

    Scratch() {
        std::error_code code;
        std::filesystem::create_directories(dir, code);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        std::filesystem::remove_all(dir, code);
    }

    [[nodiscard]] std::filesystem::path write(std::string_view name, std::string_view bytes) const {
        const std::filesystem::path path = dir / std::string{name};
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

}  // namespace

TEST_CASE("an unknown quantization type lists the accepted ones", "[models][quantize]") {
    // Checked before anything touches the filesystem, so a typo costs nothing
    // and the message can name every alternative.
    const QuantizeResult result = quantize("/in.gguf", "/out.gguf", "Q9_ULTRA");

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("unknown quantization type") != std::string::npos);
    CHECK(result.error.find("Q4_K_M") != std::string::npos);
}

TEST_CASE("the type name is matched case-insensitively", "[models][quantize]") {
    // The type is shouted in every filename that carries it, and a user
    // typing it lowercase has made no mistake worth an error.
    Scratch scratch;
    const std::filesystem::path input =
        scratch.write("in.gguf", apogee::testing::minimal_gguf("llama"));

    const QuantizeResult result = quantize(input, scratch.dir / "out.gguf", "q4_k_m");
    // Whatever happens next, it is NOT the unknown-type refusal.
    CHECK(result.error.find("unknown quantization type") == std::string::npos);
}

TEST_CASE("a missing input is reported before llama.cpp is asked", "[models][quantize]") {
    Scratch scratch;
    const QuantizeResult result =
        quantize(scratch.dir / "absent.gguf", scratch.dir / "out.gguf", "Q4_K_M");

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("no such file") != std::string::npos);
}

TEST_CASE("an existing output is never silently replaced", "[models][quantize]") {
    // The same rule acquisition follows: the file may be a model in use, and a
    // quantize that overwrote it would be the most expensive convenience.
    Scratch scratch;
    const std::filesystem::path input =
        scratch.write("in.gguf", apogee::testing::minimal_gguf("llama"));
    const std::filesystem::path output = scratch.write("out.gguf", "an existing model");

    const QuantizeResult result = quantize(input, output, "Q4_K_M");

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("already exists") != std::string::npos);

    std::ifstream in(output, std::ios::binary);
    const std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(content == "an existing model");
}

TEST_CASE("a non-GGUF input is refused with its reason", "[models][quantize]") {
    // Checked here rather than left to llama.cpp, whose own failure for this is
    // a log line and a non-zero return with nothing attached naming the file.
    Scratch scratch;
    const std::filesystem::path input = scratch.write("notamodel.gguf", "just some text");

    const QuantizeResult result = quantize(input, scratch.dir / "out.gguf", "Q4_K_M");

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("not a readable GGUF") != std::string::npos);
}

TEST_CASE("a build without llama.cpp refuses by naming the flag", "[models][quantize][build]") {
    // THE assertion this file exists for. The command is present in every
    // build; what differs is whether it can act. A refusal that names the flag
    // tells a user their build is the problem -- an absent subcommand tells
    // them the product is.
    Scratch scratch;
    const std::filesystem::path input =
        scratch.write("in.gguf", apogee::testing::minimal_gguf("llama"));

    const QuantizeResult result = quantize(input, scratch.dir / "out.gguf", "Q4_K_M");

    if (apogee::backends::llama_available()) {
        // With llama.cpp present it must NOT give the not-built-in answer,
        // whatever else happens to a one-tensor fixture.
        CHECK(result.error.find("APOGEE_ENABLE_LLAMA") == std::string::npos);
    } else {
        CHECK_FALSE(result.ok);
        CHECK(result.error.find("llama.cpp was not compiled in") != std::string::npos);
        CHECK(result.error.find("-DAPOGEE_ENABLE_LLAMA=ON") != std::string::npos);
    }
}

TEST_CASE("an already-quantized input is refused before llama.cpp is asked", "[models][quantize]") {
    // Found by running it. llama.cpp DOES refuse to requantize -- but only
    // after a couple of hundred per-tensor log lines, by which point
    // "requantizing from type q8_0 is disabled" has scrolled off the top and
    // the user sees a bare failure. The header already told us; saying it first
    // costs nothing.
    Scratch scratch;
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.string_kv("general.architecture", "llama");
    builder.u32_kv("general.file_type", 7);  // MOSTLY_Q8_0
    builder.tensor("token_embd.weight");

    const std::filesystem::path input = scratch.write("q8.gguf", builder.bytes());
    const QuantizeResult result = quantize(input, scratch.dir / "out.gguf", "Q4_K_M");

    CHECK_FALSE(result.ok);
    CHECK(result.error.find("already quantized") != std::string::npos);
    // And it names the way forward rather than only the problem.
    CHECK(result.error.find("F16") != std::string::npos);
}

TEST_CASE("an F16 input is not mistaken for a quantized one", "[models][quantize]") {
    // The false positive that would break the only path that works.
    Scratch scratch;
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.string_kv("general.architecture", "llama");
    builder.u32_kv("general.file_type", 1);  // MOSTLY_F16
    builder.tensor("token_embd.weight");

    const std::filesystem::path input = scratch.write("f16.gguf", builder.bytes());
    const QuantizeResult result = quantize(input, scratch.dir / "out.gguf", "Q4_K_M");

    CHECK(result.error.find("already quantized") == std::string::npos);
}

TEST_CASE("a header with no file_type is not assumed quantized", "[models][quantize]") {
    // Absent is not the same as quantized. Guessing here would refuse a model
    // that would have converted fine.
    Scratch scratch;
    const std::filesystem::path input =
        scratch.write("plain.gguf", apogee::testing::minimal_gguf("llama"));

    const QuantizeResult result = quantize(input, scratch.dir / "out.gguf", "Q4_K_M");
    CHECK(result.error.find("already quantized") == std::string::npos);
}

TEST_CASE("the accepted types are listed with descriptions", "[models][quantize]") {
    const auto types = apogee::models::quant_types();
    REQUIRE_FALSE(types.empty());

    for (const auto& type : types) {
        INFO("type: " << type.name);
        CHECK_FALSE(type.name.empty());
        // A bare list of names means nothing to someone choosing between them.
        CHECK_FALSE(type.summary.empty());
    }
}

// ---- llama.cpp's log stays off the terminal (M3) ------------------------------

#if !defined(_WIN32)
namespace {

/// Everything written to file descriptors 1 and 2 while `action` runs --
/// below any `std::cerr` redirection, where llama.cpp's own logger writes.
[[nodiscard]] std::string terminal_output(const std::function<void()>& action) {
    const std::filesystem::path capture =
        std::filesystem::temp_directory_path() /
        ("apogee-quant-terminal-" + std::to_string(Scratch::counter()));
    const int file = ::open(capture.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    REQUIRE(file >= 0);
    std::fflush(stdout);
    std::fflush(stderr);
    const int saved_out = ::dup(1);
    const int saved_err = ::dup(2);
    ::dup2(file, 1);
    ::dup2(file, 2);
    action();
    std::fflush(stdout);
    std::fflush(stderr);
    ::dup2(saved_out, 1);
    ::dup2(saved_err, 2);
    ::close(saved_out);
    ::close(saved_err);
    ::close(file);
    std::ifstream in{capture, std::ios::binary};
    std::string said{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    std::error_code code;
    std::filesystem::remove(capture, code);
    return said;
}

}  // namespace

TEST_CASE("a quantization writes nothing of llama.cpp's to the terminal, and counts its tensors",
          "[models][quantize][llama]") {
    if (!apogee::models::quantize_supported()) {
        SKIP("this build has no llama.cpp to quantize with");
    }
    // A real quantization -- of a 2 KiB model built here, nothing downloaded.
    // llama.cpp's loader dumps every metadata key and a line per tensor; on
    // a real model that was hundreds of lines over `models quantize`'s own.
    const Scratch scratch;
    const std::filesystem::path input =
        scratch.write("tiny-F16.gguf", apogee::testing::quantizable_gguf());
    const std::filesystem::path output = scratch.dir / "tiny-Q4_K_M.gguf";
    QuantizeResult result;
    std::vector<std::pair<std::size_t, std::size_t>> counted;
    const std::string said = terminal_output([&] {
        result = quantize(input, output, "Q4_K_M", [&counted](std::size_t done, std::size_t total) {
            counted.emplace_back(done, total);
        });
    });
    INFO(said);
    REQUIRE(result.ok);
    CHECK(said.empty());
    CHECK(apogee::models::inspect_gguf(output).is_quantized());
    // llama.cpp's per-tensor lines became the count.
    CHECK(counted == std::vector<std::pair<std::size_t, std::size_t>>{{1, 1}});
}

TEST_CASE("after a quantization the backend hears llama.cpp again", "[models][quantize][llama]") {
    if (!apogee::models::quantize_supported()) {
        SKIP("this build has no llama.cpp to quantize with");
    }
    // A quantization borrows llama.cpp's one log callback; the backend keeps
    // its own there, to attach llama.cpp's reason to a model it cannot load.
    // Left pointing at the finished quantization, that reason would be lost,
    // or worse.
    std::string reason;
    const std::unique_ptr<apogee::backends::LlamaRuntime> runtime =
        apogee::backends::make_llama_runtime(reason);
    REQUIRE(runtime != nullptr);
    const Scratch scratch;
    apogee::backends::ModelLoad broken;
    broken.path =
        scratch.write("unloadable.gguf", apogee::testing::quantizable_gguf(false)).string();
    broken.gpu_layers = 0;
    std::string before;
    REQUIRE(runtime->load(broken, before) == nullptr);
    REQUIRE(before.find("llama.cpp said") != std::string::npos);

    const std::filesystem::path input =
        scratch.write("tiny-F16.gguf", apogee::testing::quantizable_gguf());
    REQUIRE(quantize(input, scratch.dir / "tiny-Q4_K_M.gguf", "Q4_K_M").ok);

    std::string after;
    REQUIRE(runtime->load(broken, after) == nullptr);
    CHECK(after.find("llama.cpp said") != std::string::npos);
}

TEST_CASE("a quantization llama.cpp refuses carries its reason, not its log",
          "[models][quantize][llama]") {
    if (!apogee::models::quantize_supported()) {
        SKIP("this build has no llama.cpp to quantize with");
    }
    const Scratch scratch;
    const std::filesystem::path input =
        scratch.write("no-context.gguf", apogee::testing::quantizable_gguf(false));
    QuantizeResult result;
    const std::string said = terminal_output(
        [&] { result = quantize(input, scratch.dir / "no-context-Q4_K_M.gguf", "Q4_K_M"); });
    INFO(said);
    REQUIRE_FALSE(result.ok);
    CHECK(said.empty());
    CHECK(result.error.find("llama.cpp could not quantize") != std::string::npos);
    CHECK(result.error.find("\nllama.cpp said: ") != std::string::npos);
    CHECK(result.error.find("llama.context_length") != std::string::npos);
}
#endif

TEST_CASE("a level is named the way the table spells it", "[models][quantize]") {
    CHECK(apogee::models::canonical_quant_type("q4_k_m") == "Q4_K_M");
    CHECK(apogee::models::canonical_quant_type("Q8_0") == "Q8_0");
    CHECK(apogee::models::canonical_quant_type("f16") == "F16");
    CHECK_FALSE(apogee::models::canonical_quant_type("Q4_K_S").has_value());
    CHECK_FALSE(apogee::models::canonical_quant_type("").has_value());
}
