#include "contracts/config_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "support/env_guard.h"

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

namespace {

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

    // The promote path on two copies of the shipped template: append a new
    // llamacpp entry, then repoint it in place -- and the second file equals
    // the first with only the path changed.
    const std::string shipped{apogee::harness::config_template()};
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

TEST_CASE("add_allowed_host on the shipped template changes one line, and remove undoes it",
          "[config_edit][hosts]") {
    const std::string shipped{apogee::harness::config_template()};
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
