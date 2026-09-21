#include "embedstore/ingest.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>

#include "embedstore/store.h"

/// Getting files into a collection, and the two things a corpus must never do
/// quietly: swallow a binary file as if it were prose, or omit a document
/// without saying so.
namespace {

using apogee::embedstore::ingest_path;
using apogee::embedstore::IngestReport;
using apogee::embedstore::looks_binary;
using apogee::embedstore::Store;

struct Tree {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("apogee-ingest-" + std::to_string(counter()));

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
