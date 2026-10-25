#include "cli/models_pull.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "contracts/config.h"
#include "modelstore/sidecar.h"
#include "modelstore/store.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/file_time.h"
#include "support/gguf_builder.h"
#include "support/mlx_model.h"

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

TEST_CASE("a backend's name deletes the stored GGUF it points at, planned as its handle would be",
          "[commands][models][delete][backend]") {
    // M7, the user's transcript: `models delete gemma-4-E2B-F16` was refused
    // while the store path it meant was hunted by hand.
    const Store store;
    const std::filesystem::path f16 = store.add_gguf("org--repo", "111111111111", "repo-F16.gguf");
    (void)store.add_gguf("org--repo", "222222222222", "repo-Q4_K_M.gguf");
    apogee::harness::Config config;
    apogee::harness::BackendConfig local;
    local.type = apogee::harness::BackendType::LlamaCpp;
    local.model_path = f16.string();
    config.backends["repo-F16"] = local;

    const DeletePlan by_backend = plan_delete(store.roots, config, "repo-F16");
    REQUIRE(by_backend.ok);
    CHECK(by_backend.backend == "repo-F16");
    const DeletePlan by_handle = plan_delete(store.roots, "org--repo/gguf/111111111111");
    REQUIRE(by_handle.ok);
    CHECK(by_backend.removes == by_handle.removes);
    CHECK(by_backend.removes == std::vector<std::filesystem::path>{f16.parent_path()});
    CHECK(by_backend.also_backend.empty());
}

TEST_CASE("a name the store knows keeps its meaning, and a backend sharing it is said",
          "[commands][models][delete][backend]") {
    const Store store;
    (void)store.add_gguf("org--repo", "111111111111", "repo.gguf");
    const std::filesystem::path other = store.add_gguf("other", "333333333333", "other.gguf");
    apogee::harness::Config config;
    apogee::harness::BackendConfig local;
    local.type = apogee::harness::BackendType::LlamaCpp;
    local.model_path = other.string();
    config.backends["org--repo"] = local;

    const DeletePlan plan = plan_delete(store.roots, config, "org--repo");
    REQUIRE(plan.ok);
    CHECK(plan.backend.empty());
    CHECK(plan.removes == std::vector<std::filesystem::path>{store.roots.models / "org--repo" /
                                                             "gguf" / "111111111111"});
    CHECK(plan.also_backend == "org--repo");
    CHECK(plan.also_backend_weights == "other/gguf/333333333333");

    // A backend whose model is among what goes is no collision: the warning
    // that it stops working already names it.
    config.backends["org--repo"].model_path =
        (store.roots.models / "org--repo" / "gguf" / "111111111111" / "repo.gguf").string();
    CHECK(plan_delete(store.roots, config, "org--repo").also_backend.empty());
}

TEST_CASE("a backend with nothing in the store is refused, saying why",
          "[commands][models][delete][backend]") {
    const Store store;
    apogee::harness::Config config;
    apogee::harness::BackendConfig outside;
    outside.type = apogee::harness::BackendType::LlamaCpp;
    outside.model_path = "/somewhere/else.gguf";
    config.backends["hand-placed"] = outside;
    apogee::harness::BackendConfig cloud;
    cloud.type = apogee::harness::BackendType::Anthropic;
    cloud.model = "claude-sonnet-5";
    config.backends["claude"] = cloud;

    const DeletePlan placed = plan_delete(store.roots, config, "hand-placed");
    CHECK_FALSE(placed.ok);
    CHECK(placed.error ==
          "backend 'hand-placed' points at /somewhere/else.gguf, outside the model store -- "
          "Apogee deletes only what is in it");
    const DeletePlan remote = plan_delete(store.roots, config, "claude");
    CHECK_FALSE(remote.ok);
    CHECK(remote.error.starts_with("backend 'claude' (anthropic) has no model file to delete"));
    CHECK(remote.error.find("apogee config delete-backend claude") != std::string::npos);
    // A name that is neither: today's refusal, word for word.
    CHECK(plan_delete(store.roots, config, "nothing").error ==
          plan_delete(store.roots, "nothing").error);
}

TEST_CASE("deleting by a backend's name prints and removes what its handle would",
          "[commands][models][delete][backend]") {
    // The transcript, inverted, through the command line: the confirmation
    // text byte for byte the handle's, warning included, and --yes removing
    // the same entry.
    const auto install = [](apogee::testing::CliHome& home) {
        const std::filesystem::path file = home.models() / "google--gemma-4-E2B" / "gguf" /
                                           "513ee1b91245" / "gemma-4-E2B-F16.gguf";
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file, std::ios::binary} << apogee::testing::minimal_gguf("gemma4");
        std::ofstream{home.config_path(), std::ios::binary}
            << "backends:\n  gemma-4-E2B-F16:\n    type: llamacpp\n    model_path: " +
                   file.string() + "\n";
        return file;
    };
    apogee::testing::CliHome by_name{""};
    apogee::testing::CliHome by_handle{""};
    const std::filesystem::path file = install(by_name);
    (void)install(by_handle);
    std::string named;
    std::string handled;
    REQUIRE(by_name.run({"models", "delete", "gemma-4-E2B-F16"}, &named) == 0);
    REQUIRE(by_handle.run({"models", "delete", "google--gemma-4-E2B/gguf/513ee1b91245"},
                          &handled) == 0);
    const auto in_home = [](std::string text, const apogee::testing::CliHome& home) {
        // The two homes differ only in their temp paths.
        for (std::size_t at = text.find(home.home().string()); at != std::string::npos;
             at = text.find(home.home().string())) {
            text.replace(at, home.home().string().size(), "<home>");
        }
        return text;
    };
    CHECK(in_home(named, by_name) == in_home(handled, by_handle));
    CHECK(named.find("backend 'gemma-4-E2B-F16' points into this and will stop working") !=
          std::string::npos);
    CHECK(std::filesystem::exists(file));

    REQUIRE(by_name.run({"models", "delete", "gemma-4-E2B-F16", "--yes"}, &named) == 0);
    REQUIRE(by_handle.run({"models", "delete", "google--gemma-4-E2B/gguf/513ee1b91245", "--yes"},
                          &handled) == 0);
    CHECK(in_home(named, by_name) == in_home(handled, by_handle));
    CHECK_FALSE(std::filesystem::exists(file.parent_path()));
}

TEST_CASE("a store name a backend also has says so before the plan",
          "[commands][models][delete][backend]") {
    apogee::testing::CliHome home{""};
    const std::filesystem::path theirs =
        home.models() / "other" / "gguf" / "333333333333" / "other.gguf";
    for (const std::filesystem::path& file :
         {home.models() / "org--repo" / "gguf" / "111111111111" / "repo.gguf", theirs}) {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file, std::ios::binary} << apogee::testing::minimal_gguf("llama");
    }
    std::ofstream{home.config_path(), std::ios::binary}
        << "backends:\n  org--repo:\n    type: llamacpp\n    model_path: " + theirs.string() + "\n";
    std::string out;
    REQUIRE(home.run({"models", "delete", "org--repo"}, &out) == 0);
    CHECK(
        out.starts_with("'org--repo' is also a backend -- its model, other/gguf/333333333333, "
                        "is not what this removes; name that to delete it.\n\nwill remove:\n"));
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

namespace {

/// A stored GGUF set and three backends: two on it, one elsewhere -- the
/// fixture M14's consent branches run over.
struct Stranded {
    apogee::testing::CliHome home{""};
    std::filesystem::path file =
        home.models() / "org--m" / "gguf" / "aaaaaaaaaaaa" / "m-Q4_K_M.gguf";
    std::filesystem::path other = home.home() / "elsewhere.gguf";

    Stranded() {
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file, std::ios::binary} << apogee::testing::minimal_gguf("llama");
        std::ofstream{other, std::ios::binary} << apogee::testing::minimal_gguf("llama");
        std::ofstream{home.config_path(), std::ios::binary}
            << "# my backends\nbackends:\n  first:\n    type: llamacpp\n    model_path: " +
                   file.string() +
                   "  # the quick one\n  second:\n    type: llamacpp\n    "
                   "model_path: " +
                   file.string() +
                   "\n  kept:\n    type: llamacpp\n    "
                   "model_path: " +
                   other.string() + "\n";
    }

    [[nodiscard]] std::string config() const {
        std::ifstream in{home.config_path(), std::ios::binary};
        return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }
};

}  // namespace

TEST_CASE("deleting weights never touches the config without a word, and says the doors",
          "[commands][models][delete][backends]") {
    Stranded stranded;
    const std::string before = stranded.config();
    std::string out;
    // The plan alone: nothing removed, the backends' fate said.
    REQUIRE(stranded.home.run({"models", "delete", "org--m/gguf/aaaaaaaaaaaa"}, &out) == 0);
    CHECK(out.find("backend 'first' points into this and will stop working") != std::string::npos);
    CHECK(out.find("the backend entries stay unless you say -- --and-backends removes them with "
                   "the weights.\n") != std::string::npos);
    CHECK(std::filesystem::exists(stranded.file));
    REQUIRE(stranded.home.run({"models", "delete", "org--m/gguf/aaaaaaaaaaaa", "--and-backends"},
                              &out) == 0);
    CHECK(out.find("--and-backends: their 2 entries go too, through the config editor.\n") !=
          std::string::npos);
    CHECK(std::filesystem::exists(stranded.file));
    CHECK(stranded.config() == before);

    // -y alone removes the weights and never the entries: nobody to ask here.
    REQUIRE(stranded.home.run({"models", "delete", "org--m/gguf/aaaaaaaaaaaa", "--yes"}, &out) ==
            0);
    CHECK_FALSE(std::filesystem::exists(stranded.file));
    CHECK(stranded.config() == before);
    CHECK(out.find("kept backend 'first', which points at nothing now -- repoint it: apogee "
                   "config set-backend first --model-path <file>; or remove it: apogee config "
                   "delete-backend first\n") != std::string::npos);
    CHECK(out.find("kept backend 'second'") != std::string::npos);
    CHECK(out.find("kept backend 'kept'") == std::string::npos);
}

TEST_CASE(
    "with --and-backends exactly the affected entries go, as config delete-backend removes them",
    "[commands][models][delete][backends]") {
    Stranded flagged;
    Stranded by_hand;
    std::string out;
    REQUIRE(flagged.home.run(
                {"models", "delete", "org--m/gguf/aaaaaaaaaaaa", "--yes", "--and-backends"},
                &out) == 0);
    CHECK(out.find("removed backend 'first' from " + flagged.home.config_path().string()) !=
          std::string::npos);
    CHECK(out.find("removed backend 'second' from ") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(flagged.file));
    // Byte for byte the hand-typed removal's edit, comments and the
    // unrelated entry kept.
    REQUIRE(by_hand.home.run({"config", "delete-backend", "first"}, &out) == 0);
    REQUIRE(by_hand.home.run({"config", "delete-backend", "second"}, &out) == 0);
    std::string flagged_config = flagged.config();
    std::string hand_config = by_hand.config();
    const auto strip = [](std::string text, const std::string& root) {
        for (std::size_t at = text.find(root); at != std::string::npos; at = text.find(root)) {
            text.replace(at, root.size(), "<home>");
        }
        return text;
    };
    CHECK(strip(flagged_config, flagged.home.home().string()) ==
          strip(hand_config, by_hand.home.home().string()));
    CHECK(flagged_config.find("# my backends") != std::string::npos);
    CHECK(flagged_config.find("kept:") != std::string::npos);
}

TEST_CASE("the ask is the config half's own consent, default no", "[commands][models][delete]") {
    using apogee::commands::remove_backends_too;
    const std::vector<std::string> two{"first", "second"};
    std::string asked;
    const auto answer = [&asked](std::optional<std::string> said) {
        return [&asked, said](const std::string& question) {
            asked = question;
            return said;
        };
    };
    CHECK(remove_backends_too(false, two, answer("y")));
    CHECK(asked == "Remove the backend entries first, second too? [y/N] ");
    CHECK(remove_backends_too(false, {"first"}, answer("yes")));
    CHECK(asked == "Remove the backend entry first too? [y/N] ");
    CHECK_FALSE(remove_backends_too(false, two, answer("n")));
    CHECK_FALSE(remove_backends_too(false, two, answer("")));            // the default
    CHECK_FALSE(remove_backends_too(false, two, answer(std::nullopt)));  // nobody there
    CHECK_FALSE(remove_backends_too(false, two, {}));
    asked.clear();
    CHECK(remove_backends_too(true, two, answer("n")));  // the flag: never asked
    CHECK(asked.empty());
    CHECK_FALSE(remove_backends_too(true, {}, answer("y")));  // nothing to remove
}

TEST_CASE("the doctor's door for a dangling backend is one the binary accepts",
          "[commands][models][delete][check]") {
    // The user's transcript: check named `add-backend` on an existing name,
    // which the binary refuses. Each door it names now runs as printed.
    Stranded stranded;
    std::string out;
    REQUIRE(stranded.home.run({"models", "delete", "org--m/gguf/aaaaaaaaaaaa", "--yes"}, &out) ==
            0);
    (void)stranded.home.run({"check", "--no-color"}, &out);
    const std::string repoint = "apogee config set-backend first --model-path <an existing .gguf>";
    const std::string remove = "apogee config delete-backend first";
    CHECK(out.find(repoint) != std::string::npos);
    CHECK(out.find(remove) != std::string::npos);
    // The old door, refused as the user saw it.
    CHECK(stranded.home.run({"config", "add-backend", "first", "--type", "llamacpp", "--model-path",
                             stranded.other.string()},
                            &out) != 0);
    // The repoint door, as printed with a real file in its blank.
    REQUIRE(stranded.home.run(
                {"config", "set-backend", "first", "--model-path", stranded.other.string()},
                &out) == 0);
    REQUIRE(stranded.home.run({"config", "delete-backend", "second"}, &out) == 0);
    (void)stranded.home.run({"check", "--no-color"}, &out);
    CHECK(out.find("model_path does not exist") == std::string::npos);
}

// --- every reference into what goes (55d) -----------------------------------------------

namespace {

/// 55d's delete matrix: a store holding one set of each kind a reference can
/// point into, and a config pointing into every one the walk's way -- `e4b`
/// an mlx entry over a base, drafting with a stored drafter; `local` a
/// llamacpp entry over a stored GGUF, applying a stored LoRA and one outside
/// the store; and `models.default_video` naming a stored pipeline by its
/// model's name.
struct Pointed {
    apogee::testing::CliHome home{""};
    std::filesystem::path gguf =
        home.models() / "org--m" / "gguf" / "aaaaaaaaaaaa" / "m-Q4_K_M.gguf";
    std::filesystem::path lora =
        home.models() / "org--lora" / "gguf" / "bbbbbbbbbbbb" / "style.gguf";
    std::filesystem::path outside = home.home() / "mine.gguf";
    std::filesystem::path base = home.models() / "Gemma4-E4B-Instruct" / "mlx" / "cccccccccccc";
    std::filesystem::path drafter = home.models() / "Gemma4-E4B-Assistant" / "mlx" / "dddddddddddd";
    std::filesystem::path pipeline =
        home.models() / "Wan-AI--Wan2.2-TI2V-5B-Diffusers" / "pipeline" / "eeeeeeeeeeee";

    Pointed() {
        for (const std::filesystem::path& file : {gguf, lora, outside}) {
            std::filesystem::create_directories(file.parent_path());
            std::ofstream{file, std::ios::binary} << apogee::testing::minimal_gguf("llama");
        }
        apogee::testing::write_mlx_model(base, {.kind = "vision"});
        apogee::testing::write_mlx_model(drafter, {.kind = "drafter"});
        std::filesystem::create_directories(pipeline);
        std::ofstream{pipeline / "model_index.json", std::ios::binary}
            << R"({"_class_name": "WanPipeline", "_diffusers_version": "0.35.0"})";
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << "# my backends\nbackends:\n"
               "  e4b:\n    type: mlx\n    model_path: "
            << base.string() << "\n    draft: " << drafter.string()
            << "\n  local:\n    type: llamacpp\n    model_path: " << gguf.string()
            << "\n    adapters: [{path: " << lora.string()
            << ", base_sha256: " << std::string(64, 'a') << "}, {path: " << outside.string()
            << ", base_sha256: " << std::string(64, 'b') << "}]\n"
            << "models:\n  default: local\n  default_video: Wan-AI--Wan2.2-TI2V-5B-Diffusers\n";
    }

    [[nodiscard]] std::string config() const {
        std::ifstream in{home.config_path(), std::ios::binary};
        return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] apogee::harness::Config loaded() const {
        return apogee::harness::load_config(home.config_path());
    }
};

}  // namespace

TEST_CASE("models delete names and offers every reference into what goes, by the one walk",
          "[commands][models][delete][backends][store_references]") {
    Pointed pointed;
    const std::string before = pointed.config();
    std::string out;

    SECTION("a drafter: its base's draft, said, kept with its door, the door runnable") {
        // Before 55d: nothing said, and the draft left dangling -- the ladder
        // then refuses the whole base.
        REQUIRE(pointed.home.run({"models", "delete", "Gemma4-E4B-Assistant"}, &out) == 0);
        CHECK(out.find("\nbackend 'e4b' drafts with this (its draft) and is refused until it is "
                       "cleared.\n") != std::string::npos);
        CHECK(out.find("the draft of backend 'e4b' stays unless you say -- --and-backends clears "
                       "it with the weights.\n") != std::string::npos);
        CHECK(out.find("points into this") == std::string::npos);  // no entry goes
        REQUIRE(pointed.home.run({"models", "delete", "Gemma4-E4B-Assistant", "--and-backends"},
                                 &out) == 0);
        CHECK(out.find("--and-backends: the draft of backend 'e4b' is cleared too, through the "
                       "config editor.\n") != std::string::npos);
        CHECK(pointed.config() == before);
        // -y alone: the weights go, the draft is kept and its door said.
        REQUIRE(pointed.home.run({"models", "delete", "Gemma4-E4B-Assistant", "--yes"}, &out) == 0);
        CHECK_FALSE(std::filesystem::exists(pointed.drafter));
        CHECK(pointed.config() == before);
        const std::string door = "apogee config set-backend e4b --draft \"\"";
        CHECK(out.find("kept the draft of backend 'e4b', which names nothing now -- clear it: " +
                       door + "\n") != std::string::npos);
        // The door, as printed, on that state.
        REQUIRE(pointed.home.run({"config", "set-backend", "e4b", "--draft", ""}, &out) == 0);
        CHECK(pointed.loaded().find_backend("e4b")->draft.empty());
    }
    SECTION("a drafter with --and-backends: the draft cleared, as its door clears it") {
        Pointed by_hand;
        REQUIRE(pointed.home.run(
                    {"models", "delete", "Gemma4-E4B-Assistant", "--yes", "--and-backends"},
                    &out) == 0);
        CHECK(out.find("cleared the draft of backend 'e4b' in " +
                       pointed.home.config_path().string()) != std::string::npos);
        REQUIRE(by_hand.home.run({"config", "set-backend", "e4b", "--draft", ""}, &out) == 0);
        const auto strip = [](std::string text, const std::string& root) {
            for (std::size_t at = text.find(root); at != std::string::npos; at = text.find(root)) {
                text.replace(at, root.size(), "<home>");
            }
            return text;
        };
        CHECK(strip(pointed.config(), pointed.home.home().string()) ==
              strip(by_hand.config(), by_hand.home.home().string()));
        CHECK(pointed.config().find("# my backends") != std::string::npos);
        CHECK(pointed.loaded().find_backend("e4b")->model_path == pointed.base.string());
    }
    SECTION("a stored adapter: dropped from its entry's list, the rest kept with its pin") {
        REQUIRE(pointed.home.run({"models", "delete", "org--lora", "--yes", "--and-backends"},
                                 &out) == 0);
        CHECK(out.find("\nbackend 'local' applies an adapter from this (" + pointed.lora.string() +
                       ") and is refused until it is dropped.\n") != std::string::npos);
        CHECK(out.find("cleared the adapter " + pointed.lora.string() + " on backend 'local'") !=
              std::string::npos);
        const apogee::harness::Config config = pointed.loaded();
        REQUIRE(config.find_backend("local")->adapters.size() == 1);
        CHECK(config.find_backend("local")->adapters[0].path == pointed.outside.string());
        CHECK(config.find_backend("local")->adapters[0].base_sha256 == std::string(64, 'b'));
        CHECK(config.find_backend("local")->model_path == pointed.gguf.string());
    }
    SECTION("the default video pipeline: the pointer cleared, as 'set-default-video off' does") {
        Pointed by_hand;
        REQUIRE(pointed.home.run({"models", "delete", "Wan-AI--Wan2.2-TI2V-5B-Diffusers"}, &out) ==
                0);
        CHECK(out.find("\nmodels.default_video names this, and 'apogee video generate' without "
                       "--model finds nothing until it names another.\n") != std::string::npos);
        CHECK(out.find("models.default_video stays unless you say -- --and-backends clears it "
                       "with the weights.\n") != std::string::npos);
        REQUIRE(pointed.home.run({"models", "delete", "Wan-AI--Wan2.2-TI2V-5B-Diffusers", "--yes",
                                  "--and-backends"},
                                 &out) == 0);
        CHECK(out.find("cleared models.default_video in ") != std::string::npos);
        CHECK(pointed.loaded().models.default_video.empty());
        REQUIRE(by_hand.home.run({"config", "set-default-video", "off"}, &out) == 0);
        CHECK(pointed.config().substr(pointed.config().find("models:")) ==
              by_hand.config().substr(by_hand.config().find("models:")));
    }
    SECTION("the default video pipeline kept: its door said, and runnable") {
        REQUIRE(pointed.home.run({"models", "delete", "Wan-AI--Wan2.2-TI2V-5B-Diffusers", "--yes"},
                                 &out) == 0);
        CHECK(out.find("kept models.default_video, which names nothing now -- clear it: apogee "
                       "config set-default-video off\n") != std::string::npos);
        CHECK(pointed.config() == before);
        REQUIRE(pointed.home.run({"config", "set-default-video", "off"}, &out) == 0);
        CHECK(pointed.loaded().models.default_video.empty());
    }
    SECTION("a base: its entry goes whole, its draft with it, never cleared apart") {
        REQUIRE(pointed.home.run(
                    {"models", "delete", "Gemma4-E4B-Instruct", "--yes", "--and-backends"}, &out) ==
                0);
        CHECK(out.find("backend 'e4b' points into this and will stop working") !=
              std::string::npos);
        CHECK(out.find("drafts with this") == std::string::npos);
        CHECK(out.find("removed backend 'e4b' from ") != std::string::npos);
        CHECK(out.find("cleared") == std::string::npos);
        CHECK(pointed.loaded().find_backend("e4b") == nullptr);
        CHECK(pointed.loaded().find_backend("local") != nullptr);
    }
    SECTION("a set nothing points into: nothing said, the config untouched") {
        const std::filesystem::path idle =
            pointed.home.models() / "org--idle" / "gguf" / "ffffffffffff" / "idle.gguf";
        std::filesystem::create_directories(idle.parent_path());
        std::ofstream{idle, std::ios::binary} << apogee::testing::minimal_gguf("llama");
        REQUIRE(pointed.home.run({"models", "delete", "org--idle", "--yes", "--and-backends"},
                                 &out) == 0);
        CHECK(out.find("--and-backends:") == std::string::npos);
        CHECK(out.find("cleared") == std::string::npos);
        CHECK(pointed.config() == before);
    }
}

TEST_CASE("the ask names what a delete clears beside the entries it removes",
          "[commands][models][delete][store_references]") {
    using apogee::commands::remove_backends_too;
    std::string asked;
    const auto answer = [&asked](std::optional<std::string> said) {
        return [&asked, said](const std::string& question) {
            asked = question;
            return said;
        };
    };
    CHECK(remove_backends_too(false, {}, answer("y"), {"the draft of backend 'e4b'"}));
    CHECK(asked == "Clear the draft of backend 'e4b' too? [y/N] ");
    CHECK(remove_backends_too(false, {"first"}, answer("yes"),
                              {"the draft of backend 'e4b'", "models.default_video"}));
    CHECK(asked ==
          "Remove the backend entry first, and clear the draft of backend 'e4b' and "
          "models.default_video too? [y/N] ");
    CHECK_FALSE(remove_backends_too(false, {}, answer("n"), {"models.default_video"}));
    CHECK_FALSE(remove_backends_too(false, {}, {}, {"models.default_video"}));  // nobody there
    asked.clear();
    CHECK(remove_backends_too(true, {}, answer("n"), {"models.default_video"}));  // the flag
    CHECK(asked.empty());
    CHECK_FALSE(remove_backends_too(true, {}, answer("y"), {}));  // nothing to settle
}
