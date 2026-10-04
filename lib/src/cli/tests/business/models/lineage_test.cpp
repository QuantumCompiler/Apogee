#include "models/lineage.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"

/// Model lineage (M4): what a stored model was made from, read from the
/// records the store keeps -- and inferred, narrowly and labelled, only where
/// no record says.
namespace {

using apogee::models::Consumption;
using apogee::models::Lineage;
using apogee::models::LineageLink;
using apogee::models::Origin;
using apogee::models::OriginKind;
using apogee::models::read_lineage;
using apogee::models::Sidecar;

/// A model store in a temp directory, filled by hand the way the verbs fill it.
struct Store {
    apogee::testing::TempDir dir{"lineage-" + std::to_string(std::random_device{}())};
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(dir.path());

    /// A GGUF at `<model>/gguf/<id>/`, with `record` beside it when given.
    std::filesystem::path gguf(const std::string& model, const std::string& id,
                               const std::optional<Sidecar>& record = std::nullopt) const {
        const std::filesystem::path file =
            apogee::models::weights_dir(roots, apogee::models::kGgufFormat, model, id) /
            (model + ".gguf");
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file, std::ios::binary} << apogee::testing::minimal_gguf("llama");
        if (record.has_value()) {
            REQUIRE(apogee::models::write_sidecar(file, *record));
        }
        return file;
    }

    /// A SafeTensors set at `<model>/safetensors/<id>/`, pulled from `ref`
    /// when one is given.
    std::filesystem::path snapshot(const std::string& model, const std::string& id,
                                   const std::string& ref = {}) const {
        const std::filesystem::path snapshot_dir =
            apogee::models::weights_dir(roots, apogee::models::kSafetensorsFormat, model, id);
        std::filesystem::create_directories(snapshot_dir);
        std::ofstream{snapshot_dir / "config.json"} << R"({"model_type": "llama"})";
        std::ofstream{snapshot_dir / "model.safetensors"} << "weights";
        if (!ref.empty()) {
            apogee::models::Snapshot record;
            record.ref = ref;
            record.revision = "main";
            record.source = "huggingface";
            REQUIRE(apogee::models::write_snapshot(snapshot_dir, record));
        }
        return snapshot_dir;
    }

    [[nodiscard]] Lineage read() const {
        return read_lineage(roots);
    }
};

/// A record as the verb that made the file writes it: `transform` beside the
/// note, or the note is not kept.
[[nodiscard]] Sidecar made_by(std::string source, std::string ref, std::string note = {}) {
    Sidecar record;
    if (!note.empty()) {
        record.transform = source == "train" ? "promote" : source;
    }
    record.source = std::move(source);
    record.ref = std::move(ref);
    record.transform_note = std::move(note);
    return record;
}

[[nodiscard]] Sidecar converted_from(const std::string& handle) {
    return made_by("convert", handle, "--outtype f16");
}

[[nodiscard]] Sidecar quantized_from(const std::string& handle) {
    return made_by("quantize", handle, "Q4_K_M");
}

/// The chain back from the stored GGUF `handle`, as (kind, from, inferred,
/// missing) per link -- one table shape for every case below.
struct LinkRow {
    OriginKind kind;
    std::string from;
    bool inferred = false;
    bool missing = false;

    bool operator==(const LinkRow&) const = default;
};

[[nodiscard]] std::vector<LinkRow> chain_of(const Lineage& lineage, const std::string& handle) {
    std::vector<LinkRow> rows;
    for (const LineageLink& link : lineage.chain(lineage.origin_of(handle))) {
        rows.push_back({.kind = link.origin.kind,
                        .from = link.origin.from,
                        .inferred = link.origin.inferred,
                        .missing = link.missing});
    }
    return rows;
}

constexpr const char* kSnapshot = "org--m/safetensors/aaaaaaaaaaaa";
constexpr const char* kOther = "org--m/safetensors/bbbbbbbbbbbb";
constexpr const char* kF16 = "org--m/gguf/111111111111";
constexpr const char* kQuant = "org--m/gguf/222222222222";
constexpr const char* kUnknown = "org--m/gguf/333333333333";

}  // namespace

TEST_CASE("a record names how its GGUF was made", "[models][lineage]") {
    struct Row {
        std::optional<Sidecar> record;
        OriginKind kind;
        std::string from;
        std::string detail;
    };

    for (const Row& row : {
             Row{std::nullopt, OriginKind::Unknown, "", ""},
             // A record from before any source was written: unknown, never a crash.
             Row{Sidecar{}, OriginKind::Unknown, "", ""},
             Row{made_by("something-else", "x"), OriginKind::Unknown, "", ""},
             Row{converted_from(kSnapshot), OriginKind::Converted, kSnapshot, ""},
             Row{quantized_from(kF16), OriginKind::Quantized, kF16, "Q4_K_M"},
             Row{made_by("huggingface", "org/m:m.gguf"), OriginKind::Pulled, "org/m:m.gguf",
                 "huggingface"},
             Row{made_by("ollama", "llama3.2:3b"), OriginKind::Pulled, "llama3.2:3b", "ollama"},
             Row{made_by("train", "mine v3", "run abc, Q4_K_M"), OriginKind::Trained, "mine v3",
                 "run abc, Q4_K_M"},
         }) {
        INFO((row.record.has_value() ? row.record->source : std::string{"(none)"}));
        const Origin origin = apogee::models::origin_from_record(row.record);
        CHECK(origin.kind == row.kind);
        CHECK(origin.from == row.from);
        CHECK(origin.detail == row.detail);
        CHECK_FALSE(origin.inferred);
    }
}

TEST_CASE("which snapshots a conversion consumed, recorded first and inferred only where none is",
          "[models][lineage][consumed]") {
    struct Case {
        std::string name;
        std::function<void(Store&)> fill;
        std::optional<Consumption> snapshot;  // what kSnapshot answers
        std::optional<Consumption> other;     // what kOther answers
    };

    const std::vector<Case> cases{
        {"a recorded conversion",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "111111111111", converted_from(kSnapshot));
         },
         Consumption{.inferred = false, .by = {kF16}}, std::nullopt},
        {"never converted", [](Store& store) { store.snapshot("org--m", "aaaaaaaaaaaa"); },
         std::nullopt, std::nullopt},
        {"no record, one snapshot: inferred",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "333333333333");
         },
         Consumption{.inferred = true, .by = {kUnknown}}, std::nullopt},
        {"two snapshots, one converted: only the recorded one",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.snapshot("org--m", "bbbbbbbbbbbb");
             store.gguf("org--m", "111111111111", converted_from(kSnapshot));
         },
         Consumption{.inferred = false, .by = {kF16}}, std::nullopt},
        {"two snapshots and no record: nothing guessed",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.snapshot("org--m", "bbbbbbbbbbbb");
             store.gguf("org--m", "333333333333");
         },
         std::nullopt, std::nullopt},
        {"a record beats an inference",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "111111111111", converted_from(kSnapshot));
             store.gguf("org--m", "333333333333");
         },
         Consumption{.inferred = false, .by = {kF16}}, std::nullopt},
        {"a quantization of its conversion reaches it too, recorded all the way",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "111111111111", converted_from(kSnapshot));
             store.gguf("org--m", "222222222222", quantized_from(kF16));
         },
         Consumption{.inferred = false, .by = {kF16, kQuant}}, std::nullopt},
        {"the F16 deleted for its size: its quantization still ties the snapshot, inferred",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "222222222222", quantized_from(kF16));
         },
         Consumption{.inferred = true, .by = {kQuant}}, std::nullopt},
        {"the F16 deleted, two snapshots: nothing guessed",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.snapshot("org--m", "bbbbbbbbbbbb");
             store.gguf("org--m", "222222222222", quantized_from(kF16));
         },
         std::nullopt, std::nullopt},
        {"a pulled GGUF beside a snapshot was not made from it",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "111111111111", made_by("huggingface", "org/m-GGUF:m.gguf"));
         },
         std::nullopt, std::nullopt},
        {"a fine-tune does not consume its base",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--m", "111111111111", made_by("train", "mine v1", "run r1"));
         },
         std::nullopt, std::nullopt},
        {"another model's GGUF infers nothing here",
         [](Store& store) {
             store.snapshot("org--m", "aaaaaaaaaaaa");
             store.gguf("org--other", "333333333333");
         },
         std::nullopt, std::nullopt},
    };
    for (const Case& test : cases) {
        INFO(test.name);
        Store store;
        test.fill(store);
        const Lineage lineage = store.read();
        const std::optional<Consumption> snapshot = lineage.consumed(kSnapshot);
        REQUIRE(snapshot.has_value() == test.snapshot.has_value());
        if (snapshot.has_value() && test.snapshot.has_value()) {
            CHECK(snapshot->inferred == test.snapshot->inferred);
            CHECK(snapshot->by == test.snapshot->by);
        }
        CHECK(lineage.consumed(kOther).has_value() == test.other.has_value());
    }
}

TEST_CASE("what a snapshot was made into comes conversion first, then what was quantized from it",
          "[models][lineage][consumed]") {
    // Ids are hashes: the quantization's can sort before its F16's.
    Store store;
    store.snapshot("org--m", "aaaaaaaaaaaa");
    store.gguf("org--m", "999999999999", converted_from(kSnapshot));
    store.gguf("org--m", "111111111111", quantized_from("org--m/gguf/999999999999"));
    const std::optional<Consumption> consumed = store.read().consumed(kSnapshot);
    REQUIRE(consumed.has_value());
    CHECK(consumed->by ==
          std::vector<std::string>{"org--m/gguf/999999999999", "org--m/gguf/111111111111"});
}

TEST_CASE("the listing follows the store: a snapshot is consumed while a GGUF made of it is there",
          "[models][lineage][consumed]") {
    Store store;
    store.snapshot("org--m", "aaaaaaaaaaaa");
    const std::filesystem::path f16 =
        store.gguf("org--m", "111111111111", converted_from(kSnapshot));
    CHECK(store.read().consumed(kSnapshot).has_value());
    REQUIRE(apogee::models::remove_weights(f16.parent_path()).empty());
    CHECK_FALSE(store.read().consumed(kSnapshot).has_value());
}

TEST_CASE("a quantization chains through its F16 to the snapshot and the upstream it was pulled as",
          "[models][lineage][chain]") {
    Store store;
    store.snapshot("org--m", "aaaaaaaaaaaa", "org/m");
    store.gguf("org--m", "111111111111", converted_from(kSnapshot));
    store.gguf("org--m", "222222222222", quantized_from(kF16));
    const Lineage lineage = store.read();

    CHECK(chain_of(lineage, kQuant) == std::vector<LinkRow>{
                                           {.kind = OriginKind::Quantized, .from = kF16},
                                           {.kind = OriginKind::Converted, .from = kSnapshot},
                                           {.kind = OriginKind::Pulled, .from = "org/m"},
                                       });
    CHECK(lineage.converted(lineage.origin_of(kQuant)));
    // A snapshot's own origin is its pull.
    CHECK(chain_of(lineage, kSnapshot) ==
          std::vector<LinkRow>{{.kind = OriginKind::Pulled, .from = "org/m"}});
}

TEST_CASE("a broken chain names what it came from, and that it is gone",
          "[models][lineage][chain]") {
    Store store;
    const std::filesystem::path snapshot = store.snapshot("org--m", "aaaaaaaaaaaa", "org/m");
    const std::filesystem::path f16 =
        store.gguf("org--m", "111111111111", converted_from(kSnapshot));
    store.gguf("org--m", "222222222222", quantized_from(kF16));

    // The snapshot deleted: the F16 still says it was converted, from what.
    REQUIRE(apogee::models::remove_weights(snapshot).empty());
    Lineage lineage = store.read();
    CHECK(
        chain_of(lineage, kF16) ==
        std::vector<LinkRow>{{.kind = OriginKind::Converted, .from = kSnapshot, .missing = true}});
    CHECK(lineage.converted(lineage.origin_of(kF16)));

    // The F16 deleted too: the quantization still names it, and with no
    // snapshot left to infer from, the chain ends there.
    REQUIRE(apogee::models::remove_weights(f16.parent_path()).empty());
    lineage = store.read();
    CHECK(chain_of(lineage, kQuant) ==
          std::vector<LinkRow>{{.kind = OriginKind::Quantized, .from = kF16, .missing = true}});
    CHECK_FALSE(lineage.converted(lineage.origin_of(kQuant)));
}

TEST_CASE("a quantization whose F16 is gone continues, inferred, to its model's one snapshot",
          "[models][lineage][chain]") {
    // The F16 is the big one, and once quantized the one most often deleted.
    Store store;
    store.snapshot("org--m", "aaaaaaaaaaaa", "org/m");
    store.gguf("org--m", "222222222222", quantized_from(kF16));
    const Lineage lineage = store.read();
    CHECK(chain_of(lineage, kQuant) ==
          std::vector<LinkRow>{
              {.kind = OriginKind::Quantized, .from = kF16, .missing = true},
              {.kind = OriginKind::Converted, .from = kSnapshot, .inferred = true},
              {.kind = OriginKind::Pulled, .from = "org/m"},
          });
    CHECK(lineage.converted(lineage.origin_of(kQuant)));
}

TEST_CASE("an inferred conversion says it is inferred all the way down",
          "[models][lineage][chain]") {
    Store store;
    store.snapshot("org--m", "aaaaaaaaaaaa", "org/m");
    store.gguf("org--m", "333333333333");
    const Lineage lineage = store.read();
    CHECK(chain_of(lineage, kUnknown) ==
          std::vector<LinkRow>{
              {.kind = OriginKind::Converted, .from = kSnapshot, .inferred = true},
              {.kind = OriginKind::Pulled, .from = "org/m"},
          });
    CHECK(lineage.converted(lineage.origin_of(kUnknown)));
}

TEST_CASE("unknown stays unknown: no record, and nothing to infer from",
          "[models][lineage][chain]") {
    Store store;
    store.gguf("org--m", "333333333333");
    const Lineage lineage = store.read();
    CHECK(chain_of(lineage, kUnknown) == std::vector<LinkRow>{{.kind = OriginKind::Unknown}});
    CHECK_FALSE(lineage.converted(lineage.origin_of(kUnknown)));
    // Nor is anything stored under a handle the store does not hold.
    CHECK(lineage.origin_of("org--m/gguf/999999999999").kind == OriginKind::Unknown);
}

TEST_CASE("a parent outside the store is said as recorded, and checked on disk",
          "[models][lineage][chain]") {
    Store store;
    const apogee::testing::TempDir outside{"lineage-outside-" +
                                           std::to_string(std::random_device{}())};
    const std::string there = outside.path().string();
    const std::string gone = (outside.path() / "gone").string();
    store.gguf("org--m", "111111111111", converted_from(there));
    store.gguf("org--m", "222222222222", quantized_from(gone));
    const Lineage lineage = store.read();
    CHECK(chain_of(lineage, kF16) ==
          std::vector<LinkRow>{{.kind = OriginKind::Converted, .from = there}});
    CHECK(chain_of(lineage, kQuant) ==
          std::vector<LinkRow>{{.kind = OriginKind::Quantized, .from = gone, .missing = true}});
}

TEST_CASE("a file outside the store answers from its own record, never inferred",
          "[models][lineage]") {
    Store store;
    store.snapshot("org--m", "aaaaaaaaaaaa");
    const Lineage lineage = store.read();
    const std::filesystem::path elsewhere = store.dir.path() / "hand-placed.gguf";
    CHECK(lineage.origin_of(elsewhere, std::nullopt).kind == OriginKind::Unknown);
    const Origin recorded = lineage.origin_of(elsewhere, converted_from(kSnapshot));
    CHECK(recorded.kind == OriginKind::Converted);
    CHECK_FALSE(recorded.inferred);
}

TEST_CASE("a record naming itself cannot loop the walk", "[models][lineage][chain]") {
    Store store;
    store.gguf("org--m", "222222222222", quantized_from(kQuant));
    const Lineage lineage = store.read();
    const std::vector<LinkRow> chain = chain_of(lineage, kQuant);
    CHECK_FALSE(chain.empty());
    CHECK(chain.size() <= 8);
}

TEST_CASE("snapshots under paths.hf_dir chain the same as ones beside the GGUFs",
          "[models][lineage][chain]") {
    Store store;
    const apogee::testing::TempDir hf{"lineage-hf-" + std::to_string(std::random_device{}())};
    store.roots.safetensors = hf.path();
    store.snapshot("org--m", "aaaaaaaaaaaa", "org/m");
    store.gguf("org--m", "111111111111", converted_from(kSnapshot));
    const Lineage lineage = store.read();
    CHECK(lineage.consumed(kSnapshot).has_value());
    CHECK(chain_of(lineage, kF16).size() == 2);
}
