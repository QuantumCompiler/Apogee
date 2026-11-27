#include "contracts/config_edit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_yaml.h"
#include "contracts/layout.h"
#include "support/env_guard.h"
#include "support/temp_mode_watch.h"

using apogee::harness::append_agent;
using apogee::harness::append_backend;
using apogee::harness::append_embedding;
using apogee::harness::append_graph;
using apogee::harness::append_mcp_server;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::ConfigEditError;
using apogee::harness::delete_backend;
using apogee::harness::delete_embedding;
using apogee::harness::delete_graph;
using apogee::harness::delete_mcp_server;
using apogee::harness::EmbeddingConfig;
using apogee::harness::format_config;
using apogee::harness::set_mcp_server_enabled;
using apogee::harness::set_models_role;
using apogee::harness::set_permission;
using apogee::testing::TempDir;
using Catch::Matchers::ContainsSubstring;

namespace {

/// The starter config as it shipped before 28i, in YAML: the line editor's
/// own format, still what an older install holds -- so the YAML transforms
/// keep their goldens on the file most YAML configs grew from.
std::string legacy_template() {
    std::ifstream in{
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "config" / "legacy_template.yaml",
        std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// A comment-dense fixture -- the case the whole module exists for. If an edit
/// disturbs a single byte of commentary here, a test fails.
constexpr std::string_view kCommented = R"YAML(# Apogee configuration.
#
# Every one of these comments must survive every edit.

models:
  default: claude   # the backend used when nothing else is named

backends:

  # ── Anthropic ───────────────────────────────────────────────
  # Uses the Messages API directly with your own key.
  claude:
    type: anthropic
    api_key: "${ANTHROPIC_API_KEY}"
    model: claude-sonnet-5

  # ── Local inference ─────────────────────────────────────────
  # Any GGUF you supply. Apogee ships none.
  local:
    type: llamacpp
    model_path: /models/local.gguf

# status_mode: line
)YAML";

BackendConfig anthropic_backend() {
    BackendConfig backend;
    backend.type = BackendType::Anthropic;
    backend.api_key = "${NEW_KEY}";
    backend.model = "claude-opus-5";
    backend.context_size = 200000;
    return backend;
}

/// Every helper's output must survive the loader -- the guarantee that makes
/// text surgery safe rather than merely clever.
void require_parses(std::string_view content) {
    REQUIRE_NOTHROW(apogee::harness::parse_config(content, "<edit result>"));
}

}  // namespace

TEST_CASE("add then delete is byte-identical on a comment-dense config", "[config_edit][golden]") {
    // The headline acceptance criterion. Not "the comments are still there
    // somewhere" -- the file is the same bytes it was.
    const std::string added = append_backend(kCommented, "opus", anthropic_backend(), false);
    require_parses(added);
    REQUIRE(added != std::string{kCommented});

    const std::string restored = delete_backend(added, "opus");
    CHECK(restored == std::string{kCommented});
}

TEST_CASE("a new entry lands inside its section, above trailing comments",
          "[config_edit][golden]") {
    const std::string added = append_backend(kCommented, "opus", anthropic_backend(), false);

    const std::size_t entry = added.find("  opus:");
    const std::size_t trailing_comment = added.find("# status_mode: line");
    const std::size_t local_entry = added.find("  local:");
    REQUIRE(entry != std::string::npos);
    REQUIRE(trailing_comment != std::string::npos);

    // After the last real entry...
    CHECK(local_entry < entry);
    // ...and before the file's trailing commentary, which belongs to nothing
    // in `backends:`. Appending at the raw end of the section would land the
    // entry below it, since a comment does not terminate a YAML block.
    CHECK(entry < trailing_comment);
}

TEST_CASE("deleting an entry does not swallow the next entry's comment header",
          "[config_edit][golden]") {
    // The subtle one, and a real improvement over the obvious implementation:
    // scanning forward to the next sibling key absorbs the blank line and the
    // comment block that document the FOLLOWING entry.
    const std::string after = delete_backend(kCommented, "claude");
    require_parses(after);

    CHECK(after.find("  claude:") == std::string::npos);
    CHECK(after.find("api_key") == std::string::npos);

    CHECK(after.find("# ── Local inference ─") != std::string::npos);
    CHECK(after.find("# Any GGUF you supply. Apogee ships none.") != std::string::npos);
    CHECK(after.find("  local:") != std::string::npos);

    // The header above the DELETED entry is deliberately left behind: it may
    // document the section, and an orphan is recoverable where a deletion is
    // not.
    CHECK(after.find("# ── Anthropic ─") != std::string::npos);
}

TEST_CASE("appending creates the backends section when there is none", "[config_edit]") {
    const std::string added =
        append_backend("models:\n  default: x\n", "mock", BackendConfig{}, false);
    require_parses(added);
    CHECK(added.find("backends:") != std::string::npos);
    CHECK(added.find("  mock:") != std::string::npos);
    CHECK(added.find("models:\n  default: x\n") == 0);
}

TEST_CASE("appending into a totally empty config works", "[config_edit]") {
    const std::string added = append_backend("", "mock", BackendConfig{}, false);
    require_parses(added);
    const auto config = apogee::harness::parse_config(added, "<test>");
    REQUIRE(config.backends.size() == 1);
    CHECK(config.find_backend("mock")->type == BackendType::Mock);
}

TEST_CASE("a case-fold collision is refused and names the conflict", "[config_edit]") {
    try {
        (void)append_backend(kCommented, "CLAUDE", anthropic_backend(), false);
        FAIL("expected a ConfigEditError");
    } catch (const ConfigEditError& e) {
        const std::string message = e.what();
        CHECK(message.find("CLAUDE") != std::string::npos);
        CHECK(message.find("claude") != std::string::npos);
    }
}

TEST_CASE("an exact duplicate needs --force, and force replaces in place", "[config_edit]") {
    CHECK_THROWS_AS((void)append_backend(kCommented, "claude", anthropic_backend(), false),
                    ConfigEditError);

    const std::string forced = append_backend(kCommented, "claude", anthropic_backend(), true);
    require_parses(forced);

    // Replaced where it stood, so its comment header still sits above it...
    const std::size_t header = forced.find("# ── Anthropic ─");
    const std::size_t entry = forced.find("  claude:");
    REQUIRE(header != std::string::npos);
    REQUIRE(entry != std::string::npos);
    CHECK(header < entry);
    CHECK(entry < forced.find("# ── Local inference ─"));

    // ...and the values are the new ones, with the old model gone.
    CHECK(forced.find("claude-opus-5") != std::string::npos);
    CHECK(forced.find("claude-sonnet-5") == std::string::npos);

    const auto config = apogee::harness::parse_config(forced, "<test>");
    CHECK(config.backends.size() == 2);
}

TEST_CASE("deletion matches case-insensitively but removes the file's own spelling",
          "[config_edit]") {
    const std::string content = "backends:\n  Claude:\n    type: mock\n";
    const std::string after = delete_backend(content, "cLaUdE");
    CHECK(after == "backends:\n");
}

TEST_CASE("deleting something absent fails without touching the text", "[config_edit]") {
    CHECK_THROWS_AS((void)delete_backend(kCommented, "nope"), ConfigEditError);
    CHECK_THROWS_AS((void)delete_backend("models:\n  default: x\n", "nope"), ConfigEditError);
}

TEST_CASE("CRLF files stay CRLF", "[config_edit][golden]") {
    // Windows is one of the five targets. Normalising line endings would
    // rewrite every line of a Windows user's config on their first edit, and
    // the diff would be the whole file.
    const std::string crlf = "backends:\r\n  a:\r\n    type: mock\r\n";
    const std::string added = append_backend(crlf, "b", BackendConfig{}, false);

    CHECK(added.find("\r\n  b:\r\n") != std::string::npos);
    CHECK(added.find("\n  b:\n") == std::string::npos);
    CHECK(delete_backend(added, "b") == crlf);
}

TEST_CASE("appending to a file with no final newline terminates the old last line",
          "[config_edit][golden]") {
    // The one documented exception to "only the entry's own lines change". A
    // line cannot stay unterminated once something follows it, so appending
    // gives it a terminator; deleting cannot know to take it away again.
    // Everything else still round-trips exactly.
    const std::string content = "backends:\n  a:\n    type: mock";
    const std::string added = append_backend(content, "b", BackendConfig{}, false);
    require_parses(added);

    CHECK(delete_backend(added, "b") == content + "\n");
    CHECK(added.find("backends:\n  a:\n    type: mock\n") == 0);
}

TEST_CASE("a file that already ends in a newline round-trips exactly", "[config_edit][golden]") {
    const std::string content = "backends:\n  a:\n    type: mock\n";
    const std::string added = append_backend(content, "b", BackendConfig{}, false);
    require_parses(added);
    CHECK(delete_backend(added, "b") == content);
}

TEST_CASE("an entry at the very end of the file deletes cleanly", "[config_edit][golden]") {
    // Edge-of-section: nothing follows the entry, so the scan runs off the end.
    const std::string content = "models:\n  default: a\n\nbackends:\n  a:\n    type: mock\n";
    const std::string after = delete_backend(content, "a");
    require_parses(after);
    CHECK(after == "models:\n  default: a\n\nbackends:\n");
}

TEST_CASE("an entry immediately followed by a top-level key deletes cleanly",
          "[config_edit][golden]") {
    const std::string content =
        "backends:\n  a:\n    type: mock\n  b:\n    type: mock\nstatus_mode: quiet\n";
    const std::string after = delete_backend(content, "a");
    require_parses(after);
    CHECK(after == "backends:\n  b:\n    type: mock\nstatus_mode: quiet\n");

    const std::string after_b = delete_backend(content, "b");
    require_parses(after_b);
    CHECK(after_b == "backends:\n  a:\n    type: mock\nstatus_mode: quiet\n");
}

TEST_CASE("values that would confuse YAML are quoted on write", "[config_edit]") {
    BackendConfig backend;
    backend.type = BackendType::LlamaCpp;
    backend.model_path = R"(C:\models\my model.gguf)";
    backend.system_prompt = "Answer: briefly, #always";

    const std::string added = append_backend("", "win", backend, false);
    require_parses(added);

    const auto config = apogee::harness::parse_config(added, "<test>");
    const auto* written = config.find_backend("win");
    REQUIRE(written != nullptr);
    CHECK(written->model_path == backend.model_path);
    CHECK(written->system_prompt == backend.system_prompt);
}

TEST_CASE("a projector is written right after its model", "[config_edit]") {
    // What `config add-backend --mmproj-path` writes, and what `models
    // convert` hands the user for a model that reads images.
    BackendConfig backend;
    backend.type = BackendType::LlamaCpp;
    backend.model_path = "/m/Qwen-F16.gguf";
    backend.mmproj_path = "/m/Qwen-F16-mmproj.gguf";
    const std::string added = append_backend("backends:\n", "vision", backend, false);
    CHECK(added ==
          "backends:\n  vision:\n    type: llamacpp\n    model_path: /m/Qwen-F16.gguf\n"
          "    mmproj_path: /m/Qwen-F16-mmproj.gguf\n");
    const auto config = apogee::harness::parse_config(added, "<test>");
    CHECK(config.find_backend("vision")->mmproj_path == backend.mmproj_path);
}

TEST_CASE("a cache type is written after the window it keeps", "[config_edit][cache]") {
    BackendConfig backend;
    backend.type = BackendType::LlamaCpp;
    backend.model_path = "/m/Qwen.gguf";
    backend.context_size = 65536;
    backend.cache_type = apogee::harness::KvCacheType::Q4_0;
    const std::string added = append_backend("backends:\n", "local", backend, false);
    CHECK(added ==
          "backends:\n  local:\n    type: llamacpp\n    model_path: /m/Qwen.gguf\n"
          "    context_size: 65536\n    cache_type: q4_0\n");
    CHECK(apogee::harness::parse_config(added, "<test>").find_backend("local")->cache_type ==
          apogee::harness::KvCacheType::Q4_0);
}

TEST_CASE("a backend's sampling is written after its temperature, and reads back",
          "[config_edit][sampling]") {
    // 26h: every knob the config parses, the editor writes -- or an entry
    // added with them would lose them on the way to the file.
    BackendConfig backend;
    backend.type = BackendType::LlamaCpp;
    backend.model_path = "/m/Qwen.gguf";
    backend.temperature = 0.7;
    backend.top_p = 0.8;
    backend.top_k = 20;
    backend.min_p = 0.05;
    backend.repeat_penalty = 1.1;
    backend.presence_penalty = 1.5;
    backend.seed = 42;
    const std::string added = append_backend("backends:\n", "local", backend, false);
    CHECK(added ==
          "backends:\n  local:\n    type: llamacpp\n    model_path: /m/Qwen.gguf\n"
          "    temperature: 0.7\n    top_p: 0.8\n    top_k: 20\n    min_p: 0.05\n"
          "    repeat_penalty: 1.1\n    presence_penalty: 1.5\n    seed: 42\n");
    const apogee::harness::Config config = apogee::harness::parse_config(added, "<test>");
    const auto* read = config.find_backend("local");
    REQUIRE(read != nullptr);
    CHECK(read->top_p == 0.8);
    CHECK(read->top_k == 20);
    CHECK(read->min_p == 0.05);
    CHECK(read->repeat_penalty == 1.1);
    CHECK(read->presence_penalty == 1.5);
    CHECK(read->seed == 42);
}

TEST_CASE("a backend's thinking is written after its sampling, and reads back",
          "[config_edit][thinking]") {
    BackendConfig backend;
    backend.type = BackendType::LlamaCpp;
    backend.model_path = "/m/Qwen.gguf";
    backend.seed = 42;
    backend.thinking = apogee::harness::ThinkingMode::Auto;
    backend.thinking_budget = 2048;
    const std::string added = append_backend("backends:\n", "local", backend, false);
    CHECK(added ==
          "backends:\n  local:\n    type: llamacpp\n    model_path: /m/Qwen.gguf\n"
          "    seed: 42\n    thinking: auto\n    thinking_budget: 2048\n");
    const apogee::harness::Config config = apogee::harness::parse_config(added, "<test>");
    const auto* read = config.find_backend("local");
    REQUIRE(read != nullptr);
    CHECK(read->thinking == apogee::harness::ThinkingMode::Auto);
    CHECK(read->thinking_budget == 2048);
}

TEST_CASE("an api_key is stored literally, not expanded, on write", "[config_edit]") {
    const apogee::testing::EnvGuard key{"NEW_KEY", "sk-should-not-appear"};
    const std::string added = append_backend("", "a", anthropic_backend(), false);

    // The whole point of ${ENV} support: the secret never enters the file.
    CHECK(added.find("${NEW_KEY}") != std::string::npos);
    CHECK(added.find("sk-should-not-appear") == std::string::npos);
}

TEST_CASE("set_models_role replaces a value and keeps its trailing comment",
          "[config_edit][golden]") {
    const std::string after = set_models_role(kCommented, "default", "local");
    require_parses(after);

    CHECK(after.find("  default: local   # the backend used when nothing else is named") !=
          std::string::npos);
    CHECK(after.find("default: claude") == std::string::npos);
    // Nothing else moved.
    CHECK(after.find("# ── Anthropic ─") != std::string::npos);
}

TEST_CASE("set_models_role inserts a missing role without disturbing siblings",
          "[config_edit][golden]") {
    const std::string after = set_models_role(kCommented, "default_embedding", "local");
    require_parses(after);

    const auto config = apogee::harness::parse_config(after, "<test>");
    CHECK(config.models.default_embedding == "local");
    CHECK(config.models.default_backend == "claude");
    CHECK(after.find("# the backend used when nothing else is named") != std::string::npos);
}

TEST_CASE("set_models_role creates the models section when absent", "[config_edit]") {
    const std::string after = set_models_role("backends:\n  a:\n    type: mock\n", "default", "a");
    require_parses(after);
    CHECK(apogee::harness::parse_config(after, "<test>").models.default_backend == "a");
}

TEST_CASE("set_models_role rejects a field that is not a role", "[config_edit]") {
    CHECK_THROWS_AS((void)set_models_role(kCommented, "colour", "x"), ConfigEditError);
}

TEST_CASE("all three role fields are settable and independent", "[config_edit]") {
    std::string content{kCommented};
    for (const std::string_view field : apogee::harness::models_role_fields()) {
        content = set_models_role(content, field, "local");
        require_parses(content);
    }
    const auto config = apogee::harness::parse_config(content, "<test>");
    CHECK(config.models.default_backend == "local");
    CHECK(config.models.default_embedding == "local");
    CHECK(config.models.default_extraction == "local");
}

TEST_CASE("format tidies whitespace and leaves content alone", "[config_edit]") {
    const std::string messy =
        "models:   \n  default: a\t\n\n\n\nbackends:\n  a:\n    type: mock\n\n\n";
    const std::string tidy = format_config(messy);
    require_parses(tidy);

    CHECK(tidy == "models:\n  default: a\n\nbackends:\n  a:\n    type: mock\n");
    // Idempotent: formatting twice changes nothing.
    CHECK(format_config(tidy) == tidy);
}

TEST_CASE("format preserves every comment and the key order", "[config_edit][golden]") {
    const std::string tidy = format_config(kCommented);
    require_parses(tidy);
    CHECK(tidy.find("# ── Anthropic ─") != std::string::npos);
    CHECK(tidy.find("# ── Local inference ─") != std::string::npos);
    CHECK(tidy.find("claude") < tidy.find("local"));
    // This fixture is already tidy, so formatting is a no-op on it.
    CHECK(tidy == std::string{kCommented});
}

TEST_CASE("section_entry_names sees only active entries", "[config_edit]") {
    const auto names = apogee::harness::section_entry_names(kCommented, "backends");
    REQUIRE(names.size() == 2);
    CHECK(names[0] == "claude");
    CHECK(names[1] == "local");
    // A commented-out example is documentation, not configuration.
    CHECK(apogee::harness::section_entry_names("backends:\n  # x:\n", "backends").empty());
    CHECK(apogee::harness::section_entry_names(kCommented, "nonexistent").empty());
}

TEST_CASE("helpers are section-scoped and cannot cross-contaminate", "[config_edit]") {
    // A Core constraint: same-named entries in different sections must never
    // reach each other.
    const std::string content =
        "backends:\n  shared:\n    type: mock\n\nembeddings:\n  shared:\n    db: x.db\n";
    const std::string after = delete_backend(content, "shared");
    require_parses(after);

    CHECK(after.find("embeddings:\n  shared:\n    db: x.db\n") != std::string::npos);
    CHECK(apogee::harness::section_entry_names(after, "backends").empty());
}

TEST_CASE("an invalid backend name is refused before anything is written", "[config_edit]") {
    CHECK_THROWS_AS((void)append_backend("", "", BackendConfig{}, false), ConfigEditError);
    CHECK_THROWS_AS((void)append_backend("", "has space", BackendConfig{}, false), ConfigEditError);
    CHECK_THROWS_AS((void)append_backend("", "has:colon", BackendConfig{}, false), ConfigEditError);
}

TEST_CASE("a failed edit leaves the file byte-identical", "[config_edit][atomic]") {
    const TempDir dir{"edit-fail"};
    const std::filesystem::path path = std::filesystem::path{dir.path()} / "config.yaml";
    apogee::harness::write_file_atomically(path, kCommented);

    CHECK_THROWS_AS(apogee::harness::edit_config_file(
                        path, [](std::string_view c) { return delete_backend(c, "nope"); }),
                    ConfigEditError);

    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    CHECK(buffer.str() == std::string{kCommented});
}

TEST_CASE("an edit producing invalid YAML is rejected before it lands", "[config_edit][atomic]") {
    // The re-parse safety net. Text surgery is only acceptable because a
    // transform cannot corrupt the file even if it is itself wrong.
    const TempDir dir{"edit-invalid"};
    const std::filesystem::path path = std::filesystem::path{dir.path()} / "config.yaml";
    apogee::harness::write_file_atomically(path, kCommented);

    CHECK_THROWS(apogee::harness::edit_config_file(
        path, [](std::string_view) { return std::string{"backends:\n  broken: [\n"}; }));

    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    CHECK(buffer.str() == std::string{kCommented});
}

TEST_CASE("an atomic write leaves no temporary files behind", "[config_edit][atomic]") {
    const TempDir dir{"atomic"};
    const std::filesystem::path path = std::filesystem::path{dir.path()} / "config.yaml";

    apogee::harness::write_file_atomically(path, "backends:\n");
    apogee::harness::write_file_atomically(path, "backends:\n  a:\n    type: mock\n");

    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator{dir.path()}) {
        ++files;
        CHECK(entry.path().filename() == "config.yaml");
    }
    CHECK(files == 1);
}

TEST_CASE("write_file_atomically creates missing parent directories", "[config_edit][atomic]") {
    const TempDir dir{"atomic-mkdir"};
    const std::filesystem::path path =
        std::filesystem::path{dir.path()} / "a" / "b" / "config.yaml";
    apogee::harness::write_file_atomically(path, "backends:\n");
    CHECK(std::filesystem::exists(path));
}

// --- embeddings: the collection entries -----------------------------------------
//
// `apogee embed ingest` writes these without anyone typing `config`, which is
// exactly why they are held to the same byte-diff bar as everything else here:
// a registration that disturbed a comment would be a config edit the user
// never asked for AND never saw.

namespace {

/// Comment-dense, with a collection section that already has an entry, a
/// comment block above it, and file-level commentary after it.
constexpr std::string_view kWithCollections = R"YAML(# Apogee configuration.
#
# Every one of these comments must survive every edit.

models:
  default: claude   # the backend used when nothing else is named

backends:
  claude:
    type: anthropic

# auto_rag names one collection to search on every turn.
# auto_rag: adrs

embeddings:

  # ── Architecture decision records ─────────────────────────────
  # Chunked larger than prose: a decision reads as a unit.
  adrs:
    chunk_size: 768
    chunk_overlap: 96

# status_mode: line
)YAML";

EmbeddingConfig notes_collection() {
    EmbeddingConfig collection;
    collection.chunk_size = 512;
    collection.chunk_overlap = 64;
    return collection;
}

}  // namespace

TEST_CASE("registering a collection is a byte-exact insertion and nothing else moves",
          "[config_edit][golden][embeddings]") {
    // THE acceptance criterion, as a literal diff rather than a "still
    // contains": the result must be the fixture with exactly these lines
    // inserted at exactly this place -- after the last entry, above the
    // trailing commentary, one blank separator, the section's own indent.
    const std::string added =
        append_embedding(kWithCollections, "notes", notes_collection(), false);
    require_parses(added);

    constexpr std::string_view kInserted =
        "\n"
        "  notes:\n"
        "    chunk_size: 512\n"
        "    chunk_overlap: 64\n";
    const std::string expected =
        std::string{kWithCollections.substr(0, kWithCollections.find("\n# status_mode: line"))} +
        std::string{kInserted} + "\n# status_mode: line\n";
    CHECK(added == expected);
}

TEST_CASE("register then delete is byte-identical on a comment-dense config",
          "[config_edit][golden][embeddings]") {
    const std::string added =
        append_embedding(kWithCollections, "notes", notes_collection(), false);
    REQUIRE(added != std::string{kWithCollections});
    CHECK(delete_embedding(added, "notes") == std::string{kWithCollections});
}

TEST_CASE("registering creates the embeddings section when the config has none",
          "[config_edit][golden][embeddings]") {
    // The shipped template documents `embeddings:` as a comment, which is not
    // a section. The first ingest on a fresh install lands here.
    const std::string added = append_embedding(kCommented, "notes", notes_collection(), false);
    require_parses(added);
    CHECK(added == std::string{kCommented} +
                       "\nembeddings:\n  notes:\n    chunk_size: 512\n    chunk_overlap: 64\n");
    // ...and the round trip still holds, leaving an empty section behind: an
    // orphaned header is recoverable where a deleted comment is not.
    require_parses(delete_embedding(added, "notes"));
}

TEST_CASE("a collection with defaulted settings is a bare entry", "[config_edit][embeddings]") {
    const std::string added = append_embedding(kCommented, "notes", EmbeddingConfig{}, false);
    require_parses(added);
    CHECK(added.find("  notes:\n") != std::string::npos);
    CHECK(added.find("chunk_size") == std::string::npos);
}

TEST_CASE("collection names collide case-insensitively and need --force to replace",
          "[config_edit][embeddings]") {
    CHECK_THROWS_AS(append_embedding(kWithCollections, "ADRS", notes_collection(), false),
                    ConfigEditError);
    CHECK_THROWS_AS(append_embedding(kWithCollections, "adrs", notes_collection(), false),
                    ConfigEditError);

    const std::string replaced =
        append_embedding(kWithCollections, "adrs", notes_collection(), true);
    require_parses(replaced);
    CHECK(replaced.find("chunk_size: 512") != std::string::npos);
    CHECK(replaced.find("chunk_size: 768") == std::string::npos);
    // Replaced IN PLACE: the comment block above it is still above it.
    CHECK(replaced.find("# Chunked larger than prose") < replaced.find("  adrs:"));
}

TEST_CASE("the collection helpers never touch the backends section", "[config_edit][embeddings]") {
    // Section-scoped by construction: a collection named like a backend is a
    // different entry in a different section.
    const std::string added =
        append_embedding(kWithCollections, "claude", notes_collection(), false);
    require_parses(added);
    CHECK(apogee::harness::section_entry_names(added, "backends") ==
          std::vector<std::string>{"claude"});
    CHECK(apogee::harness::section_entry_names(added, "embeddings") ==
          std::vector<std::string>{"adrs", "claude"});
    CHECK_THROWS_AS(delete_embedding(kWithCollections, "claude"), ConfigEditError);
}

TEST_CASE("embedding_model is written when set and omitted when not", "[config_edit][embeddings]") {
    BackendConfig with = anthropic_backend();
    with.type = BackendType::OpenAI;
    with.embedding_model = "text-embedding-3-large";
    const std::string added = append_backend(kCommented, "gpt", with, false);
    require_parses(added);
    CHECK(added.find("    embedding_model: text-embedding-3-large\n") != std::string::npos);
    CHECK(apogee::harness::parse_config(added, "<t>").find_backend("gpt")->embedding_model ==
          "text-embedding-3-large");

    const std::string without = append_backend(kCommented, "gpt", anthropic_backend(), false);
    CHECK(without.find("embedding_model") == std::string::npos);
}

TEST_CASE("a collection's pins are written when set and round-trip", "[config_edit][embeddings]") {
    EmbeddingConfig collection = notes_collection();
    collection.backend = "embedder";
    collection.retriever = "vector";
    collection.rerank = "haiku";
    const std::string added = append_embedding(kCommented, "notes", collection, false);
    require_parses(added);
    CHECK(added.find("    backend: embedder\n") != std::string::npos);
    CHECK(added.find("    retriever: vector\n") != std::string::npos);
    CHECK(added.find("    rerank: haiku\n") != std::string::npos);
    const auto parsed = apogee::harness::parse_config(added, "<t>");
    CHECK(parsed.find_embedding("notes")->retriever == "vector");
    CHECK(delete_embedding(added, "notes") == std::string{kCommented} + "\nembeddings:\n");
}

TEST_CASE(
    "set_embedding_graph_enabled appends a block, replaces in place keeping the comment, "
    "and stays inside the embeddings section",
    "[config_edit][golden][graph]") {
    constexpr std::string_view kBase =
        "# top\n"
        "backends:\n"
        "  notes:\n"
        "    type: mock\n"
        "\nembeddings:\n"
        "  notes:\n"
        "    chunk_size: 512    # per chunk\n"
        "  other:\n"
        "    retriever: lexical\n";
    // No block yet: one is appended after the entry's last field, and the
    // same-named backend is never touched.
    const std::string appended = apogee::harness::set_embedding_graph_enabled(kBase, "notes", true);
    require_parses(appended);
    CHECK(appended ==
          "# top\n"
          "backends:\n"
          "  notes:\n"
          "    type: mock\n"
          "\nembeddings:\n"
          "  notes:\n"
          "    chunk_size: 512    # per chunk\n"
          "    graph:\n"
          "      enabled: true\n"
          "  other:\n"
          "    retriever: lexical\n");
    CHECK(apogee::harness::parse_config(appended, "<test>").find_embedding("notes")->graph.enabled);
    CHECK_FALSE(
        apogee::harness::parse_config(appended, "<test>").find_embedding("other")->graph.enabled);

    // In place: only the value token changes, the trailing comment stays,
    // and a sibling field after the block is left where it was.
    constexpr std::string_view kWithBlock =
        "embeddings:\n"
        "  notes:\n"
        "    graph:\n"
        "      extract_backend: local\n"
        "      enabled: false   # flipped by the first build\n"
        "      hops: 2\n"
        "    rerank: off\n";
    const std::string replaced =
        apogee::harness::set_embedding_graph_enabled(kWithBlock, "notes", true);
    require_parses(replaced);
    CHECK(replaced ==
          "embeddings:\n"
          "  notes:\n"
          "    graph:\n"
          "      extract_backend: local\n"
          "      enabled: true   # flipped by the first build\n"
          "      hops: 2\n"
          "    rerank: off\n");
    CHECK(apogee::harness::set_embedding_graph_enabled(replaced, "NOTES", false) ==
          std::string{kWithBlock});

    // A block without the field gains it first.
    constexpr std::string_view kBlockNoField = "embeddings:\n  notes:\n    graph:\n      hops: 2\n";
    CHECK(apogee::harness::set_embedding_graph_enabled(kBlockNoField, "notes", true) ==
          "embeddings:\n  notes:\n    graph:\n      enabled: true\n      hops: 2\n");

    // Missing section or entry: refused, nothing written.
    CHECK_THROWS_AS(apogee::harness::set_embedding_graph_enabled(
                        "backends:\n  a:\n    type: mock\n", "a", true),
                    apogee::harness::ConfigEditError);
    CHECK_THROWS_AS(apogee::harness::set_embedding_graph_enabled(kBase, "ghost", true),
                    apogee::harness::ConfigEditError);
}

TEST_CASE("set_permission replaces a level in place, keeping the trailing comment",
          "[config_edit][golden][permissions]") {
    constexpr std::string_view kWithPermissions =
        "# top\n"
        "permissions:\n"
        "  write_file: ask    # prompts each time\n"
        "  run_command: ask\n"
        "\nbackends:\n  a:\n    type: mock\n";
    const std::string after = set_permission(kWithPermissions, "write_file", "allow");
    require_parses(after);
    CHECK(after.find("  write_file: allow    # prompts each time") != std::string::npos);
    CHECK(after.find("  run_command: ask\n") != std::string::npos);
    CHECK(after.find("# top") != std::string::npos);
    // Exactly one line differs.
    CHECK(after.size() == kWithPermissions.size() + 2);
    CHECK(apogee::harness::parse_config(after, "<test>").permissions.level("write_file") ==
          apogee::harness::PermissionLevel::Allow);

    // Inserting a tool the section does not list yet.
    const std::string inserted = set_permission(kWithPermissions, "delete_note", "deny");
    require_parses(inserted);
    CHECK(apogee::harness::parse_config(inserted, "<test>").permissions.level("delete_note") ==
          apogee::harness::PermissionLevel::Deny);
    CHECK(inserted.find("  write_file: ask    # prompts each time") != std::string::npos);
}

TEST_CASE("set_permission creates the section when absent and refuses bad input",
          "[config_edit][permissions]") {
    const std::string created = set_permission(kCommented, "run_command", "deny");
    require_parses(created);
    CHECK(apogee::harness::parse_config(created, "<test>").permissions.level("run_command") ==
          apogee::harness::PermissionLevel::Deny);
    CHECK(created.find("# ── Anthropic ─") != std::string::npos);

    CHECK_THROWS_AS((void)set_permission(kCommented, "write_file", "yes"), ConfigEditError);
    CHECK_THROWS_AS((void)set_permission(kCommented, "", "allow"), ConfigEditError);
    CHECK_THROWS_AS((void)set_permission(kCommented, "write file", "allow"), ConfigEditError);
    CHECK_THROWS_AS((void)set_permission(kCommented, "../x", "allow"), ConfigEditError);
    // A namespaced MCP tool name is a valid key.
    require_parses(set_permission(kCommented, "mcp__srv__tool", "allow"));
}

TEST_CASE("an MCP server entry appends alphabetically, toggles one line, and deletes cleanly",
          "[config_edit][golden][mcp]") {
    apogee::harness::McpServerConfig server;
    server.command = "/home/u/.apogee/mcp/w/server.py";
    server.args = {"--flag", "value with space", "a,b"};
    server.env = {"KEY=v"};
    const std::string appended = append_mcp_server(kCommented, "w", server, false);
    require_parses(appended);
    // Alphabetical after the name; a comma-bearing item quoted, the rest plain.
    CHECK(appended.find("mcp_servers:\n  w:\n    args: [--flag, value with space, \"a,b\"]\n"
                        "    command: /home/u/.apogee/mcp/w/server.py\n    enabled: true\n"
                        "    env: [KEY=v]\n") != std::string::npos);
    CHECK(appended.starts_with(kCommented));
    const auto loaded = apogee::harness::parse_config(appended, "<test>");
    REQUIRE(loaded.find_mcp_server("w") != nullptr);
    CHECK(loaded.find_mcp_server("w")->args ==
          std::vector<std::string>{"--flag", "value with space", "a,b"});
    CHECK(loaded.find_mcp_server("w")->env == std::vector<std::string>{"KEY=v"});

    // A collision, case-folded, is refused without force.
    CHECK_THROWS_AS((void)append_mcp_server(appended, "W", server, false), ConfigEditError);
    CHECK_THROWS_AS((void)append_mcp_server(appended, "a b", server, false), ConfigEditError);

    // Toggling replaces the one line and keeps a trailing comment.
    std::string commented = appended;
    const std::size_t at = commented.find("    enabled: true\n");
    REQUIRE(at != std::string::npos);
    commented.replace(at, std::string{"    enabled: true\n"}.size(), "    enabled: true   # on\n");
    const std::string off = set_mcp_server_enabled(commented, "w", false);
    require_parses(off);
    CHECK(off.find("    enabled: false   # on\n") != std::string::npos);
    CHECK(off.size() == commented.size() + 1);
    CHECK_FALSE(apogee::harness::parse_config(off, "<test>").find_mcp_server("w")->enabled);
    CHECK(set_mcp_server_enabled(off, "w", true) == commented);
    CHECK_THROWS_AS((void)set_mcp_server_enabled(off, "nope", true), ConfigEditError);
    CHECK_THROWS_AS((void)set_mcp_server_enabled(kCommented, "w", true), ConfigEditError);

    // An entry with no enabled line gets one right after its name.
    const std::string bare = "mcp_servers:\n  x:\n    command: srv\n";
    const std::string with_line = set_mcp_server_enabled(bare, "x", false);
    CHECK(with_line == "mcp_servers:\n  x:\n    enabled: false\n    command: srv\n");

    // Delete is the inverse of append: the entry goes, the file it was
    // appended to is intact (the section header it created stays, empty --
    // the same as a deleted backend leaves).
    const std::string deleted = delete_mcp_server(appended, "w");
    require_parses(deleted);
    CHECK(deleted.starts_with(kCommented));
    CHECK(apogee::harness::parse_config(deleted, "<test>").mcp_servers.empty());
    CHECK(deleted.find("  w:") == std::string::npos);
    CHECK_THROWS_AS((void)delete_mcp_server(kCommented, "w"), ConfigEditError);
}

TEST_CASE("an agent entry appends alphabetically with only what is set, and deletes cleanly",
          "[config_edit][golden][agents]") {
    using apogee::harness::AgentConfig;
    using apogee::harness::append_agent;
    using apogee::harness::delete_agent;
    AgentConfig agent;
    agent.description = "Reviews: code";  // a colon: quoted on write
    agent.prompts = {"prompts/r.txt"};
    agent.schemas = {"schemas/r-output.json"};
    agent.save_subdir = "r";
    const std::string appended = append_agent(kCommented, "r", agent, false);
    require_parses(appended);
    // Alphabetical after the name; `tools` always; nothing that is unset.
    CHECK(appended.find("agents:\n  r:\n    description: \"Reviews: code\"\n"
                        "    prompts: [prompts/r.txt]\n    save_subdir: r\n"
                        "    schemas: [schemas/r-output.json]\n    tools: read-only\n") !=
          std::string::npos);
    CHECK(appended.find("questions") == std::string::npos);
    CHECK(appended.find("output_format") == std::string::npos);
    CHECK(appended.starts_with(kCommented));
    const auto loaded = apogee::harness::parse_config(appended, "<test>");
    REQUIRE(loaded.find_agent("r") != nullptr);
    CHECK(loaded.find_agent("r")->save_subdir == "r");

    // Every optional written when it says something; booleans only when true.
    AgentConfig full;
    full.collection = "adrs";
    full.mcp = {"weather"};
    full.model = "local";
    full.output_format = apogee::harness::AgentOutputFormat::Json;
    full.questions = true;
    full.save_dir = "~/reports";
    full.save_filename = "rev";
    full.tools = apogee::harness::AgentToolPolicy::All;
    const std::string with_all = append_agent(appended, "f", full, false);
    require_parses(with_all);
    CHECK(with_all.find("  f:\n    collection: adrs\n    mcp: [weather]\n    model: local\n"
                        "    output_format: json\n    questions: true\n    save_dir: ~/reports\n"
                        "    save_filename: rev\n    tools: all\n") != std::string::npos);
    const auto both = apogee::harness::parse_config(with_all, "<test>");
    CHECK(both.find_agent("f")->questions);
    CHECK(both.find_agent("f")->output_format == apogee::harness::AgentOutputFormat::Json);

    // Collisions and force, as every other section.
    CHECK_THROWS_AS((void)append_agent(appended, "R", agent, false), ConfigEditError);
    CHECK_THROWS_AS((void)append_agent(appended, "a b", agent, false), ConfigEditError);
    CHECK_NOTHROW((void)append_agent(appended, "r", full, true));

    // Delete is the inverse of append (the empty section header stays, as
    // for every section).
    const std::string deleted = delete_agent(appended, "r");
    CHECK(deleted == std::string{kCommented} + "\nagents:\n");
    CHECK_THROWS_AS((void)delete_agent(kCommented, "r"), ConfigEditError);
}

TEST_CASE("a graphs: entry round-trips byte-exactly, with only what it says written",
          "[config_edit][golden][graphs]") {
    apogee::harness::NamedGraphConfig graph;
    graph.collections = {"docs", "meetings"};
    const std::string added = append_graph(kCommented, "work", graph, false);
    require_parses(added);
    CHECK(added.starts_with(std::string{kCommented}));
    CHECK(added.ends_with("\ngraphs:\n  work:\n    collections: [docs, meetings]\n"));
    // The inverse of the append -- the section header it created stays, as
    // every section-creating helper leaves it (an empty section is valid
    // and cheap to keep; deleting it would guess at ownership).
    CHECK(delete_graph(added, "work") == std::string{kCommented} + "\ngraphs:\n");
    const auto loaded = apogee::harness::parse_config(added, "<test>");
    REQUIRE(loaded.find_graph("work") != nullptr);
    CHECK(loaded.find_graph("work")->collections == graph.collections);

    // Every knob, when it says something; the defaults stay implicit.
    graph.extract_backend = "local";
    graph.hops = 2;
    graph.max_entities = 4;
    const std::string full = append_graph("graphs:\n", "work", graph, false);
    CHECK(full ==
          "graphs:\n  work:\n    collections: [docs, meetings]\n    extract_backend: local\n"
          "    hops: 2\n    max_entities: 4\n");
    const auto knobs = apogee::harness::parse_config(full, "<test>");
    CHECK(knobs.find_graph("work")->hops == 2);
    CHECK(knobs.find_graph("work")->max_entities == 4);

    // Collision rules as every other section's: the fold, the duplicate,
    // the force replacing in place.
    CHECK_THROWS_AS((void)append_graph(added, "Work", graph, false), ConfigEditError);
    CHECK_THROWS_AS((void)append_graph(added, "work", graph, false), ConfigEditError);
    const std::string replaced = append_graph(added, "work", graph, true);
    CHECK(replaced.find("extract_backend: local") != std::string::npos);
    CHECK(apogee::harness::section_entry_names(replaced, "graphs").size() == 1);
    CHECK_THROWS_AS((void)delete_graph(kCommented, "work"), ConfigEditError);
}

TEST_CASE("a code graph's entry writes its trees and languages, and no empty collections",
          "[config_edit][golden][graphs][code]") {
    apogee::harness::NamedGraphConfig graph;
    graph.sources = {"/srv/repo/src", "/srv/other"};
    const std::string code = append_graph("graphs:\n", "code", graph, false);
    CHECK(code == "graphs:\n  code:\n    sources: [/srv/repo/src, /srv/other]\n");
    require_parses(code);
    graph.languages = {"cpp", "python"};
    graph.collections = {"docs"};
    const std::string mixed = append_graph("graphs:\n", "mixed", graph, false);
    CHECK(mixed ==
          "graphs:\n  mixed:\n    collections: [docs]\n    sources: [/srv/repo/src, /srv/other]\n"
          "    languages: [cpp, python]\n");
    const auto loaded = apogee::harness::parse_config(mixed, "<test>");
    CHECK(loaded.find_graph("mixed")->sources == graph.sources);
    CHECK(loaded.find_graph("mixed")->languages == graph.languages);
    // A graph with neither collections nor trees keeps writing `[]`, as it
    // always did.
    CHECK(append_graph("graphs:\n", "bare", apogee::harness::NamedGraphConfig{}, false) ==
          "graphs:\n  bare:\n    collections: []\n");
}

TEST_CASE("graph helpers are section-scoped: a same-named agent and MCP server stay intact",
          "[config_edit][graphs][scope]") {
    apogee::harness::AgentConfig agent;
    agent.description = "the agent";
    apogee::harness::McpServerConfig server;
    server.command = "python3";
    apogee::harness::NamedGraphConfig graph;
    graph.collections = {"docs"};
    std::string content = append_agent(kCommented, "shared", agent, false);
    content = append_mcp_server(content, "shared", server, false);
    const std::string before = content;
    content = append_graph(content, "shared", graph, false);
    require_parses(content);
    // Deleting the graph takes only the graphs: entry.
    const std::string after = delete_graph(content, "shared");
    CHECK(after == before + "\ngraphs:\n");
    CHECK(apogee::harness::section_entry_names(after, "agents") ==
          std::vector<std::string>{"shared"});
    CHECK(apogee::harness::section_entry_names(after, "mcp_servers") ==
          std::vector<std::string>{"shared"});
    CHECK(apogee::harness::section_entry_names(after, "graphs").empty());
    // And deleting the agent leaves the graph alone.
    const std::string agent_gone = apogee::harness::delete_agent(content, "shared");
    CHECK(apogee::harness::section_entry_names(agent_gone, "graphs") ==
          std::vector<std::string>{"shared"});
}

TEST_CASE(
    "set_backend_model_path replaces in place keeping the comment, inserts after type, stays "
    "inside the backends section, and is byte-exact against the shipped template",
    "[config_edit][golden][training]") {
    constexpr std::string_view kBase =
        "# top\n"
        "backends:\n"
        "  tuned:\n"
        "    type: llamacpp\n"
        "    model_path: /old/v1.gguf   # promoted 2026-09-19\n"
        "    context_size: 8192\n"
        "  other:\n"
        "    type: mock\n"
        "\nembeddings:\n"
        "  tuned:\n"
        "    chunk_size: 512\n";
    const std::string replaced =
        apogee::harness::set_backend_model_path(kBase, "tuned", "/new/v2.gguf");
    require_parses(replaced);
    CHECK(replaced ==
          "# top\n"
          "backends:\n"
          "  tuned:\n"
          "    type: llamacpp\n"
          "    model_path: /new/v2.gguf   # promoted 2026-09-19\n"
          "    context_size: 8192\n"
          "  other:\n"
          "    type: mock\n"
          "\nembeddings:\n"
          "  tuned:\n"
          "    chunk_size: 512\n");
    CHECK(apogee::harness::parse_config(replaced, "<test>").find_backend("tuned")->model_path ==
          "/new/v2.gguf");
    // Case-insensitive on the name, and the inverse edit restores the bytes.
    CHECK(apogee::harness::set_backend_model_path(replaced, "TUNED", "/old/v1.gguf") ==
          std::string{kBase});

    // No model_path yet: inserted right after type.
    const std::string inserted =
        apogee::harness::set_backend_model_path(kBase, "other", "/models/x.gguf");
    require_parses(inserted);
    CHECK(
        inserted.find("  other:\n    type: mock\n    model_path: /models/x.gguf\n\nembeddings:") !=
        std::string::npos);

    // A path YAML could misread is quoted; a missing entry or section throws.
    CHECK(apogee::harness::set_backend_model_path(kBase, "other", "C:\\models\\x: y.gguf")
              .find("model_path: ") != std::string::npos);
    CHECK_THROWS_AS(apogee::harness::set_backend_model_path(kBase, "nope", "/x"),
                    apogee::harness::ConfigEditError);
    CHECK_THROWS_AS(
        apogee::harness::set_backend_model_path("embeddings:\n  tuned:\n    x: 1\n", "tuned", "/x"),
        apogee::harness::ConfigEditError);

    // The promote path on two copies of the YAML template: append a new
    // llamacpp entry, then repoint it in place -- and the second file equals
    // the first with only the path changed.
    const std::string shipped = legacy_template();
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/home/me/.apogee/training/versions/tuned/v1.gguf";
    const std::string appended = apogee::harness::append_backend(shipped, "tuned", entry, false);
    require_parses(appended);
    CHECK(appended.find("  tuned:\n    type: llamacpp\n    model_path: "
                        "/home/me/.apogee/training/versions/tuned/v1.gguf\n") != std::string::npos);
    const std::string repointed = apogee::harness::set_backend_model_path(
        appended, "tuned", "/home/me/.apogee/training/versions/tuned/v2.gguf");
    require_parses(repointed);
    std::string expected = appended;
    const std::size_t at = expected.find("/tuned/v1.gguf");
    REQUIRE(at != std::string::npos);
    expected.replace(at, std::string_view{"/tuned/v1.gguf"}.size(), "/tuned/v2.gguf");
    CHECK(repointed == expected);
    CHECK(apogee::harness::delete_backend(appended, "tuned") == shipped);
}

TEST_CASE("set_backend_mmproj_path replaces in place, or lands right after model_path",
          "[config_edit][golden][models]") {
    // What `models migrate` rewrites when a vision model's projector moves:
    // the same one-line edit as model_path, every other byte kept.
    constexpr std::string_view kBase =
        "backends:\n"
        "  vision:\n"
        "    type: llamacpp\n"
        "    model_path: /old/llava.gguf\n"
        "    mmproj_path: /old/llava-mmproj.gguf   # the projector\n"
        "  text:\n"
        "    type: llamacpp\n"
        "    model_path: /old/text.gguf\n"
        "    context_size: 4096\n";
    const std::string replaced =
        apogee::harness::set_backend_mmproj_path(kBase, "vision", "/new/llava-mmproj.gguf");
    require_parses(replaced);
    CHECK(replaced ==
          "backends:\n"
          "  vision:\n"
          "    type: llamacpp\n"
          "    model_path: /old/llava.gguf\n"
          "    mmproj_path: /new/llava-mmproj.gguf   # the projector\n"
          "  text:\n"
          "    type: llamacpp\n"
          "    model_path: /old/text.gguf\n"
          "    context_size: 4096\n");
    CHECK(apogee::harness::parse_config(replaced, "<test>").find_backend("vision")->mmproj_path ==
          "/new/llava-mmproj.gguf");

    const std::string inserted =
        apogee::harness::set_backend_mmproj_path(kBase, "text", "/new/text-mmproj.gguf");
    require_parses(inserted);
    CHECK(inserted.find("    model_path: /old/text.gguf\n    mmproj_path: /new/text-mmproj.gguf\n"
                        "    context_size: 4096\n") != std::string::npos);
    // model_path's own edit is unchanged by the generalisation.
    CHECK(apogee::harness::set_backend_model_path(kBase, "text", "/new/text.gguf")
              .find("    model_path: /new/text.gguf\n    context_size: 4096\n") !=
          std::string::npos);
    CHECK_THROWS_AS(apogee::harness::set_backend_mmproj_path(kBase, "nope", "/x"),
                    apogee::harness::ConfigEditError);
}

// ---------------------------------------------------------------------------
// tools.allowed_hosts -- the edit behind the fetch prompt's [a]lways
// ---------------------------------------------------------------------------

TEST_CASE("add_allowed_host on the YAML template changes one line, and remove undoes it",
          "[config_edit][hosts]") {
    const std::string shipped = legacy_template();
    const std::string one = apogee::harness::add_allowed_host(shipped, "Docs.Python.org.");
    std::string expected = shipped;
    const std::size_t at = expected.find("  allowed_hosts: []");
    REQUIRE(at != std::string::npos);
    expected.replace(at, std::string{"  allowed_hosts: []"}.size(),
                     "  allowed_hosts: [docs.python.org]");
    CHECK(one == expected);  // canonical spelling written

    const std::string two = apogee::harness::add_allowed_host(one, "pypi.org");
    CHECK(two.find("  allowed_hosts: [docs.python.org, pypi.org]") != std::string::npos);
    // Already listed, in any spelling: nothing changes.
    CHECK(apogee::harness::add_allowed_host(two, "PYPI.ORG.") == two);
    // IPv6 is written quoted, so YAML reads it back as the address.
    const std::string six = apogee::harness::add_allowed_host(two, "[::1]");
    CHECK(six.find(R"([docs.python.org, pypi.org, "::1"])") != std::string::npos);
    CHECK(apogee::harness::parse_config(six, "six").tools.allowed_hosts.back() == "::1");

    // Removing the first of two takes its separator with it.
    CHECK(apogee::harness::remove_allowed_host(two, "docs.python.org")
              .find("  allowed_hosts: [pypi.org]\n") != std::string::npos);
    // Every removal order returns the exact bytes.
    CHECK(apogee::harness::remove_allowed_host(six, "::1") == two);
    CHECK(apogee::harness::remove_allowed_host(two, "pypi.org") == one);
    CHECK(apogee::harness::remove_allowed_host(one, "docs.python.org") == shipped);
    CHECK(apogee::harness::remove_allowed_host(
              apogee::harness::remove_allowed_host(two, "docs.python.org"), "pypi.org") == shipped);
    CHECK(apogee::harness::parse_config(two, "two").tools.allowed_hosts ==
          std::vector<std::string>{"docs.python.org", "pypi.org"});
}

TEST_CASE("add_allowed_host handles block lists, missing keys and missing sections",
          "[config_edit][hosts]") {
    constexpr std::string_view kBlock =
        "tools:\n"
        "  disabled:\n"
        "    - shell\n"
        "  allowed_hosts:\n"
        "    - a.example   # the first\n"
        "    - \"b.example\"\n"
        "ui:\n"
        "  markdown: true\n";
    const std::string added = apogee::harness::add_allowed_host(kBlock, "c.example");
    CHECK(added ==
          "tools:\n"
          "  disabled:\n"
          "    - shell\n"
          "  allowed_hosts:\n"
          "    - a.example   # the first\n"
          "    - \"b.example\"\n"
          "    - c.example\n"
          "ui:\n"
          "  markdown: true\n");
    CHECK(apogee::harness::add_allowed_host(kBlock, "b.example") == kBlock);  // quoted: listed
    CHECK(apogee::harness::remove_allowed_host(added, "c.example") == kBlock);
    CHECK(apogee::harness::remove_allowed_host(kBlock, "a.example").find("a.example") ==
          std::string::npos);

    // A tools section without the key: inserted after the section's last
    // line, nested list included -- never between `disabled:` and its items.
    constexpr std::string_view kNoKey =
        "tools:\n"
        "  disabled:\n"
        "    - shell\n"
        "\n"
        "ui:\n"
        "  markdown: true\n";
    const std::string inserted = apogee::harness::add_allowed_host(kNoKey, "a.example");
    CHECK(inserted ==
          "tools:\n"
          "  disabled:\n"
          "    - shell\n"
          "  allowed_hosts: [a.example]\n"
          "\n"
          "ui:\n"
          "  markdown: true\n");
    CHECK(apogee::harness::parse_config(inserted, "x").tools.disabled ==
          std::vector<std::string>{"shell"});

    // No tools section: created. CRLF kept.
    const std::string crlf =
        apogee::harness::add_allowed_host("ui:\r\n  markdown: true\r\n", "a.example");
    CHECK(crlf == "ui:\r\n  markdown: true\r\n\r\ntools:\r\n  allowed_hosts: [a.example]\r\n");

    // `allowed_hosts:` alone reads as null; it gains the one-line form.
    CHECK(
        apogee::harness::add_allowed_host("tools:\n  allowed_hosts:   # none yet\n", "a.example") ==
        "tools:\n  allowed_hosts: [a.example]   # none yet\n");
}

TEST_CASE("remove_allowed_host removes an entry as written, and every spelling of a host",
          "[config_edit][hosts][CLI-M40]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::parse_config;
    using apogee::harness::remove_allowed_host;
    // The bug as a fixture: the URL was refused before the list was read,
    // and of two spellings the first went and the typed one stayed.
    CHECK(remove_allowed_host(
              "tools:\n  allowed_hosts: [docs.python.org, https://not-a-host.example/path]\n",
              "https://not-a-host.example/path") == "tools:\n  allowed_hosts: [docs.python.org]\n");
    CHECK(remove_allowed_host("tools:\n  allowed_hosts: [Example.COM., example.com]\n",
                              "example.com") == "tools:\n  allowed_hosts: []\n");
    // The investigation's two lists.
    CHECK(remove_allowed_host("tools:\n  allowed_hosts: [docs.python.org, Example.COM., "
                              "\"https://not-a-host.example/path\", my_host]\n",
                              "https://not-a-host.example/path") ==
          "tools:\n  allowed_hosts: [docs.python.org, Example.COM., my_host]\n");
    const std::string left = remove_allowed_host(
        "tools:\n  allowed_hosts: [Example.COM., example.com, "
        "\"https://not-a-host.example/path\"]\n",
        "example.com");
    CHECK(left == "tools:\n  allowed_hosts: [\"https://not-a-host.example/path\"]\n");
    CHECK(parse_config(left, "<left>").tools.allowed_hosts ==
          std::vector<std::string>{"https://not-a-host.example/path"});
    // Wherever the spellings sit, each takes one separator: before it when an
    // entry it keeps comes first, after it in a leading run.
    for (const auto& [before, after] : std::vector<std::pair<std::string, std::string>>{
             {"[Example.COM., docs.python.org, example.com]", "[docs.python.org]"},
             {"[Example.COM., example.com, docs.python.org]", "[docs.python.org]"},
             {"[docs.python.org, Example.COM., example.com]", "[docs.python.org]"},
             {"[docs.python.org,example.com ,  Example.COM.]", "[docs.python.org]"},
             {"[example.com,  EXAMPLE.com,pypi.org, example.com.]", "[pypi.org]"},
             {"[a.example, example.com, b.example, Example.COM, c.example]",
              "[a.example, b.example, c.example]"},
             {"[ example.com ]", "[]"},
             {"[\"*.x.example\", a.example, '*.x.example']", "[a.example]"}}) {
        const std::string host =
            before.find("*.x") != std::string::npos ? "*.x.example" : "example.com";
        INFO(before << " - " << host);
        const std::string edited =
            remove_allowed_host("tools:\n  allowed_hosts: " + before + "   # kept\n", host);
        CHECK(edited == "tools:\n  allowed_hosts: " + after + "   # kept\n");
        CHECK_NOTHROW(parse_config(edited, "<edited>"));
    }

    // A block list: a quoted entry with a comment, each spelling's own line.
    constexpr std::string_view kBlock =
        "tools:\n"
        "  allowed_hosts:\n"
        "    - docs.python.org\n"
        "    # pasted from the browser\n"
        "    - \"https://not-a-host.example/path\"   # oops\n"
        "    - Example.COM.\n"
        "    - example.com   # again\n"
        "ui:\n"
        "  markdown: true\n";
    CHECK(remove_allowed_host(kBlock, "https://not-a-host.example/path") ==
          "tools:\n"
          "  allowed_hosts:\n"
          "    - docs.python.org\n"
          "    # pasted from the browser\n"
          "    - Example.COM.\n"
          "    - example.com   # again\n"
          "ui:\n"
          "  markdown: true\n");
    CHECK(remove_allowed_host(kBlock, "EXAMPLE.COM") ==
          "tools:\n"
          "  allowed_hosts:\n"
          "    - docs.python.org\n"
          "    # pasted from the browser\n"
          "    - \"https://not-a-host.example/path\"   # oops\n"
          "ui:\n"
          "  markdown: true\n");

    // Add then remove is still the exact inverse, a malformed entry beside it.
    constexpr std::string_view kPasted = "tools:\n  allowed_hosts: [\"https://x.example/p\"]\n";
    const std::string added = add_allowed_host(kPasted, "pypi.org");
    CHECK(added == "tools:\n  allowed_hosts: [\"https://x.example/p\", pypi.org]\n");
    CHECK(remove_allowed_host(added, "pypi.org") == kPasted);
    const std::string blocked = add_allowed_host(kBlock, "pypi.org");
    CHECK(remove_allowed_host(blocked, "pypi.org") == kBlock);
}

TEST_CASE("remove_allowed_host deletes nothing it does not name, in today's words",
          "[config_edit][hosts][CLI-M40]") {
    using apogee::harness::remove_allowed_host;
    constexpr std::string_view kListed =
        "tools:\n  allowed_hosts: [docs.python.org, \"https://not-a-host.example/path\"]\n";
    // Typo protection (26o): a miss is refused, never a quiet no-op.
    CHECK_THROWS_WITH(remove_allowed_host(kListed, "https://typo.example/"),
                      "'https://typo.example/' is not a host name: write the host alone, e.g. "
                      "docs.python.org");
    CHECK_THROWS_WITH(remove_allowed_host(kListed, "https://not-a-host.example/path/"),
                      ContainsSubstring("is not a host name"));
    CHECK_THROWS_WITH(remove_allowed_host(kListed, "pypi.org"),
                      "'pypi.org' is not in tools.allowed_hosts");
    CHECK_THROWS_WITH(remove_allowed_host(kListed, "PyPI.org."),
                      "'pypi.org' is not in tools.allowed_hosts");
    CHECK_THROWS_WITH(remove_allowed_host(kListed, "not-a-host.example"),
                      "'not-a-host.example' is not in tools.allowed_hosts");
    CHECK_THROWS_WITH(remove_allowed_host("ui:\n  markdown: true\n", "*.example.com"),
                      ContainsSubstring("is not a host name"));
}

TEST_CASE("allowed-host edits refuse what is not a host, or not listed", "[config_edit][hosts]") {
    const std::string shipped{apogee::harness::config_template()};
    for (const char* bad : {"https://docs.python.org", "docs.python.org/x", "*.python.org",
                            "a b.example", "", "host:8080"}) {
        INFO(bad);
        CHECK_THROWS_AS((void)apogee::harness::add_allowed_host(shipped, bad), ConfigEditError);
    }
    CHECK_THROWS_AS((void)apogee::harness::remove_allowed_host(shipped, "docs.python.org"),
                    ConfigEditError);
    CHECK_THROWS_AS((void)apogee::harness::remove_allowed_host("ui:\n  markdown: true\n", "a.b"),
                    ConfigEditError);
    // A flow list spread over lines is refused rather than misread.
    CHECK_THROWS_AS(
        (void)apogee::harness::add_allowed_host("tools:\n  allowed_hosts: [a.example,\n    "
                                                "b.example]\n",
                                                "c.example"),
        ConfigEditError);
}

// --- the gate's edits land, or say they did not (CLI-M43) --------------------

TEST_CASE("a compact host list is read: add then remove gives back its bytes",
          "[config_edit][hosts][CLI-M43]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::parse_config;
    using apogee::harness::remove_allowed_host;

    // The bug as a fixture: items at the key's own indent (what PyYAML and
    // Ruby's Psych write) read as no list -- the remove said "not in", the
    // add rewrote the key to `[x]` above the items and could not parse.
    struct Shape {
        std::string name;
        std::string text;     ///< docs.python.org and example.com, compact
        std::string added;    ///< after add_allowed_host(text, "pypi.org")
        std::string removed;  ///< after remove_allowed_host(text, "example.com")
    };

    const std::string psych_head =
        "---\nbackends:\n  m:\n    type: mock\n    model: m\nmodels:\n  default: m\n";
    const std::vector<Shape> shapes{
        {"compact", "tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n",
         "tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n  - pypi.org\n",
         "tools:\n  allowed_hosts:\n  - docs.python.org\n"},
        {"Psych's exact dump",
         psych_head + "tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n",
         psych_head +
             "tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n  - pypi.org\n",
         psych_head + "tools:\n  allowed_hosts:\n  - docs.python.org\n"},
        {"comments between and after items",
         "tools:\n  allowed_hosts:\n  - docs.python.org   # the docs\n  # pasted later\n"
         "  - example.com   # the example\n  # end of the list\nui:\n  markdown: true\n",
         "tools:\n  allowed_hosts:\n  - docs.python.org   # the docs\n  # pasted later\n"
         "  - example.com   # the example\n  - pypi.org\n  # end of the list\nui:\n"
         "  markdown: true\n",
         "tools:\n  allowed_hosts:\n  - docs.python.org   # the docs\n  # pasted later\n"
         "  # end of the list\nui:\n  markdown: true\n"},
        {"another key after the list",
         "tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n  disabled:\n  - shell\n",
         "tools:\n  allowed_hosts:\n  - docs.python.org\n  - example.com\n  - pypi.org\n"
         "  disabled:\n  - shell\n",
         "tools:\n  allowed_hosts:\n  - docs.python.org\n  disabled:\n  - shell\n"},
        {"CRLF", "tools:\r\n  allowed_hosts:\r\n  - docs.python.org\r\n  - example.com\r\n",
         "tools:\r\n  allowed_hosts:\r\n  - docs.python.org\r\n  - example.com\r\n"
         "  - pypi.org\r\n",
         "tools:\r\n  allowed_hosts:\r\n  - docs.python.org\r\n"},
        {"a tab after the dash",
         "tools:\n  allowed_hosts:\n  -\tdocs.python.org\n  - example.com\n",
         "tools:\n  allowed_hosts:\n  -\tdocs.python.org\n  - example.com\n  - pypi.org\n",
         "tools:\n  allowed_hosts:\n  -\tdocs.python.org\n"},
    };
    for (const Shape& shape : shapes) {
        INFO(shape.name);
        const std::string added = add_allowed_host(shape.text, "pypi.org");
        CHECK(added == shape.added);
        CHECK(remove_allowed_host(added, "pypi.org") == shape.text);
        CHECK(parse_config(added, "<added>").tools.allowed_hosts ==
              std::vector<std::string>{"docs.python.org", "example.com", "pypi.org"});
        const std::string removed = remove_allowed_host(shape.text, "example.com");
        CHECK(removed == shape.removed);
        CHECK(parse_config(removed, "<removed>").tools.allowed_hosts ==
              std::vector<std::string>{"docs.python.org"});
    }
    const apogee::harness::Config another = parse_config(shapes[3].added, "<another>");
    CHECK(another.tools.disabled == std::vector<std::string>{"shell"});

    // CLI-M40's cases on a compact list: every spelling of a host goes, and
    // an entry that is no host goes by its own spelling.
    constexpr std::string_view kSpelled =
        "tools:\n  allowed_hosts:\n  - docs.python.org\n  - Example.COM.\n"
        "  - \"https://not-a-host.example/path\"   # pasted\n  - example.com\n";
    CHECK(remove_allowed_host(kSpelled, "example.com") ==
          "tools:\n  allowed_hosts:\n  - docs.python.org\n"
          "  - \"https://not-a-host.example/path\"   # pasted\n");
    CHECK(remove_allowed_host(kSpelled, "https://not-a-host.example/path") ==
          "tools:\n  allowed_hosts:\n  - docs.python.org\n  - Example.COM.\n  - example.com\n");
    CHECK(add_allowed_host(kSpelled, "EXAMPLE.com") == kSpelled);  // listed: nothing to write
    CHECK_THROWS_WITH(remove_allowed_host(kSpelled, "pypi.org"),
                      "'pypi.org' is not in tools.allowed_hosts");
}

TEST_CASE("an item written over two lines is refused by name, never split from its value",
          "[config_edit][hosts][CLI-M43]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::remove_allowed_host;
    // The loader reads `-\n  docs.python.org` as one item; the scan stopped
    // at the continuation, and an add landed above it: the loader then read
    // `pypi.org docs.python.org` as one entry, which allows nothing.
    for (const std::string& text :
         {std::string{
              "tools:\n  allowed_hosts:\n    -\n      docs.python.org\n    - example.com\n"},
          std::string{"tools:\n  allowed_hosts:\n  - example.com\n  -\n    docs.python.org\n"},
          std::string{"tools:\n  allowed_hosts:\n  - docs.python.org\n    and.more\n"}}) {
        INFO(text);
        for (const std::function<std::string()>& edit : std::vector<std::function<std::string()>>{
                 [&] { return add_allowed_host(text, "pypi.org"); },
                 [&] { return remove_allowed_host(text, "example.com"); }}) {
            CHECK_THROWS_WITH(edit(),
                              "tools.allowed_hosts holds an item written over more than "
                              "one line -- write each `- host` on one line");
        }
    }
}

namespace {

/// A read-back refusal (CLI-M43): the edit would not take effect, nothing
/// was changed, and `config migrate` is the way -- never the words a broken
/// edit or a miss has.
void require_not_landed(const std::function<std::string()>& edit) {
    try {
        (void)edit();
        FAIL("the edit printed a success the loader would not read");
    } catch (const ConfigEditError& e) {
        const std::string what = e.what();
        INFO(what);
        CHECK(what.find("would not take effect") != std::string::npos);
        CHECK(what.find("nothing was changed") != std::string::npos);
        CHECK(what.find("apogee config migrate") != std::string::npos);
        CHECK(what.find("not valid YAML") == std::string::npos);
        CHECK(what.find("is not in tools.allowed_hosts") == std::string::npos);
    }
}

}  // namespace

TEST_CASE("the gate's three edits read their effect back, and refuse one that would not land",
          "[config_edit][hosts][permissions][CLI-M43]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::remove_allowed_host;
    // Each printed success before. Since CLI-M44 the line editor refuses a
    // quoted or spaced key and a later document by name first, and adds a
    // section before a `...`, so the read-back is the defense behind those
    // refusals: a key written twice, of which the loader reads the last, and
    // a key written in YAML's explicit form (`? key`), which the line editor
    // does not see and the loader reads first.
    require_not_landed([] {
        return set_permission("permissions:\n  run_command: allow\n  run_command: ask\n",
                              "run_command", "deny");
    });
    require_not_landed([] {
        return add_allowed_host("tools:\n  ? allowed_hosts\n  : [docs.python.org]\n", "pypi.org");
    });
    // A remove the line editor finds nothing to do for, while the loader
    // still lists the host: not "not in tools.allowed_hosts".
    require_not_landed([] {
        return remove_allowed_host("tools:\n  ? allowed_hosts\n  : [docs.python.org]\n",
                                   "docs.python.org");
    });
    // Where the effect already reads right, an edit the loader would not
    // read is still never written: no dead key beside the one it reads (57e's
    // "never shadowed"), which a later hand edit would be lost in.
    require_not_landed([] {
        return set_permission("permissions:\n  run_command: allow\n  run_command: deny\n",
                              "run_command", "deny");
    });
    require_not_landed(
        [] { return add_allowed_host("tools:\n  ? allowed_hosts\n  : [pypi.org]\n", "pypi.org"); });
    // The shapes the read-back caught first, each refused or landed earlier
    // now (CLI-M44): a quoted or spaced header or key by name, a later
    // document naming the hand edit, a `...` with the section before it.
    for (const std::string& text : {std::string{"\"permissions\":\n  run_command: allow\n"},
                                    std::string{"permissions :\n  run_command: allow\n"}}) {
        INFO(text);
        CHECK_THROWS_WITH(set_permission(text, "run_command", "deny"),
                          ContainsSubstring("the key permissions is written") &&
                              ContainsSubstring("'apogee config migrate'"));
    }
    for (const std::string& text : {std::string{"tools:\n  \"allowed_hosts\": [docs.python.org]\n"},
                                    std::string{"tools:\n  allowed_hosts : [docs.python.org]\n"}}) {
        INFO(text);
        CHECK_THROWS_WITH(add_allowed_host(text, "pypi.org"),
                          ContainsSubstring("the key tools.allowed_hosts is written"));
        CHECK_THROWS_WITH(remove_allowed_host(text, "docs.python.org"),
                          ContainsSubstring("the key tools.allowed_hosts is written"));
    }
    CHECK_THROWS_WITH(
        add_allowed_host("\"tools\":\n  allowed_hosts: [docs.python.org]\n", "pypi.org"),
        ContainsSubstring("the key tools is written"));
    CHECK_THROWS_WITH(
        set_permission("backends:\n  m:\n    type: mock\n---\npermissions:\n  run_command: allow\n",
                       "run_command", "ask"),
        ContainsSubstring("holds a second YAML document"));
    CHECK_THROWS_WITH(
        remove_allowed_host(
            "backends:\n  m:\n    type: mock\n...\ntools:\n  allowed_hosts: [a.example]\n",
            "a.example"),
        ContainsSubstring("holds a second YAML document"));
    CHECK(set_permission("backends:\n  m:\n    type: mock\n...\n", "run_command", "deny") ==
          "backends:\n  m:\n    type: mock\n\npermissions:\n  run_command: deny\n...\n");
    CHECK(add_allowed_host("backends:\n  m:\n    type: mock\n...\n", "pypi.org") ==
          "backends:\n  m:\n    type: mock\n\ntools:\n  allowed_hosts: [pypi.org]\n...\n");
    // The control: the same edits on a bare header land, as today, a level
    // already set included.
    CHECK(set_permission("permissions:\n  run_command: allow\n", "run_command", "deny") ==
          "permissions:\n  run_command: deny\n");
    CHECK(set_permission("permissions:\n  run_command: allow   # mine\n", "run_command", "allow") ==
          "permissions:\n  run_command: allow   # mine\n");
    CHECK(set_permission("backends:\n  m:\n    type: mock\n", "run_command", "ask") ==
          "backends:\n  m:\n    type: mock\n\npermissions:\n  run_command: ask\n");
    CHECK(add_allowed_host("tools:\n  allowed_hosts: [docs.python.org]\n", "pypi.org") ==
          "tools:\n  allowed_hosts: [docs.python.org, pypi.org]\n");
}

TEST_CASE(
    "a block list at the end of a file with no final newline takes an add on a line of its own",
    "[config_edit][hosts][CLI-M43]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::parse_config;
    // The new item was glued onto the last one: the loader read the one
    // entry `a.example    - pypi.org`, which allows nothing.
    for (const auto& [text, added] : std::vector<std::pair<std::string, std::string>>{
             {"tools:\n  allowed_hosts:\n  - a.example",
              "tools:\n  allowed_hosts:\n  - a.example\n  - pypi.org\n"},
             {"tools:\n  allowed_hosts:\n    - a.example   # mine",
              "tools:\n  allowed_hosts:\n    - a.example   # mine\n    - pypi.org\n"},
             {"tools:\r\n  allowed_hosts:\r\n  - a.example",
              "tools:\r\n  allowed_hosts:\r\n  - a.example\r\n  - pypi.org\r\n"}}) {
        INFO(text);
        const std::string edited = add_allowed_host(text, "pypi.org");
        CHECK(edited == added);
        CHECK(parse_config(edited, "<added>").tools.allowed_hosts ==
              std::vector<std::string>{"a.example", "pypi.org"});
    }
}

TEST_CASE("a remove is refused while an entry spelled through a variable names the host",
          "[config_edit][hosts][CLI-M43]") {
    using apogee::harness::remove_allowed_host;
    // CLI-M40 left it to this read-back: the loader expands `${VAR}`, the
    // line editor reads the entry as written, so "removed" left the host
    // reached unasked.
    const std::string text = "tools:\n  allowed_hosts: [example.com, \"${APOGEE_M43_HOST}\"]\n";
    const std::string json =
        "{\"tools\": {\"allowed_hosts\": [\"example.com\", \"${APOGEE_M43_HOST}\"]}}\n";
    {
        const apogee::testing::EnvGuard variable{"APOGEE_M43_HOST", "Example.COM."};
        for (const std::string& content :
             {text, json, std::string{"tools:\n  allowed_hosts: [\"${APOGEE_M43_HOST}\"]\n"}}) {
            INFO(content);
            CHECK_THROWS_WITH(
                remove_allowed_host(content, "example.com"),
                "removing example.com from tools.allowed_hosts would not take effect -- as Apogee "
                "reads this file, the list still holds it, through the entry "
                "'${APOGEE_M43_HOST}', which the line editor reads as written -- so nothing was "
                "changed: edit that entry by hand");
        }
    }
    {
        // The entry named as written goes; the literal entry stays.
        const apogee::testing::EnvGuard variable{"APOGEE_M43_HOST", "example.com"};
        CHECK(remove_allowed_host(text, "${APOGEE_M43_HOST}") ==
              "tools:\n  allowed_hosts: [example.com]\n");
    }
    // Unset, the entry names no host, and the remove lands.
    const apogee::testing::EnvUnsetGuard unset{"APOGEE_M43_HOST"};
    CHECK(remove_allowed_host(text, "example.com") ==
          "tools:\n  allowed_hosts: [\"${APOGEE_M43_HOST}\"]\n");
}

TEST_CASE("the helper role pointers are set through the one editor", "[config_edit][roles]") {
    const std::string base = "models:\n  default: a  # the chat\nbackends:\n  a:\n    type: mock\n";
    std::string edited = apogee::harness::set_models_role(base, "default_utility", "helper");
    edited = apogee::harness::set_models_role(edited, "default_vision", "eyes");
    edited = apogee::harness::set_models_role(edited, "default_transcription", "ears");
    const auto config = apogee::harness::parse_config(edited, "<test>");
    CHECK(config.models.default_utility == "helper");
    CHECK(config.models.default_vision == "eyes");
    CHECK(config.models.default_transcription == "ears");
    // The comment on the line next to them is kept.
    CHECK(edited.find("default: a  # the chat") != std::string::npos);

    try {
        (void)apogee::harness::set_models_role(base, "default_helper", "x");
        FAIL("expected a ConfigEditError");
    } catch (const apogee::harness::ConfigEditError& e) {
        const std::string message = e.what();
        CHECK(message.find("default_helper") != std::string::npos);
        CHECK(message.find("default_vision, default_transcription, default_utility") !=
              std::string::npos);
    }
}

namespace {

apogee::harness::SuiteConfig research_suite() {
    apogee::harness::SuiteConfig suite;
    suite.members["chat"] = {.backend = "claude"};
    suite.members["utility"] = {
        .backend = "local", .context_size = 4096, .toolset = std::vector<std::string>{"fs"}};
    suite.members["embedding"] = {.backend = "local"};
    return suite;
}

/// A suite written by hand, comments and all, to edit one member of.
constexpr std::string_view kHandSuite = R"YAML(models:
  default: claude
backends:
  claude:
    type: mock
  local:
    type: mock
  helper:
    type: mock
suites:
  # The everyday suite.
  research:
    description: Deep work   # what it is for
    members:
      chat: claude          # the big model
      # The small one does the chores.
      utility:
        backend: local
        context_size: 4096  # small on purpose
      vision: claude
  fast:
    members:
      chat: local
)YAML";

}  // namespace

TEST_CASE("a suites: entry round-trips byte-exactly, members in role order",
          "[config_edit][golden][suites]") {
    using apogee::harness::append_suite;
    using apogee::harness::delete_suite;
    const std::string added = append_suite(kCommented, "research", research_suite(), false);
    require_parses(added);
    CHECK(added.starts_with(std::string{kCommented}));
    // Role order, not the map's; the short form where nothing is pinned.
    CHECK(
        added.ends_with("\nsuites:\n  research:\n    members:\n      chat: claude\n"
                        "      embedding: local\n      utility:\n        backend: local\n"
                        "        context_size: 4096\n        toolset: [fs]\n"));
    CHECK(delete_suite(added, "research") == std::string{kCommented} + "\nsuites:\n");
    const auto loaded = apogee::harness::parse_config(added, "<test>");
    REQUIRE(loaded.find_suite("research") != nullptr);
    CHECK(*loaded.find_suite("research") == research_suite());

    // A description when it says something, and an empty toolset written as one.
    apogee::harness::SuiteConfig described;
    described.description = "Deep: work";
    described.members["chat"] = {.backend = "claude", .toolset = std::vector<std::string>{}};
    const std::string full = append_suite("suites:\n", "s", described, false);
    CHECK(full ==
          "suites:\n  s:\n    description: \"Deep: work\"\n    members:\n      chat:\n"
          "        backend: claude\n        toolset: []\n");
    CHECK(*apogee::harness::parse_config(full, "<test>").find_suite("s") == described);

    // Collisions as every section's, and `off` -- `/suite off` -- refused.
    CHECK_THROWS_AS((void)append_suite(added, "Research", research_suite(), false),
                    ConfigEditError);
    CHECK_THROWS_AS((void)append_suite(added, "research", research_suite(), false),
                    ConfigEditError);
    CHECK_THROWS_AS((void)append_suite(kCommented, "off", research_suite(), false),
                    ConfigEditError);
    CHECK_THROWS_AS((void)append_suite(kCommented, "OFF", research_suite(), true), ConfigEditError);
    const std::string replaced = append_suite(added, "research", described, true);
    CHECK(apogee::harness::section_entry_names(replaced, "suites").size() == 1);
    CHECK(*apogee::harness::parse_config(replaced, "<test>").find_suite("research") == described);
    CHECK_THROWS_AS((void)delete_suite(kCommented, "research"), ConfigEditError);
}

TEST_CASE("setting one member leaves every other line of the suite as it was",
          "[config_edit][golden][suites]") {
    using apogee::harness::set_suite_member;
    using apogee::harness::SuiteMember;
    require_parses(kHandSuite);
    const std::string hand{kHandSuite};

    // Replaced in place: the member's own lines go, its neighbours' comments stay.
    const std::string swapped =
        set_suite_member(hand, "research", "utility", SuiteMember{.backend = "helper"});
    require_parses(swapped);
    std::string expected = hand;
    const std::string utility_lines =
        "      utility:\n        backend: local\n        context_size: 4096  # small on "
        "purpose\n";
    expected.replace(expected.find(utility_lines), utility_lines.size(), "      utility: helper\n");
    CHECK(swapped == expected);

    // Inserted in role order: embedding after chat, before its comment-led
    // utility -- which keeps its comment.
    const std::string inserted =
        set_suite_member(hand, "RESEARCH", "embedding", SuiteMember{.backend = "local"});
    expected = hand;
    expected.insert(expected.find("      # The small one"), "      embedding: local\n");
    CHECK(inserted == expected);
    // After every role it knows: at the end of the block.
    const std::string last = set_suite_member(
        hand, "fast", "utility", SuiteMember{.backend = "helper", .context_size = 1024});
    expected = hand;
    expected += "      utility:\n        backend: helper\n        context_size: 1024\n";
    CHECK(last == expected);

    // Removed: the member and only it.
    const std::string removed = set_suite_member(hand, "research", "vision", std::nullopt);
    expected = hand;
    expected.erase(expected.find("      vision: claude\n"),
                   std::string{"      vision: claude\n"}.size());
    CHECK(removed == expected);
    // And put back, it goes in role order: before utility and the comment
    // leading it.
    expected = removed;
    expected.insert(expected.find("      # The small one"), "      vision: claude\n");
    CHECK(set_suite_member(removed, "research", "vision", SuiteMember{.backend = "claude"}) ==
          expected);

    // A suite with no members: block yet gains one.
    const std::string bare = "suites:\n  s:\n    description: x\n";
    CHECK(set_suite_member(bare, "s", "chat", SuiteMember{.backend = "claude"}) ==
          "suites:\n  s:\n    description: x\n    members:\n      chat: claude\n");

    // Refusals: a role that is none, a suite or a member that is not there, a
    // members: block written on one line.
    CHECK_THROWS_AS((void)set_suite_member(hand, "research", "root", SuiteMember{.backend = "x"}),
                    ConfigEditError);
    CHECK_THROWS_AS((void)set_suite_member(hand, "nope", "chat", SuiteMember{.backend = "x"}),
                    ConfigEditError);
    CHECK_THROWS_AS((void)set_suite_member(hand, "fast", "vision", std::nullopt), ConfigEditError);
    CHECK_THROWS_AS((void)set_suite_member(kCommented, "s", "chat", SuiteMember{.backend = "x"}),
                    ConfigEditError);
    CHECK_THROWS_AS((void)set_suite_member("suites:\n  s:\n    members: {chat: claude}\n", "s",
                                           "utility", SuiteMember{.backend = "x"}),
                    ConfigEditError);
}

TEST_CASE("the default suite is set like a pointer, and the loader holds it to a suite",
          "[config_edit][suites]") {
    using apogee::harness::set_default_suite;
    const std::string hand{kHandSuite};
    const std::string set = set_default_suite(hand, "fast");
    CHECK(set == "models:\n  default: claude\n  default_suite: fast\n" +
                     hand.substr(std::string{"models:\n  default: claude\n"}.size()));
    CHECK(apogee::harness::parse_config(set, "<test>").models.default_suite == "fast");
    // Replaced in place, then cleared.
    const std::string cleared = set_default_suite(set_default_suite(set, "research"), "");
    CHECK(cleared.find("  default_suite: \"\"\n") != std::string::npos);
    CHECK(apogee::harness::parse_config(cleared, "<test>").models.default_suite.empty());
    // The re-parse is what refuses a dangling name, and the default suite's
    // deletion: the edit never lands.
    CHECK_THROWS_AS(apogee::harness::parse_config(set_default_suite(hand, "nope"), "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(
        apogee::harness::parse_config(apogee::harness::delete_suite(set, "fast"), "<test>"),
        apogee::harness::ConfigError);
}

TEST_CASE("the default video pipeline is set like a pointer, read back, and cleared",
          "[config_edit][video]") {
    // 49c: `models.default_video` -- the pipeline `video generate` runs --
    // written as the suite's pointer is, beside the roles, never one of them.
    using apogee::harness::set_default_video;
    const std::string hand{kHandSuite};
    const std::string set = set_default_video(hand, "org--clip/pipeline/aaaaaaaaaaaa");
    CHECK(set == "models:\n  default: claude\n  default_video: org--clip/pipeline/aaaaaaaaaaaa\n" +
                     hand.substr(std::string{"models:\n  default: claude\n"}.size()));
    const apogee::harness::Config config = apogee::harness::parse_config(set, "<test>");
    CHECK(config.models.default_video == "org--clip/pipeline/aaaaaaaaaaaa");
    CHECK(config.models.default_backend == "claude");
    // Replaced in place, then cleared.
    const std::string cleared = set_default_video(set_default_video(set, "org--clip"), "");
    CHECK(cleared.find("  default_video: \"\"\n") != std::string::npos);
    CHECK(apogee::harness::parse_config(cleared, "<test>").models.default_video.empty());
    // Not a role: the role editor refuses the field.
    CHECK_THROWS_AS(apogee::harness::set_models_role(hand, "default_video", "x"), ConfigEditError);
}

TEST_CASE("a suite's consultable members and caps are written after its members, and round-trip",
          "[config_edit][golden][suites]") {
    using apogee::harness::append_suite;
    apogee::harness::SuiteConfig suite = research_suite();
    suite.consultable = {"utility"};
    suite.consult_caps.per_turn = 2;
    suite.consult_caps.answer_tokens = 256;
    const std::string added = append_suite(kCommented, "research", suite, false);
    require_parses(added);
    CHECK(
        added.ends_with("        toolset: [fs]\n    consultable: [utility]\n"
                        "    consult_caps:\n      per_turn: 2\n      answer_tokens: 256\n"));
    CHECK(*apogee::harness::parse_config(added, "<test>").find_suite("research") == suite);
    // The inverse is the same entry's: everything it wrote goes.
    CHECK(apogee::harness::delete_suite(added, "research") ==
          std::string{kCommented} + "\nsuites:\n");
}

TEST_CASE("setting a suite's consultable members and caps leaves every other line as it was",
          "[config_edit][golden][suites]") {
    using apogee::harness::ConsultCaps;
    using apogee::harness::set_suite_consult_caps;
    using apogee::harness::set_suite_consultable;
    const std::string hand{kHandSuite};

    // Added at the entry's end -- after its members, before the next suite.
    const std::string added = set_suite_consultable(hand, "research", {"utility"});
    require_parses(added);
    std::string expected = hand;
    expected.insert(expected.find("  fast:\n"), "    consultable: [utility]\n");
    CHECK(added == expected);
    CHECK(apogee::harness::parse_config(added, "<test>").find_suite("research")->consultable ==
          std::vector<std::string>{"utility"});

    // Replaced where it stands, its neighbours untouched; a block list too.
    const std::string widened = set_suite_consultable(added, "research", {"utility", "vision"});
    expected = hand;
    expected.insert(expected.find("  fast:\n"), "    consultable: [utility, vision]\n");
    CHECK(widened == expected);
    const std::string block = std::string{kHandSuite}.insert(
        std::string{kHandSuite}.find("  fast:\n"),
        "    consultable:   # who it asks\n      - utility\n    # caps follow\n    consult_caps:\n"
        "      per_turn: 3\n");
    require_parses(block);
    const std::string reflowed = set_suite_consultable(block, "research", {"vision"});
    CHECK(reflowed == std::string{kHandSuite}.insert(std::string{kHandSuite}.find("  fast:\n"),
                                                     "    consultable: [vision]\n    # caps "
                                                     "follow\n    consult_caps:\n      per_turn: "
                                                     "3\n"));
    // A block list whose items sit at the key's own indent -- legal YAML --
    // goes with its key.
    const std::string flush = std::string{kHandSuite}.insert(
        std::string{kHandSuite}.find("  fast:\n"), "    consultable:\n    - utility\n");
    require_parses(flush);
    CHECK(set_suite_consultable(flush, "research", {"vision"}) ==
          std::string{kHandSuite}.insert(std::string{kHandSuite}.find("  fast:\n"),
                                         "    consultable: [vision]\n"));
    // Removed: the key and its list go, nothing else.
    CHECK(set_suite_consultable(widened, "research", {}) == hand);
    CHECK(set_suite_consultable(hand, "research", {}) == hand);

    // Caps: added at the end, replaced in place, removed when none is set.
    ConsultCaps caps;
    caps.brief_tokens = 512;
    const std::string capped = set_suite_consult_caps(added, "research", caps);
    expected = added;
    expected.insert(expected.find("  fast:\n"), "    consult_caps:\n      brief_tokens: 512\n");
    CHECK(capped == expected);
    caps.per_turn = 1;
    const std::string recapped = set_suite_consult_caps(capped, "research", caps);
    expected = added;
    expected.insert(expected.find("  fast:\n"),
                    "    consult_caps:\n      per_turn: 1\n      brief_tokens: 512\n");
    CHECK(recapped == expected);
    CHECK(set_suite_consult_caps(recapped, "research", ConsultCaps{}) == added);
    // A consultable list set on a suite with caps goes above them, and above
    // the comment leading them.
    const std::string caps_first = set_suite_consult_caps(hand, "fast", caps);
    const std::string then_list = set_suite_consultable(
        std::string{caps_first}.insert(caps_first.find("    consult_caps:"), "    # bounds\n"),
        "fast", {"utility"});
    CHECK(
        then_list.ends_with("      chat: local\n    consultable: [utility]\n    # bounds\n"
                            "    consult_caps:\n      per_turn: 1\n      brief_tokens: 512\n"));

    // A suite that is not there is refused, as for a member.
    CHECK_THROWS_AS((void)set_suite_consultable(hand, "nope", {"utility"}), ConfigEditError);
    CHECK_THROWS_AS((void)set_suite_consult_caps(kCommented, "s", caps), ConfigEditError);
}

TEST_CASE("a suite's validate: block is written last, only what is set, and round-trips",
          "[config_edit][golden][suites][validate]") {
    using apogee::harness::append_suite;
    apogee::harness::SuiteConfig suite = research_suite();
    suite.consultable = {"utility"};
    suite.validate.tool_args = true;
    suite.validate.answers = "always";
    const std::string added = append_suite(kCommented, "research", suite, false);
    require_parses(added);
    CHECK(
        added.ends_with("        toolset: [fs]\n    consultable: [utility]\n"
                        "    validate:\n      tool_args: on\n      answers: always\n"));
    CHECK(*apogee::harness::parse_config(added, "<test>").find_suite("research") == suite);
    CHECK(apogee::harness::delete_suite(added, "research") ==
          std::string{kCommented} + "\nsuites:\n");
    // Every key, in its writing order.
    suite.validate.verifier = "vision";
    suite.validate.extraction = false;
    suite.members["vision"] = {.backend = "claude"};
    const std::string full = append_suite(kCommented, "research", suite, false);
    CHECK(
        full.ends_with("    validate:\n      verifier: vision\n      tool_args: on\n"
                       "      extraction: off\n      answers: always\n"));
    CHECK(*apogee::harness::parse_config(full, "<test>").find_suite("research") == suite);
}

TEST_CASE("setting a suite's validate: block leaves every other line as it was",
          "[config_edit][golden][suites][validate]") {
    using apogee::harness::set_suite_validate;
    using apogee::harness::ValidateConfig;
    const std::string hand{kHandSuite};

    // Added at the entry's end.
    const std::string added =
        set_suite_validate(hand, "research", ValidateConfig{.tool_args = true});
    require_parses(added);
    std::string expected = hand;
    expected.insert(expected.find("  fast:\n"), "    validate:\n      tool_args: on\n");
    CHECK(added == expected);

    // Replaced in place, a comment inside it gone with it and one above kept.
    const std::string commented = std::string{kHandSuite}.insert(
        std::string{kHandSuite}.find("  fast:\n"),
        "    # checks\n    validate:\n      tool_args: on   # tools first\n");
    require_parses(commented);
    const std::string widened = set_suite_validate(
        commented, "research", ValidateConfig{.tool_args = true, .answers = "always"});
    CHECK(widened ==
          std::string{kHandSuite}.insert(
              std::string{kHandSuite}.find("  fast:\n"),
              "    # checks\n    validate:\n      tool_args: on\n      answers: always\n"));
    // Removed: nothing set, no block.
    CHECK(set_suite_validate(added, "research", ValidateConfig{}) == hand);
    CHECK(set_suite_validate(hand, "research", ValidateConfig{}) == hand);
    // The last suite: at the file's end.
    CHECK(set_suite_validate(hand, "fast", ValidateConfig{.answers = "always"})
              .ends_with("      chat: local\n    validate:\n      answers: always\n"));
    // The re-parse is what refuses a verifier with no member.
    CHECK_THROWS_AS(
        apogee::harness::parse_config(
            set_suite_validate(hand, "fast", ValidateConfig{.tool_args = true}), "<test>"),
        apogee::harness::ConfigError);
    CHECK_THROWS_AS((void)set_suite_validate(hand, "nope", ValidateConfig{.tool_args = true}),
                    ConfigEditError);
}

TEST_CASE("a suite's orchestrate: is written last, only when on, and round-trips",
          "[config_edit][golden][suites][orchestrate]") {
    using apogee::harness::append_suite;
    apogee::harness::SuiteConfig suite = research_suite();
    suite.validate.tool_args = true;
    suite.orchestrate = true;
    const std::string added = append_suite(kCommented, "research", suite, false);
    require_parses(added);
    CHECK(added.ends_with("    validate:\n      tool_args: on\n    orchestrate: true\n"));
    CHECK(*apogee::harness::parse_config(added, "<test>").find_suite("research") == suite);
    CHECK(apogee::harness::delete_suite(added, "research") ==
          std::string{kCommented} + "\nsuites:\n");
    // Off is absent: nothing written.
    suite.orchestrate = false;
    CHECK(append_suite(kCommented, "research", suite, false).find("orchestrate") ==
          std::string::npos);
}

TEST_CASE("setting a suite's orchestrate: leaves every other line as it was",
          "[config_edit][golden][suites][orchestrate]") {
    using apogee::harness::set_suite_orchestrate;
    const std::string hand{kHandSuite};
    // On: added at the entry's end.
    const std::string on = set_suite_orchestrate(hand, "research", true);
    require_parses(on);
    std::string expected = hand;
    expected.insert(expected.find("  fast:\n"), "    orchestrate: true\n");
    CHECK(on == expected);
    CHECK(apogee::harness::parse_config(on, "<test>").find_suite("research")->orchestrate);
    // On again: unchanged. Off: the key removed, the file as it was.
    CHECK(set_suite_orchestrate(on, "research", true) == on);
    CHECK(set_suite_orchestrate(on, "research", false) == hand);
    CHECK(set_suite_orchestrate(hand, "research", false) == hand);
    // A hand-written value, with a comment above it: replaced in place, the
    // comment kept.
    const std::string commented = std::string{kHandSuite}.insert(
        std::string{kHandSuite}.find("  fast:\n"), "    # plays on its own\n    orchestrate: on\n");
    require_parses(commented);
    CHECK(set_suite_orchestrate(commented, "research", true) ==
          std::string{kHandSuite}.insert(std::string{kHandSuite}.find("  fast:\n"),
                                         "    # plays on its own\n    orchestrate: true\n"));
    // The last suite: at the file's end.
    CHECK(set_suite_orchestrate(hand, "fast", true).ends_with("    orchestrate: true\n"));
    CHECK_THROWS_AS((void)set_suite_orchestrate(hand, "nope", true), ConfigEditError);
}

TEST_CASE("attachments.graph is set through the one editor, every comment kept",
          "[config_edit][attachments]") {
    using apogee::harness::AttachmentGraphMethod;
    using apogee::harness::set_attachments_graph;
    // 27p: the YAML template keeps its commented example whole, and the
    // block is appended after everything -- the only change.
    const std::string shipped = legacy_template();
    const std::string off = set_attachments_graph(shipped, "off");
    CHECK(off == shipped + "\nattachments:\n  graph: off\n");
    CHECK(apogee::harness::parse_config(off, "<test>").attachments.graph ==
          AttachmentGraphMethod::Off);
    // Set again: one token, in place.
    const std::string code = set_attachments_graph(off, "code");
    CHECK(code == shipped + "\nattachments:\n  graph: code\n");
    CHECK(apogee::harness::parse_config(code, "<test>").attachments.graph ==
          AttachmentGraphMethod::Code);

    // A hand-written block: the value replaced, its trailing comment and the
    // comment-dense rest untouched.
    const std::string hand = std::string{kCommented} +
                             "\n# How attaches are indexed.\nattachments:\n"
                             "  graph: code    # build the code graph\n";
    const std::string edited = set_attachments_graph(hand, "off");
    CHECK(edited == std::string{kCommented} +
                        "\n# How attaches are indexed.\nattachments:\n"
                        "  graph: off    # build the code graph\n");
    require_parses(edited);
    // A block with no graph line gains one; a config with none gains the block.
    CHECK(set_attachments_graph("attachments:\n", "off") == "attachments:\n  graph: off\n");
    CHECK(set_attachments_graph(kCommented, "code") ==
          std::string{kCommented} + "\nattachments:\n  graph: code\n");
}

TEST_CASE("attachments.graph off its set, or on a line the editor cannot see into, is refused",
          "[config_edit][attachments]") {
    using apogee::harness::set_attachments_graph;
    CHECK_THROWS_WITH(set_attachments_graph(kCommented, "tree"),
                      "attachments.graph: unknown value 'tree' (accepted: code, off)");
    CHECK_THROWS_AS(set_attachments_graph(kCommented, ""), ConfigEditError);
    CHECK_THROWS_AS(set_attachments_graph(kCommented, "Off"), ConfigEditError);
    // A one-line mapping: appending a block beside it would be a second key
    // the loader never reads -- refused by the section lookup every editor
    // shares (57e).
    CHECK_THROWS_WITH(set_attachments_graph("attachments: { graph: off }\n", "code"),
                      ContainsSubstring("the attachments: line holds more than its key"));
    // A header with a comment is the section's header: edited under it, the
    // comment kept (57e).
    CHECK(set_attachments_graph("attachments:   # how\n  graph: off\n", "code") ==
          "attachments:   # how\n  graph: code\n");
    // A comment naming the key is not the key.
    CHECK(set_attachments_graph("# attachments: { graph: off }\n", "off") ==
          "# attachments: { graph: off }\n\nattachments:\n  graph: off\n");

    // Through the file layer: refused before the file is touched.
    const TempDir dir{"edit-attachments"};
    const std::filesystem::path path = std::filesystem::path{dir.path()} / "config.yaml";
    apogee::harness::write_file_atomically(path, kCommented);
    CHECK_THROWS_AS(apogee::harness::edit_config_file(
                        path, [](std::string_view c) { return set_attachments_graph(c, "tree"); }),
                    ConfigEditError);
    {
        // Closed before the next edit: on Windows a file held open cannot be
        // replaced, and the rename that lands an edit fails "access denied".
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        CHECK(buffer.str() == std::string{kCommented});
    }
    // And a good edit lands, read back by the loader.
    apogee::harness::edit_config_file(
        path, [](std::string_view c) { return set_attachments_graph(c, "off"); });
    CHECK(apogee::harness::load_config(path).attachments.graph ==
          apogee::harness::AttachmentGraphMethod::Off);
}

// --- renaming a backend (46a) ----------------------------------------------

namespace {

/// Every way a backend is named in a config, around comments, with the
/// traps a rename must leave alone: an MCP server and a collection of the
/// same name, a prompt that says it, a name that only begins with it.
constexpr std::string_view kRenameYaml = R"(# The config, with notes.
backends:
  # The local model, quantized.
  gemma:   # the one we use
    type: llamacpp
    model_path: /models/gemma.gguf   # 12B
  gemma-old:
    type: mock
  claude:
    type: anthropic
models:
  default: gemma   # the chat
  default_utility: "gemma"
  default_vision: claude
suites:
  duo:
    members:
      chat: gemma
      utility: {backend: gemma, window: 8192}
  flow:
    members: {chat: gemma, utility: claude}
embeddings:
  gemma:
    backend: gemma
    rerank: gemma
    graph:
      extract_backend: gemma
graphs:
  code:
    extract_backend: gemma
agents:
  reviewer:
    model: gemma
mcp_servers:
  gemma:
    command: /bin/echo
symphonies:
  say:
    description: Says gemma.
    stages:
      - name: one
        role: chat
        prompt: |
          backend: gemma
          model: gemma
training:
  judge_backend: gemma
  regimes:
    mine:
      teacher: gemma
      kits: [summarization]
  cycle:
    backend: gemma
    judge_backend: claude
    sources:
    - type: sessions
      log_consent: true
      backend: gemma
    - type: directory
      path: /data
)";

}  // namespace

TEST_CASE("rename_backend renames the key and every reference, and nothing else",
          "[config_edit][rename]") {
    const std::string renamed = apogee::harness::rename_backend(kRenameYaml, "gemma", "gemma4-12b");
    std::string expected{kRenameYaml};
    const auto swap = [&expected](std::string_view from, std::string_view to) {
        const std::size_t at = expected.find(from);
        REQUIRE(at != std::string::npos);
        expected.replace(at, from.size(), to);
    };
    swap("  gemma:   # the one we use", "  gemma4-12b:   # the one we use");
    swap("  default: gemma   # the chat", "  default: gemma4-12b   # the chat");
    swap("  default_utility: \"gemma\"", "  default_utility: gemma4-12b");
    swap("      chat: gemma\n", "      chat: gemma4-12b\n");
    swap("{backend: gemma, window: 8192}", "{backend: gemma4-12b, window: 8192}");
    swap("{chat: gemma, utility: claude}", "{chat: gemma4-12b, utility: claude}");
    swap("    backend: gemma\n    rerank: gemma\n",
         "    backend: gemma4-12b\n    rerank: gemma4-12b\n");
    swap("      extract_backend: gemma\n", "      extract_backend: gemma4-12b\n");
    swap("    extract_backend: gemma\nagents", "    extract_backend: gemma4-12b\nagents");
    swap("    model: gemma\nmcp", "    model: gemma4-12b\nmcp");
    swap("  judge_backend: gemma\n", "  judge_backend: gemma4-12b\n");
    swap("      teacher: gemma\n", "      teacher: gemma4-12b\n");
    swap("    backend: gemma\n    judge_backend: claude",
         "    backend: gemma4-12b\n    judge_backend: claude");
    swap("      backend: gemma\n    - type: directory",
         "      backend: gemma4-12b\n    - type: directory");
    CHECK(renamed == expected);
    // It loads, the name moved and its references with it.
    const apogee::harness::Config config = apogee::harness::parse_config(renamed, "renamed");
    CHECK(config.find_backend("gemma4-12b") != nullptr);
    CHECK(config.find_backend("gemma") == nullptr);
    CHECK(config.models.default_backend == "gemma4-12b");
    // Back again is the file it was -- but for the value written quoted, which
    // the editor writes plain, as it writes every value YAML reads unchanged.
    std::string back{kRenameYaml};
    back.replace(back.find("default_utility: \"gemma\""), 24, "default_utility: gemma");
    CHECK(apogee::harness::rename_backend(renamed, "gemma4-12b", "gemma") == back);
}

TEST_CASE("rename_backend refuses an unknown name, a taken one, and a name no key can be",
          "[config_edit][rename]") {
    using apogee::harness::ConfigEditError;
    using apogee::harness::rename_backend;
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "nope", "x"), "no backend named 'nope'");
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", "claude"),
                      "a backend named 'claude' already exists");
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", "Claude"),
                      "a backend named 'claude' already exists");
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", "gemma"), "'gemma' is already its name");
    // The name add-backend would take (55f): one both formats edit.
    constexpr std::string_view kRule =
        "a name is letters, digits, '_', '-', '.' and '/', first "
        "neither '-' nor '.', and not null";
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", "two words"),
                      "'two words' cannot name a backend -- " + std::string{kRule});
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", "a:b"),
                      "'a:b' cannot name a backend -- " + std::string{kRule});
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", "qwen@home"),
                      "'qwen@home' cannot name a backend -- " + std::string{kRule});
    CHECK_THROWS_WITH(rename_backend(kRenameYaml, "gemma", ".hidden"),
                      "'.hidden' cannot name a backend -- " + std::string{kRule});
    // A change of case is the backend's own name: allowed.
    CHECK_NOTHROW(rename_backend(kRenameYaml, "gemma", "Gemma"));
}

// --- editing a backend in place (46b) ---------------------------------------

namespace {

constexpr std::string_view kFieldsYaml = R"(backends:
  # The local model.
  gemma:
    type: llamacpp   # in process
    model_path: /models/gemma.gguf   # 12B
    mmproj_path: /models/gemma-mmproj.gguf
  claude:
    type: anthropic
    model: claude-sonnet-5   # the default model
models:
  default: gemma
)";

}  // namespace

TEST_CASE("set_backend_field touches exactly its field: in place, inserted, or cleared",
          "[config_edit][set-backend]") {
    using apogee::harness::set_backend_field;
    // In place: the value token, the comment after it kept.
    std::string expected{kFieldsYaml};
    expected.replace(expected.find("model: claude-sonnet-5"), 22, "model: claude-opus-5-5");
    CHECK(set_backend_field(kFieldsYaml, "claude", "model", "claude-opus-5-5") == expected);
    // A field the entry lacks: after `type:`.
    expected = kFieldsYaml;
    expected.insert(expected.find("    model_path: /models/gemma.gguf"),
                    "    context_size: 32768\n");
    CHECK(set_backend_field(kFieldsYaml, "gemma", "context_size", "32768") == expected);
    // Cleared: its line, and nothing else.
    expected = kFieldsYaml;
    expected.erase(expected.find("    mmproj_path: /models/gemma-mmproj.gguf\n"), 43);
    CHECK(set_backend_field(kFieldsYaml, "gemma", "mmproj_path", "") == expected);
    // A field it lacks, cleared: the file as it was.
    CHECK(set_backend_field(kFieldsYaml, "claude", "seed", "") == kFieldsYaml);
    // Composed: each its own span.
    const std::string both = set_backend_field(
        set_backend_field(kFieldsYaml, "claude", "temperature", "0.2"), "claude", "model", "x");
    const apogee::harness::Config config = apogee::harness::parse_config(both, "edited");
    CHECK(config.find_backend("claude")->model == "x");
    CHECK(config.find_backend("claude")->temperature == 0.2);
    CHECK(config.find_backend("gemma")->mmproj_path == "/models/gemma-mmproj.gguf");
}

TEST_CASE("set_backend_field refuses a key, a field no backend has, and a cleared type",
          "[config_edit][set-backend]") {
    using apogee::harness::set_backend_field;
    CHECK_THROWS_WITH(set_backend_field(kFieldsYaml, "claude", "api_key", "sk-x"),
                      "a key is never taken on the command line -- store it with 'apogee auth "
                      "add' (asked with echo off, or --stdin, or --from-env)");
    CHECK_THROWS_WITH(set_backend_field(kFieldsYaml, "claude", "colour", "x"),
                      "'colour' is no field of a backend");
    CHECK_THROWS_WITH(set_backend_field(kFieldsYaml, "claude", "type", ""),
                      "a backend's type cannot be cleared -- name the type it is");
    CHECK_THROWS_WITH(set_backend_field(kFieldsYaml, "nope", "model", "x"),
                      "backend 'nope' not found in config");
}

TEST_CASE("the editable fields are the loader's, and every one add-backend writes",
          "[config_edit][set-backend][enumeration]") {
    // 46b's tie, both ways. Each editable field set through the transform is
    // read back by the loader -- a name it does not know would leave the
    // entry as it was -- and every field add-backend's writer writes is
    // editable, so a field the writer learns fails here until it has a flag.
    constexpr std::string_view kBase = "backends:\n  all:\n    type: llamacpp\n";
    const std::map<std::string_view, std::string> samples{{"type", "mlx"},
                                                          {"model", "m"},
                                                          {"model_path", "/m.gguf"},
                                                          {"mmproj_path", "/p.gguf"},
                                                          {"embedding_model", "e"},
                                                          {"system_prompt", "s"},
                                                          {"context_size", "4096"},
                                                          {"cache_type", "q4_0"},
                                                          {"max_tokens", "64"},
                                                          {"temperature", "0.5"},
                                                          {"top_p", "0.9"},
                                                          {"top_k", "40"},
                                                          {"min_p", "0.05"},
                                                          {"repeat_penalty", "1.1"},
                                                          {"presence_penalty", "0.2"},
                                                          {"seed", "7"},
                                                          {"thinking", "off"},
                                                          {"thinking_budget", "128"},
                                                          {"binary", "/bin/x"},
                                                          {"mode", "m"},
                                                          {"host", "h"},
                                                          {"idle_unload_seconds", "30"},
                                                          {"server_tools", "web_search"},
                                                          {"mcp_serve", "complete"},
                                                          {"adapters", "/a.gguf"},
                                                          {"draft", "/drafter"},
                                                          {"draft_block_size", "4"}};
    // Only a vendor API runs server-side tools (47a): that field is set on an
    // anthropic entry, alone, since the chain below turns its entry local.
    constexpr std::string_view kVendorBase = "backends:\n  all:\n    type: anthropic\n";
    // Only an mlx entry drafts (53i): its two fields set on one.
    constexpr std::string_view kMlxBase = "backends:\n  all:\n    type: mlx\n";
    const std::string base_json = [&]() {
        const apogee::harness::Config base = apogee::harness::parse_config(kBase, "base");
        return apogee::harness::append_backend("", "all", *base.find_backend("all"), false);
    }();
    std::string all{kBase};
    for (const std::string_view field : apogee::harness::backend_editable_fields()) {
        INFO(field);
        REQUIRE(samples.contains(field));  // every field has a sample here
        if (field == "server_tools") {
            const apogee::harness::Config config = apogee::harness::parse_config(
                apogee::harness::set_backend_field(kVendorBase, "all", field, samples.at(field)),
                "edited");
            CHECK(config.find_backend("all")->server_tools ==
                  std::vector<std::string>{"web_search"});
            continue;
        }
        if (field == "draft" || field == "draft_block_size") {
            const apogee::harness::Config config = apogee::harness::parse_config(
                apogee::harness::set_backend_field(kMlxBase, "all", field, samples.at(field)),
                "edited");
            CHECK((config.find_backend("all")->draft == "/drafter" ||
                   config.find_backend("all")->draft_block_size == 4));
            all = apogee::harness::set_backend_field(all, "all", field, samples.at(field));
            continue;
        }
        const std::string edited =
            apogee::harness::set_backend_field(kBase, "all", field, samples.at(field));
        const apogee::harness::Config config = apogee::harness::parse_config(edited, "edited");
        // The writer shows what the loader read -- but for the six it never
        // writes, checked by name below.
        const bool hand_written = field == "binary" || field == "mode" || field == "host" ||
                                  field == "idle_unload_seconds" || field == "adapters" ||
                                  field == "mcp_serve";
        const bool read = apogee::harness::append_backend("", "all", *config.find_backend("all"),
                                                          false) != base_json;
        CHECK((read || hand_written));
        all = apogee::harness::set_backend_field(all, "all", field, samples.at(field));
    }
    CHECK(samples.size() == apogee::harness::backend_editable_fields().size());
    // Every field the writer writes is one of them.
    const apogee::harness::Config config = apogee::harness::parse_config(all, "all");
    const std::string written =
        apogee::harness::append_backend("", "all", *config.find_backend("all"), false);
    std::istringstream lines{written};
    const std::vector<std::string_view> editable = apogee::harness::backend_editable_fields();
    std::size_t seen = 0;
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with("    ") && line.find(':') != std::string::npos) {
            const std::string key = line.substr(4, line.find(':') - 4);
            INFO(key);
            CHECK(std::ranges::find(editable, key) != editable.end());
            ++seen;
        }
    }
    CHECK(seen == 18);  // the writer's fields: all but the eight written by hand
    // The eight the writer never writes are read by the loader all the same.
    const apogee::harness::BackendConfig& entry = *config.find_backend("all");
    CHECK(entry.binary == "/bin/x");
    CHECK(entry.mode == "m");
    CHECK(entry.host == "h");
    CHECK(entry.idle_unload_seconds == 30);
    CHECK(entry.adapters ==
          std::vector<apogee::harness::AdapterConfig>{{.path = "/a.gguf", .scale = 1.0}});
    CHECK(entry.draft == "/drafter");
    CHECK(entry.draft_block_size == 4);
    CHECK(entry.mcp_serve == std::vector<std::string>{"complete"});
}

TEST_CASE("config set moves each allow-listed scalar in place, comments kept, as its kind reads",
          "[config_edit][config-set]") {
    // 52e: the declared table's keys, each a golden round trip on a
    // comment-dense config -- set, re-parsed to the value, set again in place.
    using apogee::harness::set_scalar;
    const std::string start =
        std::string{kCommented} +
        "\n# Recall across chats.\nmemory:\n  recall: true   # on by default\n";
    const std::string off = set_scalar(start, "memory.recall", "false");
    CHECK(off == std::string{kCommented} +
                     "\n# Recall across chats.\nmemory:\n  recall: false   # on by default\n");
    CHECK_FALSE(apogee::harness::parse_config(off, "<test>").memory.recall);

    // A nested key: its block added at the end of its section, then moved in
    // place; its peers written beside it.
    std::string search = set_scalar(kCommented, "tools.search.url", "http://127.0.0.1:8888");
    search = set_scalar(search, "tools.search.provider", "searxng");
    search = set_scalar(search, "tools.search.results", "7");
    const apogee::harness::Config read = apogee::harness::parse_config(search, "<test>");
    CHECK(read.tools.search.url == "http://127.0.0.1:8888");
    CHECK(read.tools.search.provider == "searxng");
    CHECK(read.tools.search.results == 7);
    const std::string moved = set_scalar(search, "tools.search.url", "http://10.0.0.2:8080");
    CHECK(apogee::harness::parse_config(moved, "<test>").tools.search.url ==
          "http://10.0.0.2:8080");
    // One token, in place: the rest byte for byte.
    std::string expected = search;
    expected.replace(expected.find("http://127.0.0.1:8888"), 21, "http://10.0.0.2:8080");
    CHECK(moved == expected);

    // Each kind as its loader reads it: booleans and numbers bare, words and
    // text quoted when they must be.
    for (const auto& [key, value] :
         std::vector<std::pair<std::string, std::string>>{{"knowledge.auto_capture", "true"},
                                                          {"ui.markdown", "false"},
                                                          {"ui.thinking_display", "raw"},
                                                          {"attachments.graph", "off"}}) {
        INFO(key);
        require_parses(set_scalar(kCommented, key, value));
    }
    const apogee::harness::Config kinds = apogee::harness::parse_config(
        set_scalar(set_scalar(kCommented, "knowledge.auto_capture", "true"), "ui.thinking_display",
                   "raw"),
        "<test>");
    CHECK(kinds.knowledge.auto_capture);
    CHECK(kinds.ui.thinking_display == apogee::harness::ThinkingDisplay::Raw);

    // M19: the lasting choice of the training environment's Python has a
    // command, and the loader reads what it wrote.
    CHECK(apogee::harness::parse_config(
              set_scalar(kCommented, "training.python", "/opt/homebrew/bin/python3.12"), "<test>")
              .training.python == "/opt/homebrew/bin/python3.12");
}

TEST_CASE("config set refuses every key outside its table, naming the right door",
          "[config_edit][config-set]") {
    using apogee::harness::scalar_refusal;
    using apogee::harness::set_scalar;
    // Secret-shaped, wherever it sits: never into the file.
    for (const std::string key : {"backends.claude.api_key", "tools.search.token", "x.password"}) {
        INFO(key);
        CHECK_THAT(scalar_refusal(key), ContainsSubstring("apogee auth add"));
        CHECK_THROWS_AS(set_scalar(kCommented, key, "s"), ConfigEditError);
    }
    // A key a typed verb owns names that verb.
    CHECK_THAT(scalar_refusal("models.default"), ContainsSubstring("config set-default"));
    CHECK_THAT(scalar_refusal("backends.local.model"), ContainsSubstring("config set-backend"));
    CHECK_THAT(scalar_refusal("permissions.write_file"),
               ContainsSubstring("config set-permission"));
    CHECK_THAT(scalar_refusal("tools.allowed_hosts"), ContainsSubstring("add-allowed-host"));
    // Anything else quotes the table.
    CHECK_THAT(scalar_refusal("color"),
               ContainsSubstring("it moves: memory.recall, knowledge.auto_capture"));
    CHECK(scalar_refusal("memory.recall").empty());
    // A value the key does not take, named.
    CHECK_THROWS_WITH(set_scalar(kCommented, "memory.recall", "yes"),
                      "memory.recall takes true or false, not 'yes'");
    CHECK_THROWS_WITH(set_scalar(kCommented, "ui.thinking_display", "loud"),
                      "ui.thinking_display takes summary raw, not 'loud'");
    CHECK_THROWS_WITH(set_scalar(kCommented, "tools.search.results", "21"),
                      "tools.search.results takes a whole number from 1 to 20, not '21'");
    CHECK_THROWS_AS(set_scalar(kCommented, "tools.search.results", "7x"), ConfigEditError);
    // A block written inline is not misread.
    CHECK_THROWS_AS(set_scalar("tools:\n  search: {url: x}\n", "tools.search.url", "y"),
                    ConfigEditError);
    // Every table key is settable; the table has no duplicates.
    std::set<std::string_view> seen;
    for (const apogee::harness::SettableScalar& scalar : apogee::harness::settable_scalars()) {
        CHECK(seen.insert(scalar.key).second);
        CHECK(scalar_refusal(scalar.key).empty());
    }
}

// --- 55f: an edit writes only what it means ---------------------------------
//
// Four ways an accepted edit corrupted or stranded a config (the 2026-10-22
// audit): a block list's items left behind to fold into the field above, a
// `#` inside a value read as a comment, a name added that no later edit could
// find, and two edits at once each writing over the other.

namespace {

/// An entry whose `server_tools:` is a block list -- a shape the loader
/// accepts -- with comments inside and around it.
constexpr std::string_view kBlockList = R"(backends:
  claude:
    type: anthropic   # the vendor's own API
    model: claude-sonnet-5
    api_key: sk-test-literal
    server_tools:   # the vendor's tools
      # its search
      - web_search
      - code_execution
    # the cap
    max_tokens: 1024
  other:
    type: mock
)";

/// `text` with the run from `from`'s first byte to the end of `to`'s line
/// replaced by `with`.
[[nodiscard]] std::string spliced(std::string_view text, std::string_view from, std::string_view to,
                                  std::string_view with) {
    std::string out{text};
    const std::size_t start = out.find(from);
    REQUIRE(start != std::string::npos);
    const std::size_t stop = out.find(to, start);
    REQUIRE(stop != std::string::npos);
    out.replace(start, stop + to.size() - start, with);
    return out;
}

/// One backend entry, `e`, holding `fields`.
[[nodiscard]] std::string with_backend_lines(std::string_view fields) {
    return "backends:\n  e:\n" + std::string{fields};
}

/// A config file of its own, under a directory named with a random draw.
struct EditFixture {
    apogee::testing::TempDir dir{"edit-55f-" + std::to_string(std::random_device{}())};
    std::filesystem::path path = dir.path() / "config.json";

    [[nodiscard]] std::string bytes() const {
        std::ifstream in{path, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
};

}  // namespace

TEST_CASE("a block-list field cleared or replaced takes its own lines and no neighbour's",
          "[config_edit][block-list][golden]") {
    using apogee::harness::parse_config;
    using apogee::harness::set_backend_field;
    // Cleared: the key and every item, the comment among them with it; the
    // comment after the list is the next field's and stays.
    const std::string cleared = set_backend_field(kBlockList, "claude", "server_tools", "");
    CHECK(cleared == spliced(kBlockList, "    server_tools:", "      - code_execution\n", ""));
    const apogee::harness::Config after = parse_config(cleared, "<cleared>");
    CHECK(after.find_backend("claude")->api_key == "sk-test-literal");
    CHECK(after.find_backend("claude")->server_tools.empty());
    CHECK(after.find_backend("claude")->max_tokens == 1024);

    // Replaced: one flow list on the key's line, its comment kept where it
    // stood; nothing of the old items left.
    const std::string replaced =
        set_backend_field(kBlockList, "claude", "server_tools", "web_search,code_execution");
    CHECK(replaced ==
          spliced(kBlockList, "    server_tools:", "      - code_execution\n",
                  "    server_tools: [web_search, code_execution]   # the vendor's tools\n"));
    const apogee::harness::Config again = parse_config(replaced, "<replaced>");
    CHECK(again.find_backend("claude")->server_tools ==
          std::vector<std::string>{"web_search", "code_execution"});
    CHECK(again.find_backend("claude")->api_key == "sk-test-literal");

    // A list written at the key's own indent, and one carried over two lines.
    const std::string flat = with_backend_lines(
        "    type: anthropic\n    server_tools:\n    - web_search\n    max_tokens: 9\n");
    CHECK(set_backend_field(flat, "e", "server_tools", "") ==
          with_backend_lines("    type: anthropic\n    max_tokens: 9\n"));
    CHECK(set_backend_field(flat, "e", "server_tools", "code_execution") ==
          with_backend_lines(
              "    type: anthropic\n    server_tools: [code_execution]\n    max_tokens: 9\n"));
    const std::string carried = with_backend_lines(
        "    type: anthropic\n    server_tools: [web_search,\n      code_execution]\n"
        "    max_tokens: 9\n");
    CHECK(set_backend_field(carried, "e", "server_tools", "web_search") ==
          with_backend_lines(
              "    type: anthropic\n    server_tools: [web_search]\n    max_tokens: 9\n"));

    // The file's line endings and its unterminated end, kept.
    const std::string crlf =
        "backends:\r\n  e:\r\n    type: anthropic\r\n    server_tools:\r\n      - web_search\r\n";
    CHECK(set_backend_field(crlf, "e", "server_tools", "") ==
          "backends:\r\n  e:\r\n    type: anthropic\r\n");
    const std::string open =
        "backends:\n  e:\n    type: anthropic\n    server_tools:\n      - web_search";
    CHECK(set_backend_field(open, "e", "server_tools", "") ==
          "backends:\n  e:\n    type: anthropic\n");
    require_parses(set_backend_field(open, "e", "server_tools", "code_execution"));
}

TEST_CASE("a multi-line scalar replaced or cleared takes every line of its text",
          "[config_edit][block-list][golden]") {
    // The same fold through a block scalar: the new value on the key's line
    // and the old text's lines left under it read as one run-on value.
    using apogee::harness::set_backend_field;
    const std::string block = with_backend_lines(
        "    type: mock\n    system_prompt: |   # house style\n      You are terse.\n"
        "      Answer in one line.\n    max_tokens: 64\n");
    const std::string set = set_backend_field(block, "e", "system_prompt", "Be kind.");
    CHECK(set == with_backend_lines("    type: mock\n    system_prompt: Be kind.   # house style\n"
                                    "    max_tokens: 64\n"));
    CHECK(apogee::harness::parse_config(set, "<set>").find_backend("e")->system_prompt ==
          "Be kind.");
    CHECK(set_backend_field(block, "e", "system_prompt", "") ==
          with_backend_lines("    type: mock\n    max_tokens: 64\n"));
    // A field the entry lacks lands under the one it follows -- below that
    // one's own lines, never between them.
    const std::string drafted =
        set_backend_field(with_backend_lines("    type: mlx\n    model_path: |\n      /models/m\n"),
                          "e", "draft", "/models/d");
    CHECK(drafted ==
          with_backend_lines(
              "    type: mlx\n    model_path: |\n      /models/m\n    draft: /models/d\n"));
}

TEST_CASE("a hash inside a value survives every edit that moves its line",
          "[config_edit][hash][golden]") {
    using apogee::harness::parse_config;
    using apogee::harness::set_backend_field;
    using apogee::harness::set_scalar;
    constexpr std::string_view kHashes = R"(tools:
  search:
    provider: searxng
    url: "http://127.0.0.1:8888/#frag"  # my instance
training:
  python: "/opt/py #3/bin/python3"  # the one I built
backends:
  local:
    type: llamacpp
    model_path: "/models/run #1/a.gguf"  # first run
    mmproj_path: /models/p.gguf#2   # a hash after a non-space
)";
    // A quoted fragment: only the value moves; the comment, and its spacing,
    // stay exactly where they were.
    const std::string url = set_scalar(kHashes, "tools.search.url", "http://10.0.0.2:8080");
    std::string expected{kHashes};
    expected.replace(expected.find("\"http://127.0.0.1:8888/#frag\""), 29,
                     "\"http://10.0.0.2:8080\"");
    CHECK(url == expected);
    // Durable: a later set starts from a clean line, and a `#` written in is
    // the value's too.
    const std::string fragment = set_scalar(url, "tools.search.url", "http://10.0.0.3/#top");
    CHECK(parse_config(fragment, "<test>").tools.search.url == "http://10.0.0.3/#top");
    CHECK_THAT(fragment, ContainsSubstring("url: \"http://10.0.0.3/#top\"  # my instance\n"));
    CHECK(set_scalar(fragment, "tools.search.url", "http://10.0.0.2:8080") == url);
    // An unquoted `#` after a non-space is the value's too.
    const std::string plain =
        spliced(kHashes, "    url:", "\n", "    url: http://127.0.0.1:8888/#frag  # my instance\n");
    CHECK(set_scalar(plain, "tools.search.url", "http://10.0.0.2:8080") == url);

    // A section's own scalar (`set_section_rendered`).
    const std::string python = set_scalar(kHashes, "training.python", "/usr/bin/python3");
    CHECK(python ==
          spliced(kHashes, "  python:", "\n", "  python: /usr/bin/python3  # the one I built\n"));
    // A backend's field (`set_backend_field`), quoted and unquoted.
    const std::string weights = set_backend_field(kHashes, "local", "model_path", "/models/b.gguf");
    CHECK(weights == spliced(kHashes, "    model_path:", "\n",
                             "    model_path: /models/b.gguf  # first run\n"));
    const std::string projector =
        set_backend_field(kHashes, "local", "mmproj_path", "/models/q.gguf");
    CHECK(projector == spliced(kHashes, "    mmproj_path:", "\n",
                               "    mmproj_path: /models/q.gguf   # a hash after a non-space\n"));
    for (const std::string& edited : {url, python, weights, projector}) {
        require_parses(edited);
    }
    // A quote inside a comment opens nothing.
    const std::string server =
        "mcp_servers:\n  fs:\n    command: npx\n    enabled: true  # don't \"quote\" me\n";
    CHECK(set_mcp_server_enabled(server, "fs", false) ==
          "mcp_servers:\n  fs:\n    command: npx\n    enabled: false  # don't \"quote\" me\n");
}

TEST_CASE("a new section scalar lands after its neighbour's whole block, never inside it",
          "[config_edit][block-list]") {
    // What the JSONC bridge writes for a nested object: the new key used to
    // go between `cycle:` and its fields, and the edit failed to parse.
    using apogee::harness::set_scalar;
    const std::string nested =
        "training:\n  cycle:\n    backend: x\nbackends:\n  x:\n"
        "    type: mock\n";
    const std::string set = set_scalar(nested, "training.python", "/usr/bin/python3");
    CHECK(set ==
          "training:\n  cycle:\n    backend: x\n  python: /usr/bin/python3\nbackends:\n"
          "  x:\n    type: mock\n");
    CHECK(apogee::harness::parse_config(set, "<test>").training.python == "/usr/bin/python3");
}

TEST_CASE("an entry name both formats cannot edit is refused at add, naming the rule",
          "[config_edit][names]") {
    constexpr std::string_view kRule =
        "a name is letters, digits, '_', '-', '.' and '/', first "
        "neither '-' nor '.', and not null";
    CHECK_THROWS_WITH(
        append_backend(kCommented, "qwen@home", anthropic_backend(), false),
        "backend name 'qwen@home' is not one both config formats can edit -- " +
            std::string{kRule} +
            "; a backend already named otherwise is reached by 'apogee config rename-backend'");
    for (const std::string_view name :
         {"two words", "a:b", "x#y", "-x", ".x", "null", "NULL", "~", "a\"b", "é", "a,b", "[x]"}) {
        INFO(name);
        CHECK_THROWS_AS(append_backend(kCommented, name, anthropic_backend(), false),
                        ConfigEditError);
    }
    // Every section's add holds the same rule; the rename hint is a backend's.
    CHECK_THROWS_WITH(
        append_agent("", "x@y", apogee::harness::AgentConfig{}, false),
        "agent name 'x@y' is not one both config formats can edit -- " + std::string{kRule});
    CHECK_THROWS_AS(append_embedding("", ".notes", EmbeddingConfig{}, false), ConfigEditError);

    // Every name it accepts is edited and deleted, the file back to its bytes.
    for (const std::string_view name :
         {"qwen-home", "qwen_home", "Qwen3.5-9B-Q4KM", "org/model", "_x", "9b", "true", "a.b"}) {
        INFO(name);
        const std::string added = append_backend(kCommented, name, anthropic_backend(), false);
        const std::string edited =
            apogee::harness::set_backend_field(added, name, "max_tokens", "512");
        CHECK(apogee::harness::parse_config(edited, "<test>").find_backend(name)->max_tokens ==
              512);
        CHECK(apogee::harness::set_backend_field(edited, name, "max_tokens", "") == added);
        CHECK(delete_backend(added, name) == kCommented);
        // And renamed to: the renamed entry is one the editor still finds.
        const std::string renamed = apogee::harness::rename_backend(added, name, "renamed");
        CHECK(apogee::harness::rename_backend(renamed, "renamed", name) == added);
    }
}

TEST_CASE("a hand-written odd name in a YAML config is edited, and renamed into the rule",
          "[config_edit][names]") {
    // Legal YAML, written by hand: the YAML editor finds it by its text, and
    // rename-backend is the way to a name every format edits.
    const std::string odd = "backends:\n  qwen@home:\n    type: openai\n";
    const std::string set = apogee::harness::set_backend_field(odd, "qwen@home", "max_tokens", "9");
    CHECK(set == "backends:\n  qwen@home:\n    type: openai\n    max_tokens: 9\n");
    CHECK(apogee::harness::rename_backend(odd, "qwen@home", "qwen-home") ==
          "backends:\n  qwen-home:\n    type: openai\n");
}

TEST_CASE("concurrent config edits all land, each under the lock beside the file",
          "[config_edit][lock][concurrent]") {
    // The completeness pass's probe (55f), over the real function: several
    // editors at once, each reading, editing and renaming into place. Without
    // the lock each read the file before another wrote it, and the last
    // rename kept only its own entry.
    for (const bool jsonc : {true, false}) {
        INFO((jsonc ? "jsonc" : "yaml"));
        const EditFixture fixture;
        apogee::harness::write_file_atomically(
            fixture.path, jsonc ? std::string{"// mine\n{\n  \"backends\": {} // none yet\n}\n"}
                                : std::string{kCommented});
        constexpr int kEditors = 12;
        std::vector<std::thread> editors;
        editors.reserve(kEditors);
        std::vector<std::string> failures(kEditors);
        for (int i = 0; i < kEditors; ++i) {
            editors.emplace_back([&fixture, &failures, i] {
                try {
                    BackendConfig backend;
                    backend.type = BackendType::Mock;
                    apogee::harness::edit_config_file(
                        fixture.path, [i, &backend](std::string_view content) {
                            return append_backend(content, "b" + std::to_string(i), backend, false);
                        });
                } catch (const std::exception& e) {
                    failures[static_cast<std::size_t>(i)] = e.what();
                }
            });
        }
        for (std::thread& editor : editors) {
            editor.join();
        }
        for (const std::string& failure : failures) {
            CHECK(failure.empty());
        }
        const apogee::harness::Config config =
            apogee::harness::parse_config(fixture.bytes(), "<test>");
        for (int i = 0; i < kEditors; ++i) {
            INFO(i);
            CHECK(config.find_backend("b" + std::to_string(i)) != nullptr);
        }
        CHECK_THAT(fixture.bytes(),
                   ContainsSubstring(jsonc ? "// mine" : "# Uses the Messages API"));
        // The lock file stays beside the config; no temporary file is left.
        std::filesystem::path lock = fixture.path;
        lock += ".lock";
        CHECK(std::filesystem::exists(lock));
        std::size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator{fixture.dir.path()}) {
            (void)entry;
            ++files;
        }
        CHECK(files == 2);
    }
}

TEST_CASE("an edit that cannot take the lock in time is refused, said, and writes nothing",
          "[config_edit][lock]") {
    const EditFixture fixture;
    apogee::harness::write_file_atomically(fixture.path, kCommented);
    const apogee::platform::FileLock held = apogee::harness::lock_config_file(fixture.path);
    bool transformed = false;
    const auto start = std::chrono::steady_clock::now();
    try {
        apogee::harness::edit_config_file(
            fixture.path,
            [&transformed](std::string_view content) {
                transformed = true;
                return delete_backend(content, "local");
            },
            std::chrono::milliseconds{150});
        FAIL("the edit went through a held lock");
    } catch (const ConfigEditError& e) {
        const std::string said = e.what();
        CHECK_THAT(said, ContainsSubstring("another edit of " + fixture.path.string() +
                                           " is still running"));
        CHECK_THAT(said, ContainsSubstring("config.json.lock stayed locked for all of the 150 ms"));
        CHECK_THAT(said, ContainsSubstring("nothing was changed"));
    }
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds{100});
    CHECK_FALSE(transformed);
    CHECK(fixture.bytes() == kCommented);
}

TEST_CASE("an edit of a config that is not there says so, and leaves no lock behind",
          "[config_edit][lock]") {
    const EditFixture fixture;
    CHECK_THROWS_WITH(
        apogee::harness::edit_config_file(
            fixture.path, [](std::string_view content) { return std::string{content}; }),
        ContainsSubstring("cannot open config file"));
    CHECK_THROWS_AS(
        apogee::harness::edit_config_file(
            fixture.path, [](std::string_view content) { return std::string{content}; }),
        apogee::harness::ConfigError);
    CHECK(std::filesystem::is_empty(fixture.dir.path()));
}

// --- 57e: the editor writes what it says it did, and keeps what it does not edit

namespace {

/// How many times each top-level key is written in `text` -- a key written
/// twice is a shadow the loader never reads.
std::map<std::string, int> top_level_keys(std::string_view text) {
    std::map<std::string, int> keys;
    std::istringstream in{std::string{text}};
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == ' ' || line[0] == '#' || line[0] == '\t') {
            continue;
        }
        ++keys[line.substr(0, line.find(':'))];
    }
    return keys;
}

/// The four sandbox shapes the sweep found shadowed (57e): a header with a
/// comment, and three sections written on their header's own line.
constexpr std::string_view kCommentHeader =
    "backends:   # my models\n  a:\n    type: mock\n    model: m\n";
constexpr std::string_view kFlowModels =
    "backends:\n  a:\n    type: mock\nmodels: {default: a, default_utility: a}\n";
constexpr std::string_view kFlowMemory =
    "backends:\n  a:\n    type: mock\nmemory: {recall: true}\n";
constexpr std::string_view kFlowTools =
    "backends:\n  a:\n    type: mock\ntools: {allowed_hosts: [a.com]}\n";

BackendConfig mock_entry() {
    BackendConfig backend;
    backend.type = BackendType::Mock;
    backend.model = "m";
    return backend;
}

std::string bytes_of(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::filesystem::perms mode_of(const std::filesystem::path& path) {
    return std::filesystem::status(path).permissions() & std::filesystem::perms::mask;
}

constexpr std::filesystem::perms kOwnerOnly =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;

}  // namespace

TEST_CASE("a section header with a comment is edited under it, the comment kept",
          "[config_edit][57e][headers][golden]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::parse_config;
    using apogee::harness::section_entry_names;
    // What `config add-backend c` printed success for while the loader read
    // only `a`: the entry lands under the header, its comment where it was.
    const std::string added = append_backend(kCommentHeader, "c", mock_entry(), false);
    CHECK(added == std::string{kCommentHeader} + "\n  c:\n    type: mock\n    model: m\n");
    CHECK(section_entry_names(added, "backends") == std::vector<std::string>{"a", "c"});
    CHECK(parse_config(added, "<test>").backend_names() == std::vector<std::string>{"a", "c"});
    CHECK(delete_backend(added, "c") == kCommentHeader);
    // Deleted, the header and its comment stay; nothing says "not found".
    CHECK(delete_backend(kCommentHeader, "a") == "backends:   # my models\n");
    // A field set, a role set: in place, under the one header.
    CHECK(apogee::harness::set_backend_field(kCommentHeader, "a", "model", "n") ==
          "backends:   # my models\n  a:\n    type: mock\n    model: n\n");
    const std::string roled =
        set_models_role("models:  # who answers\n  default: a\n", "default", "b");
    CHECK(roled == "models:  # who answers\n  default: b\n");
    CHECK(add_allowed_host("tools:   # web\n  allowed_hosts: [a.com]\n", "b.com") ==
          "tools:   # web\n  allowed_hosts: [a.com, b.com]\n");
    CHECK(apogee::harness::set_scalar("memory:  # recall\n  recall: true\n", "memory.recall",
                                      "false") == "memory:  # recall\n  recall: false\n");
}

TEST_CASE("a section written on its header's line is refused by every editor, never shadowed",
          "[config_edit][57e][headers]") {
    using apogee::harness::add_allowed_host;
    using apogee::harness::parse_config;
    using apogee::harness::remove_allowed_host;
    using apogee::harness::set_scalar;
    using Verb = std::function<std::string(std::string_view)>;
    const std::vector<std::pair<std::string_view, Verb>> verbs{
        {"add-backend",
         [](std::string_view c) { return append_backend(c, "c", mock_entry(), false); }},
        {"delete-backend", [](std::string_view c) { return delete_backend(c, "a"); }},
        {"set memory.recall",
         [](std::string_view c) { return set_scalar(c, "memory.recall", "false"); }},
        {"add-allowed-host", [](std::string_view c) { return add_allowed_host(c, "b.com"); }},
        {"set-default", [](std::string_view c) { return set_models_role(c, "default", ""); }},
        {"remove-allowed-host", [](std::string_view c) { return remove_allowed_host(c, "a.com"); }},
        {"set tools.search.url",
         [](std::string_view c) { return set_scalar(c, "tools.search.url", "http://x"); }},
    };
    for (const std::string_view shape : {kCommentHeader, kFlowModels, kFlowMemory, kFlowTools}) {
        for (const auto& [name, verb] : verbs) {
            INFO(std::string{shape} + "  <- " + std::string{name});
            try {
                const std::string edited = verb(shape);
                // A landed edit: parsed, and no key written twice.
                REQUIRE_NOTHROW(parse_config(edited, "<edited>"));
                for (const auto& [key, count] : top_level_keys(edited)) {
                    INFO(key);
                    CHECK(count == 1);
                }
            } catch (const ConfigEditError& e) {
                const std::string said = e.what();
                // A refusal says what it cannot edit, or is the verb's own
                // honest one (no such host in an empty list).
                const bool shape_refusal =
                    said.find("line holds more than its key") != std::string::npos;
                CHECK((shape_refusal ||
                       said.find("is not in tools.allowed_hosts") != std::string::npos));
                if (shape_refusal) {
                    CHECK_THAT(said, ContainsSubstring("'apogee config migrate'"));
                }
            }
        }
    }
    // Each flow shape's own verb, refused by name.
    CHECK_THROWS_WITH(set_models_role(kFlowModels, "default", ""),
                      "the models: line holds more than its key -- the editor edits the block "
                      "form, 'models:' alone with its fields under it; rewrite it so, or carry "
                      "the file to JSONC with 'apogee config migrate', whose every form the "
                      "editor edits");
    CHECK_THROWS_WITH(set_scalar(kFlowMemory, "memory.recall", "false"),
                      ContainsSubstring("the memory: line holds more than its key"));
    CHECK_THROWS_WITH(add_allowed_host(kFlowTools, "b.com"),
                      ContainsSubstring("the tools: line holds more than its key"));
    CHECK_THROWS_WITH(remove_allowed_host(kFlowTools, "a.com"),
                      ContainsSubstring("the tools: line holds more than its key"));
    CHECK_THROWS_WITH(append_backend("backends: {}\n", "c", mock_entry(), false),
                      ContainsSubstring("the backends: line holds more than its key"));
    CHECK_THROWS_WITH(apogee::harness::rename_backend("backends: {a: {type: mock}}\n", "a", "b"),
                      ContainsSubstring("the backends: line holds more than its key"));

    // Through the file layer: refused, the file's bytes as they were.
    const EditFixture fixture;
    apogee::harness::write_file_atomically(fixture.path, kFlowMemory);
    CHECK_THROWS_AS(apogee::harness::edit_config_file(
                        fixture.path,
                        [](std::string_view c) { return set_scalar(c, "memory.recall", "false"); }),
                    ConfigEditError);
    CHECK(fixture.bytes() == kFlowMemory);
}

TEST_CASE("an entry line with a comment is the entry; one holding its value is refused by name",
          "[config_edit][57e][headers]") {
    using apogee::harness::section_entry_names;
    using apogee::harness::set_backend_field;
    constexpr std::string_view kTailed = "backends:\n  a:   # mine\n    type: mock\n";
    CHECK(section_entry_names(kTailed, "backends") == std::vector<std::string>{"a"});
    CHECK(set_backend_field(kTailed, "a", "model", "x") ==
          "backends:\n  a:   # mine\n    type: mock\n    model: x\n");
    CHECK_THROWS_WITH(append_backend(kTailed, "a", mock_entry(), false),
                      "backend 'a' already exists; pass --force to replace it");
    CHECK(delete_backend(kTailed, "a") == "backends:\n");

    // `  a: {type: mock}`: neither "not found" nor a second `a:` beside it.
    constexpr std::string_view kInline = "backends:\n  a: {type: mock}\n";
    const std::string refusal =
        "the backends.a: line holds more than its key -- the editor edits the block form, 'a:' "
        "alone with its fields under it";
    CHECK_THROWS_WITH(set_backend_field(kInline, "a", "model", "x"), ContainsSubstring(refusal));
    CHECK_THROWS_WITH(set_backend_field(kInline, "A", "model", "x"),
                      ContainsSubstring("the backends.A: line holds more than its key"));
    CHECK_THROWS_WITH(delete_backend(kInline, "a"), ContainsSubstring(refusal));
    CHECK_THROWS_WITH(append_backend(kInline, "a", mock_entry(), true), ContainsSubstring(refusal));
    // A tab after the colon is the same line to YAML, and to the refusal.
    constexpr std::string_view kTabbed = "backends:\n  a:\t{type: mock, model: m}\n";
    CHECK_THROWS_WITH(set_backend_field(kTabbed, "a", "model", "z"), ContainsSubstring(refusal));
    CHECK_THROWS_WITH(delete_backend(kTabbed, "a"), ContainsSubstring(refusal));
    // Another name is another entry, added beside it.
    const std::string other = append_backend(kInline, "b", mock_entry(), false);
    CHECK(other == std::string{kInline} + "\n  b:\n    type: mock\n    model: m\n");
    require_parses(other);
}

TEST_CASE("a multi-line value is written on one line, escaped, and reads back exactly",
          "[config_edit][57e][multiline][golden]") {
    using apogee::harness::parse_config;
    using apogee::harness::set_backend_field;
    const std::string prompt = "You are terse.\nAnswer in French.\n\nBe kind.";
    const std::string set =
        set_backend_field(with_backend_lines("    type: mock\n"), "e", "system_prompt", prompt);
    CHECK(set == with_backend_lines("    type: mock\n    system_prompt: \"You are terse.\\nAnswer "
                                    "in French.\\n\\nBe kind.\"\n"));
    CHECK(parse_config(set, "<set>").find_backend("e")->system_prompt == prompt);
    // Replaced: the old text goes whole, and the file stays one an add can
    // follow.
    const std::string shorter = set_backend_field(set, "e", "system_prompt", "short");
    CHECK(shorter == with_backend_lines("    type: mock\n    system_prompt: short\n"));
    const std::string after = append_backend(shorter, "zz", mock_entry(), false);
    require_parses(after);
    // Added: the same bytes, the same read.
    BackendConfig backend = mock_entry();
    backend.system_prompt = prompt;
    const std::string added = append_backend("", "d", backend, false);
    CHECK_THAT(added,
               ContainsSubstring(
                   "    system_prompt: \"You are terse.\\nAnswer in French.\\n\\nBe kind.\"\n"));
    CHECK(parse_config(added, "<add>").find_backend("d")->system_prompt == prompt);
    // A tab, a carriage return and another control character, exactly.
    for (const std::string& text :
         {std::string{"a\tb\r\nc"}, std::string{"bell\x07"}, std::string{"q\"uo\\te\n"}}) {
        INFO(text);
        const std::string written =
            set_backend_field(with_backend_lines("    type: mock\n"), "e", "system_prompt", text);
        CHECK(written.find('\t') == std::string::npos);
        CHECK(std::count(written.begin(), written.end(), '\n') == 4);  // its four lines
        CHECK(parse_config(written, "<test>").find_backend("e")->system_prompt == text);
    }
}

TEST_CASE("yaml_scalar quotes text the bridge would read as a number, and only text",
          "[config_edit][57e][numbers]") {
    using apogee::harness::yaml_scalar;
    for (const std::string_view number : {"1.50", "1e3", "2.0", "-2", "0", "1E-3", "~"}) {
        INFO(number);
        CHECK(yaml_scalar(number) == "\"" + std::string{number} + "\"");
    }
    for (const std::string_view text : {"9b", "1.5.0", "v2", "gemma4-12b", "0x10", "1_000"}) {
        INFO(text);
        CHECK(yaml_scalar(text) == text);
    }
    CHECK(yaml_scalar("a\nb") == "\"a\\nb\"");
    // A number field stays the number it is; a text field the text.
    using apogee::harness::set_backend_field;
    const std::string base = with_backend_lines("    type: mock\n");
    CHECK(set_backend_field(base, "e", "max_tokens", "9") ==
          with_backend_lines("    type: mock\n    max_tokens: 9\n"));
    CHECK(set_backend_field(base, "e", "temperature", "0.5") ==
          with_backend_lines("    type: mock\n    temperature: 0.5\n"));
    CHECK(set_backend_field(base, "e", "model", "1.50") ==
          with_backend_lines("    type: mock\n    model: \"1.50\"\n"));
    // The text fields are fields a backend has.
    const std::vector<std::string_view> editable = apogee::harness::backend_editable_fields();
    for (const std::string_view field : apogee::harness::backend_text_fields()) {
        INFO(field);
        CHECK(std::ranges::find(editable, field) != editable.end());
    }
}

TEST_CASE("a value quoted with '' keeps its own text and the comment after it",
          "[config_edit][57e][hash][golden]") {
    using apogee::harness::parse_config;
    using apogee::harness::set_backend_field;
    const std::string quoted = with_backend_lines("    type: mock\n    model: 'it''s #1'  # c1\n");
    CHECK(parse_config(quoted, "<test>").find_backend("e")->model == "it's #1");
    const std::string set = set_backend_field(quoted, "e", "model", "NEWa");
    CHECK(set == with_backend_lines("    type: mock\n    model: NEWa  # c1\n"));
    CHECK(parse_config(set, "<test>").find_backend("e")->model == "NEWa");
    // A rename reads the same quote: the comment is the line's, the value
    // the scalar's.
    const std::string named =
        "backends:\n  x:\n    type: mock\nmodels:\n  default: 'x'  # 'q''s #2'\n";
    CHECK(apogee::harness::rename_backend(named, "x", "y") ==
          "backends:\n  y:\n    type: mock\nmodels:\n  default: y  # 'q''s #2'\n");
}

TEST_CASE("a config reached through a link is edited at its target, the link kept",
          "[config_edit][57e][link]") {
    const apogee::testing::TempDir dir{"edit-link-" + std::to_string(std::random_device{}())};
    const std::filesystem::path dot = dir.path() / "dot" / "config.json";
    const std::filesystem::path home = dir.path() / "home" / "config";
    std::filesystem::create_directories(home);
    apogee::harness::write_file_atomically(dot, kCommented);
    const std::filesystem::path link = home / "config.json";
    std::error_code code;
    std::filesystem::create_symlink(std::filesystem::path{"../../dot/config.json"}, link, code);
    if (code) {
        SKIP("no symbolic links here: " + code.message());
    }
    CHECK(apogee::harness::config_edit_target(link) == std::filesystem::canonical(dot));
    apogee::harness::edit_config_file(
        link, [](std::string_view c) { return set_models_role(c, "default", "local"); });
    // The link is still a link, its target holds the edit, and nothing of the
    // edit's is left beside the target.
    CHECK(std::filesystem::is_symlink(link));
    CHECK(std::filesystem::read_symlink(link) == std::filesystem::path{"../../dot/config.json"});
    CHECK_THAT(bytes_of(dot), ContainsSubstring("  default: local   # the backend used"));
    CHECK(apogee::harness::load_config(link).models.default_backend == "local");
    std::size_t beside_target = 0;
    for (const auto& entry : std::filesystem::directory_iterator{dot.parent_path()}) {
        (void)entry;
        ++beside_target;
    }
    CHECK(beside_target == 1);
    std::filesystem::path lock = link;
    lock += ".lock";
    CHECK(std::filesystem::exists(lock));

    // A link that leads nowhere is refused, both ends named, nothing made.
    const std::filesystem::path dangling = home / "dangling.json";
    std::filesystem::create_symlink(dir.path() / "nowhere" / "config.json", dangling);
    try {
        apogee::harness::edit_config_file(
            dangling, [](std::string_view c) { return set_models_role(c, "default", "local"); });
        FAIL("a dangling link was edited");
    } catch (const ConfigEditError& e) {
        CHECK_THAT(e.what(), ContainsSubstring(dangling.string() + " is a link to " +
                                               (dir.path() / "nowhere" / "config.json").string() +
                                               ", which is not there -- nothing was changed"));
    }
    CHECK_FALSE(std::filesystem::exists(dir.path() / "nowhere"));
    CHECK(std::filesystem::is_symlink(dangling));
}

TEST_CASE("an edit keeps the file's mode, and a write never sits wider than it ends",
          "[config_edit][57e][modes]") {
    if (!apogee::harness::supports_private_modes()) {
        SKIP("no file modes on this platform");
    }
    const apogee::testing::UmaskGuard umask;
    const EditFixture fixture;
    // A config chmodded 0600 for the key it holds stays 0600 across an edit.
    apogee::harness::write_file_atomically(fixture.path, kCommented);
    CHECK(mode_of(fixture.path) ==
          (kOwnerOnly | std::filesystem::perms::group_read | std::filesystem::perms::others_read));
    std::filesystem::permissions(fixture.path, kOwnerOnly);
    apogee::harness::edit_config_file(
        fixture.path, [](std::string_view c) { return set_models_role(c, "default", "local"); });
    CHECK(mode_of(fixture.path) == kOwnerOnly);
    // Any mode, kept: a 0640 file written over is 0640.
    const std::filesystem::path shared = fixture.dir.path() / "ledger.json";
    apogee::harness::write_file_atomically(shared, "{}\n");
    const std::filesystem::perms group = kOwnerOnly | std::filesystem::perms::group_read;
    std::filesystem::permissions(shared, group);
    apogee::harness::write_file_atomically(shared, "{\"a\": 1}\n");
    CHECK(mode_of(shared) == group);
    // Private wins over what was there.
    apogee::harness::write_file_atomically(shared, "{}\n", /*private_mode=*/true);
    CHECK(mode_of(shared) == kOwnerOnly);

    // Watched while written: a private file's temporary file, and a 0600
    // config's, are never caught readable beyond the owner -- the probe
    // that caught 149 of 150 credential writes at 0644 (57e).
    const std::string secret(std::size_t{64} * 1024, 'k');
    {
        apogee::testing::TempModeWatch watch{fixture.dir.path()};
        for (int i = 0; i < 20000 && watch.seen() < 3; ++i) {
            apogee::harness::write_file_atomically(fixture.dir.path() / "credentials.json", secret,
                                                   /*private_mode=*/true);
        }
        watch.stop();
        CHECK(watch.wider() == 0);
        REQUIRE(watch.seen() > 0);
    }
    {
        const std::string config = std::string{kCommented} + "# " + secret + "\n";
        apogee::harness::write_file_atomically(fixture.path, config);
        std::filesystem::permissions(fixture.path, kOwnerOnly);
        apogee::testing::TempModeWatch watch{fixture.dir.path()};
        for (int i = 0; i < 20000 && watch.seen() < 3; ++i) {
            apogee::harness::edit_config_file(fixture.path, [i](std::string_view c) {
                return set_models_role(c, "default", i % 2 == 0 ? "local" : "claude");
            });
        }
        watch.stop();
        CHECK(watch.wider() == 0);
        REQUIRE(watch.seen() > 0);
        CHECK(mode_of(fixture.path) == kOwnerOnly);
    }
}

// --- CLI-M44: the YAML editor refuses, by name, the layouts it does not write

namespace {

/// The sweep's config (CLI-M44): every section an editor writes, in the
/// 2-space layout the line editor writes, its lists nested under their keys.
constexpr std::string_view kLayoutBase = R"(# Every editor's section.
backends:
  m:
    type: mock
    model: m
  n:
    type: mock
    model: nn
models:
  default: m
suites:
  s:
    members:
      chat: m
mcp_servers:
  srv:
    args:
      - hi
    command: /bin/echo
    enabled: true
permissions:
  write_file: ask
tools:
  allowed_hosts:
    - docs.python.org
    - example.com
  search:
    provider: searxng
    url: http://127.0.0.1:8888
memory:
  recall: true
ui:
  markdown: true
embeddings:
  notes:
    chunk_size: 400
graphs:
  g:
    collections:
      - notes
agents:
  a:
    tools: read-only
)";

/// The top-level sections of `kLayoutBase`.
const std::vector<std::string>& layout_sections() {
    static const std::vector<std::string> sections{"backends",    "models", "suites", "mcp_servers",
                                                   "permissions", "tools",  "memory", "ui",
                                                   "embeddings",  "graphs", "agents"};
    return sections;
}

/// `text` with each line's leading spaces counted again by `indent`.
std::string reindented(std::string_view text,
                       const std::function<std::size_t(std::size_t)>& indent) {
    std::string out;
    std::istringstream in{std::string{text}};
    for (std::string line; std::getline(in, line);) {
        const std::size_t at = line.find_first_not_of(' ');
        out += at == std::string::npos ? line : std::string(indent(at), ' ') + line.substr(at);
        out += '\n';
    }
    return out;
}

/// `text` with `from` replaced by `to` -- once, and `from` there.
std::string replaced(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

/// The config as YAML reads `text`, untyped: what an edit's effect is
/// measured on -- the first document, every key at its own path.
nlohmann::json raw_reading(std::string_view text) {
    return nlohmann::json::parse(
        apogee::harness::json_of_yaml(text, nlohmann::ordered_json{}).dump());
}

/// The first key `text` writes twice in one mapping, its path dotted --
/// quoted and spaced spellings read as YAML reads them -- or "": a key
/// written twice is a shadow, whichever one the loader takes. Read up to
/// the end of the first document, lists and their items left out.
std::string written_twice(std::string_view text) {
    std::vector<std::pair<std::size_t, std::string>> stack;
    std::set<std::string> seen;
    std::istringstream in{std::string{text}};
    bool started = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("---") || line.starts_with("...")) {
            if (started || line.starts_with("...")) {
                break;
            }
            started = true;
            continue;
        }
        const std::size_t at = line.find_first_not_of(' ');
        if (at == std::string::npos || line[at] == '#' || line[at] == '-') {
            continue;
        }
        started = true;
        std::size_t colon = std::string::npos;
        std::string key;
        if (line[at] == '"' || line[at] == '\'') {
            const std::size_t close = line.find(line[at], at + 1);
            key = line.substr(at + 1, close - at - 1);
            colon = line.find(':', close);
        } else {
            colon = line.find(':', at);
            key = line.substr(at, colon - at);
            while (!key.empty() && key.back() == ' ') {
                key.pop_back();
            }
        }
        if (colon == std::string::npos) {
            continue;
        }
        while (!stack.empty() && stack.back().first >= at) {
            stack.pop_back();
        }
        std::string path;
        for (const auto& [indent, parent] : stack) {
            path += parent + ".";
        }
        path += key;
        if (!seen.insert(path).second) {
            return path;
        }
        stack.emplace_back(at, key);
    }
    return {};
}

/// `section` without `name` -- null when it was the last, as YAML reads a
/// section left with nothing under it.
void without(nlohmann::json& section, const std::string& name) {
    section.erase(name);
    if (section.empty()) {
        section = nullptr;
    }
}

/// One editor, as the sweep runs it: what it looks up -- a section, or a
/// key under one (`permissions.write_file`) -- and the change to the raw
/// reading it lands as.
struct LayoutVerb {
    std::string name;
    std::vector<std::string> looks_up;
    std::function<std::string(std::string_view)> edit;
    std::function<void(nlohmann::json&)> effect;
};

/// One layout: its text, and the lookups refused on it, each with a word
/// the refusal must say (the indent measured, the key as written).
struct LayoutShape {
    std::string name;
    std::string text;
    std::map<std::string, std::string> refused;
};

std::vector<LayoutVerb> layout_verbs() {
    using apogee::harness::AgentConfig;
    using apogee::harness::McpServerConfig;
    using apogee::harness::NamedGraphConfig;
    using apogee::harness::SuiteConfig;
    using apogee::harness::SuiteMember;
    using nlohmann::json;
    namespace h = apogee::harness;
    BackendConfig z;
    z.type = BackendType::Mock;
    z.model = "zz";
    BackendConfig replacement;
    replacement.type = BackendType::Mock;
    replacement.model = "mm";
    SuiteConfig suite;
    suite.members["chat"] = SuiteMember{.backend = "n"};
    McpServerConfig server;
    server.command = "/bin/true";
    AgentConfig agent;
    NamedGraphConfig graph;
    graph.collections = {"notes"};
    EmbeddingConfig collection;
    collection.chunk_size = 300;
    return {
        {"add-backend",
         {"backends"},
         [z](std::string_view c) { return append_backend(c, "z", z, false); },
         [](json& j) { j["backends"]["z"] = {{"type", "mock"}, {"model", "zz"}}; }},
        {"add-backend --force",
         {"backends"},
         [replacement](std::string_view c) { return append_backend(c, "m", replacement, true); },
         [](json& j) { j["backends"]["m"] = {{"type", "mock"}, {"model", "mm"}}; }},
        {"delete-backend",
         {"backends"},
         [](std::string_view c) { return delete_backend(c, "n"); },
         [](json& j) { without(j["backends"], "n"); }},
        {"set-backend --model",
         {"backends"},
         [](std::string_view c) { return h::set_backend_field(c, "m", "model", "q"); },
         [](json& j) { j["backends"]["m"]["model"] = "q"; }},
        {"set-backend --max-tokens",
         {"backends"},
         [](std::string_view c) { return h::set_backend_field(c, "m", "max_tokens", "9"); },
         [](json& j) { j["backends"]["m"]["max_tokens"] = 9; }},
        {"set-backend --model ''",
         {"backends"},
         [](std::string_view c) { return h::set_backend_field(c, "n", "model", ""); },
         [](json& j) { j["backends"]["n"].erase("model"); }},
        {"rename-backend",
         {"backends"},
         [](std::string_view c) { return h::rename_backend(c, "n", "o"); },
         [](json& j) {
             j["backends"]["o"] = j["backends"]["n"];
             j["backends"].erase("n");
         }},
        {"set-default",
         {"models"},
         [](std::string_view c) { return set_models_role(c, "default", "n"); },
         [](json& j) { j["models"]["default"] = "n"; }},
        {"set-default-utility",
         {"models"},
         [](std::string_view c) { return set_models_role(c, "default_utility", "m"); },
         [](json& j) { j["models"]["default_utility"] = "m"; }},
        {"set-default-suite",
         {"models"},
         [](std::string_view c) { return h::set_default_suite(c, "s"); },
         [](json& j) { j["models"]["default_suite"] = "s"; }},
        {"set-permission (replace)",
         {"permissions", "permissions.write_file"},
         [](std::string_view c) { return set_permission(c, "write_file", "allow"); },
         [](json& j) { j["permissions"]["write_file"] = "allow"; }},
        {"set-permission (insert)",
         {"permissions", "permissions.run_command"},
         [](std::string_view c) { return set_permission(c, "run_command", "deny"); },
         [](json& j) { j["permissions"]["run_command"] = "deny"; }},
        {"add-suite",
         {"suites"},
         [suite](std::string_view c) { return h::append_suite(c, "t", suite, false); },
         [](json& j) { j["suites"]["t"] = {{"members", {{"chat", "n"}}}}; }},
        {"set-suite",
         {"suites"},
         [](std::string_view c) {
             return h::set_suite_member(c, "s", "utility", SuiteMember{.backend = "n"});
         },
         [](json& j) { j["suites"]["s"]["members"]["utility"] = "n"; }},
        {"delete-suite",
         {"suites"},
         [](std::string_view c) { return h::delete_suite(c, "s"); },
         [](json& j) { without(j["suites"], "s"); }},
        {"set memory.recall",
         {"memory"},
         [](std::string_view c) { return h::set_scalar(c, "memory.recall", "false"); },
         [](json& j) { j["memory"]["recall"] = false; }},
        {"set ui.markdown",
         {"ui"},
         [](std::string_view c) { return h::set_scalar(c, "ui.markdown", "false"); },
         [](json& j) { j["ui"]["markdown"] = false; }},
        {"set tools.search.url",
         {"tools"},
         [](std::string_view c) { return h::set_scalar(c, "tools.search.url", "http://x"); },
         [](json& j) { j["tools"]["search"]["url"] = "http://x"; }},
        {"set tools.search.results",
         {"tools"},
         [](std::string_view c) { return h::set_scalar(c, "tools.search.results", "5"); },
         [](json& j) { j["tools"]["search"]["results"] = 5; }},
        {"add-allowed-host",
         {"tools", "tools.allowed_hosts"},
         [](std::string_view c) { return h::add_allowed_host(c, "pypi.org"); },
         [](json& j) { j["tools"]["allowed_hosts"].push_back("pypi.org"); }},
        {"delete-allowed-host",
         {"tools", "tools.allowed_hosts"},
         [](std::string_view c) { return h::remove_allowed_host(c, "example.com"); },
         [](json& j) { j["tools"]["allowed_hosts"] = json::array({"docs.python.org"}); }},
        {"mcp create",
         {"mcp_servers"},
         [server](std::string_view c) { return append_mcp_server(c, "srv2", server, false); },
         [](json& j) { j["mcp_servers"]["srv2"] = {{"command", "/bin/true"}, {"enabled", true}}; }},
        {"mcp disable",
         {"mcp_servers"},
         [](std::string_view c) { return set_mcp_server_enabled(c, "srv", false); },
         [](json& j) { j["mcp_servers"]["srv"]["enabled"] = false; }},
        {"delete-mcp-server",
         {"mcp_servers"},
         [](std::string_view c) { return delete_mcp_server(c, "srv"); },
         [](json& j) { without(j["mcp_servers"], "srv"); }},
        {"add-graph",
         {"graphs"},
         [graph](std::string_view c) { return append_graph(c, "g2", graph, false); },
         [](json& j) { j["graphs"]["g2"] = {{"collections", json::array({"notes"})}}; }},
        {"delete-graph",
         {"graphs"},
         [](std::string_view c) { return delete_graph(c, "g"); },
         [](json& j) { without(j["graphs"], "g"); }},
        {"agents create",
         {"agents"},
         [agent](std::string_view c) { return append_agent(c, "a2", agent, false); },
         [](json& j) { j["agents"]["a2"] = {{"tools", "read-only"}}; }},
        {"agents delete",
         {"agents"},
         [](std::string_view c) { return h::delete_agent(c, "a"); },
         [](json& j) { without(j["agents"], "a"); }},
        {"embed ingest's registration",
         {"embeddings"},
         [collection](std::string_view c) {
             return append_embedding(c, "notes2", collection, false);
         },
         [](json& j) { j["embeddings"]["notes2"] = {{"chunk_size", 300}}; }},
        {"graph build's mark",
         {"embeddings"},
         [](std::string_view c) { return h::set_embedding_graph_enabled(c, "notes", true); },
         [](json& j) { j["embeddings"]["notes"]["graph"] = {{"enabled", true}}; }},
    };
}

/// Every section of `kLayoutBase` refused, saying `word`.
std::map<std::string, std::string> every_section(const std::string& word) {
    std::map<std::string, std::string> refused;
    for (const std::string& section : layout_sections()) {
        refused[section] = word;
    }
    return refused;
}

std::vector<LayoutShape> layout_shapes() {
    const std::string base{kLayoutBase};
    std::string compact = replaced(base, "    args:\n      - hi\n", "    args:\n    - hi\n");
    compact = replaced(compact, "  allowed_hosts:\n    - docs.python.org\n    - example.com\n",
                       "  allowed_hosts:\n  - docs.python.org\n  - example.com\n");
    compact =
        replaced(compact, "    collections:\n      - notes\n", "    collections:\n    - notes\n");
    std::map<std::string, std::string> fields3;
    std::map<std::string, std::string> fields6;
    for (const std::string section :
         {"backends", "suites", "mcp_servers", "tools", "embeddings", "graphs", "agents"}) {
        fields3[section] = "indented 3 spaces";
        fields6[section] = "indented 6 spaces";
    }
    // The investigation's mixed file: backends in the editor's layout, models
    // and tools at 4 -- the backends' verbs land, the others' refuse.
    std::string mixed = replaced(base, "models:\n  default: m\n", "models:\n    default: m\n");
    mixed = replaced(mixed,
                     "tools:\n  allowed_hosts:\n    - docs.python.org\n    - example.com\n"
                     "  search:\n    provider: searxng\n    url: http://127.0.0.1:8888\n",
                     "tools:\n    allowed_hosts:\n        - docs.python.org\n"
                     "        - example.com\n    search:\n        provider: searxng\n"
                     "        url: http://127.0.0.1:8888\n");
    return {
        {"the editor's own layout", base, {}},
        {"compact lists", compact, {}},
        {"a document marked at both ends", "---\n" + base + "...\n# the end\n", {}},
        {"4 spaces", reindented(base, [](std::size_t n) { return n * 2; }),
         every_section("indented 4 spaces")},
        {"1 space", reindented(base, [](std::size_t n) { return n / 2; }),
         every_section("indented 1 space")},
        {"fields at 3", reindented(base, [](std::size_t n) { return n >= 4 ? n - 1 : n; }),
         fields3},
        {"fields at 6", reindented(base, [](std::size_t n) { return n >= 4 ? n + 2 : n; }),
         fields6},
        {"a quoted header and key",
         replaced(replaced(base, "\nbackends:\n", "\n\"backends\":\n"), "  write_file: ask\n",
                  "  \"write_file\": ask\n"),
         {{"backends", "'\"backends\":'"}, {"permissions.write_file", "'\"write_file\":'"}}},
        {"spaced headers",
         replaced(replaced(base, "\nmodels:\n", "\nmodels :\n"), "\ntools:\n", "\ntools :\n"),
         {{"models", "'models :'"}, {"tools", "'tools :'"}}},
        {"a mixed file", mixed, {{"models", "indented 4 spaces"}, {"tools", "indented 4 spaces"}}},
    };
}

}  // namespace

TEST_CASE(
    "every editor lands its exact effect on the layout it writes, and refuses another by name",
    "[config_edit][CLI-M44][layout]") {
    using apogee::harness::parse_config;
    for (const LayoutShape& shape : layout_shapes()) {
        INFO("shape: " + shape.name + "\n" + shape.text);
        REQUIRE_NOTHROW(parse_config(shape.text, "<shape>"));
        const nlohmann::json before = raw_reading(shape.text);
        for (const LayoutVerb& verb : layout_verbs()) {
            INFO("verb: " + verb.name);
            std::optional<std::string> expected_word;
            for (const std::string& lookup : verb.looks_up) {
                if (const auto it = shape.refused.find(lookup); it != shape.refused.end()) {
                    expected_word = it->second;
                }
            }
            try {
                const std::string edited = verb.edit(shape.text);
                INFO("edited:\n" + edited);
                CHECK_FALSE(expected_word.has_value());
                // A landed edit is its effect, exactly, as YAML reads it --
                // never a key written twice, never a file that does not load.
                bool loads = true;
                try {
                    (void)parse_config(edited, "<edited>");
                } catch (const apogee::harness::ConfigError& e) {
                    INFO(e.what());
                    loads = false;
                }
                CHECK(loads);
                CHECK(written_twice(edited).empty());
                nlohmann::json expected = before;
                verb.effect(expected);
                CHECK((loads && raw_reading(edited) == expected));
            } catch (const ConfigEditError& e) {
                const std::string said = e.what();
                INFO("refused: " + said);
                REQUIRE(expected_word.has_value());
                CHECK_THAT(said, ContainsSubstring(*expected_word));
                CHECK_THAT(said, ContainsSubstring("'apogee config migrate'"));
                // Every name the verbs look for is one the loader lists.
                CHECK(said.find("not valid YAML") == std::string::npos);
                CHECK(said.find("not found") == std::string::npos);
                CHECK(said.find("is not in") == std::string::npos);
                CHECK(said.find("no backend named") == std::string::npos);
            }
        }
    }
}

TEST_CASE("a layout refusal names the level, the indent measured, the editor's and the way out",
          "[config_edit][CLI-M44][layout]") {
    using apogee::harness::set_backend_field;
    using apogee::harness::set_scalar;
    // The investigation's reproductions, each in the words it now says.
    const std::string four = reindented(kLayoutBase, [](std::size_t n) { return n * 2; });
    CHECK_THROWS_WITH(set_models_role(four, "default", "m"),
                      "the lines under models: are indented 4 spaces, where the editor writes 2 "
                      "-- it edits the 2-space layout Apogee writes, each level 2 spaces in from "
                      "its key; re-indent the file so, or carry it to JSONC with 'apogee config "
                      "migrate', whose every form the editor edits");
    CHECK_THROWS_WITH(set_backend_field(four, "m", "model", "q"),
                      ContainsSubstring("the lines under backends: are indented 4 spaces"));
    const std::string one = reindented(kLayoutBase, [](std::size_t n) { return n / 2; });
    BackendConfig z;
    z.type = BackendType::Mock;
    z.model = "zz";
    CHECK_THROWS_WITH(append_backend(one, "z", z, false),
                      ContainsSubstring("the lines under backends: are indented 1 space, where "
                                        "the editor writes 2"));
    const std::string three =
        reindented(kLayoutBase, [](std::size_t n) { return n >= 4 ? n - 1 : n; });
    CHECK_THROWS_WITH(delete_backend(three, "m"),
                      ContainsSubstring("the lines under backends.m: are indented 3 spaces, "
                                        "where the editor writes 4"));
    const std::string six =
        reindented(kLayoutBase, [](std::size_t n) { return n >= 4 ? n + 2 : n; });
    CHECK_THROWS_WITH(set_scalar(six, "tools.search.url", "http://x"),
                      ContainsSubstring("the lines under tools.search: are indented 6 spaces, "
                                        "where the editor writes 4"));
    // A level deeper in: a suite's members, a long-form member's fields.
    CHECK_THROWS_WITH(
        apogee::harness::set_suite_member("suites:\n  s:\n    members:\n        chat: m\n", "s",
                                          "utility", apogee::harness::SuiteMember{.backend = "m"}),
        ContainsSubstring("the lines under suites.s.members: are indented 8 spaces, where the "
                          "editor writes 6"));
    // A quoted or spaced key the verb looks for, by its spelling.
    CHECK_THROWS_WITH(
        set_permission("permissions:\n  \"write_file\": ask\n", "write_file", "allow"),
        "the key permissions.write_file is written '\"write_file\":', and the editor finds a key "
        "only written bare, 'write_file:' -- rewrite it so, or carry the file to JSONC with "
        "'apogee config migrate', whose every form the editor edits");
    CHECK_THROWS_WITH(delete_backend("backends:\n  'm' :\n    type: mock\n", "M"),
                      ContainsSubstring("the key backends.M is written ''m' :'"));
    CHECK_THROWS_WITH(
        set_backend_field("backends:\n  m:\n    type: mock\n    model : x\n", "m", "model", "q"),
        ContainsSubstring("the key backends.m.model is written 'model :'"));
    CHECK_THROWS_WITH(set_mcp_server_enabled("mcp_servers:\n  srv:\n    command: x\n"
                                             "    \"enabled\": true\n",
                                             "srv", false),
                      ContainsSubstring("the key mcp_servers.srv.enabled is written"));
    CHECK_THROWS_WITH(
        apogee::harness::rename_backend("backends:\n  \"n\":\n    type: mock\n", "n", "o"),
        ContainsSubstring("the key backends.n is written '\"n\":'"));
    // Another key quoted beside the one looked for is read as YAML reads it.
    CHECK(set_permission("permissions:\n  \"write_file\": ask\n", "run_command", "deny") ==
          "permissions:\n  \"write_file\": ask\n  run_command: deny\n");
    // A name no plain key spells keeps its own words, naming its way out:
    // rename-backend reaches it however it is written.
    CHECK_THROWS_WITH(
        set_backend_field("backends:\n  \"my model\":\n    type: mock\n", "my model", "model", "q"),
        ContainsSubstring("is not one both config formats can edit"));
    CHECK(apogee::harness::rename_backend("backends:\n  \"my model\" :\n    type: mock\n",
                                          "my model",
                                          "mine") == "backends:\n  mine:\n    type: mock\n");
}

TEST_CASE("a compact list, a block scalar and a list of mappings are no layout fault",
          "[config_edit][CLI-M44][layout]") {
    using apogee::harness::parse_config;
    using apogee::harness::set_backend_field;
    // Lists at their key's own indent, or deeper than the editor writes them;
    // a block scalar's text and what sits under a list item, unmeasured.
    const std::string text =
        "backends:\n  m:\n    type: mock\n    server_tools:\n    - web_search\n"
        "    system_prompt: |\n        indented text\n          deeper\n"
        "training:\n  cycle:\n    sources:\n    -   type: sessions\n        backend: m\n"
        "tools:\n  allowed_hosts:\n        - docs.python.org\n";
    const std::string edited = set_backend_field(text, "m", "model", "q");
    CHECK(edited == replaced(text, "    type: mock\n", "    type: mock\n    model: q\n"));
    CHECK(apogee::harness::add_allowed_host(text, "pypi.org") == text + "        - pypi.org\n");
    CHECK(apogee::harness::set_scalar(text, "training.python", "/usr/bin/python3")
              .find("  python: /usr/bin/python3\n") != std::string::npos);

    // A value's own lines, from the review: a scalar on the line below its
    // key, and a quoted one carried on over lines, are text, never a level.
    for (const std::string& valued :
         {std::string{"backends:\n  m:\n    type: mock\n    model:\n        m\n"},
          std::string{"backends:\n  m:\n    type: mock\n    system_prompt:\n        \"You are\n"
                      "      helpful\"\n"},
          std::string{"backends:\n  m:\n    type: mock\n    system_prompt: \"Rules\n    follow:\n"
                      "        be nice\"\n"},
          std::string{"backends:\n  m:\n    type: mock\n    system_prompt: 'it''s\n    a: b'\n"}}) {
        INFO(valued);
        REQUIRE_NOTHROW(parse_config(valued, "<valued>"));
        const std::string set = set_backend_field(valued, "m", "max_tokens", "9");
        CHECK(parse_config(set, "<set>").find_backend("m")->max_tokens == 9);
        CHECK(written_twice(set).empty());
        BackendConfig z;
        z.type = BackendType::Mock;
        CHECK(parse_config(append_backend(valued, "z", z, false), "<added>").backend_names() ==
              std::vector<std::string>{"m", "z"});
    }
    // A tab inside a block scalar's text is text: the lines after it are
    // still measured, and a level in another indent there still refused.
    const std::string tabbed =
        "backends:\n  m:\n    type: mock\n    system_prompt: |\n      \tcode\n  n:\n"
        "      type: mock\n      model: n\n";
    REQUIRE_NOTHROW(parse_config(tabbed, "<tabbed>"));
    CHECK_THROWS_WITH(set_backend_field(tabbed, "n", "model", "q"),
                      ContainsSubstring("the lines under backends.n: are indented 6 spaces"));
    CHECK_THROWS_WITH(delete_backend(tabbed, "m"),
                      ContainsSubstring("the lines under backends.n: are indented 6 spaces"));
}

TEST_CASE("an edit adds its section before a final '...', in the document the loader reads",
          "[config_edit][CLI-M44][documents]") {
    using apogee::harness::parse_config;
    using apogee::harness::set_scalar;
    const std::string doc_end = "backends:\n  m:\n    type: mock\n    model: m\n...\n";
    const std::string set = set_scalar(doc_end, "ui.markdown", "false");
    CHECK(set == "backends:\n  m:\n    type: mock\n    model: m\n\nui:\n  markdown: false\n...\n");
    CHECK(parse_config(set, "<set>").ui.markdown == false);
    // Psych's start, a trailing comment, CRLF: each kept where it stood.
    const std::string psych = "---\r\nbackends:\r\n  m:\r\n    type: mock\r\n...\r\n# end\r\n";
    const std::string denied = set_permission(psych, "run_command", "deny");
    CHECK(denied ==
          "---\r\nbackends:\r\n  m:\r\n    type: mock\r\n\r\npermissions:\r\n  run_command: "
          "deny\r\n...\r\n# end\r\n");
    CHECK(parse_config(denied, "<denied>").permissions.level("run_command") ==
          apogee::harness::PermissionLevel::Deny);
    const std::string hosts = apogee::harness::add_allowed_host(doc_end, "pypi.org");
    CHECK(hosts ==
          "backends:\n  m:\n    type: mock\n    model: m\n\ntools:\n"
          "  allowed_hosts: [pypi.org]\n...\n");
    // An empty document after the first, a comment after a marker: no content.
    const std::string empty_later = "backends:\n  m:\n    type: mock\n--- # later\n...\n";
    CHECK(delete_backend(empty_later, "m") == "backends:\n--- # later\n...\n");
    // Add then delete is the exact inverse there too.
    BackendConfig z;
    z.type = BackendType::Mock;
    CHECK(delete_backend(append_backend(doc_end, "z", z, false), "z") == doc_end);
}

TEST_CASE("a config holding a second YAML document with content is refused, naming the hand edit",
          "[config_edit][CLI-M44][documents]") {
    using apogee::harness::rename_backend;
    using apogee::harness::set_scalar;
    for (const std::string& text :
         {std::string{"backends:\n  m:\n    type: mock\n...\nui:\n  markdown: true\n"},
          std::string{"backends:\n  m:\n    type: mock\n---\npermissions:\n  run_command: allow\n"},
          std::string{"---\n---\nbackends:\n  m:\n    type: mock\n"},
          std::string{"backends:\n  m:\n    type: mock\n--- later\n"}}) {
        INFO(text);
        for (const std::function<std::string()>& edit : std::vector<std::function<std::string()>>{
                 [&] { return set_scalar(text, "ui.markdown", "false"); },
                 [&] { return set_permission(text, "run_command", "deny"); },
                 [&] { return delete_backend(text, "m"); },
                 [&] { return rename_backend(text, "m", "n"); },
                 [&] { return apogee::harness::add_allowed_host(text, "pypi.org"); }}) {
            try {
                (void)edit();
                FAIL("an edit of the first document printed a success");
            } catch (const ConfigEditError& e) {
                const std::string said = e.what();
                CHECK_THAT(said, ContainsSubstring("holds a second YAML document"));
                CHECK_THAT(said, ContainsSubstring("by hand"));
                CHECK(said.find("config migrate") == std::string::npos);
            }
        }
    }
    CHECK_THROWS_WITH(set_scalar("backends:\n  m:\n    type: mock\n...\nui:\n  markdown: true\n",
                                 "ui.markdown", "false"),
                      "this config holds a second YAML document, after the '...' on line 4, "
                      "which Apogee never reads -- move what it holds into the first document, "
                      "or delete it, by hand");
}

namespace {

/// `text`, UTF-8, as UTF-16 with its byte-order mark: little-endian, or
/// big-endian when `big` -- a config another editor saved so.
std::string as_utf16(std::string_view text, bool big) {
    std::string out = big ? std::string{"\xFE\xFF"} : std::string{"\xFF\xFE"};
    const auto unit = [&](std::uint32_t value) {
        const char high = static_cast<char>((value >> 8U) & 0xFFU);
        const char low = static_cast<char>(value & 0xFFU);
        out += big ? high : low;
        out += big ? low : high;
    };
    for (std::size_t i = 0; i < text.size();) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::uint32_t code = byte;
        std::size_t length = 1;
        if (byte >= 0xF0U) {
            code = byte & 0x07U;
            length = 4;
        } else if (byte >= 0xE0U) {
            code = byte & 0x0FU;
            length = 3;
        } else if (byte >= 0xC0U) {
            code = byte & 0x1FU;
            length = 2;
        }
        for (std::size_t k = 1; k < length; ++k) {
            code = (code << 6U) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
        }
        if (code >= 0x10000U) {
            code -= 0x10000U;
            unit(0xD800U + (code >> 10U));
            unit(0xDC00U + (code & 0x3FFU));
        } else {
            unit(code);
        }
        i += length;
    }
    return out;
}

}  // namespace

TEST_CASE("a config that is not UTF-8 is refused by every editor, naming its encoding",
          "[config_edit][CLI-M44][encoding]") {
    namespace h = apogee::harness;
    using Edit = std::function<std::string(std::string_view)>;
    BackendConfig z;
    z.type = BackendType::Mock;
    h::SymphonySpec symphony;
    symphony.name = "say";
    h::SymphonyStage stage;
    stage.name = "one";
    stage.role = "chat";
    stage.prompt = "hi";
    symphony.stages.push_back(stage);
    h::McpServerConfig server;
    server.command = "x";
    h::ReviewTargetConfig target;
    target.repo = "a/b";
    const std::vector<Edit> edits{
        [&](std::string_view c) { return append_backend(c, "z", z, false); },
        [](std::string_view c) { return delete_backend(c, "z"); },
        [](std::string_view c) { return h::rename_backend(c, "z", "y"); },
        [](std::string_view c) { return h::set_backend_field(c, "z", "model", "q"); },
        [](std::string_view c) { return h::set_backend_model_path(c, "z", "/m.gguf"); },
        [](std::string_view c) { return h::set_backend_mmproj_path(c, "z", "/p.gguf"); },
        [](std::string_view c) { return h::set_backend_adapters(c, "z", {}); },
        [](std::string_view c) { return set_models_role(c, "default", "z"); },
        [](std::string_view c) { return h::set_default_suite(c, "s"); },
        [](std::string_view c) { return h::set_default_video(c, "v"); },
        [](std::string_view c) { return h::set_attachments_graph(c, "off"); },
        [](std::string_view c) { return h::set_scalar(c, "ui.markdown", "false"); },
        [](std::string_view c) { return set_permission(c, "run_command", "deny"); },
        [](std::string_view c) { return h::add_allowed_host(c, "pypi.org"); },
        [](std::string_view c) { return h::remove_allowed_host(c, "pypi.org"); },
        [](std::string_view c) { return append_embedding(c, "notes", EmbeddingConfig{}, false); },
        [](std::string_view c) { return delete_embedding(c, "notes"); },
        [](std::string_view c) { return h::set_embedding_graph_enabled(c, "notes", true); },
        [&](std::string_view c) { return append_mcp_server(c, "srv", server, false); },
        [](std::string_view c) { return delete_mcp_server(c, "srv"); },
        [](std::string_view c) { return set_mcp_server_enabled(c, "srv", false); },
        [&](std::string_view c) { return h::append_review_target(c, "r", target, false); },
        [](std::string_view c) { return h::delete_review_target(c, "r"); },
        [](std::string_view c) { return append_agent(c, "a", h::AgentConfig{}, false); },
        [](std::string_view c) { return h::delete_agent(c, "a"); },
        [&](std::string_view c) { return h::append_symphony(c, "say", symphony, false); },
        [](std::string_view c) { return h::delete_symphony(c, "say"); },
        [](std::string_view c) { return append_graph(c, "g", h::NamedGraphConfig{}, false); },
        [](std::string_view c) { return delete_graph(c, "g"); },
        [](std::string_view c) { return h::append_suite(c, "s", h::SuiteConfig{}, false); },
        [](std::string_view c) { return h::delete_suite(c, "s"); },
        [](std::string_view c) { return h::set_suite_member(c, "s", "chat", std::nullopt); },
        [](std::string_view c) { return h::set_suite_consultable(c, "s", {}); },
        [](std::string_view c) { return h::set_suite_consult_caps(c, "s", h::ConsultCaps{}); },
        [](std::string_view c) { return h::set_suite_validate(c, "s", h::ValidateConfig{}); },
        [](std::string_view c) { return h::set_suite_orchestrate(c, "s", true); },
        [](std::string_view c) { return format_config(c); },
    };
    const std::string legacy = legacy_template();
    REQUIRE(!legacy.empty());
    for (const auto& [text, word] : std::vector<std::pair<std::string, std::string>>{
             {as_utf16(legacy, false), "UTF-16 text (little-endian)"},
             {as_utf16(legacy, true), "UTF-16 text (big-endian)"},
             {as_utf16(legacy, false).substr(2), "UTF-16 text (little-endian)"},
             {std::string{"\xFF\xFE\x00\x00", 4} + "b\0\0\0", "UTF-32 text (little-endian)"},
             {std::string{"\x00\x00\xFE\xFF", 4}, "UTF-32 text (big-endian)"},
             {"backends:\n  m:\n    type: mock\n    model: \"a" + std::string(1, '\0') + "\"\n",
              "a NUL byte at offset 43"}}) {
        INFO(word);
        for (std::size_t i = 0; i < edits.size(); ++i) {
            INFO("edit " + std::to_string(i));
            CHECK_THROWS_WITH(edits[i](text), ContainsSubstring(word) &&
                                                  ContainsSubstring("save it as UTF-8") &&
                                                  ContainsSubstring("nothing was changed"));
        }
    }
    CHECK_THROWS_WITH(set_models_role(as_utf16(legacy, false), "default", "z"),
                      "this config is UTF-16 text (little-endian), and the editor edits UTF-8 "
                      "alone -- save it as UTF-8; nothing was changed");
    // A UTF-8 byte-order mark is UTF-8: kept, and the edit lands after it.
    const std::string marked =
        "\xEF\xBB\xBF"
        "backends:\n  m:\n    type: mock\n";
    CHECK(append_backend(marked, "z", z, false) ==
          std::string{marked} + "\n  z:\n    type: mock\n");
    CHECK(apogee::harness::parse_config(append_backend(marked, "z", z, false), "<m>")
              .backend_names() == std::vector<std::string>{"m", "z"});
}

TEST_CASE("an edit whose result would not load says it was not applied, and why",
          "[config_edit][CLI-M44][atomic]") {
    const EditFixture fixture;
    // A layout refused: the bytes as they were, and no "not valid YAML".
    const std::string four = reindented(kLayoutBase, [](std::size_t n) { return n * 2; });
    apogee::harness::write_file_atomically(fixture.path, four);
    try {
        apogee::harness::edit_config_file(fixture.path, [](std::string_view c) {
            return apogee::harness::add_allowed_host(c, "pypi.org");
        });
        FAIL("an edit of a 4-space file landed");
    } catch (const ConfigEditError& e) {
        CHECK_THAT(e.what(), ContainsSubstring("'apogee config migrate'"));
        CHECK(std::string{e.what()}.find("not valid YAML") == std::string::npos);
    }
    CHECK(fixture.bytes() == four);
    // A result the typed loader refuses -- the default suite's deletion --
    // is a refused edit in the words it now has, never "<edited config>:".
    const std::string named =
        "backends:\n  m:\n    type: mock\nsuites:\n  s:\n    members:\n"
        "      chat: m\nmodels:\n  default_suite: s\n";
    apogee::harness::write_file_atomically(fixture.path, named);
    try {
        apogee::harness::edit_config_file(
            fixture.path, [](std::string_view c) { return apogee::harness::delete_suite(c, "s"); });
        FAIL("an edit that would not load landed");
    } catch (const ConfigEditError& e) {
        const std::string said = e.what();
        INFO(said);
        CHECK(
            said.starts_with("the edit was not applied because its result would not load, so "
                             "nothing was changed: models.default_suite"));
        CHECK(said.find("<edited config>") == std::string::npos);
    }
    CHECK(fixture.bytes() == named);
    // A result that is no YAML at all: the same words, the parser's detail after.
    try {
        apogee::harness::edit_config_file(
            fixture.path, [](std::string_view) { return std::string{"backends:\n  broken: [\n"}; });
        FAIL("a broken result landed");
    } catch (const ConfigEditError& e) {
        CHECK_THAT(e.what(), ContainsSubstring("the edit was not applied because its result would "
                                               "not load, so nothing was changed: not valid YAML"));
        CHECK(std::string{e.what()}.find("<edited config>: not valid YAML") == std::string::npos);
    }
    // A config already broken is still the loader's own words, by its path.
    std::ofstream{fixture.path, std::ios::binary | std::ios::trunc} << "backends: [\n";
    CHECK_THROWS_AS(apogee::harness::edit_config_file(
                        fixture.path, [](std::string_view c) { return std::string{c}; }),
                    apogee::harness::ConfigError);
}
