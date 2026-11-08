#include "embedstore/ingest.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "embedstore/store.h"
#include "platform/child_process.h"

/// Getting files into a collection, and the two things a corpus must never do
/// quietly: swallow a binary file as if it were prose, or omit a document
/// without saying so.
namespace {

using apogee::embedstore::ingest_path;
using apogee::embedstore::IngestReport;
using apogee::embedstore::looks_binary;
using apogee::embedstore::Store;

struct Tree {
    // A random draw beside the counter: ctest runs cases as parallel
    // processes, and a per-process counter alone names the same directory in
    // several of them at once.
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("apogee-ingest-" + std::to_string(std::random_device{}()) + "-" +
                                 std::to_string(counter()));

    Tree() {
        std::error_code code;
        std::filesystem::create_directories(dir / "docs", code);
    }

    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    Tree(Tree&&) = delete;
    Tree& operator=(Tree&&) = delete;

    ~Tree() {
        std::error_code code;
        std::filesystem::remove_all(dir, code);
    }

    void write(std::string_view relative, std::string_view content) const {
        const std::filesystem::path path = dir / "docs" / std::string{relative};
        std::error_code code;
        std::filesystem::create_directories(path.parent_path(), code);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    [[nodiscard]] std::filesystem::path docs() const {
        return dir / "docs";
    }

    [[nodiscard]] std::filesystem::path db() const {
        return dir / "c.db";
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

}  // namespace

TEST_CASE("a directory of text files is ingested and searchable", "[embedstore][ingest]") {
    Tree tree;
    tree.write("alpha.md", "the alpha document mentions zarquon");
    tree.write("beta.txt", "the beta document mentions something else");

    const IngestReport report = ingest_path(tree.db(), tree.docs());

    CHECK(report.files_read == 2);
    CHECK(report.chunks_written >= 2);
    CHECK(report.files_skipped == 0);

    const Store store{tree.db()};
    const auto hits = store.search("zarquon", 5);
    REQUIRE_FALSE(hits.empty());
    CHECK(hits.front().chunk.source == "alpha.md");
}

TEST_CASE("sources are recorded relative to the ingest root", "[embedstore][ingest]") {
    // So a corpus survives being moved, and two machines record the same source
    // names for the same documents. Absolute paths would make every collection
    // machine-specific and every diff of one meaningless.
    Tree tree;
    tree.write("nested/deep/file.md", "content here");

    (void)ingest_path(tree.db(), tree.docs());

    const Store store{tree.db()};
    const std::vector<std::string> sources = store.sources();
    REQUIRE(sources.size() == 1);
    CHECK(sources.front() == "nested/deep/file.md");
    CHECK(sources.front().find(tree.dir.string()) == std::string::npos);
}

TEST_CASE("a binary file is skipped, and the skip is named", "[embedstore][ingest]") {
    // Both halves matter. Ingesting binary fills a corpus with noise that
    // matches nothing and inflates every score's denominator; skipping it
    // SILENTLY leaves a user certain their document is in there.
    Tree tree;
    tree.write("notes.md", "readable prose");
    tree.write("image.png", std::string("PNG\x89", 4) + std::string("\0\0binary", 8));

    const IngestReport report = ingest_path(tree.db(), tree.docs());

    CHECK(report.files_read == 1);
    CHECK(report.files_skipped == 1);
    REQUIRE(report.skips.size() == 1);
    CHECK(report.skips.front().find("image.png") != std::string::npos);
    CHECK(report.skips.front().find("binary") != std::string::npos);
}

TEST_CASE("binary sniffing keys on a NUL byte", "[embedstore][ingest]") {
    // The same test grep has used for decades. Not exact -- no cheap test is --
    // but right about the cases that matter: images, archives and executables
    // have NULs and prose does not.
    CHECK_FALSE(looks_binary("ordinary prose, with punctuation!"));
    CHECK_FALSE(looks_binary("unicode: café 日本語 🎉"));
    CHECK(looks_binary(std::string("PNG\0\0data", 9)));
    CHECK_FALSE(looks_binary(""));
}

TEST_CASE("re-ingesting a directory replaces rather than duplicates", "[embedstore][ingest]") {
    Tree tree;
    tree.write("doc.md", "first content");
    (void)ingest_path(tree.db(), tree.docs());

    tree.write("doc.md", "second content entirely");
    (void)ingest_path(tree.db(), tree.docs());

    const Store store{tree.db()};
    CHECK(store.sources().size() == 1);
    CHECK(store.search("first", 5).empty());
    CHECK_FALSE(store.search("second", 5).empty());
}

TEST_CASE("a single file can be ingested on its own", "[embedstore][ingest]") {
    Tree tree;
    tree.write("only.md", "just this one");

    const IngestReport report = ingest_path(tree.db(), tree.docs() / "only.md");

    CHECK(report.files_read == 1);
    const Store store{tree.db()};
    CHECK(store.sources() == std::vector<std::string>{"only.md"});
}

TEST_CASE("an empty file is skipped with a reason rather than stored", "[embedstore][ingest]") {
    Tree tree;
    tree.write("empty.md", "");
    tree.write("real.md", "content");

    const IngestReport report = ingest_path(tree.db(), tree.docs());

    CHECK(report.files_read == 1);
    CHECK(report.files_skipped == 1);
    CHECK(report.skips.front().find("empty.md") != std::string::npos);
}

TEST_CASE("a PDF without pdftotext is skipped by name, never fatally",
          "[embedstore][ingest][pdf]") {
    // pdftotext is Apogee's ONE optional external binary. A PDF sitting in a
    // directory of Markdown must not stop the Markdown being ingested, and its
    // absence from answers must be explainable.
    Tree tree;
    tree.write("notes.md", "this must still be ingested");
    tree.write("paper.pdf", "%PDF-1.4\nnot actually a readable pdf\n");

    const IngestReport report = ingest_path(tree.db(), tree.docs());

    // The Markdown got in regardless -- that is the property.
    CHECK(report.files_read == 1);
    CHECK(report.files_skipped == 1);
    REQUIRE(report.skips.size() == 1);
    CHECK(report.skips.front().find("paper.pdf") != std::string::npos);
    // Named either way: "not installed" on a machine without it, "could not
    // read it" on one with it. Both say which file and why.
    CHECK(report.skips.front().find("pdftotext") != std::string::npos);
}

TEST_CASE("an ingest says each file as its turn comes, against the whole walk",
          "[embedstore][ingest][busy]") {
    // M1: the walk is counted before the first file is read, so a busy line
    // can say how far through it is -- skipped files included, since they
    // take their turn too.
    const Tree tree;
    tree.write("a.md", "alpha");
    tree.write("b.bin", std::string{"\0\1\2", 3});
    tree.write("nested/c.txt", "gamma");

    std::vector<std::pair<std::size_t, std::size_t>> counts;
    std::vector<std::string> names;
    const IngestReport report =
        ingest_path(tree.db(), tree.docs(), {}, {},
                    [&](std::size_t done, std::size_t total, const std::filesystem::path& file) {
                        counts.emplace_back(done, total);
                        names.push_back(file.filename().string());
                    });
    CHECK(report.files_read == 2);
    CHECK(report.files_skipped == 1);
    CHECK(counts == std::vector<std::pair<std::size_t, std::size_t>>{{1, 3}, {2, 3}, {3, 3}});
    CHECK(names == std::vector<std::string>{"a.md", "b.bin", "c.txt"});
}

// ---------------------------------------------------------------------------
// What a directory walk takes, and the folders sources come from (57s)
// ---------------------------------------------------------------------------

namespace {

using apogee::embedstore::folder_identity;

[[nodiscard]] bool git_can_run() {
    return !apogee::platform::find_on_path("git").empty() &&
           apogee::platform::supports_child_processes();
}

/// `git init` in `folder`, quietly: a test fixture, as attachments_test's.
void git_init(const std::filesystem::path& folder) {
    const std::string init = "git -C '" + folder.string() + "' init -q >/dev/null 2>&1";
    REQUIRE(std::system(init.c_str()) == 0);  // NOLINT(cert-env33-c): a test fixture
}

}  // namespace

TEST_CASE("a directory ingest leaves out hidden entries and vendored folders, and counts them",
          "[embedstore][ingest][57s]") {
    // The sweep's probe, outside a repository: a token in .git/config and a
    // secret in .env became chunks a metered model could be sent.
    Tree tree;
    tree.write(".git/config",
               "[remote \"origin\"]\n\turl = https://user:ghp_FAKE@github.com/x/y\n");
    tree.write(".env", "SECRET=abc123\n");
    tree.write("node_modules/x/index.js", "module.exports = 1;\n");
    tree.write(".hidden/notes.md", "hidden notes\n");
    tree.write("main.md", "the main document\n");

    const IngestReport report = ingest_path(tree.db(), tree.docs());

    CHECK(report.files_read == 1);
    CHECK(report.left_out ==
          std::vector<std::string>{".env", ".git/", ".hidden/", "node_modules/"});
    const Store store{tree.db()};
    CHECK(store.sources() == std::vector<std::string>{"main.md"});
    CHECK(store.search("ghp_FAKE", 5).empty());
    CHECK(store.search("abc123", 5).empty());
    // Nothing of it was earlier in the collection, so nothing is said held.
    CHECK(report.still_held.empty());
}

TEST_CASE("inside a git work tree git decides what is ignored, and hidden and vendored stay out",
          "[embedstore][ingest][git][57s]") {
    if (!git_can_run()) {
        SKIP("git is not installed, or this build starts no child processes");
    }
    Tree tree;
    git_init(tree.docs());
    tree.write(".gitignore", "*.log\nbuild/\n");
    tree.write("main.md", "the main document\n");
    tree.write("notes.md", "untracked but not ignored\n");
    tree.write("run.log", "an ignored log\n");
    tree.write("build/out.md", "an ignored build output\n");
    tree.write("node_modules/x/index.js", "untracked, not ignored, vendored\n");
    tree.write(".env", "SECRET=abc123\n");

    const IngestReport report = ingest_path(tree.db(), tree.docs());

    const Store store{tree.db()};
    CHECK(store.sources() == std::vector<std::string>{"main.md", "notes.md"});
    CHECK(report.left_out == std::vector<std::string>{".env", ".git/", ".gitignore", "build/",
                                                      "node_modules/", "run.log"});

    // A folder git ignores whole, named on its own, is walked: the person
    // asked for it.
    const IngestReport built = ingest_path(tree.dir / "built.db", tree.docs() / "build");
    CHECK(built.files_read == 1);
    CHECK(Store{tree.dir / "built.db"}.sources() == std::vector<std::string>{"out.md"});
}

TEST_CASE("a file named on its own is taken whatever its name", "[embedstore][ingest][57s]") {
    Tree tree;
    tree.write(".env", "SECRET=abc123\n");
    tree.write("node_modules/x/index.js", "module.exports = 1;\n");

    CHECK(ingest_path(tree.db(), tree.docs() / ".env").files_read == 1);
    // And a folder inside a vendored one, named, is walked from where it is.
    CHECK(ingest_path(tree.db(), tree.docs() / "node_modules" / "x").files_read == 1);
    const Store store{tree.db()};
    CHECK(store.sources() == std::vector<std::string>{".env", "index.js"});
}

TEST_CASE("a same-named document from another folder is refused, never replaced",
          "[embedstore][ingest][roots][57s]") {
    // The sweep's probe: /b/README.md silently replaced /a/README.md.
    Tree tree;
    tree.write("a/README.md", "Alpha project uses Postgres.\n");
    tree.write("b/README.md", "Beta project uses Redis.\n");
    const std::filesystem::path a = tree.docs() / "a";
    const std::filesystem::path b = tree.docs() / "b";

    const IngestReport first = ingest_path(tree.db(), a);
    CHECK(first.replaced.empty());
    CHECK(first.root == folder_identity(a));

    const IngestReport second = ingest_path(tree.db(), b);
    CHECK(second.files_read == 0);
    REQUIRE(second.collisions.size() == 1);
    CHECK(second.collisions.front().source == "README.md");
    CHECK(second.collisions.front().root == folder_identity(a));
    CHECK(second.written.empty());
    {
        const Store store{tree.db()};
        CHECK_FALSE(store.search("Postgres", 5).empty());
        CHECK(store.search("Redis", 5).empty());
    }

    // The same folder again replaces, and says so.
    const IngestReport again = ingest_path(tree.db(), a);
    REQUIRE(again.replaced.size() == 1);
    CHECK(again.replaced.front().source == "README.md");
    CHECK(again.replaced.front().moved_from.empty());
    CHECK_FALSE(again.replaced.front().unrecorded);

    // A single file is rooted at its folder: refused the same way.
    CHECK(ingest_path(tree.db(), b / "README.md").collisions.size() == 1);

    // With /a gone, the corpus moved: /b replaces it and names the move.
    const std::string gone = folder_identity(a);
    std::filesystem::remove_all(a);
    const IngestReport moved = ingest_path(tree.db(), b);
    REQUIRE(moved.replaced.size() == 1);
    CHECK(moved.replaced.front().moved_from == gone);
    const Store store{tree.db()};
    CHECK_FALSE(store.search("Redis", 5).empty());
    CHECK(store.search("Postgres", 5).empty());
}

TEST_CASE("a source stored with no folder recorded is replaced and the replacement named",
          "[embedstore][ingest][roots][57s]") {
    Tree tree;
    tree.write("README.md", "new text\n");
    {
        Store store{tree.db()};
        store.replace_source("README.md", {"text an earlier build stored"});
    }
    const IngestReport report = ingest_path(tree.db(), tree.docs());
    REQUIRE(report.replaced.size() == 1);
    CHECK(report.replaced.front().unrecorded);

    // A source deleted by name takes its folder with it: another folder's
    // document of that name is then let in.
    tree.write("other/README.md", "another folder's readme\n");
    {
        Store store{tree.db()};
        CHECK(store.delete_source("README.md") == 1);
    }
    const IngestReport other = ingest_path(tree.db(), tree.docs() / "other");
    CHECK(other.collisions.empty());
    CHECK(other.files_read == 1);
}

TEST_CASE("sources an earlier build took that a walk now leaves out are named, not removed",
          "[embedstore][ingest][57s]") {
    Tree tree;
    tree.write("main.md", "the main document\n");
    {
        Store store{tree.db()};
        store.replace_source(".env", {"SECRET=abc123"});
        store.replace_source(".git/config", {"url = https://user:ghp_FAKE@github.com/x/y"});
        store.replace_source("node_modules/x/index.js", {"module.exports = 1;"});
    }
    const IngestReport report = ingest_path(tree.db(), tree.docs());
    CHECK(report.still_held ==
          std::vector<std::string>{".env", ".git/config", "node_modules/x/index.js"});
    // Named, never cleaned on Apogee's initiative.
    CHECK(Store{tree.db()}.sources().size() == 4);
}

TEST_CASE("each source records the folder and the model its vectors are from",
          "[embedstore][ingest][roots][57s]") {
    Tree tree;
    tree.write("one.md", "first\n");
    tree.write("two.md", "second\n");
    const auto embed = [](const std::vector<std::string>& chunks) {
        return std::vector<std::vector<float>>(chunks.size(), std::vector<float>{1.0F, 0.0F});
    };
    (void)ingest_path(tree.db(), tree.docs() / "one.md", {}, embed, {}, "model-a");
    (void)ingest_path(tree.db(), tree.docs() / "two.md");

    const Store store{tree.db()};
    const auto ingests = store.source_ingests();
    REQUIRE(ingests.size() == 2);
    CHECK(ingests.at("one.md").root == folder_identity(tree.docs()));
    CHECK(ingests.at("one.md").model == "model-a");
    // Stored without vectors: no model.
    CHECK(ingests.at("two.md").model.empty());
    CHECK(store.vectored_sources() == std::vector<std::string>{"one.md"});
}
