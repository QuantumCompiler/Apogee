#include "operations/store_references.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "modelstore/store.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"

/// The one reference walk (55d), as a matrix: every kind of reference a
/// config holds into the model store -- a backend's `model_path`,
/// `mmproj_path`, `draft:` and each adapter, and `models.default_video` --
/// listed, found inside the set that holds it and in no other, repointed by a
/// rename and let go by a delete, each through its own editor transform and
/// byte for byte the command a person would type for it.
namespace {

namespace fs = std::filesystem;
using apogee::commands::StoreReference;

void write_file(const fs::path& path, const std::string& bytes = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A store holding one set of each kind a reference can point into, and a
/// config pointing into every one: `local` runs a GGUF and its projector and
/// applies two adapters, one of them stored; `e4b` is an mlx entry over a
/// base drafting with a stored drafter; `models.default_video` names a
/// stored pipeline by its model's name.
struct Referenced {
    apogee::testing::TempDir root{"store-references-" + std::to_string(std::random_device{}())};
    fs::path models = root.path() / "models";
    apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(models);
    fs::path gguf = models / "org--gemma" / "gguf" / "111111111111";
    fs::path lora = models / "org--lora" / "gguf" / "222222222222";
    fs::path base = models / "Gemma4-E4B-Instruct" / "mlx" / "bbbbbbbbbbbb";
    fs::path drafter = models / "Gemma4-E4B-Assistant" / "mlx" / "aaaaaaaaaaaa";
    fs::path pipeline = models / "Wan-AI--Wan2.2-TI2V-5B-Diffusers" / "pipeline" / "cccccccccccc";
    fs::path outside = root.path() / "elsewhere" / "mine.gguf";
    fs::path config_path = root.path() / "config.yaml";

    Referenced() {
        write_file(gguf / "gemma-Q4_K_M.gguf", "GGUF");
        write_file(gguf / "gemma-Q4_K_M-mmproj.gguf", "GGUF");
        write_file(lora / "style.gguf", "GGUF");
        write_file(outside, "GGUF");
        apogee::testing::write_mlx_model(base, {.kind = "vision"});
        apogee::testing::write_mlx_model(drafter, {.kind = "drafter"});
        write_file(pipeline / "model_index.json",
                   R"({"_class_name": "WanPipeline", "_diffusers_version": "0.35.0"})");
        write_file(config_path,
                   "# the user's config\n"
                   "backends:\n"
                   "  e4b:\n"
                   "    type: mlx\n"
                   "    model_path: " +
                       base.string() +
                       "\n"
                       "    draft: " +
                       drafter.string() +
                       "   # beside its base\n"
                       "  local:\n"
                       "    type: llamacpp\n"
                       "    model_path: " +
                       (gguf / "gemma-Q4_K_M.gguf").string() +
                       "\n"
                       "    mmproj_path: " +
                       (gguf / "gemma-Q4_K_M-mmproj.gguf").string() +
                       "\n"
                       "    adapters: [{path: " +
                       (lora / "style.gguf").string() +
                       ", scale: 0.5, base_sha256: " + std::string(64, 'a') +
                       "}, {path: " + outside.string() + ", base_sha256: " + std::string(64, 'b') +
                       "}]\n"
                       "  cloud:\n"
                       "    type: anthropic\n"
                       "models:\n"
                       "  default: local\n"
                       "  default_video: Wan-AI--Wan2.2-TI2V-5B-Diffusers\n");
    }

    [[nodiscard]] apogee::harness::Config config() const {
        return apogee::harness::load_config(config_path);
    }

    [[nodiscard]] std::vector<StoreReference> references() const {
        return apogee::commands::store_references(config());
    }
};

[[nodiscard]] std::vector<std::string> wheres(const std::vector<StoreReference>& references) {
    std::vector<std::string> out;
    for (const StoreReference& reference : references) {
        out.push_back(reference.where() + (reference.weights ? " (weights)" : ""));
    }
    return out;
}

}  // namespace

TEST_CASE("the one walk lists every reference a config holds into the store, in one order",
          "[store_references][walk]") {
    const Referenced store;
    const std::vector<StoreReference> references = store.references();
    // Each backend by name, its fields in the declaration's order, then the
    // video pointer; an entry holding no path (the cloud one) and the role
    // pointers, which name backends, are no reference.
    CHECK(wheres(references) == std::vector<std::string>{
                                    "backends.e4b.model_path (weights)",
                                    "backends.e4b.draft",
                                    "backends.local.model_path (weights)",
                                    "backends.local.mmproj_path (weights)",
                                    "backends.local.adapters[0]",
                                    "backends.local.adapters[1]",
                                    "models.default_video",
                                });
    REQUIRE(references.size() == 7);
    CHECK(references[1].value == store.drafter.string());
    CHECK(references[5].value == store.outside.string());
    CHECK(references[5].index == 1);
    CHECK(references[6].value == "Wan-AI--Wan2.2-TI2V-5B-Diffusers");
    // Each points into the stored model holding it, the outside file into none.
    std::vector<std::string> models;
    for (const StoreReference& reference : references) {
        models.push_back(apogee::commands::referenced_model(store.roots, reference));
    }
    CHECK(models == std::vector<std::string>{"Gemma4-E4B-Instruct", "Gemma4-E4B-Assistant",
                                             "org--gemma", "org--gemma", "org--lora", "",
                                             "Wan-AI--Wan2.2-TI2V-5B-Diffusers"});
    // An empty config holds none.
    CHECK(apogee::commands::store_references(apogee::harness::Config{}).empty());
}

TEST_CASE("each reference points inside the set that holds it, and no other",
          "[store_references][walk]") {
    const Referenced store;
    const std::vector<StoreReference> references = store.references();
    const auto inside = [&](const fs::path& dir) {
        std::vector<std::string> out;
        for (const StoreReference& reference : references) {
            if (apogee::commands::points_inside(store.roots, reference, dir)) {
                out.push_back(reference.where());
            }
        }
        return out;
    };
    CHECK(inside(store.gguf) ==
          std::vector<std::string>{"backends.local.model_path", "backends.local.mmproj_path"});
    CHECK(inside(store.lora) == std::vector<std::string>{"backends.local.adapters[0]"});
    CHECK(inside(store.base) == std::vector<std::string>{"backends.e4b.model_path"});
    CHECK(inside(store.drafter) == std::vector<std::string>{"backends.e4b.draft"});
    CHECK(inside(store.pipeline) == std::vector<std::string>{"models.default_video"});
    CHECK(inside(store.models / "org--nothing" / "gguf" / "333333333333").empty());

    // The video pointer, by every spelling it takes (49c): the set it names,
    // or the one video model its model holds -- never a set of that model it
    // cannot run.
    const fs::path wan = store.models / "Wan-AI--Wan2.2-TI2V-5B-Diffusers";
    const fs::path original = wan / "safetensors" / "dddddddddddd";
    write_file(original / "config.json", R"({"_class_name": "WanModel"})");
    write_file(original / "model.safetensors");
    const fs::path mlx_video = wan / "mlx" / "eeeeeeeeeeee";
    apogee::testing::write_mlx_model(mlx_video, {.kind = "video"});
    for (const std::string& spelling :
         {std::string{"Wan-AI--Wan2.2-TI2V-5B-Diffusers"},
          std::string{"Wan-AI/Wan2.2-TI2V-5B-Diffusers"},
          std::string{"Wan-AI--Wan2.2-TI2V-5B-Diffusers/pipeline/cccccccccccc"},
          std::string{"cccccccccccc"}, store.pipeline.string()}) {
        INFO(spelling);
        const StoreReference pointer{.backend = {},
                                     .field = "default_video",
                                     .value = spelling,
                                     .index = 0,
                                     .weights = false};
        CHECK(apogee::commands::points_inside(store.roots, pointer, store.pipeline));
        CHECK_FALSE(apogee::commands::points_inside(store.roots, pointer, original));
    }
    // By its model's name it names that model's MLX video model too; by the
    // pipeline's handle, only the pipeline.
    CHECK(apogee::commands::points_inside(
        store.roots, {.backend = {}, .field = "default_video", .value = wan.filename().string()},
        mlx_video));
    CHECK_FALSE(apogee::commands::points_inside(
        store.roots,
        {.backend = {},
         .field = "default_video",
         .value = "Wan-AI--Wan2.2-TI2V-5B-Diffusers/pipeline/cccccccccccc"},
        mlx_video));
    // A pointer naming nothing stored points inside nothing.
    CHECK_FALSE(apogee::commands::points_inside(
        store.roots, {.backend = {}, .field = "default_video", .value = "org--gone"},
        store.pipeline));
}

TEST_CASE("a rename repoints each reference through its own transform, every other byte kept",
          "[store_references][walk][renames]") {
    const Referenced store;
    const apogee::harness::Config config = store.config();
    const std::vector<StoreReference> references = store.references();
    const std::string before = slurp(store.config_path);

    // The adapters' folder: the list written whole, each pin kept, the
    // adapter outside the store untouched.
    std::string text = apogee::commands::repoint_references(before, config, store.roots, references,
                                                            "org--lora", "Lora");
    apogee::harness::Config after = apogee::harness::parse_config(text, "<test>");
    REQUIRE(after.find_backend("local") != nullptr);
    const std::vector<apogee::harness::AdapterConfig>& adapters =
        after.find_backend("local")->adapters;
    REQUIRE(adapters.size() == 2);
    CHECK(adapters[0].path ==
          (store.models / "Lora" / "gguf" / "222222222222" / "style.gguf").string());
    CHECK(adapters[0].scale == 0.5);
    CHECK(adapters[0].base_sha256 == std::string(64, 'a'));
    CHECK(adapters[1] == config.find_backend("local")->adapters[1]);
    CHECK(after.find_backend("local")->model_path == config.find_backend("local")->model_path);

    // The drafter's folder: its `draft:` alone, its comment kept.
    text = apogee::commands::repoint_references(before, config, store.roots, references,
                                                "Gemma4-E4B-Assistant", "E4B-Drafter");
    CHECK(text.find("    draft: " +
                    apogee::harness::yaml_scalar(
                        (store.models / "E4B-Drafter" / "mlx" / "aaaaaaaaaaaa").string()) +
                    "   # beside its base\n") != std::string::npos);
    CHECK(apogee::harness::parse_config(text, "<test>").find_backend("e4b")->model_path ==
          store.base.string());

    // The pipeline's folder: the pointer as it was spelled, by its new name.
    text = apogee::commands::repoint_references(before, config, store.roots, references,
                                                "Wan-AI--Wan2.2-TI2V-5B-Diffusers", "Wan");
    CHECK(apogee::harness::parse_config(text, "<test>").models.default_video == "Wan");
    CHECK(text.find("# the user's config\n") == 0);

    // A folder nothing points into: the text as it was.
    CHECK(apogee::commands::repoint_references(before, config, store.roots, references,
                                               "org--nothing", "x") == before);
    // A ~ the user wrote stays theirs.
    const StoreReference home{.backend = "local",
                              .field = "model_path",
                              .value = "~/models/org--gemma/gguf/111111111111/g.gguf",
                              .index = 0,
                              .weights = true};
    const apogee::testing::EnvGuard guard{"HOME", store.root.path().string()};
    const apogee::testing::EnvGuard profile{"USERPROFILE", store.root.path().string()};
    CHECK(apogee::commands::repointed_value(store.roots, home, "org--gemma", "Gemma") ==
          std::optional<std::string>{"~/models/Gemma/gguf/111111111111/g.gguf"});
}

TEST_CASE("a delete lets each reference go byte for byte as its own command would",
          "[store_references][walk][delete]") {
    // The drafter, the stored adapter and the pipeline go: what the walk
    // clears is what `config set-backend --draft ""`, `--adapters <the rest>`
    // and `config set-default-video off` write, through the same editor.
    const Referenced store;
    const apogee::harness::Config config = store.config();
    std::vector<StoreReference> stranded;
    for (const StoreReference& reference : store.references()) {
        for (const fs::path& dir : {store.drafter, store.lora, store.pipeline}) {
            if (apogee::commands::points_inside(store.roots, reference, dir)) {
                stranded.push_back(reference);
            }
        }
    }
    REQUIRE(wheres(stranded) == std::vector<std::string>{"backends.e4b.draft",
                                                         "backends.local.adapters[0]",
                                                         "models.default_video"});
    const std::string before = slurp(store.config_path);
    const std::string cleared = apogee::commands::clear_references(before, config, stranded);

    std::string by_hand = apogee::harness::set_backend_field(before, "e4b", "draft", "");
    by_hand = apogee::harness::set_backend_adapters(by_hand, "local",
                                                    {config.find_backend("local")->adapters[1]});
    by_hand = apogee::harness::set_default_video(by_hand, "");
    CHECK(cleared == by_hand);
    const apogee::harness::Config after = apogee::harness::parse_config(cleared, "<test>");
    CHECK(after.find_backend("e4b")->draft.empty());
    CHECK(after.find_backend("e4b")->model_path == store.base.string());
    REQUIRE(after.find_backend("local")->adapters.size() == 1);
    CHECK(after.find_backend("local")->adapters[0].path == store.outside.string());
    CHECK(after.models.default_video.empty());
    CHECK(cleared.find("# the user's config\n") == 0);

    // A weights reference is never cleared: its entry goes whole instead.
    const std::vector<StoreReference> all = store.references();
    CHECK(apogee::commands::clear_references(before, config, {all[0]}) == before);

    // Each kept one's door, a command the binary takes on that state.
    CHECK(apogee::commands::clear_door(config, stranded, stranded[0]) ==
          "apogee config set-backend e4b --draft \"\"");
    CHECK(apogee::commands::clear_door(config, stranded, stranded[1]) ==
          "apogee config set-backend local --adapters " + store.outside.string());
    CHECK(apogee::commands::clear_door(config, stranded, stranded[2]) ==
          "apogee config set-default-video off");
    CHECK(apogee::commands::clear_door(config, stranded, all[0]).empty());
    // Both adapters gone: the list cleared, and its door says so.
    const std::vector<StoreReference> both{all[4], all[5]};
    CHECK(apogee::commands::clear_door(config, both, all[4]) ==
          "apogee config set-backend local --adapters \"\"");
    CHECK(apogee::harness::parse_config(apogee::commands::clear_references(before, config, both),
                                        "<test>")
              .find_backend("local")
              ->adapters.empty());
    // A scale other than 1 is carried as the flag takes it.
    CHECK(apogee::commands::clear_door(config, {all[5]}, all[5]) ==
          "apogee config set-backend local --adapters " + (store.lora / "style.gguf").string() +
              ":0.5");
}
