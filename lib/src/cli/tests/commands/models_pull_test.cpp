#include "commands/models_pull.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>

#include "models/sidecar.h"
#include "models/store.h"
#include "support/env_guard.h"
#include "support/file_time.h"
#include "support/gguf_builder.h"

/// The mutating verbs over the model store, and the boundary they must never
/// cross.
///
/// The assertion carrying the most weight is that **"delete a model by name"
/// can never become "delete a file by path"**. A models directory is a place a
/// user will type names into carelessly, and a `..` that resolved would make a
/// listing command into a deletion tool aimed anywhere.
namespace {

using apogee::commands::DeletePlan;
using apogee::commands::plan_delete;
using apogee::commands::render_repair;

struct Store {
    apogee::testing::TempDir root{"pull-" + std::to_string(std::random_device{}())};
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(root.path() / "models");

    [[nodiscard]] std::filesystem::path add_gguf(
        std::string_view model, std::string_view id, std::string_view name,
        const std::string& bytes = apogee::testing::minimal_gguf("llama")) const {
        const std::filesystem::path dir =
            roots.models / std::string{model} / "gguf" / std::string{id};
        std::filesystem::create_directories(dir);
        const std::filesystem::path path = dir / std::string{name};
        std::ofstream{path, std::ios::binary} << bytes;
        return path;
    }

    [[nodiscard]] std::filesystem::path add_snapshot(std::string_view model,
                                                     std::string_view id) const {
        const std::filesystem::path dir =
            roots.safetensors / std::string{model} / "safetensors" / std::string{id};
        std::filesystem::create_directories(dir);
        std::ofstream{dir / "config.json"} << R"({"architectures": ["LlamaForCausalLM"]})";
        std::ofstream{dir / "model.safetensors"} << "weights";
        return dir;
    }

    static void add_sidecar(const std::filesystem::path& model, std::string_view source,
                            std::string_view ref) {
        apogee::models::Sidecar sidecar;
        sidecar.ref = std::string{ref};
        sidecar.source = std::string{source};
        sidecar.file = model.filename().string();
        sidecar.file_size = static_cast<std::int64_t>(std::filesystem::file_size(model));
        sidecar.file_digest = apogee::models::file_sha256(model);
        sidecar.verification.header_checked = true;
        sidecar.verification.header_parsed = true;
        (void)apogee::models::write_sidecar(model, sidecar);
    }
};

/// A GGUF whose header says its weights are Q4_K_M -- already quantized.
[[nodiscard]] std::string quantized_gguf() {
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.string_kv("general.architecture", "llama");
    builder.u32_kv("general.file_type", 15);
    builder.tensor("token_embd.weight");
    return builder.bytes();
}

}  // namespace

TEST_CASE("a model is deleted whole by name, every format and every set",
          "[commands][models][delete]") {
    const Store store;
    (void)store.add_gguf("org--repo", "111111111111", "repo-F16.gguf");
    (void)store.add_gguf("org--repo", "222222222222", "repo-Q4_K_M.gguf");
    (void)store.add_snapshot("org--repo", "aaaaaaaaaaaa");
    (void)store.add_gguf("other", "333333333333", "other.gguf");

    for (const std::string_view name : {"org--repo", "org/repo"}) {
        INFO(name);
        const DeletePlan plan = plan_delete(store.roots, name);
        REQUIRE(plan.ok);
        CHECK(plan.removes.size() == 3);
        for (const std::filesystem::path& dir : plan.removes) {
            CHECK(dir.string().find("org--repo") != std::string::npos);
        }
    }
}

TEST_CASE("one set of weights is deleted by its id or its store path",
          "[commands][models][delete]") {
    const Store store;
    const std::filesystem::path kept = store.add_gguf("m", "111111111111", "a.gguf");
    const std::filesystem::path gone = store.add_gguf("m", "222222222222", "b.gguf");

    for (const std::string_view name : {"222222222222", "m/gguf/222222222222"}) {
        INFO(name);
        const DeletePlan plan = plan_delete(store.roots, name);
        REQUIRE(plan.ok);
        REQUIRE(plan.removes.size() == 1);
        CHECK(plan.removes.front() == gone.parent_path());
    }
    CHECK(std::filesystem::exists(kept));
}

TEST_CASE("a name containing .. is refused before anything is touched",
          "[commands][models][delete][safety]") {
    // The boundary this command must never cross. A models directory is
    // somewhere a user types names carelessly, and a traversal that resolved
    // would turn a model manager into a deletion tool aimed anywhere on disk.
    const Store store;
    (void)store.add_gguf("thing", "111111111111", "thing.gguf");

    for (const std::string_view name :
         {"../../etc/passwd", "..", "sub/../../../thing", "a/../../b", "../thing/gguf"}) {
        INFO("name: " << name);
        const DeletePlan plan = plan_delete(store.roots, name);
        CHECK_FALSE(plan.ok);
        CHECK_FALSE(plan.error.empty());
        CHECK(plan.removes.empty());
    }
}

TEST_CASE("a path outside the store is refused", "[commands][models][delete][safety]") {
    const Store store;
    // A real file, made here: `/etc/hosts` exists on POSIX only, and on
    // Windows the refusal came from the name lookup instead of this rule.
    const apogee::testing::TempDir elsewhere{"outside-" + std::to_string(std::random_device{}())};
    const std::filesystem::path file = elsewhere.path() / "hosts";
    std::ofstream{file} << "127.0.0.1 localhost\n";
    const DeletePlan plan = plan_delete(store.roots, file.string());
    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("named, not pathed") != std::string::npos);
    CHECK(std::filesystem::exists(file));

    // And a path that does not exist is still no model.
    CHECK_FALSE(plan_delete(store.roots, "/etc/hosts").ok);
}

TEST_CASE("a name that is not stored reports so", "[commands][models][delete]") {
    const Store store;
    const DeletePlan plan = plan_delete(store.roots, "absent");
    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("no model 'absent'") != std::string::npos);
}

TEST_CASE("a model still in the flat layout is refused with the migration named",
          "[commands][models][delete][legacy]") {
    const Store store;
    std::filesystem::create_directories(store.roots.models);
    std::ofstream{store.roots.models / "old.gguf", std::ios::binary}
        << apogee::testing::minimal_gguf("llama");
    const DeletePlan plan = plan_delete(store.roots, "old.gguf");
    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("apogee models migrate") != std::string::npos);
}

TEST_CASE("an Ollama-sourced model names ollama rm rather than touching the store",
          "[commands][models][delete][safety]") {
    // Ollama's blobs are SHARED between models. Deleting one by hand silently
    // corrupts every sibling that referenced it, so Apogee removes only its own
    // copy and points at the vendor's reference-counting GC.
    const Store store;
    const std::filesystem::path model =
        store.add_gguf("llama3.2-3b", "111111111111", "llama3.2-3b.gguf");
    Store::add_sidecar(model, "ollama", "llama3.2:3b");

    const DeletePlan plan = plan_delete(store.roots, "llama3.2-3b");
    REQUIRE(plan.ok);
    CHECK(plan.ollama_sourced);
    CHECK(plan.ollama_ref == "llama3.2:3b");
    // The plan touches Apogee's copy and nothing else.
    for (const std::filesystem::path& dir : plan.removes) {
        CHECK(dir.string().rfind(store.roots.models.string(), 0) == 0);
    }
}

TEST_CASE("repair on a sound model says there is nothing to do", "[commands][models][repair]") {
    const Store store;
    const std::filesystem::path model = store.add_gguf("m", "111111111111", "good.gguf");
    Store::add_sidecar(model, "huggingface", "owner/repo:good.gguf");

    const std::string body = render_repair(store.roots, "m");
    CHECK(body.find("Nothing to repair") != std::string::npos);
}

TEST_CASE("repair on a changed model says how to re-acquire it", "[commands][models][repair]") {
    const Store store;
    const std::filesystem::path model =
        store.add_gguf("llama3.2-3b", "111111111111", "llama3.2-3b.gguf");
    Store::add_sidecar(model, "ollama", "llama3.2:3b");

    // The file changes after its record was written.
    {
        std::ofstream out(model, std::ios::binary | std::ios::app);
        out << "appended later";
    }

    const std::string body = render_repair(store.roots, "111111111111");
    CHECK(body.find("no longer matches") != std::string::npos);
    // The exact commands, so they can be copied.
    CHECK(body.find("apogee models delete llama3.2-3b/gguf/111111111111") != std::string::npos);
    CHECK(body.find("apogee models pull llama3.2:3b") != std::string::npos);
}

TEST_CASE("repair on a hand-placed model reports the absence of a record, not an error",
          "[commands][models][repair]") {
    // A model a user dropped in themselves is legitimate; it simply has nothing
    // to recheck integrity against.
    const Store store;
    (void)store.add_gguf("byhand", "111111111111", "byhand.gguf");

    const std::string body = render_repair(store.roots, "byhand");
    CHECK(body.find("record:  none") != std::string::npos);
    CHECK(body.find("header:  ok") != std::string::npos);
}

TEST_CASE("repair on a broken hand-placed model says what to do", "[commands][models][repair]") {
    const Store store;
    (void)store.add_gguf("broken", "111111111111", "broken.gguf", "GGUF");  // magic, nothing more

    const std::string body = render_repair(store.roots, "broken");
    CHECK(body.find("FAILED") != std::string::npos);
    CHECK(body.find("apogee models delete broken/gguf/111111111111") != std::string::npos);
}

TEST_CASE("repair on an unknown name yields nothing for the caller to report",
          "[commands][models][repair]") {
    const Store store;
    CHECK(render_repair(store.roots, "absent").empty());
}

TEST_CASE("convert reads a model's newest SafeTensors set unless told which",
          "[commands][models][convert]") {
    using apogee::commands::choose_snapshot;
    const Store store;
    const std::filesystem::path older = store.add_snapshot("Qwen--Qwen3-8B", "aaaaaaaaaaaa");
    const std::filesystem::path newer = store.add_snapshot("Qwen--Qwen3-8B", "bbbbbbbbbbbb");
    const auto now = std::filesystem::file_time_type::clock::now();
    apogee::testing::set_modified_time(older, now - std::chrono::hours{1});
    apogee::testing::set_modified_time(newer, now);

    CHECK(choose_snapshot(store.roots, "Qwen/Qwen3-8B").path == newer);
    CHECK(choose_snapshot(store.roots, "Qwen--Qwen3-8B", "aaaaaaaaaaaa").path == older);
    CHECK(choose_snapshot(store.roots, "Qwen--Qwen3-8B/safetensors/aaaaaaaaaaaa").path == older);
    CHECK(choose_snapshot(store.roots, "Qwen--Qwen3-8B").model == "Qwen--Qwen3-8B");

    // An id the model does not have names the ones it does.
    const std::string missing =
        choose_snapshot(store.roots, "Qwen--Qwen3-8B", "cccccccccccc").error;
    CHECK(missing.find("aaaaaaaaaaaa") != std::string::npos);
    CHECK(missing.find("bbbbbbbbbbbb") != std::string::npos);

    // A GGUF is not something to convert.
    (void)store.add_gguf("Qwen--Qwen3-8B", "111111111111", "q.gguf");
    CHECK(
        choose_snapshot(store.roots, "Qwen--Qwen3-8B/gguf/111111111111").error.find("is a GGUF") !=
        std::string::npos);
}

TEST_CASE("convert takes a snapshot directory from outside the store, named after it",
          "[commands][models][convert]") {
    using apogee::commands::choose_snapshot;
    const Store store;
    const std::filesystem::path outside = store.root.path() / "my-finetune";
    std::filesystem::create_directories(outside);
    std::ofstream{outside / "config.json"} << "{}";
    std::ofstream{outside / "model.safetensors"} << "weights";

    const apogee::commands::SnapshotChoice choice = choose_snapshot(store.roots, outside.string());
    CHECK(choice.path == outside);
    CHECK(choice.model == "my-finetune");
    CHECK(choice.id.empty());

    std::filesystem::create_directories(store.root.path() / "not-a-snapshot");
    CHECK_FALSE(choose_snapshot(store.roots, (store.root.path() / "not-a-snapshot").string())
                    .error.empty());
}

TEST_CASE("convert refuses a snapshot still in the flat layout, naming the migration",
          "[commands][models][convert][legacy]") {
    const Store store;
    const std::filesystem::path flat = store.roots.models / "Qwen--Qwen3-8B";
    std::filesystem::create_directories(flat);
    std::ofstream{flat / "config.json"} << "{}";
    std::ofstream{flat / "model.safetensors"} << "weights";
    CHECK(apogee::commands::choose_snapshot(store.roots, "Qwen/Qwen3-8B")
              .error.find("apogee models migrate") != std::string::npos);
}

TEST_CASE("quantize starts from the newest unquantized GGUF, never a quantized one",
          "[commands][models][quantize]") {
    using apogee::commands::choose_gguf;
    const Store store;
    const std::filesystem::path f16 = store.add_gguf("m", "111111111111", "m-F16.gguf");
    const std::filesystem::path q4 =
        store.add_gguf("m", "222222222222", "m-Q4_K_M.gguf", quantized_gguf());
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(f16, now - std::chrono::hours{1});
    std::filesystem::last_write_time(q4, now);  // newer, but already quantized

    CHECK(choose_gguf(store.roots, "m").file == f16);
    CHECK(choose_gguf(store.roots, "m", "222222222222").file == q4);  // asked for by name

    // Nothing unquantized: say so, and name what there is.
    const Store only_quantized;
    (void)only_quantized.add_gguf("q", "222222222222", "q.gguf", quantized_gguf());
    const std::string error = choose_gguf(only_quantized.roots, "q").error;
    CHECK(error.find("no unquantized GGUF") != std::string::npos);
    CHECK(error.find("222222222222") != std::string::npos);

    // A .gguf from outside the store is taken, its model named after it.
    const std::filesystem::path outside = store.root.path() / "Downloaded-7B.gguf";
    std::ofstream{outside, std::ios::binary} << apogee::testing::minimal_gguf("llama");
    const apogee::commands::GgufChoice imported = choose_gguf(store.roots, outside.string());
    CHECK(imported.file == outside);
    CHECK(imported.model == "Downloaded-7B");
    CHECK(imported.projector.empty());
}

TEST_CASE("quantize's source brings its projector along",
          "[commands][models][quantize][projector]") {
    // Quantizing leaves the projector as it was, and a quantized model that
    // lost it could no longer read images.
    using apogee::commands::choose_gguf;
    const Store store;
    const std::filesystem::path f16 = store.add_gguf("m", "111111111111", "m-F16.gguf");
    const std::filesystem::path projector = store.add_gguf("m", "111111111111", "m-F16-mmproj.gguf",
                                                           apogee::testing::minimal_gguf("clip"));
    CHECK(choose_gguf(store.roots, "m").projector == projector);
    CHECK(choose_gguf(store.roots, "m", "111111111111").projector == projector);

    // Outside the store: the `<stem>-mmproj.gguf` beside the file.
    const std::filesystem::path outside = store.root.path() / "Vision-7B.gguf";
    std::ofstream{outside, std::ios::binary} << apogee::testing::minimal_gguf("llama");
    std::ofstream{store.root.path() / "Vision-7B-mmproj.gguf", std::ios::binary}
        << apogee::testing::minimal_gguf("clip");
    CHECK(choose_gguf(store.roots, outside.string()).projector ==
          store.root.path() / "Vision-7B-mmproj.gguf");
    (void)f16;
}

TEST_CASE("a conversion is recognised by its record: the same set, at the same precision",
          "[commands][models][convert]") {
    using apogee::commands::find_conversion;
    const Store store;
    const std::filesystem::path made = store.add_gguf("org--repo", "111111111111", "repo-F16.gguf");
    apogee::models::Sidecar record;
    record.ref = "org--repo/safetensors/aaaaaaaaaaaa";
    record.source = "convert";
    record.transform = "convert";
    record.transform_note = "--outtype f16";
    REQUIRE(apogee::models::write_record(made, record).empty());

    const auto found =
        find_conversion(store.roots, "org--repo", "org--repo/safetensors/aaaaaaaaaaaa", "f16");
    REQUIRE(found.has_value());
    CHECK(found->file == made);
    CHECK_FALSE(
        find_conversion(store.roots, "org--repo", "org--repo/safetensors/aaaaaaaaaaaa", "bf16"));
    CHECK_FALSE(
        find_conversion(store.roots, "org--repo", "org--repo/safetensors/bbbbbbbbbbbb", "f16"));

    // A pulled GGUF is not a conversion, whatever it says it came from.
    record.source = "huggingface";
    REQUIRE(apogee::models::write_record(made, record).empty());
    CHECK_FALSE(
        find_conversion(store.roots, "org--repo", "org--repo/safetensors/aaaaaaaaaaaa", "f16"));
}
