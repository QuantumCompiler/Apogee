#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/chunk.h"

/// Getting files into the chunk store.
namespace apogee::embedstore {

/// What one ingest run did.
/// Turns a source's chunks into vectors, one per chunk. Empty means a
/// lexical-only ingest. Throws to report a failure; the walk records it for
/// that source and stores the chunks lexical-only rather than dropping them.
using EmbedChunks = std::function<std::vector<std::vector<float>>(const std::vector<std::string>&)>;

/// Hears each file as its turn comes: the 1-based place of `file` among
/// `total`, the whole walk counted before the first is read. A caller's busy
/// line says it; a test passes none (M1).
using IngestProgress =
    std::function<void(std::size_t done, std::size_t total, const std::filesystem::path& file)>;

/// A source an ingest wrote over (57s). Named, never silent: replacing a
/// document is changing what the collection answers from.
struct Replacement {
    std::string source;
    /// The folder the replaced copy came from, when that folder is gone: a
    /// moved corpus. Empty for a re-ingest from the same folder.
    std::string moved_from{};
    /// The replaced copy was stored before folders were recorded, so where it
    /// came from is unknown.
    bool unrecorded = false;
};

/// A file refused because a document of the same name, from another folder
/// that still exists, is already in the collection (57s). Nothing of it was
/// read into the store; `apogee embed delete <c> --source <name>` is the way
/// to let it in.
struct Collision {
    std::string source;
    /// The folder the copy in the collection came from.
    std::string root;
};

struct IngestReport {
    std::int64_t files_read = 0;
    std::int64_t files_skipped = 0;
    std::int64_t chunks_written = 0;

    /// Why each skipped file was skipped, e.g. "image.png: looks binary".
    ///
    /// **Named, never silent.** A corpus that quietly omitted half a directory
    /// answers questions wrongly and gives no clue why — the user believes the
    /// document is in there. Every skip says which file and what reason.
    std::vector<std::string> skips;
    /// Sources stored without vectors because embedding them failed. Each
    /// names the file and the reason; the store then holds lexical-only rows,
    /// which the resolver reports rather than hides.
    std::vector<std::string> unvectorised;
    std::int64_t vectors_written = 0;

    /// The folder this run's sources are named relative to and recorded
    /// under: absolute, symbolic links resolved, the generic form.
    std::string root{};
    /// What a directory walk left out (57s), relative to the folder, as
    /// `FolderListing::left_out` names it: hidden entries, what git ignores,
    /// vendored folders, each folder once. Counted always, named on asking.
    std::vector<std::string> left_out{};
    /// Every source this run wrote, in walk order: what the binding after a
    /// vector ingest reads as rewritten.
    std::vector<std::string> written{};
    /// The sources this run wrote over.
    std::vector<Replacement> replaced{};
    /// The files refused because their name is taken from another folder.
    std::vector<Collision> collisions{};
    /// Sources already in the collection that a directory walk now leaves
    /// out -- hidden or vendored by name, or what this walk left out -- from
    /// an earlier ingest. Never removed on Apogee's initiative: named, with
    /// the way to remove each.
    std::vector<std::string> still_held{};
};

/// Whether `bytes` look like a binary file rather than text.
///
/// A NUL byte in the first few kilobytes is the test, and it is the same one
/// `grep` has used for decades. It is not exact — no cheap test is — but it is
/// right about the cases that matter: images, archives and executables have
/// NULs, and prose does not.
///
/// Getting this wrong in the permissive direction fills a corpus with binary
/// noise that matches nothing and inflates every score's denominator.
[[nodiscard]] bool looks_binary(std::string_view bytes) noexcept;

/// Whether `path` is a PDF, by extension.
[[nodiscard]] bool is_pdf(const std::filesystem::path& path);

/// Extracts text from a PDF using `pdftotext`.
///
/// **Apogee's one optional external binary.** It is not bundled, not installed,
/// and not required: when it is absent PDFs are skipped with a named reason
/// rather than failing the run, because a PDF in a directory of Markdown should
/// not stop the Markdown being ingested. `apogee check` reports its presence as
/// an informational line for the same reason — its absence is not a fault.
[[nodiscard]] bool pdftotext_available();

/// Reads `path` as text, converting a PDF when possible.
///
/// Returns false with `reason` filled when the file should be skipped.
[[nodiscard]] bool read_as_text(const std::filesystem::path& path, std::string& text,
                                std::string& reason);

/// The folder `root` as an ingest records it: absolute, symbolic links
/// resolved where it exists (so macOS's `/tmp` and `/private/tmp` are one
/// folder), the generic form, no trailing separator.
[[nodiscard]] std::string folder_identity(const std::filesystem::path& root);

/// Ingests one file or every file under a directory into `store_path`.
///
/// Sources are recorded as paths **relative to the ingest root** when a
/// directory is given, so a corpus stays meaningful after it moves and two
/// machines produce the same source names.
///
/// **What a directory gives** (57s) is `list_folder`'s answer under the
/// ingest's rules: inside a git work tree, git's tracked and untracked but
/// not ignored files, else a walk; hidden entries and vendored folders left
/// out either way, and a folder git ignores whole walked, since it was named.
/// A file named on its own is always taken. Each source's folder is recorded
/// beside it, and a same-named document already in the store from another
/// folder that still exists is refused, never replaced.
///
/// `embed_model` names the model `embed` embeds with, recorded beside each
/// source it vectorises, so a store partly re-embedded under another model
/// can say which spaces it holds.
[[nodiscard]] IngestReport ingest_path(const std::filesystem::path& store_path,
                                       const std::filesystem::path& target,
                                       const ChunkOptions& options = {},
                                       const EmbedChunks& embed = {},
                                       const IngestProgress& progress = {},
                                       std::string_view embed_model = {});

}  // namespace apogee::embedstore
