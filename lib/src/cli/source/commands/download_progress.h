#pragma once

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "commands/status_line.h"
#include "commands/terminal.h"

/// Byte progress for a download -- `models pull`, `models pull --safetensors`,
/// `datasets pull` -- on one line that repaints in place.
///
/// **A terminal gets one climbing line, not a scrolling log.** A 3 GiB shard
/// used to print fifty "N MiB of 3 GiB" lines, and a 51 GiB snapshot eight
/// hundred; on a terminal the count is now the `StatusLine`'s transient line,
/// and each file of a tree leaves exactly one permanent line behind. A pipe has
/// nothing to repaint, so it keeps the coarse permanent line per 64 MiB a log
/// can read.
namespace apogee::commands {

/// A size for a progress line: whole units up to MiB, GiB to one decimal.
///
/// The decimal is the point. Whole GiB left a 3 GiB shard reading "1 GiB of
/// 3 GiB" for a third of its transfer, which on a repainting line looks
/// exactly like a stalled download. Truncated, never rounded, so a file is
/// never shown as complete before its last byte.
[[nodiscard]] std::string format_progress_size(std::int64_t bytes);

/// "1.4 GiB of 3.2 GiB (43%)", or the count alone when the total is unknown.
[[nodiscard]] std::string format_download_progress(std::int64_t written, std::int64_t total);

/// `text` in at most `width` display cells, keeping its END behind a leading
/// "…" -- the distinctive part of a shard or dataset file name is its tail.
/// Codepoint-safe; a width of zero yields an empty string.
[[nodiscard]] std::string fit_tail(std::string_view text, std::size_t width);

class DownloadProgress {
public:
    struct Options {
        /// Repaint in place. False on a pipe, which gets permanent lines only.
        bool live = true;
        /// Terminal width. The live line is kept under it: a line that wraps
        /// cannot be erased by the repaint, and would scroll exactly as the
        /// per-increment lines did.
        std::size_t width = 80;
    };

    DownloadProgress(std::ostream& out, Options options);
    ~DownloadProgress();

    DownloadProgress(const DownloadProgress&) = delete;
    DownloadProgress& operator=(const DownloadProgress&) = delete;
    DownloadProgress(DownloadProgress&&) = delete;
    DownloadProgress& operator=(DownloadProgress&&) = delete;

    /// One file's bytes -- the `models::ProgressFn` shape.
    void bytes(std::int64_t written, std::int64_t total);

    /// A tree's per-file bytes -- the `models::TreeProgressFn` shape. A new
    /// `index` settles the previous file into its permanent line.
    void file(std::size_t index, std::size_t count, std::string_view relative, std::int64_t written,
              std::int64_t size);

    /// Ends the transfer: the live count goes, the last file's line stays.
    /// Call it before printing anything else, success or failure. Idempotent.
    void finish();

private:
    void report(std::int64_t written, std::int64_t total);
    [[nodiscard]] std::string file_line() const;
    [[nodiscard]] std::string live_line(std::int64_t written, std::int64_t total) const;

    Options options_;
    TerminalWriter writer_;
    StatusLine status_;
    /// The tree file in flight, 1-based; 0 for a single-file download.
    std::size_t index_ = 0;
    std::string counter_;
    std::string name_;
    /// What the live line last showed. A chunk arrives every few KiB; the line
    /// is repainted only when its text would actually change.
    std::string painted_;
    std::int64_t last_report_ = 0;
};

/// Options for a download reporting on stdout: live on a terminal, and sized
/// to it.
[[nodiscard]] DownloadProgress::Options stdout_download_options();

}  // namespace apogee::commands
