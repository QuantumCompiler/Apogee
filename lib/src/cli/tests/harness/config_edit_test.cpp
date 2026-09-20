#include "harness/config_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "harness/config.h"
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
    // Windows is one of the six targets. Normalising line endings would
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
