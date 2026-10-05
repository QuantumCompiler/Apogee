#include "modelstore/footprint.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/kv_cache.h"
#include "modelstore/sidecar.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"

/// A suite priced against a machine (27e): admission tables over a scripted
/// store -- GGUF headers the builder writes, records beside them claiming the
/// sizes the store would have recorded -- and fixed machine budgets. The
/// numbers are 26a's own: Llama 3.2's geometries, whose caches llama.cpp
/// sized in MILESTONES.
namespace {

using apogee::harness::BackendConfig;
using apogee::harness::Config;
using apogee::harness::SuiteBackend;
using apogee::models::Admission;
using apogee::models::GgufInfo;
using apogee::models::MachineBudget;
using apogee::models::MemberFootprint;
using apogee::models::Sidecar;
using apogee::models::SuiteFootprint;

constexpr std::int64_t kMiB = std::int64_t{1024} * 1024;
constexpr std::int64_t kGiB = 1024 * kMiB;

/// A llama header with `blocks` layers, `embedding` wide, `heads` query and
/// `kv_heads` key-value heads -- 26a's arithmetic over it is the cache.
[[nodiscard]] std::string llama_header(std::uint32_t blocks, std::uint32_t embedding,
                                       std::uint32_t heads, std::uint32_t kv_heads) {
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(6);
    builder.string_kv("general.architecture", "llama");
    builder.u32_kv("llama.block_count", blocks);
    builder.u32_kv("llama.context_length", 131072);
    builder.u32_kv("llama.embedding_length", embedding);
    builder.u32_kv("llama.attention.head_count", heads);
    builder.u32_kv("llama.attention.head_count_kv", kv_heads);
    builder.tensor("token_embd.weight");
    return builder.bytes();
}

/// Llama 3.2 3B's geometry: 1904 MiB of q8_0 cache at 32K, 238 MiB at 4K.
[[nodiscard]] std::string llama_3b() {
    return llama_header(28, 3072, 24, 8);
}

/// Llama 3.2 1B's: 544 MiB at 32K, 68 MiB at 4K.
[[nodiscard]] std::string llama_1b() {
    return llama_header(16, 2048, 32, 8);
}

/// A scripted store: model files with the records the store keeps.
struct Store {
    apogee::testing::TempDir dir{"footprint-" + std::to_string(std::random_device{}())};

    /// Writes `<name>.gguf` with `header`, and -- unless `recorded` is
    /// nullopt -- the record beside it saying the file is that many bytes.
    [[nodiscard]] std::string model(const std::string& name, const std::string& header,
                                    std::optional<std::int64_t> recorded) const {
        const std::filesystem::path file = dir.path() / (name + ".gguf");
        std::ofstream{file, std::ios::binary} << header;
        if (recorded.has_value()) {
            Sidecar record;
            record.file = file.filename().string();
            record.file_size = *recorded;
            REQUIRE(apogee::models::write_sidecar(file, record));
        }
        return file.string();
    }
};

[[nodiscard]] Config parse(const std::string& yaml) {
    return apogee::harness::parse_config(yaml, "footprint_test");
}

/// The member of `footprint` on `backend`.
[[nodiscard]] const MemberFootprint& member(const SuiteFootprint& footprint,
                                            const std::string& backend) {
    for (const MemberFootprint& each : footprint.members) {
        if (each.backend == backend) {
            return each;
        }
    }
    FAIL("no member " << backend);
    return footprint.members.front();
}

}  // namespace

TEST_CASE("a member is the store's record and 26a's cache, and nothing else",
          "[models][footprint]") {
    // By construction: the weights are the record's size, the cache exactly
    // what `local_window` states for the entry as it runs -- the function
    // takes those types, so there is no second estimator to drift.
    GgufInfo header;
    header.parsed = true;
    header.architecture = "llama";
    header.attention.block_count = 28;
    header.attention.context_length = 131072;
    header.attention.embedding_length = 3072;
    header.attention.head_count = {24};
    header.attention.head_count_kv = {8};

    Sidecar record;
    record.file_size = 2019 * kMiB;
    BackendConfig as_run;
    as_run.type = apogee::harness::BackendType::LlamaCpp;
    as_run.context_size = 4096;

    const SuiteBackend seat{.backend = "helper", .roles = {"utility"}};
    const MemberFootprint priced =
        apogee::models::gguf_member_footprint(seat, as_run, record, std::nullopt, header);
    CHECK(priced.local);
    CHECK(priced.weights == 2019 * kMiB);
    REQUIRE(priced.window.has_value());
    const apogee::models::LocalWindow expected = apogee::models::local_window(header, as_run);
    CHECK(priced.window->window == expected.window);
    CHECK(priced.window->cache_bytes == expected.cache_bytes);
    CHECK(priced.window->cache_bytes == 238 * kMiB);
    CHECK(priced.bytes() == (2019 + 238) * kMiB);
    CHECK(priced.unknown.empty());
    CHECK(priced.roles == std::vector<std::string>{"utility"});

    // A projector the entry loads is weights too -- and unknown without its
    // record, never left out.
    as_run.mmproj_path = "/models/helper-mmproj.gguf";
    Sidecar projector;
    projector.file_size = 812 * kMiB;
    CHECK(apogee::models::gguf_member_footprint(seat, as_run, record, projector, header).weights ==
          2831 * kMiB);
    const MemberFootprint unrecorded =
        apogee::models::gguf_member_footprint(seat, as_run, record, std::nullopt, header);
    CHECK_FALSE(unrecorded.bytes().has_value());
    CHECK(unrecorded.unknown == "its projector has no recorded size");

    // No record at all; a record of no size; a header that would not read; an
    // architecture whose cache cannot be sized: each unknown, and said.
    as_run.mmproj_path.clear();
    CHECK(apogee::models::gguf_member_footprint(seat, as_run, std::nullopt, std::nullopt, header)
              .unknown == "no recorded size");
    CHECK(apogee::models::gguf_member_footprint(seat, as_run, Sidecar{}, std::nullopt, header)
              .unknown == "no recorded size");
    CHECK(apogee::models::gguf_member_footprint(seat, as_run, record, std::nullopt, GgufInfo{})
              .unknown == "its header could not be read");
    header.attention.latent_attention = true;
    CHECK(
        apogee::models::gguf_member_footprint(seat, as_run, record, std::nullopt, header).unknown ==
        "its cache size is not known for this architecture");
}

TEST_CASE("admission: fits, over budget, unknown size, unknown budget, nothing held",
          "[models][footprint]") {
    const Store store;
    const std::string root = store.model("root", llama_3b(), 1926 * kMiB);
    const std::string helper = store.model("helper", llama_1b(), 770 * kMiB);
    const std::string loose = store.model("loose", llama_1b(), std::nullopt);
    const Config config = parse(R"(models:
  default: root
backends:
  root:
    type: llamacpp
    model_path: )" + root + R"(
  helper:
    type: llamacpp
    model_path: )" + helper + R"(
  loose:
    type: llamacpp
    model_path: )" + loose + R"(
  claude:
    type: anthropic
    model: claude-sonnet
  mlxy:
    type: mlx
    model_path: /models/mlx/nothing
suites:
  research:
    members:
      chat: root
      utility:
        backend: helper
        context_size: 4096
      embedding: helper
  partial:
    members:
      chat: root
      embedding: loose
  cloud:
    members:
      chat: claude
  apple:
    members:
      chat: mlxy
)");

    // root at the default 32K: 1926 + 1904; helper at its pin: 770 + 68,
    // counted once for the two roles it answers for.
    const std::int64_t research = (1926 + 1904 + 770 + 68) * kMiB;

    SECTION("fits") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "research", MachineBudget{.bytes = 96 * kGiB});
        REQUIRE(footprint.members.size() == 2);
        CHECK(member(footprint, "helper").roles ==
              std::vector<std::string>{"embedding", "utility"});
        CHECK(member(footprint, "helper").window->window == 4096);
        CHECK(member(footprint, "root").window->window == 32768);
        CHECK(footprint.known_bytes() == research);
        CHECK(footprint.needed() == research + apogee::models::kFitMargin);
        CHECK_FALSE(footprint.has_unknown());
        CHECK(footprint.admission() == Admission::Fits);
    }
    SECTION("over budget, by the arithmetic, at the margin's edge") {
        const std::int64_t needed = research + apogee::models::kFitMargin;
        CHECK(apogee::models::suite_footprint(config, "research", MachineBudget{.bytes = needed})
                  .admission() == Admission::Fits);
        CHECK(
            apogee::models::suite_footprint(config, "research", MachineBudget{.bytes = needed - 1})
                .admission() == Admission::OverBudget);
    }
    SECTION("an unknown size is named, never guessed, and never refuses") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "partial", MachineBudget{.bytes = 96 * kGiB});
        CHECK(member(footprint, "loose").unknown == "no recorded size");
        CHECK(footprint.has_unknown());
        CHECK(footprint.known_bytes() == (1926 + 1904) * kMiB);
        CHECK(footprint.admission() == Admission::FitsAsFarAsKnown);
        // What IS known can still be over: that refuses.
        CHECK(apogee::models::suite_footprint(config, "partial", MachineBudget{.bytes = 2 * kGiB})
                  .admission() == Admission::OverBudget);
    }
    SECTION("an unknown budget states the footprint and judges nothing") {
        CHECK(apogee::models::suite_footprint(config, "research", MachineBudget{}).admission() ==
              Admission::BudgetUnknown);
    }
    SECTION("a cloud suite holds nothing here, whatever the machine") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "cloud", MachineBudget{.bytes = 1});
        CHECK_FALSE(footprint.holds_local());
        CHECK(footprint.needed() == 0);
        CHECK(member(footprint, "claude").bytes() == 0);
        CHECK(footprint.admission() == Admission::NothingHeld);
    }
    SECTION("an MLX member is unknown, not guessed -- and still admitted") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "apple", MachineBudget{.bytes = 96 * kGiB});
        CHECK(member(footprint, "mlxy").local);
        CHECK_FALSE(member(footprint, "mlxy").bytes().has_value());
        CHECK(member(footprint, "mlxy").unknown == "an MLX model's footprint is not stated yet");
        CHECK(footprint.admission() == Admission::FitsAsFarAsKnown);
    }
}

TEST_CASE("a suite is priced at its own pins, active or not", "[models][footprint]") {
    const Store store;
    const std::string model = store.model("shared", llama_3b(), 1926 * kMiB);
    Config config = parse(R"(models:
  default: shared
  default_suite: roomy
backends:
  Shared:
    type: llamacpp
    model_path: )" + model +
                          R"(
suites:
  roomy:
    members:
      chat: shared
  tight:
    members:
      chat:
        backend: SHARED
        context_size: 4096
)");
    // Another suite active, or none: the suite asked about pins its own window,
    // and its member is named as the config spells the backend.
    const SuiteFootprint tight =
        apogee::models::suite_footprint(config, "tight", MachineBudget{.bytes = 96 * kGiB});
    REQUIRE(tight.members.size() == 1);
    CHECK(tight.members.front().backend == "Shared");
    CHECK(tight.members.front().window->window == 4096);
    CHECK(tight.members.front().window->cache_bytes == 238 * kMiB);
    CHECK(apogee::models::suite_footprint(config, "roomy", {}).members.front().window->window ==
          32768);
    config.models.default_suite.clear();
    CHECK(apogee::models::suite_footprint(config, "tight", {}).members.front().window->window ==
          4096);
    // No such suite: nothing to price.
    CHECK(apogee::models::suite_footprint(config, "nope", {}).members.empty());
}
