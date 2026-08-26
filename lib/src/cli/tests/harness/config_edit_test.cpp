#include "harness/config_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "harness/config.h"
#include "support/env_guard.h"

using apogee::harness::append_backend;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::ConfigEditError;
using apogee::harness::delete_backend;
using apogee::harness::format_config;
using apogee::harness::set_models_role;
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
