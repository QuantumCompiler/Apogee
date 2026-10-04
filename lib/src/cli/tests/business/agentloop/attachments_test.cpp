#include "agentloop/attachments.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "agentloop/media.h"
#include "embedstore/ingest.h"
#include "embedstore/store.h"
#include "platform/child_process.h"
#include "support/env_guard.h"

using apogee::agentloop::attachment_excerpts;
using apogee::agentloop::attachment_source;
using apogee::agentloop::AttachmentIndex;
using apogee::agentloop::Embedder;
using apogee::agentloop::find_attachment_files;
using apogee::agentloop::FoundFile;
using apogee::agentloop::FoundFiles;
using apogee::agentloop::read_attachment_text;
using apogee::embedstore::SearchHit;
using apogee::embedstore::Store;

namespace {

struct Scratch {
    apogee::testing::TempDir dir{"attach-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path write(const std::string& name,
                                              const std::string& text) const {
        const std::filesystem::path path = dir.path() / name;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;
        return path;
    }

    [[nodiscard]] FoundFile found(const std::string& name) const {
        const std::filesystem::path path = dir.path() / name;
        return FoundFile{.path = path,
                         .name = name,
                         .bytes = static_cast<std::uint64_t>(std::filesystem::file_size(path))};
    }
};

/// A deterministic embedder that counts its calls and the texts it saw.
struct CountingEmbedder {
    std::shared_ptr<std::atomic<int>> calls = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> texts = std::make_shared<std::atomic<int>>(0);

    [[nodiscard]] Embedder make(const std::string& model = "count-embed") const {
        Embedder embedder;
        embedder.backend = "embedder";
        embedder.model = model;
        embedder.dimensions = 3;
        embedder.metered = false;
        embedder.embed = [calls = calls, texts = texts](const std::vector<std::string>& batch,
                                                        const apogee::harness::CancellationToken&) {
            ++*calls;
            *texts += static_cast<int>(batch.size());
            std::vector<std::vector<float>> out;
            for (const std::string& text : batch) {
                out.push_back({static_cast<float>(text.size()), 1.0F, 0.5F});
            }
            return out;
        };
        return embedder;
    }
};

/// A PDF of `pages`, one line of text each, with a correct cross-reference
/// table -- small enough to build here, real enough for `pdftotext`.
std::string build_pdf(const std::vector<std::string>& pages) {
    std::vector<std::string> objects;
    std::string kids;
    const std::size_t font = 3 + (pages.size() * 2);
    for (std::size_t index = 0; index < pages.size(); ++index) {
        kids += std::to_string(3 + (index * 2)) + " 0 R ";
    }
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    objects.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " +
                      std::to_string(pages.size()) + " >>");
    for (std::size_t index = 0; index < pages.size(); ++index) {
        const std::string stream = "BT /F1 12 Tf 72 720 Td (" + pages[index] + ") Tj ET";
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents " +
                          std::to_string(4 + (index * 2)) + " 0 R /Resources << /Font << /F1 " +
                          std::to_string(font) + " 0 R >> >> >>");
        objects.push_back("<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream +
                          "\nendstream");
    }
    objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    std::string pdf = "%PDF-1.4\n";
    std::vector<std::size_t> offsets;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        offsets.push_back(pdf.size());
        pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const std::size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const std::size_t offset : offsets) {
        char entry[32];
        std::snprintf(entry, sizeof entry, "%010zu 00000 n \n", offset);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) +
           " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

nlohmann::json metadata_of(const apogee::embedstore::Chunk& chunk) {
    return nlohmann::json::parse(chunk.metadata);
}

/// A long text of numbered lines, several chunks long.
std::string numbered_lines(int count) {
    std::string text;
    for (int line = 1; line <= count; ++line) {
        text += "line " + std::to_string(line) + " of the document, with some words in it\n";
    }
    return text;
}

}  // namespace

// ---------------------------------------------------------------------------
// Finding files
// ---------------------------------------------------------------------------

TEST_CASE("a file is found by name, a folder walked without its hidden entries",
          "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("notes.md", "notes");
    (void)scratch.write("src/a.cpp", "a");
    (void)scratch.write("src/deep/b.cpp", "b");
    (void)scratch.write("src/.cache/c.cpp", "hidden folder");
    (void)scratch.write("src/.env", "hidden file");

    const FoundFiles one = find_attachment_files("notes.md", scratch.dir.path());
    REQUIRE(one.files.size() == 1);
    CHECK(one.files.front().name == "notes.md");
    CHECK(one.files.front().bytes == 5);
    CHECK(one.error.empty());

    const FoundFiles folder = find_attachment_files("src", scratch.dir.path());
    std::vector<std::string> names;
    for (const FoundFile& file : folder.files) {
        names.push_back(file.name);
    }
    CHECK(names == std::vector<std::string>{"src/a.cpp", "src/deep/b.cpp"});
    CHECK(folder.bytes == 2);

    // Named explicitly, a hidden file is attached like any other.
    CHECK(find_attachment_files("src/.env", scratch.dir.path()).files.size() == 1);
    CHECK(find_attachment_files("missing.md", scratch.dir.path()).error ==
          "no file or folder at missing.md");
}

TEST_CASE("a glob matches within names with * and across folders with **",
          "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("a.md", "a");
    (void)scratch.write("b.txt", "b");
    (void)scratch.write("docs/c.md", "c");
    (void)scratch.write("docs/deep/d.md", "d");

    const auto names = [&](std::string_view spec) {
        std::vector<std::string> out;
        for (const FoundFile& file : find_attachment_files(spec, scratch.dir.path()).files) {
            out.push_back(file.name);
        }
        return out;
    };
    CHECK(names("*.md") == std::vector<std::string>{"a.md"});
    CHECK(names("docs/*.md") == std::vector<std::string>{"docs/c.md"});
    CHECK(names("docs/**/*.md") == std::vector<std::string>{"docs/c.md", "docs/deep/d.md"});
    CHECK(names("?.txt") == std::vector<std::string>{"b.txt"});
    // `?` is one character of a name, never the folder separator.
    CHECK(names("docs?c.md").empty());
    CHECK(find_attachment_files("*.pdf", scratch.dir.path()).error == "nothing matches *.pdf");
}

TEST_CASE("inside a git repository, a folder leaves out what git ignores",
          "[agentloop][attachments][git]") {
    if (apogee::platform::find_on_path("git").empty()) {
        SKIP("git is not installed");
    }
    const Scratch scratch;
    (void)scratch.write("repo/.gitignore", "build/\n*.log\n");
    (void)scratch.write("repo/main.cpp", "int main() {}");
    (void)scratch.write("repo/build/out.o", "object");
    (void)scratch.write("repo/run.log", "log");
    const std::string init =
        "git -C '" + (scratch.dir.path() / "repo").string() + "' init -q >/dev/null 2>&1";
    REQUIRE(std::system(init.c_str()) == 0);  // NOLINT(cert-env33-c): a test fixture

    std::vector<std::string> names;
    for (const FoundFile& file : find_attachment_files("repo", scratch.dir.path()).files) {
        names.push_back(file.name);
    }
    // The .gitignore itself is hidden; untracked but not ignored files count.
    CHECK(names == std::vector<std::string>{"repo/main.cpp"});
}

TEST_CASE("the size guard trips over 500 files or 50 MB", "[agentloop][attachments]") {
    FoundFiles found;
    found.files.resize(500);
    CHECK_FALSE(found.large());
    found.files.resize(501);
    CHECK(found.large());
    found.files.resize(1);
    found.bytes = apogee::agentloop::kLargeAttachmentBytes;
    CHECK_FALSE(found.large());
    found.bytes += 1;
    CHECK(found.large());
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

TEST_CASE("text is read as it is; binary, Office and media files are refused by name",
          "[agentloop][attachments]") {
    const Scratch scratch;
    const auto text = read_attachment_text(scratch.write("a.cpp", "int x = 1;\n"));
    CHECK(text.ok());
    CHECK(text.reader == "text");
    CHECK(text.text == "int x = 1;\n");

    CHECK(read_attachment_text(scratch.write("blob.bin", std::string{"ab\0cd", 5})).reason ==
          "looks binary");
    CHECK(read_attachment_text(scratch.write("report.docx", "PK")).reason ==
          "a Word document: Office formats are not read yet");
    CHECK(read_attachment_text(scratch.write("sheet.xlsx", "PK")).reason ==
          "an Excel spreadsheet: Office formats are not read yet");
    CHECK(read_attachment_text(scratch.write("deck.pptx", "PK")).reason ==
          "a PowerPoint deck: Office formats are not read yet");
    CHECK(read_attachment_text(scratch.write("photo.JPG", "x")).reason ==
          "an image, which a model reads rather than as text");
    CHECK(read_attachment_text(scratch.write("talk.mp3", "x")).reason ==
          "audio, which a model reads rather than as text");
}

TEST_CASE("HTML is read as its main content, links resolved against the file",
          "[agentloop][attachments]") {
    const Scratch scratch;
    const std::filesystem::path page =
        scratch.write("site/page.html",
                      "<html><head><title>Guide</title></head><body><nav>menu menu</nav><main>"
                      "<h1>Guide</h1><p>Read the <a href=\"other.html\">other page</a> first.</p>"
                      "</main></body></html>");
    const auto read = read_attachment_text(page);
    REQUIRE(read.ok());
    CHECK(read.reader == "html");
    CHECK(read.text.find("Read the") != std::string::npos);
    const std::string link =
        "file://" + (scratch.dir.path() / "site" / "other.html").generic_string();
    CHECK(read.text.find(link) != std::string::npos);
    CHECK(read.text.find("file.invalid") == std::string::npos);
}

TEST_CASE("a PDF is read through pdftotext, its pages numbered", "[agentloop][attachments][pdf]") {
    if (!apogee::embedstore::pdftotext_available()) {
        SKIP("pdftotext is not installed");
    }
    const Scratch scratch;
    (void)scratch.write("report.pdf", build_pdf({"The first page.", "The second page.",
                                                 "The launch code is 7731."}));
    const auto read = read_attachment_text(scratch.dir.path() / "report.pdf");
    REQUIRE(read.ok());
    CHECK(read.reader == "pdftotext");
    CHECK(read.text.find('\f') != std::string::npos);

    AttachmentIndex index{scratch.dir.path() / "index.db", {}, std::nullopt};
    const auto added = index.add(scratch.found("report.pdf"), {});
    REQUIRE(added.file.has_value());
    const Store store{scratch.dir.path() / "index.db"};
    const auto hits = store.search("launch code", 4);
    REQUIRE_FALSE(hits.empty());
    const auto excerpts = attachment_excerpts(hits);
    REQUIRE_FALSE(excerpts.empty());
    // Three short pages are one chunk, spanning all three.
    CHECK(excerpts.front().label == "report.pdf pp. 1–3");
    // The form feeds are the page numbers, not the text.
    CHECK(excerpts.front().text.find('\f') == std::string::npos);
}

TEST_CASE("without pdftotext, a PDF is skipped with its reason", "[agentloop][attachments][pdf]") {
    if (apogee::embedstore::pdftotext_available()) {
        SKIP("pdftotext is installed");
    }
    const Scratch scratch;
    CHECK(read_attachment_text(scratch.write("report.pdf", build_pdf({"x"}))).reason ==
          "needs pdftotext, which is not installed");
}

// ---------------------------------------------------------------------------
// The index
// ---------------------------------------------------------------------------

TEST_CASE("a file is indexed under its content, each chunk carrying its line range",
          "[agentloop][attachments]") {
    const Scratch scratch;
    const std::string text = numbered_lines(60);
    (void)scratch.write("notes.txt", text);
    AttachmentIndex index{scratch.dir.path() / "index.db", {}, std::nullopt};
    const auto added = index.add(scratch.found("notes.txt"), {});
    REQUIRE(added.file.has_value());
    CHECK(added.file->name == "notes.txt");
    CHECK(added.file->reader == "text");
    CHECK(added.file->bytes == text.size());
    CHECK(added.chunks > 3);
    CHECK_FALSE(added.vectorised);
    CHECK(index.holds(added.file->sha256));

    const Store store{scratch.dir.path() / "index.db"};
    const auto chunks = store.chunks_by_source(attachment_source(added.file->sha256));
    REQUIRE(chunks.size() == static_cast<std::size_t>(added.chunks));
    CHECK(metadata_of(chunks.front())["lines"][0] == 1);
    CHECK(metadata_of(chunks.back())["lines"][1] == 60);
    CHECK(metadata_of(chunks.front())["file"] == "notes.txt");

    // Rebuilt exactly, the overlap between chunks not repeated.
    CHECK(index.text_of(added.file->sha256) == text);

    // The same content again is already there: nothing is read or stored twice.
    const auto again = index.add(scratch.found("notes.txt"), {});
    CHECK(again.already);
    CHECK(store.chunks_by_source(attachment_source(added.file->sha256)).size() == chunks.size());

    index.remove(added.file->sha256);
    CHECK_FALSE(index.holds(added.file->sha256));
}

TEST_CASE("the hash cache copies another chat's index instead of embedding again",
          "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("report.txt", numbered_lines(80));
    const std::filesystem::path chats = scratch.dir.path() / "chats";
    std::filesystem::create_directories(chats);
    const CountingEmbedder counter;

    AttachmentIndex first{chats / "one.db", chats, counter.make()};
    const auto embedded = first.add(scratch.found("report.txt"), {});
    REQUIRE(embedded.file.has_value());
    CHECK(embedded.vectorised);
    const int calls = *counter.calls;
    CHECK(calls > 0);

    // Attached in a second chat, under another name: copied, vectors and
    // all, with no embed call -- and cited by its new name.
    (void)scratch.write("renamed.txt", numbered_lines(80));
    AttachmentIndex second{chats / "two.db", chats, counter.make()};
    const auto copied = second.add(scratch.found("renamed.txt"), {});
    REQUIRE(copied.file.has_value());
    CHECK(copied.copied);
    CHECK(copied.vectorised);
    CHECK(*counter.calls == calls);
    const Store store{chats / "two.db"};
    const auto chunks = store.chunks_by_source(attachment_source(copied.file->sha256));
    REQUIRE_FALSE(chunks.empty());
    CHECK(metadata_of(chunks.front())["file"] == "renamed.txt");
    CHECK_FALSE(store.chunk_vector(chunks.front().id).empty());
    CHECK(store.embedding_model().model == "count-embed");

    // Another model's vectors are another space: embedded, not copied.
    AttachmentIndex other_model{chats / "three.db", chats, counter.make("another-embed")};
    const auto fresh = other_model.add(scratch.found("report.txt"), {});
    CHECK_FALSE(fresh.copied);
    CHECK(*counter.calls > calls);

    // And a lexical-only index copies only from another lexical one.
    AttachmentIndex lexical{chats / "four.db", chats, std::nullopt};
    const auto words = lexical.add(scratch.found("report.txt"), {});
    CHECK_FALSE(words.copied);
    CHECK_FALSE(words.vectorised);
    AttachmentIndex lexical_again{chats / "five.db", chats, std::nullopt};
    CHECK(lexical_again.add(scratch.found("report.txt"), {}).copied);
}

TEST_CASE("embedding reports its progress, and another model's index stays lexical for it",
          "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("a.txt", numbered_lines(800));  // several batches of 32
    (void)scratch.write("b.txt", "one more file\n");
    const CountingEmbedder counter;
    AttachmentIndex index{scratch.dir.path() / "index.db", {}, counter.make()};
    std::vector<std::size_t> seen;
    std::size_t total = 0;
    const auto added =
        index.add(scratch.found("a.txt"), {}, [&](std::size_t done, std::size_t all) {
            seen.push_back(done);
            total = all;
        });
    REQUIRE(added.file.has_value());
    CHECK(seen.size() > 1);
    CHECK(seen.back() == total);
    CHECK(total == static_cast<std::size_t>(added.chunks));

    // The same index, now asked to embed with another model: its words only.
    AttachmentIndex changed{scratch.dir.path() / "index.db", {}, counter.make("another-embed")};
    const auto words = changed.add(scratch.found("b.txt"), {});
    REQUIRE(words.file.has_value());
    CHECK_FALSE(words.vectorised);
    CHECK(words.note ==
          "b.txt: this chat's index holds vectors from count-embed, so it is searched by its "
          "words only");
}

TEST_CASE("an embedder that fails leaves the file indexed by its words, and says so",
          "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("a.txt", "some words\n");
    Embedder broken;
    broken.model = "broken";
    broken.embed = [](const std::vector<std::string>&, const apogee::harness::CancellationToken&)
        -> std::vector<std::vector<float>> { throw std::runtime_error("no GPU"); };
    AttachmentIndex index{scratch.dir.path() / "index.db", {}, broken};
    const auto added = index.add(scratch.found("a.txt"), {});
    REQUIRE(added.file.has_value());
    CHECK_FALSE(added.vectorised);
    CHECK(added.note == "a.txt: embedding failed (no GPU), so it is searched by its words only");
}

TEST_CASE("cancelled, a file is not stored at all", "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("a.txt", numbered_lines(200));
    const CountingEmbedder counter;
    AttachmentIndex index{scratch.dir.path() / "index.db", {}, counter.make()};
    const auto token = apogee::harness::CancellationToken::create();
    token.cancel();
    const auto added = index.add(scratch.found("a.txt"), token);
    CHECK_FALSE(added.file.has_value());
    CHECK(added.skip == "a.txt: cancelled");

    // A lexical index embeds nothing, and is stopped all the same.
    AttachmentIndex lexical{scratch.dir.path() / "words.db", {}, std::nullopt};
    const auto words = lexical.add(scratch.found("a.txt"), token);
    CHECK_FALSE(words.file.has_value());
    CHECK(words.skip == "a.txt: cancelled");
}

// ---------------------------------------------------------------------------
// Excerpts
// ---------------------------------------------------------------------------

TEST_CASE("adjacent chunks of one file merge into one excerpt, labelled by lines",
          "[agentloop][attachments]") {
    const Scratch scratch;
    const std::string text = numbered_lines(60);
    (void)scratch.write("src/parser.cpp", text);
    AttachmentIndex index{scratch.dir.path() / "index.db", {}, std::nullopt};
    const auto added = index.add(scratch.found("src/parser.cpp"), {});
    REQUIRE(added.file.has_value());
    const Store store{scratch.dir.path() / "index.db"};
    const auto chunks = store.chunks_by_source(attachment_source(added.file->sha256));
    REQUIRE(chunks.size() >= 4);

    // Chunks 1 and 2 are neighbours; chunk 4 stands alone.
    std::vector<SearchHit> hits{
        SearchHit{.chunk = chunks[3], .score = 0.9, .retriever = "lexical"},
        SearchHit{.chunk = chunks[1], .score = 0.5, .retriever = "lexical"},
        SearchHit{.chunk = chunks[0], .score = 0.7, .retriever = "lexical"}};
    const auto excerpts = attachment_excerpts(hits);
    REQUIRE(excerpts.size() == 2);
    CHECK(excerpts[0].score == 0.9);
    CHECK(excerpts[1].score == 0.7);
    const nlohmann::json first = metadata_of(chunks[0]);
    const nlohmann::json second = metadata_of(chunks[1]);
    CHECK(excerpts[1].label ==
          "src/parser.cpp:" + first["lines"][0].dump() + "–" + second["lines"][1].dump());
    // Merged without the overlap: the text as it stands in the file.
    const std::size_t begin = first["begin"].get<std::size_t>();
    const std::size_t end = second["end"].get<std::size_t>();
    CHECK(excerpts[1].text == text.substr(begin, end - begin));

    const std::string rendered = apogee::agentloop::render_attachment_excerpts(excerpts);
    CHECK(rendered.find("--- " + excerpts[1].label + " ---") != std::string::npos);
    CHECK(rendered.find("cite it by the label") != std::string::npos);
}

TEST_CASE("an inlined file is framed by its name", "[agentloop][attachments]") {
    CHECK(apogee::agentloop::render_inline_attachment("notes.md", "hello") ==
          "--- attached file: notes.md ---\nhello\n--- end of notes.md ---\n");
}

TEST_CASE("an attachment is inlined when it fits the attachment share beside the others",
          "[agentloop][attachments]") {
    apogee::agentloop::TurnBudget budget;
    budget.budget.window = 10000;
    budget.budget.reserve = 0;
    // A share of 3,000.
    CHECK(apogee::agentloop::fits_inline(budget, 0, 3000));
    CHECK_FALSE(apogee::agentloop::fits_inline(budget, 1, 3000));
    CHECK(apogee::agentloop::fits_inline(budget, 2000, 1000));
    // An unknown window never reads as room.
    CHECK_FALSE(apogee::agentloop::fits_inline(apogee::agentloop::TurnBudget{}, 0, 1));
    CHECK(apogee::agentloop::inline_tokens(budget, "a.md", std::string(400, 'x')) > 100);
}

TEST_CASE("code names are found in a question; file names and prose are not",
          "[agentloop][attachments]") {
    using apogee::agentloop::code_names_in;
    CHECK(code_names_in("Where is fitting_prefix defined?") ==
          std::vector<std::string>{"fitting_prefix"});
    CHECK(code_names_in("what calls parseConfig and Store::search?") ==
          std::vector<std::string>{"parseConfig", "Store::search"});
    CHECK(code_names_in("does run() throw?") == std::vector<std::string>{"run"});
    CHECK(code_names_in("In ledger.pdf, how many crates are there?").empty());
    CHECK(code_names_in("What is the capital of France?").empty());
    CHECK(code_names_in("_leading and trailing_ underscores").empty());
}

namespace {

/// A timeline long enough to chunk: a screen and a line said every five
/// seconds for ten minutes.
[[nodiscard]] std::string long_timeline() {
    std::string text = "(A video, 10:00 long: what was on screen, and what was said.)\n";
    for (int at = 0; at < 600; at += 5) {
        const std::string stamp =
            std::to_string(at / 60) + ":" + (at % 60 < 10 ? "0" : "") + std::to_string(at % 60);
        text += "[" + stamp + "] screen: a slide numbered " + std::to_string(at) +
                " with a chart of the quarter's figures on it\n";
    }
    return text;
}

}  // namespace

TEST_CASE("a medium is read by the media reader the index is given, into indexed text",
          "[agentloop][attachments][media]") {
    const Scratch scratch;
    (void)scratch.write("talk.mp4", "not really a video");
    int asked = 0;
    AttachmentIndex index{scratch.dir.path() / "chat.db",
                          {},
                          std::nullopt,
                          [&asked](const FoundFile& file, apogee::harness::Medium medium,
                                   const apogee::harness::CancellationToken&) {
                              ++asked;
                              CHECK(file.name == "talk.mp4");
                              CHECK(medium == apogee::harness::Medium::Video);
                              return apogee::agentloop::AttachmentText{
                                  .text = long_timeline(),
                                  .reader = "timeline: eyes",
                                  .reason = {},
                                  .notes = {"talk.mp4: its sound was not "
                                            "transcribed"}};
                          }};
    const AttachmentIndex::Added added = index.add(scratch.found("talk.mp4"), {});
    REQUIRE(added.file.has_value());
    CHECK(asked == 1);
    CHECK(added.file->reader == "timeline: eyes");
    CHECK(added.note == "talk.mp4: its sound was not transcribed");
    REQUIRE(added.chunks > 2);

    // Each chunk is dated by the stamps its lines open with, and cited by them.
    const Store store{scratch.dir.path() / "chat.db"};
    const std::vector<apogee::embedstore::Chunk> chunks =
        store.chunks_by_source(attachment_source(added.file->sha256));
    const nlohmann::json first = nlohmann::json::parse(chunks.front().metadata);
    REQUIRE(first.contains("times"));
    CHECK(first["times"][0].get<double>() == 0.0);
    const nlohmann::json last = nlohmann::json::parse(chunks.back().metadata);
    CHECK(last["times"][1].get<double>() == 595.0);
    CHECK_FALSE(first.contains("lines"));
    const std::vector<apogee::agentloop::AttachmentExcerpt> excerpts =
        attachment_excerpts({SearchHit{.chunk = chunks[1], .score = 0.5}});
    REQUIRE(excerpts.size() == 1);
    const nlohmann::json second = nlohmann::json::parse(chunks[1].metadata);
    CHECK(excerpts.front().label ==
          "talk.mp4 " + apogee::agentloop::clock_time(second["times"][0].get<double>()) + "–" +
              apogee::agentloop::clock_time(second["times"][1].get<double>()));

    // The same file attached again is not read again.
    (void)index.add(scratch.found("talk.mp4"), {});
    CHECK(asked == 1);
}

TEST_CASE("a medium's reader failing skips it with its reason", "[agentloop][attachments][media]") {
    const Scratch scratch;
    (void)scratch.write("note.m4a", "audio");
    AttachmentIndex index{
        scratch.dir.path() / "chat.db",
        {},
        std::nullopt,
        [](const FoundFile&, apogee::harness::Medium, const apogee::harness::CancellationToken&) {
            return apogee::agentloop::AttachmentText{
                .text = {}, .reader = {}, .reason = "no model here hears audio"};
        }};
    const AttachmentIndex::Added added = index.add(scratch.found("note.m4a"), {});
    CHECK_FALSE(added.file.has_value());
    CHECK(added.skip == "note.m4a: no model here hears audio");

    // With no reader at all, a medium is refused by what it is.
    AttachmentIndex plain{scratch.dir.path() / "plain.db", {}, std::nullopt};
    CHECK(plain.add(scratch.found("note.m4a"), {}).skip ==
          "note.m4a: audio, which a model reads rather than as text");
}

TEST_CASE("the chunks covering a moment are found by their times", "[agentloop][attachments]") {
    const Scratch scratch;
    (void)scratch.write("talk.mp4", "video");
    (void)scratch.write("notes.md", "the quarter's figures on a chart\n");
    AttachmentIndex index{
        scratch.dir.path() / "chat.db",
        {},
        std::nullopt,
        [](const FoundFile&, apogee::harness::Medium, const apogee::harness::CancellationToken&) {
            return apogee::agentloop::AttachmentText{.text = long_timeline(),
                                                     .reader = "timeline: eyes"};
        }};
    const AttachmentIndex::Added talk = index.add(scratch.found("talk.mp4"), {});
    (void)index.add(scratch.found("notes.md"), {});
    const Store store{scratch.dir.path() / "chat.db"};

    const std::vector<SearchHit> at = apogee::agentloop::hits_at(store, {270.0}, {}, 2.0);
    REQUIRE_FALSE(at.empty());
    for (const SearchHit& hit : at) {
        CHECK(hit.score == 2.0);
        CHECK(hit.chunk.text.find("[4:30] screen") != std::string::npos);
    }
    CHECK(apogee::agentloop::hits_at(store, {}, {}, 1.0).empty());
    CHECK(apogee::agentloop::hits_at(store, {270.0}, {attachment_source(talk.file->sha256)}, 1.0)
              .empty());
    // Past the end, nothing.
    CHECK(apogee::agentloop::hits_at(store, {3600.0}, {}, 1.0).empty());
}
