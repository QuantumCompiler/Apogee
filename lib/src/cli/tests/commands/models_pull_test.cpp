#include "commands/models_pull.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "models/acquire.h"
#include "models/sidecar.h"
#include "support/gguf_builder.h"

/// The mutating verbs, and the boundary they must never cross.
///
/// The assertion carrying the most weight is that **"delete a model by name"
/// can never become "delete a file by path"**. A models directory is a place a
/// user will type names into carelessly, and a `..` that resolved would make a
/// listing command into a deletion tool aimed anywhere.
namespace {

using apogee::commands::DeletePlan;
using apogee::commands::plan_delete;
using apogee::commands::render_repair;

struct Models {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("apogee-pull-" + std::to_string(counter()));

    Models() {
        std::error_code code;
        std::filesystem::create_directories(root, code);
    }

    Models(const Models&) = delete;
    Models& operator=(const Models&) = delete;
    Models(Models&&) = delete;
    Models& operator=(Models&&) = delete;

    ~Models() {
        std::error_code code;
        std::filesystem::remove_all(root, code);
    }

    [[nodiscard]] std::filesystem::path add(std::string_view name,
                                            std::string_view architecture = "llama") const {
        const std::filesystem::path path = root / std::string{name};
        const std::string bytes = apogee::testing::minimal_gguf(architecture);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return path;
    }

    void add_sidecar(const std::filesystem::path& model, std::string_view source,
                     std::string_view ref) const {
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

private:
    static int counter() {
        static int next = 0;
        return ++next;
    }
};

}  // namespace

TEST_CASE("a model is deleted by name", "[commands][models][delete]") {
    Models models;
    const std::filesystem::path model = models.add("thing.gguf");

    const DeletePlan plan = plan_delete(models.root, "thing.gguf");
    REQUIRE(plan.ok);
    CHECK(plan.model == model);
    CHECK_FALSE(plan.has_sidecar);
}

TEST_CASE("the .gguf extension is optional in the name", "[commands][models][delete]") {
    Models models;
    models.add("thing.gguf");

    const DeletePlan plan = plan_delete(models.root, "thing");
    CHECK(plan.ok);
}

TEST_CASE("a name containing .. is refused before anything is touched",
          "[commands][models][delete][safety]") {
    // The boundary this command must never cross. A models directory is
    // somewhere a user types names carelessly, and a traversal that resolved
    // would turn a model manager into a deletion tool aimed anywhere on disk.
    Models models;
    models.add("thing.gguf");

    for (const std::string_view name :
         {"../../etc/passwd", "..", "sub/../../../thing", "a/../../b"}) {
        INFO("name: " << name);
        const DeletePlan plan = plan_delete(models.root, name);
        CHECK_FALSE(plan.ok);
        CHECK_FALSE(plan.error.empty());
    }
}

TEST_CASE("an absolute path is refused", "[commands][models][delete][safety]") {
    Models models;
    const DeletePlan plan = plan_delete(models.root, "/etc/hosts");
    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("named, not pathed") != std::string::npos);
}

TEST_CASE("a name that does not exist reports so", "[commands][models][delete]") {
    Models models;
    const DeletePlan plan = plan_delete(models.root, "absent.gguf");
    CHECK_FALSE(plan.ok);
    CHECK(plan.error.find("no model named") != std::string::npos);
}

TEST_CASE("the sidecar is planned for removal alongside its model", "[commands][models][delete]") {
    Models models;
    const std::filesystem::path model = models.add("thing.gguf");
    models.add_sidecar(model, "huggingface", "owner/repo:thing.gguf");

    const DeletePlan plan = plan_delete(models.root, "thing.gguf");
    REQUIRE(plan.ok);
    CHECK(plan.has_sidecar);
    CHECK(plan.sidecar == apogee::models::sidecar_path_for(model));
}

TEST_CASE("an Ollama-sourced model names ollama rm rather than touching the store",
          "[commands][models][delete][safety]") {
    // Ollama's blobs are SHARED between models. Deleting one by hand silently
    // corrupts every sibling that referenced it, so Apogee removes only its own
    // copy and points at the vendor's reference-counting GC.
    Models models;
    const std::filesystem::path model = models.add("llama3.2-3b.gguf");
    models.add_sidecar(model, "ollama", "llama3.2:3b");

    const DeletePlan plan = plan_delete(models.root, "llama3.2-3b.gguf");
    REQUIRE(plan.ok);
    CHECK(plan.ollama_sourced);
    CHECK(plan.ollama_ref == "llama3.2:3b");
    // The plan touches Apogee's copy and nothing else.
    CHECK(plan.model.string().rfind(models.root.string(), 0) == 0);
}

TEST_CASE("repair on a sound model says there is nothing to do", "[commands][models][repair]") {
    Models models;
    const std::filesystem::path model = models.add("good.gguf");
    models.add_sidecar(model, "huggingface", "owner/repo:good.gguf");

    const std::string body = render_repair(models.root, "good.gguf");
    CHECK(body.find("Nothing to repair") != std::string::npos);
}

TEST_CASE("repair on a changed model says how to re-acquire it", "[commands][models][repair]") {
    Models models;
    const std::filesystem::path model = models.add("drifted.gguf");
    models.add_sidecar(model, "ollama", "llama3.2:3b");

    // The file changes after its record was written.
    {
        std::ofstream out(model, std::ios::binary | std::ios::app);
        out << "appended later";
    }

    const std::string body = render_repair(models.root, "drifted.gguf");
    CHECK(body.find("no longer matches") != std::string::npos);
    // The exact commands, so they can be copied.
    CHECK(body.find("apogee models delete drifted.gguf") != std::string::npos);
    CHECK(body.find("apogee models pull llama3.2:3b") != std::string::npos);
}

TEST_CASE("repair on a hand-placed model reports the absence of a record, not an error",
          "[commands][models][repair]") {
    // A model a user dropped in themselves is legitimate; it simply has nothing
    // to recheck integrity against.
    Models models;
    models.add("byhand.gguf");

    const std::string body = render_repair(models.root, "byhand.gguf");
    CHECK(body.find("record:  none") != std::string::npos);
    CHECK(body.find("header:  ok") != std::string::npos);
}

TEST_CASE("repair on a broken hand-placed model says what to do", "[commands][models][repair]") {
    Models models;
    {
        std::ofstream out(models.root / "broken.gguf", std::ios::binary);
        out << "GGUF";  // valid magic, nothing behind it
    }

    const std::string body = render_repair(models.root, "broken.gguf");
    CHECK(body.find("FAILED") != std::string::npos);
    CHECK(body.find("apogee models delete broken.gguf") != std::string::npos);
}

TEST_CASE("repair on an unknown name yields nothing for the caller to report",
          "[commands][models][repair]") {
    Models models;
    CHECK(render_repair(models.root, "absent.gguf").empty());
}
