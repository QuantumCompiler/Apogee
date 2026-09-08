#include "commands/models.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "harness/roles.h"
#include "models/sidecar.h"
#include "support/gguf_builder.h"

/// The `models` listing surface.
///
/// The assertion that carries the most weight here is not about formatting: it
/// is that the ROLES column and `models status` both come from
/// `harness::resolve_backend_key`, so this command cannot tell the user one
/// thing while a run does another. That is the property the whole role-resolver
/// item exists to establish, and this is where it is visible.
namespace {

using apogee::commands::build_model_rows;
using apogee::commands::ModelRow;
using apogee::commands::render_model_info;
using apogee::commands::render_model_jsonl;
using apogee::commands::render_model_table;
using apogee::commands::render_role_status;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::Config;

/// A real, parseable GGUF on disk, so tests can exercise the branch that reads
/// one. Removed by `RealModel`'s destructor.
struct RealModel {
    /// A directory of its own per instance. A shared path let one test's
    /// sidecar leak into another's: the destructor removed the .gguf but not
    /// the .json beside it, so the result depended on test order. An
    /// order-dependent test is worse than no test.
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("apogee-models-test-" + std::to_string(counter()));
    std::filesystem::path path = dir / "real.gguf";

    explicit RealModel(std::string_view architecture) {
        std::error_code code;
        std::filesystem::create_directories(dir, code);
        const std::string bytes = apogee::testing::minimal_gguf(architecture);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        REQUIRE(std::filesystem::exists(path));
    }

    RealModel(const RealModel&) = delete;
    RealModel& operator=(const RealModel&) = delete;
    RealModel(RealModel&&) = delete;
    RealModel& operator=(RealModel&&) = delete;

    ~RealModel() {
        std::error_code code;
        // The whole directory, so a sidecar written beside the model goes too.
        std::filesystem::remove_all(dir, code);
    }

private:
    static int counter() {
        static int next = 0;
        return ++next;
    }
};

[[nodiscard]] Config sample_config() {
    Config config;
    config.models.default_backend = "cloud";
    config.models.default_embedding = "embedder";

    BackendConfig cloud;
    cloud.type = BackendType::Anthropic;
    cloud.model = "claude-sonnet-5";
    config.backends["cloud"] = cloud;

    BackendConfig embedder;
    embedder.type = BackendType::LlamaCpp;
    embedder.model_path = "/nonexistent/embedder.gguf";
    config.backends["embedder"] = embedder;

    BackendConfig unset;
    unset.type = BackendType::LlamaCpp;
    config.backends["unset"] = unset;

    return config;
}

/// Takes an lvalue only: returning a reference into a temporary vector was a
/// real bug in the first draft of this file. The rvalue overload is deleted so
/// the mistake is a compile error rather than a test that reads uninitialised
/// memory and reports empty strings.
[[nodiscard]] const ModelRow& row_for(std::vector<ModelRow>&&, std::string_view) = delete;

[[nodiscard]] const ModelRow& row_for(const std::vector<ModelRow>& rows, std::string_view backend) {
    const auto match = std::ranges::find_if(
        rows, [backend](const ModelRow& row) { return row.backend == backend; });
    REQUIRE(match != rows.end());
    return *match;
}

}  // namespace

TEST_CASE("the roles column comes from the shared resolver", "[commands][models][roles]") {
    // If this column were computed from config.models.* directly it could drift
    // from what a run does -- which is the exact Ommi bug the resolver exists
    // to prevent, reproduced one layer up in the display.
    const Config config = sample_config();
    const std::vector<ModelRow> rows = build_model_rows(config);

    // extraction has no pointer, so it falls through to models.default = cloud.
    CHECK(row_for(rows, "cloud").roles == "chat, extraction");
    CHECK(row_for(rows, "embedder").roles == "embedding");
    CHECK(row_for(rows, "unset").roles.empty());
}

TEST_CASE("a cloud backend is not reported as a missing local file",
          "[commands][models][listing]") {
    // A remote backend has no file, and rendering it as "missing" would send a
    // user looking for a model that was never supposed to be there.
    const std::vector<ModelRow> rows = build_model_rows(sample_config());
    const ModelRow& cloud = row_for(rows, "cloud");

    CHECK(cloud.provenance == "-");
    CHECK(cloud.state == "-");
    CHECK(cloud.architecture == "-");
    CHECK(cloud.note.empty());
}

TEST_CASE("a dangling model_path is reported with its reason", "[commands][models][listing]") {
    const std::vector<ModelRow> rows = build_model_rows(sample_config());
    const ModelRow& embedder = row_for(rows, "embedder");

    CHECK(embedder.provenance == "local");
    CHECK(embedder.state == "missing");
    CHECK_FALSE(embedder.note.empty());
}

TEST_CASE("a llamacpp entry with no model_path says so rather than reporting a bad read",
          "[commands][models][listing]") {
    const std::vector<ModelRow> rows = build_model_rows(sample_config());
    const ModelRow& unset = row_for(rows, "unset");

    CHECK(unset.state == "missing");
    CHECK(unset.note == "no model_path set");
}

TEST_CASE("architecture and profile are separate claims", "[commands][models][listing]") {
    // The one that is easy to get wrong and hard to notice: an architecture is
    // what the file says it is; a profile is what Apogee knows about how that
    // family behaves. Until the profile registry lands, every row is
    // unprofiled, and printing the architecture in that column would claim
    // knowledge Apogee does not have.
    //
    // This needs a model whose header actually PARSES -- asserting it only over
    // unreadable files never reaches the branch that sets the architecture, and
    // the first draft of this test was vacuous for exactly that reason.
    const RealModel model{"gemma3"};

    Config config = sample_config();
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = model.path.string();
    config.backends["local"] = local;

    const std::vector<ModelRow> rows = build_model_rows(config);
    const ModelRow& parsed = row_for(rows, "local");

    REQUIRE(parsed.state == "ok");
    CHECK(parsed.architecture == "gemma3");
    CHECK(parsed.profile == "gemma3");

    // Where the two genuinely differ: Qwen's GGUF declares `qwen35`, and the
    // profile covering it is named `qwen3` because one profile spans a family's
    // several architecture spellings. A column that echoed the architecture
    // would be a weaker, different claim.
    const RealModel qwen{"qwen35"};
    const std::vector<ModelRow> qwen_rows = build_model_rows(Config{}, qwen.dir);
    const ModelRow& qwen_row = row_for(qwen_rows, "(not configured)");
    CHECK(qwen_row.architecture == "qwen35");
    CHECK(qwen_row.profile == "qwen3");
}

TEST_CASE("an unrecognised architecture is reported but not profiled",
          "[commands][models][listing]") {
    // Both halves matter. The architecture is still shown -- a fact from the
    // file, useful even when nothing is known about it -- while the profile
    // honestly says nothing is.
    const RealModel model{"some-brand-new-arch"};

    const std::vector<ModelRow> rows = build_model_rows(Config{}, model.dir);
    const ModelRow& parsed = row_for(rows, "(not configured)");

    CHECK(parsed.architecture == "some-brand-new-arch");
    CHECK(parsed.profile == "unprofiled");
}

TEST_CASE("an unverified profile says so in the listing", "[commands][models][listing]") {
    // llama3 is registered from its published format but was never seen working
    // here. Under the open-model policy that qualifier is the only signal a
    // user gets about how much Apogee actually knows.
    const RealModel model{"llama"};

    const std::vector<ModelRow> rows = build_model_rows(Config{}, model.dir);
    const ModelRow& parsed = row_for(rows, "(not configured)");

    CHECK(parsed.profile.find("llama3") != std::string::npos);
    CHECK(parsed.profile.find("unverified") != std::string::npos);
}

TEST_CASE("info on a readable model reports the header and its resolved profile",
          "[commands][models][info]") {
    const RealModel model{"qwen35"};

    Config config = sample_config();
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = model.path.string();
    config.backends["local"] = local;

    const std::string body = render_model_info(config, "local");

    CHECK(body.find("header:       ok") != std::string::npos);
    CHECK(body.find("architecture: qwen35") != std::string::npos);
    CHECK(body.find("profile:      qwen3") != std::string::npos);
    CHECK(body.find("1 total, 1 text") != std::string::npos);
}

TEST_CASE("a model on disk is listed even when no backend points at it",
          "[commands][models][listing]") {
    // Found by running it: after a 460 MB pull, `models list` said "no backends
    // configured". A freshly acquired model is not in the config, so a listing
    // built only from `backends:` cannot see the thing the user just fetched.
    const RealModel model{"llama"};
    const std::vector<ModelRow> rows = build_model_rows(Config{}, model.dir);
    const auto match = std::ranges::find_if(
        rows, [&](const ModelRow& row) { return row.model == model.path.filename().string(); });
    REQUIRE(match != rows.end());

    CHECK(match->backend == "(not configured)");
    CHECK(match->provenance == "local");
    CHECK(match->architecture == "llama");
    CHECK(match->state == "ok");
    // No sidecar: honest about that rather than claiming anything.
    CHECK(match->verified == "no record");
}

TEST_CASE("the verified column reports what was checked, never the word verified",
          "[commands][models][listing]") {
    // The honesty rule, surfaced where a user reads it. "no digest published"
    // and "DIGEST MISMATCH" must not both render as one reassuring word.
    const RealModel model{"llama"};

    apogee::models::Sidecar sidecar;
    sidecar.source = "huggingface";
    sidecar.file = model.path.filename().string();
    sidecar.verification.header_checked = true;
    sidecar.verification.header_parsed = true;
    REQUIRE(apogee::models::write_sidecar(model.path, sidecar));

    const std::vector<ModelRow> rows = build_model_rows(Config{}, model.dir);
    const auto match = std::ranges::find_if(
        rows, [&](const ModelRow& row) { return row.model == model.path.filename().string(); });
    REQUIRE(match != rows.end());

    CHECK(match->verified.find("no digest published") != std::string::npos);
    CHECK(match->verified.find("header ok") != std::string::npos);
    CHECK(match->verified != "verified");
}

TEST_CASE("an empty config explains itself instead of printing a bare header",
          "[commands][models][listing]") {
    const std::string table = render_model_table({});
    CHECK(table.find("no backends configured") != std::string::npos);
    CHECK(table.find("BACKEND") == std::string::npos);
}

TEST_CASE("the table lists every configured backend", "[commands][models][listing]") {
    const std::string table = render_model_table(build_model_rows(sample_config()));

    for (const std::string_view backend : {"cloud", "embedder", "unset"}) {
        INFO("backend: " << backend);
        CHECK(table.find(backend) != std::string::npos);
    }
    CHECK(table.find("BACKEND") != std::string::npos);
}

TEST_CASE("the jsonl listing is one object per row", "[commands][models][machine]") {
    // Machine mode's rule, applied to a second command: every line is a JSON
    // object carrying a type, so a driver parses this the way it parses chat.
    const std::string jsonl = render_model_jsonl(build_model_rows(sample_config()));

    int lines = 0;
    for (std::size_t start = 0; start < jsonl.size();) {
        const std::size_t end = jsonl.find('\n', start);
        const std::string line = jsonl.substr(start, end - start);
        if (!line.empty()) {
            ++lines;
            INFO("line: " << line);
            CHECK(line.front() == '{');
            CHECK(line.find("\"type\":\"model\"") != std::string::npos);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    CHECK(lines == 3);
}

TEST_CASE("info on an unknown backend yields nothing for the caller to report",
          "[commands][models][info]") {
    CHECK(render_model_info(sample_config(), "nope").empty());
}

TEST_CASE("info on a broken model states the reason, never a blank field",
          "[commands][models][info]") {
    // The reporting failure this surface exists to prevent: an unreadable
    // header rendering as an empty line reads like "nothing wrong here".
    const std::string body = render_model_info(sample_config(), "embedder");
    REQUIRE_FALSE(body.empty());

    // The reason must be ON the header line. Searching the whole body is what
    // the first draft did, and it passed for the wrong reason: the path also
    // appears on the model_path line, so deleting the reason from the failure
    // line left the assertion green.
    const std::size_t start = body.find("header:");
    REQUIRE(start != std::string::npos);
    const std::string header_line = body.substr(start, body.find('\n', start) - start);

    INFO("header line: " << header_line);
    CHECK(header_line.find("FAILED") != std::string::npos);
    CHECK(header_line.find("embedder.gguf") != std::string::npos);
    // Something beyond the bare verdict -- an empty reason is the reporting
    // failure this surface exists to prevent.
    CHECK(header_line.size() > std::string{"header:       FAILED -- "}.size());

    CHECK(body.find("repair") != std::string::npos);
}

TEST_CASE("info on a cloud backend says there is no local file", "[commands][models][info]") {
    const std::string body = render_model_info(sample_config(), "cloud");

    CHECK(body.find("remote") != std::string::npos);
    // It must not print GGUF fields that would read like a failed read.
    CHECK(body.find("header:") == std::string::npos);
    CHECK(body.find("tensors:") == std::string::npos);
}

TEST_CASE("status names the rung each role resolved through", "[commands][models][roles]") {
    const std::string status = render_role_status(sample_config());

    CHECK(status.find("chat: cloud") != std::string::npos);
    CHECK(status.find("embedding: embedder") != std::string::npos);
    // extraction has no pointer of its own, and saying which rung answered is
    // what makes an unexpected backend diagnosable instead of mysterious.
    CHECK(status.find("extraction: cloud   (via models.default)") != std::string::npos);
}

TEST_CASE("status flags a role pointing at a backend that is not configured",
          "[commands][models][roles]") {
    // Resolving and validating are separate: the resolver returns the key it
    // was told, and this surface reports that it names nothing.
    Config config = sample_config();
    config.models.default_embedding = "ghost";

    const std::string status = render_role_status(config);
    CHECK(status.find("embedding: ghost") != std::string::npos);
    CHECK(status.find("[not configured]") != std::string::npos);
}

TEST_CASE("status on an empty config reports unset rather than a blank name",
          "[commands][models][roles]") {
    const std::string status = render_role_status(Config{});
    CHECK(status.find("(unset") != std::string::npos);
}
