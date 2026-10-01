#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/budget.h"
#include "agentloop/embed_func.h"
#include "embedstore/store.h"
#include "harness/cancellation.h"
#include "logger/session.h"

/// Attachments: documents, code and folders attached to a conversation (26d).
///
/// A file, a folder or a glob is read, indexed into the chat's own store --
/// embedded by the `embedding` role when one is configured, lexical-only when
/// not -- and from then on its parts that bear on each question are retrieved
/// and handed to the model, cited by file and page or line. A small local
/// model can then work over a 300-page PDF or a whole repository, which it
/// cannot do by pasting. One that fits the budget's attachment share is also
/// inlined, whole, on the message it was attached with.
///
/// This is the core every surface shares -- chat, `complete`, machine mode --
/// so an attachment is read, indexed and cited the same way on each. What
/// comes from a model arrives as a closure (`Embedder`), as it does for
/// `knowledge/` and `graph/`.
///
/// ## How the index is keyed
///
/// A file's chunks are stored under its content, `sha256:<hex>`, with its name
/// and its page or line range in each chunk's metadata. Before anything is
/// embedded, the other chats' indexes are searched for the same content under
/// the same embedding model, and a match is copied rather than processed again
/// -- the hash cache.
namespace apogee::agentloop {

/// A folder or glob this big asks first on a terminal and is refused on a
/// pipe: it protects against attaching a home directory by accident.
inline constexpr std::size_t kLargeAttachmentFiles = 500;
inline constexpr std::uint64_t kLargeAttachmentBytes = std::uint64_t{50} * 1024 * 1024;

/// A file found for an attachment, not yet read.
struct FoundFile {
    std::filesystem::path path;
    /// Its name as the model will cite it: relative to the working
    /// directory, `/`-separated, or absolute when it lies outside it.
    std::string name;
    std::uint64_t bytes = 0;
};

/// What an attachment spec names.
struct FoundFiles {
    std::vector<FoundFile> files;
    /// Files left out, each with why -- named, never silent. What git ignores
    /// and hidden entries are left out without a line each: that is what the
    /// user asked for by attaching a folder.
    std::vector<std::string> skips;
    std::uint64_t bytes = 0;
    /// Set when the spec names nothing at all.
    std::string error;

    /// Over `kLargeAttachmentFiles` files or `kLargeAttachmentBytes`.
    [[nodiscard]] bool large() const noexcept;
};

/// The files `spec` names, relative to `working_directory`: one file; a
/// folder, walked recursively, hidden entries skipped and, inside a git
/// repository, what git ignores (`git ls-files`); or a glob, `*` and `?`
/// within a name and `**` across folders.
[[nodiscard]] FoundFiles find_attachment_files(std::string_view spec,
                                               const std::filesystem::path& working_directory);

/// One file's text, as it is attached.
struct AttachmentText {
    std::string text;
    /// `text`, `pdftotext` or `html`.
    std::string reader;
    /// Why the file was not read, when it was not.
    std::string reason;

    [[nodiscard]] bool ok() const noexcept {
        return reason.empty();
    }
};

/// Reads `path`: text and code as they are (a binary one refused), a PDF
/// through `pdftotext` with its page breaks kept, HTML through the reader
/// `fetch_url` uses. Word, Excel and PowerPoint files are refused by name, and
/// so are images, audio and video, which are not read here.
[[nodiscard]] AttachmentText read_attachment_text(const std::filesystem::path& path);

/// The key a file's chunks are stored under.
[[nodiscard]] std::string attachment_source(std::string_view sha256);

/// One excerpt of an attached file, as retrieval hands it to the model.
struct AttachmentExcerpt {
    std::string source;
    /// `report.pdf p. 41`, `src/parser.cpp:120–168`.
    std::string label;
    std::string text;
    double score = 0.0;
};

/// `hits` from a chat's index as excerpts: each labelled from its chunk's
/// metadata, and adjacent chunks of one file merged into one excerpt with
/// their overlap removed. Best first.
[[nodiscard]] std::vector<AttachmentExcerpt> attachment_excerpts(
    const std::vector<embedstore::SearchHit>& hits);

/// The injected text for `excerpts`, each under its label.
[[nodiscard]] std::string render_attachment_excerpts(
    const std::vector<AttachmentExcerpt>& excerpts);

/// The code names `question` uses: a token with an underscore inside it
/// (`fitting_prefix`), a lower-to-upper case change inside it
/// (`parseConfig`), a `::` (`Store::search`), or a trailing `()`. A file name
/// is not one. Empty when there are none.
///
/// A question naming code is searched by its words for those names alone: an
/// embedding captures meaning, and an exact name carries little of it -- the
/// definition asked for ranks low by meaning and first by its name.
[[nodiscard]] std::vector<std::string> code_names_in(std::string_view question);

/// The block an inlined file rides its message as.
[[nodiscard]] std::string render_inline_attachment(std::string_view name, std::string_view text);

/// A chat's attachment index.
class AttachmentIndex {
public:
    /// `store_path` is this index; `others` the folder of the other chats'
    /// indexes, searched by content before anything is embedded (empty for
    /// none). With no `embedder`, the index is lexical-only.
    AttachmentIndex(std::filesystem::path store_path, std::filesystem::path others,
                    std::optional<Embedder> embedder);

    /// What adding one file did.
    struct Added {
        /// Set when it was indexed, or already was.
        std::optional<logger::AttachedFile> file;
        /// Why it was not.
        std::string skip;
        /// Copied from another chat's index rather than read and embedded.
        bool copied = false;
        /// This index already held its content.
        bool already = false;
        std::int64_t chunks = 0;
        /// Its chunks were embedded (or copied with their vectors).
        bool vectorised = false;
        /// Worth saying: why it is lexical-only when vectors were expected.
        std::string note;
    };

    /// Progress through one file: chunks embedded so far, of all.
    using Progress = std::function<void(std::size_t done, std::size_t total)>;

    /// Reads, chunks and indexes `file` -- or copies it from another chat's
    /// index when one holds the same content under the same embedding model,
    /// or does nothing when this one already does. Cancelling stops it before
    /// anything of the file is stored.
    [[nodiscard]] Added add(const FoundFile& file, const harness::CancellationToken& cancellation,
                            const Progress& progress = {});

    /// Whether this index holds content `sha256`.
    [[nodiscard]] bool holds(std::string_view sha256) const;

    /// A file's whole text, rebuilt exactly from its chunks' offsets.
    [[nodiscard]] std::string text_of(std::string_view sha256) const;

    /// Removes content `sha256`.
    void remove(std::string_view sha256);

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return store_path_;
    }

private:
    [[nodiscard]] Added held(const FoundFile& file, std::string_view sha256) const;
    [[nodiscard]] Added copy_from_others(const FoundFile& file, std::string_view sha256);
    [[nodiscard]] Added copy_from(const std::filesystem::path& other_path, const FoundFile& file,
                                  std::string_view sha256);
    /// The chunks' vectors -- empty for a lexical-only index, or when the
    /// embedder failed (`note` says so) -- or nullopt when cancelled.
    [[nodiscard]] std::optional<std::vector<std::vector<float>>> embed_chunks(
        const std::vector<std::string>& texts, std::string_view name,
        const harness::CancellationToken& cancellation, const Progress& progress,
        std::string& note) const;
    [[nodiscard]] std::string model() const;

    std::filesystem::path store_path_;
    std::filesystem::path others_;
    std::optional<Embedder> embedder_;
};

/// How many tokens `text` costs inlined as `name`, by `budget`'s count.
[[nodiscard]] std::int64_t inline_tokens(const TurnBudget& budget, std::string_view name,
                                         std::string_view text);

/// Whether an attachment of `tokens` is inlined beside `already` inlined
/// ones: when the window is known and both fit its attachment share. An
/// unknown window never reads as room, so nothing is inlined on one.
[[nodiscard]] bool fits_inline(const TurnBudget& budget, std::int64_t already, std::int64_t tokens);

}  // namespace apogee::agentloop
