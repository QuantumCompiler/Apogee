#include "modelstore/footprint.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
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
#include "support/draft_head_configs.h"
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

namespace {

/// Writes `bytes` bytes at `path`: an adapter file of a known size.
void sized(const std::filesystem::path& path, std::int64_t bytes) {
    std::ofstream{path, std::ios::binary} << std::string(static_cast<std::size_t>(bytes), 'w');
}

}  // namespace

TEST_CASE("a base is the model file, its projector and its cache type, however it is spelled",
          "[models][footprint][shared-base]") {
    using apogee::harness::KvCacheType;
    using apogee::models::base_key;
    const apogee::models::BaseKey plain = base_key("/m/base.gguf", "", KvCacheType::Q8_0, false);
    CHECK(base_key("/m/./x/../base.gguf", "", KvCacheType::Q8_0, false) == plain);
    CHECK(base_key("/m/other.gguf", "", KvCacheType::Q8_0, false) != plain);
    CHECK(base_key("/m/base.gguf", "/m/mmproj.gguf", KvCacheType::Q8_0, false) != plain);
    CHECK(base_key("/m/base.gguf", "", KvCacheType::F16, false) != plain);
    CHECK(base_key("/m/base.gguf", "", KvCacheType::Q8_0, true) != plain);
    // As the factory reads an entry: the default cache type unless named,
    // and its window and adapters no part of it.
    BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/m/base.gguf";
    CHECK(base_key(entry) == plain);
    entry.context_size = 4096;
    entry.adapters = {{.path = "/m/style.gguf"}};
    CHECK(base_key(entry) == plain);
    entry.cache_type = KvCacheType::Q8_0;
    CHECK(base_key(entry) == base_key("/m/base.gguf", "", KvCacheType::Q8_0, true));
}

TEST_CASE("one base, many roles: its weights counted once, each member its own cache and adapters",
          "[models][footprint][shared-base]") {
    const Store store;
    const std::string base = store.model("base", llama_3b(), 1926 * kMiB);
    const std::string other = store.model("other", llama_1b(), 770 * kMiB);
    const std::string unrecorded = store.model("unrecorded", llama_1b(), std::nullopt);
    const std::filesystem::path style = store.dir.path() / "style.gguf";
    const std::filesystem::path tone = store.dir.path() / "tone.gguf";
    sized(style, 3 * kMiB);
    sized(tone, kMiB);
    const std::string respelled = (store.dir.path() / "." / "base.gguf").string();
    const Config config = parse(R"(models:
  default: root
backends:
  root:
    type: llamacpp
    model_path: )" + base + R"(
  helper:
    type: llamacpp
    model_path: )" + base + R"(
    adapters: [{path: )" + style.string() +
                                R"(}]
  scribe:
    type: llamacpp
    model_path: )" + respelled + R"(
    adapters: [{path: )" + tone.string() +
                                R"(, scale: 0.5}]
  wide:
    type: llamacpp
    model_path: )" + base + R"(
    cache_type: f16
  apart:
    type: llamacpp
    model_path: )" + other + R"(
  broken:
    type: llamacpp
    model_path: )" + base + R"(
    adapters: [{path: )" + (store.dir.path() / "gone.gguf").string() +
                                R"(}]
  first:
    type: llamacpp
    model_path: )" + unrecorded +
                                R"(
  second:
    type: llamacpp
    model_path: )" + unrecorded +
                                R"(
suites:
  roles:
    members:
      chat: root
      utility:
        backend: helper
        context_size: 4096
      extraction:
        backend: scribe
        context_size: 4096
  loaded-apart:
    members:
      chat: root
      utility: wide
      extraction: apart
  tuned-first:
    members:
      chat: helper
      utility: root
  unread:
    members:
      chat: root
      utility:
        backend: broken
        context_size: 4096
  unrecorded:
    members:
      chat: first
      utility:
        backend: second
        context_size: 4096
)");

    SECTION("the first member on the base counts it; the others their own cache and adapters") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "roles", MachineBudget{.bytes = 96 * kGiB});
        CHECK(member(footprint, "root").shares.empty());
        CHECK(member(footprint, "root").adapters == 0);
        CHECK(member(footprint, "root").bytes() == (1926 + 1904) * kMiB);
        // Its weights stated, not counted; its cache at its pin; its adapter.
        CHECK(member(footprint, "helper").shares == "root");
        CHECK(member(footprint, "helper").weights == 1926 * kMiB);
        CHECK(member(footprint, "helper").adapters == 1);
        CHECK(member(footprint, "helper").adapter_bytes == 3 * kMiB);
        CHECK(member(footprint, "helper").bytes() == (238 + 3) * kMiB);
        CHECK(member(footprint, "helper").unknown.empty());
        // The same file spelled another way is the same base.
        CHECK(member(footprint, "scribe").shares == "root");
        CHECK(member(footprint, "scribe").bytes() == (238 + 1) * kMiB);

        const std::int64_t known = (1926 + 1904 + 238 + 3 + 238 + 1) * kMiB;
        CHECK(footprint.known_bytes() == known);
        CHECK(footprint.saved_bytes() == 2 * 1926 * kMiB);
        // Admitted where the base counted once fits, and only there.
        const std::int64_t needed = known + apogee::models::kFitMargin;
        CHECK(apogee::models::suite_footprint(config, "roles", MachineBudget{.bytes = needed})
                  .admission() == Admission::Fits);
        CHECK(apogee::models::suite_footprint(config, "roles", MachineBudget{.bytes = needed - 1})
                  .admission() == Admission::OverBudget);
    }
    SECTION("a base loaded otherwise, or another file, is counted for itself") {
        const SuiteFootprint footprint = apogee::models::suite_footprint(
            config, "loaded-apart", MachineBudget{.bytes = 96 * kGiB});
        CHECK(member(footprint, "wide").shares.empty());
        CHECK(member(footprint, "apart").shares.empty());
        CHECK(footprint.saved_bytes() == 0);
        CHECK(member(footprint, "wide").bytes() ==
              1926 * kMiB + *member(footprint, "wide").window->cache_bytes);
    }
    SECTION("whichever member comes first in the suite counts the base") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "tuned-first", MachineBudget{});
        CHECK(member(footprint, "helper").shares.empty());
        CHECK(member(footprint, "helper").bytes() == (1926 + 1904 + 3) * kMiB);
        CHECK(member(footprint, "root").shares == "helper");
        CHECK(member(footprint, "root").bytes() == 1904 * kMiB);
    }
    SECTION("an adapter that cannot be read is unknown, never guessed") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "unread", MachineBudget{.bytes = 96 * kGiB});
        CHECK(member(footprint, "broken").shares == "root");
        CHECK(member(footprint, "broken").adapters == 1);
        CHECK_FALSE(member(footprint, "broken").adapter_bytes.has_value());
        CHECK_FALSE(member(footprint, "broken").bytes().has_value());
        CHECK(member(footprint, "broken").unknown == "an adapter's file could not be read");
        CHECK(footprint.admission() == Admission::FitsAsFarAsKnown);
    }
    SECTION("an unrecorded base is unknown once, where it is counted") {
        const SuiteFootprint footprint = apogee::models::suite_footprint(
            config, "unrecorded", MachineBudget{.bytes = 96 * kGiB});
        CHECK(member(footprint, "first").unknown == "no recorded size");
        CHECK(member(footprint, "second").shares == "first");
        CHECK(member(footprint, "second").unknown.empty());
        CHECK(member(footprint, "second").bytes() == 68 * kMiB);
        CHECK(footprint.saved_bytes() == 0);
    }
}

TEST_CASE("a member that drafts counts its drafter by its weights' bytes on disk, and only it",
          "[models][footprint][drafter]") {
    // M25: 53i's drafter loads beside its base, so a suite that fits on
    // paper may not once its member drafts. The E4B pair's drafter, as
    // mlx-vlm's conversion lays it out: its config, a tokenizer that is no
    // weight, and two shards.
    const Store store;
    const std::string root = store.model("root", llama_3b(), 1926 * kMiB);
    const std::filesystem::path base =
        store.dir.path() / "Gemma4-E4B-Instruct" / "mlx" / "bbbbbbbbbbbb";
    const std::filesystem::path drafter =
        store.dir.path() / "Gemma4-E4B-Assistant" / "mlx" / "aaaaaaaaaaaa";
    std::filesystem::create_directories(base);
    std::filesystem::create_directories(drafter);
    std::ofstream{base / "config.json"} << apogee::testing::kGemma4BaseE4BConfig;
    std::ofstream{drafter / "config.json"} << apogee::testing::kGemma4AssistantE4BConfig;
    sized(drafter / "tokenizer.json", kMiB);
    sized(drafter / "model-00001-of-00002.safetensors", 2 * kMiB);
    sized(drafter / "model-00002-of-00002.safetensors", kMiB);
    const Config config = parse(R"(models:
  default: root
backends:
  root:
    type: llamacpp
    model_path: )" + root + R"(
  e4b:
    type: mlx
    model_path: )" + base.string() +
                                R"(
    draft: )" + drafter.string() +
                                R"(
  plain:
    type: mlx
    model_path: )" + base.string() +
                                R"(
  lost:
    type: mlx
    model_path: )" + base.string() +
                                R"(
    draft: )" + (store.dir.path() / "gone").string() +
                                R"(
suites:
  drafting:
    members:
      chat: e4b
      utility: root
  plain:
    members:
      chat: plain
      utility: root
  lost:
    members:
      chat: lost
      utility: root
)");
    // root at the default 32K: what each suite knows without a drafter.
    const std::int64_t rooted = (1926 + 1904) * kMiB;

    SECTION("a member drafting with nothing is as it was") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "plain", MachineBudget{.bytes = 96 * kGiB});
        const MemberFootprint plain = member(footprint, "plain");
        CHECK(plain.drafter.empty());
        CHECK(plain.drafter_bytes == 0);
        CHECK_FALSE(plain.bytes().has_value());
        CHECK(plain.counted_bytes() == 0);
        CHECK(plain.unknown == "an MLX model's footprint is not stated yet");
        CHECK(footprint.known_bytes() == rooted);
        CHECK(member(footprint, "root").drafter_bytes == 0);
        CHECK(member(footprint, "root").bytes() == rooted);
    }
    SECTION("its drafter's shards are counted, its base still unknown") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "drafting", MachineBudget{.bytes = 96 * kGiB});
        const MemberFootprint e4b = member(footprint, "e4b");
        CHECK(e4b.drafter == drafter.string());
        CHECK(e4b.drafter_bytes == 3 * kMiB);  // the shards, never the tokenizer
        CHECK_FALSE(e4b.bytes().has_value());
        CHECK(e4b.counted_bytes() == 3 * kMiB);
        CHECK(e4b.unknown == "an MLX model's footprint is not stated yet");
        CHECK(footprint.known_bytes() == rooted + 3 * kMiB);
        CHECK(footprint.has_unknown());
        CHECK(footprint.admission() == Admission::FitsAsFarAsKnown);
    }
    SECTION("what fits on paper does not once its member drafts") {
        const std::int64_t budget = rooted + apogee::models::kFitMargin + kMiB;
        CHECK(apogee::models::suite_footprint(config, "plain", MachineBudget{.bytes = budget})
                  .admission() == Admission::FitsAsFarAsKnown);
        CHECK(apogee::models::suite_footprint(config, "drafting", MachineBudget{.bytes = budget})
                  .admission() == Admission::OverBudget);
    }
    SECTION("a drafter whose weights cannot be read is unknown, never guessed") {
        const SuiteFootprint footprint =
            apogee::models::suite_footprint(config, "lost", MachineBudget{.bytes = 96 * kGiB});
        const MemberFootprint lost = member(footprint, "lost");
        CHECK(lost.drafter == (store.dir.path() / "gone").string());
        CHECK_FALSE(lost.drafter_bytes.has_value());
        CHECK(lost.counted_bytes() == 0);
        CHECK(lost.unknown ==
              "an MLX model's footprint is not stated yet; its drafter's weights could not be "
              "read");
        CHECK(footprint.known_bytes() == rooted);
    }
}
