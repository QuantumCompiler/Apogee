#include "commands/models.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "harness/roles.h"
#include "models/sidecar.h"
#include "models/snapshot.h"
#include "models/store.h"
#include "secrets/resolve.h"
#include "secrets/store.h"
#include "support/env_guard.h"
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
    /// Where the model store keeps a GGUF: `<model>/gguf/<id>/<file>`.
    std::filesystem::path path = dir / "m" / "gguf" / "111111111111" / "real.gguf";
    /// What `models list` calls it: the handle the other verbs take.
    std::string handle = "m/gguf/111111111111";

    explicit RealModel(std::string_view architecture) {
        std::error_code code;
        std::filesystem::create_directories(path.parent_path(), code);
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
    // user looking for a model that was never supposed to be there. What its
    // state column carries instead is the key -- where it comes from, never
    // what it is -- so the developer's own shell is kept out with an empty
    // snapshot.
    const apogee::secrets::EnvSnapshot empty;
    const std::vector<ModelRow> rows = build_model_rows(sample_config(), {}, {}, &empty);
    const ModelRow& cloud = row_for(rows, "cloud");

    CHECK(cloud.provenance == "-");
    CHECK(cloud.state == "no key");
    CHECK(cloud.architecture == "-");
    CHECK(cloud.note.find("apogee auth add anthropic") != std::string::npos);
}

TEST_CASE("a cloud row names where its key comes from, through the one resolver",
          "[commands][models][listing][secrets]") {
    // The same chain the factory builds with, so this column and a run agree:
    // config, then the store beside the config, then the snapshot.
    const apogee::testing::TempDir home{"models-secrets-" + std::to_string(std::random_device{}())};
    const std::filesystem::path config_path = home.path() / "config.yaml";
    apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(config_path)};
    const apogee::secrets::EnvSnapshot env = apogee::secrets::EnvSnapshot::capture(
        [](std::string_view name) { return name == "ANTHROPIC_API_KEY" ? "sk-ENVSECRET" : ""; });
    Config config = sample_config();

    const std::vector<ModelRow> from_env = build_model_rows(config, {}, config_path, &env);
    CHECK(row_for(from_env, "cloud").state == "key: ANTHROPIC_API_KEY");
    store.put("anthropic", "sk-STORESECRET");
    const std::vector<ModelRow> from_store = build_model_rows(config, {}, config_path, &env);
    CHECK(row_for(from_store, "cloud").state == "key: store");
    // No config path means no store is consulted.
    const std::vector<ModelRow> no_store = build_model_rows(config, {}, {}, &env);
    CHECK(row_for(no_store, "cloud").state == "key: ANTHROPIC_API_KEY");
    config.backends["cloud"].api_key = "sk-CFGSECRET";
    const std::vector<ModelRow> rows = build_model_rows(config, {}, config_path, &env);
    CHECK(row_for(rows, "cloud").state == "key: config");
    CHECK(row_for(rows, "cloud").note.empty());
    // A local backend's column is untouched by any of this.
    CHECK(row_for(rows, "unset").state == "missing");

    // And nothing rendered carries a key.
    for (const std::string& rendered : {render_model_table(rows), render_model_jsonl(rows)}) {
        CHECK(rendered.find("SECRET") == std::string::npos);
    }
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
    // No template in the header: said, with what that means.
    CHECK(body.find("template:     none -- most likely a base (pretrained) model") !=
          std::string::npos);
}

TEST_CASE("info states the window a local backend gets and what its cache costs",
          "[commands][models][info][cache]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "apogee-models-info-qwen38.gguf";
    {
        const std::string bytes = apogee::testing::qwen38_like_gguf();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Config config = sample_config();
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = path.string();
    config.backends["local"] = local;
    local.context_size = 5000;
    local.cache_type = apogee::harness::KvCacheType::Q4_0;
    config.backends["pinned"] = local;

    const std::string unset = render_model_info(config, "local");
    CHECK(unset.find("window:       32768 tokens (the default; trained for 262144)\n") !=
          std::string::npos);
    CHECK(unset.find("cache:        1088 MiB at q8_0 (the default)\n") != std::string::npos);

    // Exactly as written; the cache is what llama.cpp allocates for it, 5,120
    // positions at 18 bytes per 32 values.
    const std::string pinned = render_model_info(config, "pinned");
    CHECK(pinned.find("window:       5000 tokens (context_size; trained for 262144)\n") !=
          std::string::npos);
    CHECK(pinned.find("cache:        90 MiB at q4_0\n") != std::string::npos);

    std::error_code code;
    std::filesystem::remove(path, code);

    // A header with no attention geometry cannot be sized, and says so.
    const RealModel model{"llama"};
    BackendConfig plain;
    plain.type = BackendType::LlamaCpp;
    plain.model_path = model.path.string();
    config.backends["plain"] = plain;
    CHECK(render_model_info(config, "plain")
              .find("cache:        not known for this "
                    "architecture\n") != std::string::npos);
}

TEST_CASE("info says when a model's sliding layers keep only their window",
          "[commands][models][info][cache]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "apogee-models-info-gemma4.gguf";
    {
        const std::string bytes = apogee::testing::gemma4_like_gguf();
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Config config = sample_config();
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = path.string();
    config.backends["gemma"] = local;

    // 272 MiB for the 8 full layers over 32,768 positions, 255 for the 40
    // sliding ones over 1,536 -- llama.cpp's own sizes (26m).
    const std::string body = render_model_info(config, "gemma");
    CHECK(body.find("cache:        527 MiB at q8_0 (the default), sliding layers at 1536 "
                    "positions\n") != std::string::npos);

    std::error_code code;
    std::filesystem::remove(path, code);
}

TEST_CASE("info says when a model ships its own chat template", "[commands][models][info]") {
    apogee::testing::GgufBuilder builder;
    builder.magic().u32(3).u64(1).u64(2);
    builder.string_kv("general.architecture", "llama");
    builder.string_kv("tokenizer.chat_template", "{{ messages }}");
    builder.tensor("token_embd.weight");
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "apogee-models-info-templated.gguf";
    REQUIRE(builder.write_to(path));

    Config config = sample_config();
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = path.string();
    config.backends["instruct"] = local;
    CHECK(render_model_info(config, "instruct").find("template:     the model's own\n") !=
          std::string::npos);

    std::error_code code;
    std::filesystem::remove(path, code);
}

TEST_CASE("a model on disk is listed even when no backend points at it",
          "[commands][models][listing]") {
    // Found by running it: after a 460 MB pull, `models list` said "no backends
    // configured". A freshly acquired model is not in the config, so a listing
    // built only from `backends:` cannot see the thing the user just fetched.
    const RealModel model{"llama"};
    const std::vector<ModelRow> rows = build_model_rows(Config{}, model.dir);
    const auto match =
        std::ranges::find_if(rows, [&](const ModelRow& row) { return row.model == model.handle; });
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
    const auto match =
        std::ranges::find_if(rows, [&](const ModelRow& row) { return row.model == model.handle; });
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

TEST_CASE("status lists the helper roles, and what a local one costs to load",
          "[commands][models][roles]") {
    // Unset, a helper runs on the chat's own backend -- not a fact this
    // command knows, so it says that rather than naming models.default.
    const std::string unset = render_role_status(sample_config());
    CHECK(unset.find("vision: (unset -- the chat's own backend)\n") != std::string::npos);
    CHECK(unset.find("transcription: (unset -- the chat's own backend)\n") != std::string::npos);
    CHECK(unset.find("utility: (unset -- the chat's own backend)\n") != std::string::npos);

    // Pointed at a local model, the line says what it costs: the weights and
    // the cache its window allocates, beside the chat model's.
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "apogee-models-status-helper.gguf";
    {
        const std::string bytes = apogee::testing::qwen38_like_gguf();
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Config config = sample_config();
    BackendConfig helper;
    helper.type = BackendType::LlamaCpp;
    helper.model_path = path.string();
    config.backends["helper"] = helper;
    config.models.default_utility = "helper";
    const std::string set = render_role_status(config);
    CHECK(set.find("utility: helper   [local: 0 MiB of weights, 1088 MiB of cache]\n") !=
          std::string::npos);
    // A role pointer, so no "via" note.
    CHECK(set.find("utility: helper   (via") == std::string::npos);

    std::error_code code;
    std::filesystem::remove(path, code);
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

TEST_CASE("a SafeTensors snapshot is listed by its state, architecture and record, with no note",
          "[commands][models][listing][snapshot]") {
    const RealModel model{"llama"};
    const std::filesystem::path snapshot =
        model.dir / "owner--repo" / "safetensors" / "aaaaaaaaaaaa";
    std::filesystem::create_directories(snapshot);
    std::ofstream{snapshot / "config.json"} << R"({"architectures": ["Qwen2ForCausalLM"]})";
    std::ofstream{snapshot / "model.safetensors"} << "weights";
    const auto is_snapshot_row = [](const ModelRow& row) {
        return row.model == "owner--repo/safetensors/aaaaaaaaaaaa";
    };

    std::vector<ModelRow> rows = build_model_rows(Config{}, model.dir);
    auto match = std::ranges::find_if(rows, is_snapshot_row);
    REQUIRE(match != rows.end());
    CHECK(match->backend == "(not configured)");
    CHECK(match->state == "safetensors");
    CHECK(match->architecture == "Qwen2");
    CHECK(match->provenance == "local");
    CHECK(match->verified == "no record");
    // Its state column says what it is. The line that used to sit under every
    // snapshot ("full weights, trainable -- ... makes a GGUF") was noise, and
    // was asked to go (2026-09-25).
    CHECK(match->note.empty());
    CHECK_FALSE(match->attention);

    apogee::models::Snapshot record;
    record.ref = "owner/repo";
    record.source = "huggingface";
    record.files.push_back({"model.safetensors", 7, "abc"});
    REQUIRE(apogee::models::write_snapshot(snapshot, record));
    rows = build_model_rows(Config{}, model.dir);
    match = std::ranges::find_if(rows, is_snapshot_row);
    REQUIRE(match != rows.end());
    CHECK(match->provenance == "huggingface");
    CHECK(match->verified == "1 file(s) on record");

    // paths.hf_dir is where SafeTensors sets live when it is set.
    const apogee::testing::TempDir hf{"models-hf-" + std::to_string(std::random_device{}())};
    const std::filesystem::path other = hf.path() / "org--base" / "safetensors" / "bbbbbbbbbbbb";
    std::filesystem::create_directories(other);
    std::ofstream{other / "config.json"} << R"({"model_type": "gemma3"})";
    std::ofstream{other / "w.safetensors"} << "w";
    Config config;
    config.paths.hf_dir = hf.path().string();
    rows = build_model_rows(config, model.dir);
    match = std::ranges::find_if(rows, [](const ModelRow& row) {
        return row.model == "org--base/safetensors/bbbbbbbbbbbb";
    });
    REQUIRE(match != rows.end());
    CHECK(match->architecture == "gemma3");
}

TEST_CASE("a model still in the flat layout is listed with the migration named",
          "[commands][models][listing][legacy]") {
    const apogee::testing::TempDir models{"models-flat-" + std::to_string(std::random_device{}())};
    std::ofstream{models.path() / "old.gguf", std::ios::binary}
        << apogee::testing::minimal_gguf("llama");
    const std::vector<ModelRow> rows = build_model_rows(Config{}, models.path());
    const auto match =
        std::ranges::find_if(rows, [](const ModelRow& row) { return row.model == "old.gguf"; });
    REQUIRE(match != rows.end());
    CHECK(match->state == "old layout");
    CHECK(match->note.find("apogee models migrate") != std::string::npos);
}

TEST_CASE("a row says whether it is configured and whether it needs attention",
          "[commands][models][listing][color]") {
    const RealModel stored{"llama"};
    Config config = sample_config();
    config.backends["keyed"] = config.backends["cloud"];
    config.backends["keyed"].api_key = "sk-test";
    const apogee::secrets::EnvSnapshot empty;
    const std::vector<ModelRow> rows = build_model_rows(config, stored.dir, {}, &empty);

    // Configured and working.
    CHECK(row_for(rows, "keyed").configured);
    CHECK_FALSE(row_for(rows, "keyed").attention);
    // Configured, and each needing something: a key, a file, a model_path.
    for (const std::string_view broken : {"cloud", "embedder", "unset"}) {
        INFO(broken);
        CHECK(row_for(rows, broken).configured);
        CHECK(row_for(rows, broken).attention);
    }
    // On disk, fine, and no backend points at it.
    CHECK_FALSE(row_for(rows, "(not configured)").configured);
    CHECK_FALSE(row_for(rows, "(not configured)").attention);
}

TEST_CASE("rows are coloured by what they are, and align the same without colour",
          "[commands][models][listing][color]") {
    // Asked for directly (2026-09-25): configured backends in Apogee's cyan,
    // what no backend points at in another colour. Attention is yellow either
    // way -- a configured backend with no file is not one to look healthy.
    const auto row = [](std::string backend, bool configured, bool attention, std::string note) {
        ModelRow out;
        out.backend = std::move(backend);
        out.type = "llamacpp";
        out.model = "m.gguf";
        out.configured = configured;
        out.attention = attention;
        out.note = std::move(note);
        return out;
    };
    const std::vector<ModelRow> rows{row("ready", true, false, ""),
                                     row("broken", true, true, "no model_path set"),
                                     row("(not configured)", false, false, "")};

    const std::string plain = render_model_table(rows);
    CHECK(plain.find('\033') == std::string::npos);

    const std::string coloured = render_model_table(rows, apogee::ansi::Style{true});
    const std::string cyan{apogee::ansi::color_code(apogee::ansi::Color::Cyan)};
    const std::string yellow{apogee::ansi::color_code(apogee::ansi::Color::Yellow)};
    const std::string dim{apogee::ansi::kDim};
    const auto line_starting = [&coloured](std::string_view text) {
        const std::size_t at = coloured.find(text);
        REQUIRE(at != std::string::npos);
        return coloured.substr(coloured.rfind('\n', at) + 1, at - coloured.rfind('\n', at) - 1);
    };
    CHECK(line_starting("ready") == cyan);
    CHECK(line_starting("broken") == yellow);
    CHECK(line_starting("    no model_path set") == yellow);  // a note is its row's colour
    CHECK(line_starting("(not configured)") == dim);
    CHECK(coloured.find("BACKEND") == 0);  // the header stays plain

    // Colour is laid over the text, never counted into a column's width.
    CHECK(std::regex_replace(coloured, std::regex{"\033\\[[0-9;]*m"}, "") == plain);
}

TEST_CASE("the listing says each read as it goes, numbered against the whole sweep",
          "[commands][models][listing][busy]") {
    // M1: `models list` used to read every header in silence. The sweep now
    // names each read, against a total counted before the first -- and saying
    // so changes nothing the listing prints.
    const RealModel stored{"llama"};
    const RealModel elsewhere{"qwen3"};
    const std::filesystem::path snapshot =
        stored.dir / "owner--repo" / "safetensors" / "aaaaaaaaaaaa";
    std::filesystem::create_directories(snapshot);
    std::ofstream{snapshot / "config.json"} << R"({"architectures": ["Qwen2ForCausalLM"]})";
    std::ofstream{snapshot / "model.safetensors"} << "weights";

    Config config = sample_config();  // a cloud backend, a dangling path, an unset one
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = elsewhere.path.string();
    config.backends["local"] = local;

    std::vector<std::tuple<std::string, std::size_t, std::size_t>> heard;
    const std::vector<ModelRow> rows =
        build_model_rows(config, stored.dir, {}, nullptr,
                         [&heard](std::string_view label, std::size_t done, std::size_t total) {
                             heard.emplace_back(label, done, total);
                         });
    CHECK(render_model_table(rows) == render_model_table(build_model_rows(config, stored.dir)));

    // The two configured local files (one dangling), the stored GGUF no
    // backend points at, and the snapshot -- never the cloud backend or the
    // one with no path.
    REQUIRE(heard.size() == 4);
    for (std::size_t at = 0; at < heard.size(); ++at) {
        CHECK(std::get<1>(heard[at]) == at + 1);
        CHECK(std::get<2>(heard[at]) == 4);
    }
    CHECK(std::get<0>(heard[0]) == "reading model headers: embedder.gguf");
    CHECK(std::get<0>(heard[1]) == "reading model headers: real.gguf");
    CHECK(std::get<0>(heard[2]) == "reading model headers: real.gguf");
    CHECK(std::get<0>(heard[3]) == "reading snapshots: owner--repo");
}

TEST_CASE("info and status say the header they read", "[commands][models][busy]") {
    const RealModel model{"llama"};
    Config config;
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.model_path = model.path.string();
    config.backends["local"] = local;
    config.models.default_backend = "local";

    std::vector<std::string> heard;
    const auto sink = [&heard](std::string_view label, std::size_t, std::size_t total) {
        // No count: neither knows a total worth claiming.
        CHECK(total == 0);
        heard.emplace_back(label);
    };
    CHECK(render_model_info(config, "local", sink) == render_model_info(config, "local"));
    REQUIRE(heard.size() == 1);
    CHECK(heard.front() == "reading real.gguf");

    heard.clear();
    CHECK(render_role_status(config, sink) == render_role_status(config));
    REQUIRE_FALSE(heard.empty());
    CHECK(heard.front() == "reading local's model header");
}

namespace {

/// The motivating listing (2026-10-03) in small: `org/m` pulled, converted to
/// an F16 and quantized -- the quantization a backend -- and `org/e2b` pulled
/// and never converted. Every record as the verbs write them.
struct LineageStore {
    apogee::testing::TempDir dir{"models-lineage-" + std::to_string(std::random_device{}())};
    std::filesystem::path models = dir.path();
    std::filesystem::path snapshot = models / "org--m" / "safetensors" / "aaaaaaaaaaaa";
    std::filesystem::path waiting = models / "org--e2b" / "safetensors" / "cccccccccccc";
    std::filesystem::path f16 = models / "org--m" / "gguf" / "111111111111" / "m-F16.gguf";
    std::filesystem::path quant = models / "org--m" / "gguf" / "222222222222" / "m-Q4_K_M.gguf";
    Config config;

    LineageStore() {
        write_snapshot_dir(snapshot, "org/m");
        write_snapshot_dir(waiting, "org/e2b");
        apogee::models::Sidecar converted;
        converted.source = "convert";
        converted.transform = "convert";
        converted.ref = "org--m/safetensors/aaaaaaaaaaaa";
        converted.transform_note = "--outtype f16";
        write_gguf(f16, converted);
        apogee::models::Sidecar quantized;
        quantized.source = "quantize";
        quantized.transform = "quantize";
        quantized.ref = "org--m/gguf/111111111111";
        quantized.transform_note = "Q4_K_M";
        write_gguf(quant, quantized);

        BackendConfig backend;
        backend.type = BackendType::LlamaCpp;
        backend.model_path = quant.string();
        config.backends["m-q4"] = backend;
    }

    static void write_snapshot_dir(const std::filesystem::path& at, const std::string& ref) {
        std::filesystem::create_directories(at);
        std::ofstream{at / "config.json"} << R"({"architectures": ["LlamaForCausalLM"]})";
        std::ofstream{at / "model.safetensors"} << "weights";
        apogee::models::Snapshot record;
        record.ref = ref;
        record.revision = "main";
        record.source = "huggingface";
        record.files.push_back({.path = "model.safetensors", .size = 7, .sha256 = "abc"});
        REQUIRE(apogee::models::write_snapshot(at, record));
    }

    static void write_gguf(const std::filesystem::path& at,
                           const std::optional<apogee::models::Sidecar>& record) {
        std::filesystem::create_directories(at.parent_path());
        std::ofstream{at, std::ios::binary} << apogee::testing::minimal_gguf("llama");
        if (record.has_value()) {
            REQUIRE(apogee::models::write_sidecar(at, *record));
        }
    }

    [[nodiscard]] std::vector<ModelRow> rows() const {
        return build_model_rows(config, models);
    }

    [[nodiscard]] std::string info(std::string_view name) const {
        return render_model_info(config, name, {}, models);
    }
};

[[nodiscard]] const ModelRow* row_named(const std::vector<ModelRow>& rows, std::string_view model) {
    const auto match =
        std::ranges::find_if(rows, [model](const ModelRow& row) { return row.model == model; });
    return match == rows.end() ? nullptr : &*match;
}

[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        out.push_back(line);
    }
    return out;
}

}  // namespace

TEST_CASE("a snapshot a conversion consumed folds out of the listing, and the fold is said",
          "[commands][models][listing][lineage]") {
    // The user's rule (2026-10-03): a snapshot used to make a GGUF no longer
    // shows as an unconfigured model; one never converted still does.
    const LineageStore store;
    const std::vector<ModelRow> rows = store.rows();
    REQUIRE(row_named(rows, "org--m/safetensors/aaaaaaaaaaaa") != nullptr);
    CHECK(row_named(rows, "org--m/safetensors/aaaaaaaaaaaa")->consumed);
    REQUIRE(row_named(rows, "org--e2b/safetensors/cccccccccccc") != nullptr);
    CHECK_FALSE(row_named(rows, "org--e2b/safetensors/cccccccccccc")->consumed);

    const std::string folded = render_model_table(rows);
    CHECK(folded.find("org--m/safetensors/aaaaaaaaaaaa") == std::string::npos);
    CHECK(folded.find("org--e2b/safetensors/cccccccccccc") != std::string::npos);
    CHECK(folded.ends_with("\n1 snapshot consumed by a conversion is folded -- --all lists it\n"));

    // --all: everything, and no tail line -- every line the folded table has
    // (its tail aside) is in it.
    const std::string all = render_model_table(rows, {}, true);
    CHECK(all.find("org--m/safetensors/aaaaaaaaaaaa") != std::string::npos);
    CHECK(all.find("folded") == std::string::npos);
    const std::vector<std::string> folded_lines = lines_of(folded);
    const std::vector<std::string> all_lines = lines_of(all);
    for (std::size_t at = 0; at + 2 < folded_lines.size(); ++at) {
        INFO(folded_lines[at]);
        CHECK(std::ranges::find(all_lines, folded_lines[at]) != all_lines.end());
    }
    CHECK(all_lines.size() == folded_lines.size() - 2 + 1);

    // The fold is the table's: a machine reader gets every row.
    CHECK(render_model_jsonl(rows).find("org--m/safetensors/aaaaaaaaaaaa") != std::string::npos);
}

TEST_CASE("a GGUF made from a snapshot says converted, backend or not",
          "[commands][models][listing][lineage]") {
    const LineageStore store;
    // A pulled GGUF beside them was not converted, and says what it said.
    apogee::models::Sidecar pulled;
    pulled.source = "huggingface";
    pulled.ref = "org/m-GGUF:m.gguf";
    LineageStore::write_gguf(store.models / "org--m" / "gguf" / "444444444444" / "p.gguf", pulled);

    const std::vector<ModelRow> rows = store.rows();
    CHECK(row_for(rows, "m-q4").provenance == "converted");
    REQUIRE(row_named(rows, "org--m/gguf/111111111111") != nullptr);
    CHECK(row_named(rows, "org--m/gguf/111111111111")->provenance == "converted");
    REQUIRE(row_named(rows, "org--m/gguf/444444444444") != nullptr);
    CHECK(row_named(rows, "org--m/gguf/444444444444")->provenance == "huggingface");
    CHECK(row_named(rows, "org--e2b/safetensors/cccccccccccc")->provenance == "huggingface");
}

TEST_CASE("deleting a snapshot's GGUFs returns it to the listing; deleting it leaves theirs",
          "[commands][models][listing][lineage]") {
    {
        const LineageStore store;
        REQUIRE(apogee::models::remove_weights(store.f16.parent_path()).empty());
        REQUIRE(apogee::models::remove_weights(store.quant.parent_path()).empty());
        const std::vector<ModelRow> rows = store.rows();
        CHECK_FALSE(row_named(rows, "org--m/safetensors/aaaaaaaaaaaa")->consumed);
        CHECK(render_model_table(rows).find("folded") == std::string::npos);
    }
    {
        // Only the F16 deleted -- for its size, once quantized: the snapshot
        // stays folded, and the quantization says how that is known.
        const LineageStore store;
        REQUIRE(apogee::models::remove_weights(store.f16.parent_path()).empty());
        const std::vector<ModelRow> rows = store.rows();
        CHECK(row_named(rows, "org--m/safetensors/aaaaaaaaaaaa")->consumed);
        CHECK(row_for(rows, "m-q4").provenance == "converted");
        CHECK(store.info("m-q4").find(
                  "lineage:      quantized to Q4_K_M from org--m/gguf/111111111111 (recorded) -- "
                  "no longer on disk\n"
                  "              converted from org--m/safetensors/aaaaaaaaaaaa (inferred)\n"
                  "              pulled from org/m (Hugging Face)\n") != std::string::npos);
    }
    {
        const LineageStore store;
        REQUIRE(apogee::models::remove_weights(store.snapshot).empty());
        const std::vector<ModelRow> rows = store.rows();
        CHECK(row_named(rows, "org--m/safetensors/aaaaaaaaaaaa") == nullptr);
        // Still converted -- the record says so -- and info names the break.
        CHECK(row_named(rows, "org--m/gguf/111111111111")->provenance == "converted");
        CHECK(store.info("org--m/gguf/111111111111")
                  .find("lineage:      converted from org--m/safetensors/aaaaaaaaaaaa (recorded) "
                        "-- source snapshot no longer on disk\n") != std::string::npos);
        CHECK(store.info("m-q4").find("              converted from "
                                      "org--m/safetensors/aaaaaaaaaaaa (recorded) -- source "
                                      "snapshot no longer on disk\n") != std::string::npos);
    }
}

TEST_CASE("a consumed snapshot needing attention is never folded",
          "[commands][models][listing][lineage]") {
    const LineageStore store;
    std::ofstream{store.snapshot / "config.json"}
        << apogee::models::serialize(apogee::models::Sidecar{});
    const std::vector<ModelRow> rows = store.rows();
    const ModelRow* row = row_named(rows, "org--m/safetensors/aaaaaaaaaaaa");
    REQUIRE(row != nullptr);
    CHECK(row->attention);
    CHECK_FALSE(row->consumed);
}

TEST_CASE("a consumed snapshot a backend points at is never folded",
          "[commands][models][listing][lineage]") {
    // Not possible with llama.cpp alone; an MLX backend will run a snapshot
    // as it is (31a), and a backend's own model is never hidden.
    LineageStore store;
    BackendConfig direct;
    direct.type = BackendType::LlamaCpp;
    direct.model_path = store.snapshot.string();
    store.config.backends["m-direct"] = direct;
    const std::vector<ModelRow> rows = store.rows();
    const ModelRow* row = row_named(rows, "org--m/safetensors/aaaaaaaaaaaa");
    REQUIRE(row != nullptr);
    CHECK_FALSE(row->consumed);
}

TEST_CASE("the lineage adds no read to the sweep", "[commands][models][listing][lineage][busy]") {
    // M2 made the sweep fast; this item reads the records it already visits.
    const LineageStore store;
    std::vector<std::string> heard;
    const std::vector<ModelRow> rows =
        build_model_rows(store.config, store.models, {}, nullptr,
                         [&heard](std::string_view label, std::size_t, std::size_t total) {
                             heard.emplace_back(label);
                             CHECK(total == 4);
                         });
    // The backend's file, the one GGUF no backend points at, two snapshots:
    // one read each.
    CHECK(heard == std::vector<std::string>{
                       "reading model headers: m-Q4_K_M.gguf", "reading model headers: m-F16.gguf",
                       "reading snapshots: org--e2b", "reading snapshots: org--m"});
}

TEST_CASE("info names a model's chain back to the upstream it was pulled as",
          "[commands][models][info][lineage]") {
    const LineageStore store;
    const std::string quant = store.info("m-q4");
    CHECK(quant.find("model_path:   " + store.quant.string() +
                     "\n"
                     "lineage:      quantized to Q4_K_M from org--m/gguf/111111111111 (recorded)\n"
                     "              converted from org--m/safetensors/aaaaaaaaaaaa (recorded)\n"
                     "              pulled from org/m (Hugging Face)\n"
                     "header:       ok") != std::string::npos);

    // A fresh conversion is no backend yet: info takes its handle.
    const std::string f16 = store.info("org--m/gguf/111111111111");
    CHECK(
        f16.starts_with("weights:      org--m/gguf/111111111111\n"
                        "backends:     none -- "));
    CHECK(f16.find("lineage:      converted from org--m/safetensors/aaaaaaaaaaaa (recorded)\n"
                   "              pulled from org/m (Hugging Face)\n") != std::string::npos);
    CHECK(f16.find("header:       ok") != std::string::npos);
    // And a backend's file, by its handle, names the backend.
    CHECK(store.info("org--m/gguf/222222222222").find("backends:     m-q4\n") != std::string::npos);

    // Without the store, info is what it was.
    CHECK(render_model_info(store.config, "m-q4").find("lineage:") == std::string::npos);
}

TEST_CASE("info on a snapshot says what was made of it and whether the listing folds it",
          "[commands][models][info][lineage]") {
    const LineageStore store;
    const std::string consumed = store.info("org--m/safetensors/aaaaaaaaaaaa");
    CHECK(consumed.starts_with("weights:      org--m/safetensors/aaaaaaaaaaaa\n"));
    CHECK(consumed.find("lineage:      pulled from org/m (Hugging Face)\n") != std::string::npos);
    CHECK(consumed.find("made from it: org--m/gguf/111111111111 (recorded)\n"
                        "              org--m/gguf/222222222222 (recorded)\n") !=
          std::string::npos);
    CHECK(consumed.find("listing:      folded -- a conversion consumed it; 'apogee models list "
                        "--all' shows it\n") != std::string::npos);

    const std::string waiting = store.info("org--e2b/safetensors/cccccccccccc");
    CHECK(waiting.find("made from it: nothing yet -- 'apogee models convert "
                       "org--e2b/safetensors/cccccccccccc' makes a GGUF of it\n") !=
          std::string::npos);
    CHECK(waiting.find("listing:") == std::string::npos);
    // A bare id names the same weights.
    CHECK(store.info("cccccccccccc") == waiting);
}

TEST_CASE("info says an inference is one, and an unknown origin is unknown",
          "[commands][models][info][lineage]") {
    const apogee::testing::TempDir dir{"models-inferred-" + std::to_string(std::random_device{}())};
    LineageStore::write_snapshot_dir(dir.path() / "org--x" / "safetensors" / "dddddddddddd",
                                     "org/x");
    LineageStore::write_gguf(dir.path() / "org--x" / "gguf" / "555555555555" / "x.gguf",
                             std::nullopt);
    LineageStore::write_gguf(dir.path() / "org--y" / "gguf" / "666666666666" / "y.gguf",
                             std::nullopt);
    const Config config;
    CHECK(render_model_info(config, "org--x/gguf/555555555555", {}, dir.path())
              .find("lineage:      converted from org--x/safetensors/dddddddddddd (inferred)\n"
                    "              pulled from org/x (Hugging Face)\n") != std::string::npos);
    CHECK(render_model_info(config, "org--x/safetensors/dddddddddddd", {}, dir.path())
              .find("made from it: org--x/gguf/555555555555 (inferred)\n") != std::string::npos);
    CHECK(render_model_info(config, "org--y/gguf/666666666666", {}, dir.path())
              .find("lineage:      unknown -- no record says where it came from\n") !=
          std::string::npos);
    // The inferred conversion folds its snapshot all the same, and says converted.
    const std::vector<ModelRow> rows = build_model_rows(config, dir.path());
    CHECK(row_named(rows, "org--x/safetensors/dddddddddddd")->consumed);
    CHECK(row_named(rows, "org--x/gguf/555555555555")->provenance == "converted");
    CHECK(row_named(rows, "org--y/gguf/666666666666")->provenance == "local");
}

TEST_CASE("info on a whole model, or nothing stored, yields nothing for the caller to report",
          "[commands][models][info][lineage]") {
    const LineageStore store;
    CHECK(store.info("org--m").empty());
    CHECK(store.info("org--m/gguf/999999999999").empty());
    CHECK(store.info("no-such-backend").empty());
}
