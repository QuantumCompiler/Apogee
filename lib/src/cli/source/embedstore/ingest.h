#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "embedstore/chunk.h"

/// Getting files into the chunk store.
namespace apogee::embedstore {

/// What one ingest run did.
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

/// Ingests one file or every file under a directory into `store_path`.
///
/// Sources are recorded as paths **relative to the ingest root** when a
/// directory is given, so a corpus stays meaningful after it moves and two
/// machines produce the same source names.
[[nodiscard]] IngestReport ingest_path(const std::filesystem::path& store_path,
                                       const std::filesystem::path& target,
                                       const ChunkOptions& options = {});

}  // namespace apogee::embedstore
