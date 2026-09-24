#include "commands/download_progress.h"

#include <algorithm>

#include "commands/thinking_view.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

constexpr std::int64_t kKiB = 1024;
constexpr std::int64_t kMiB = kKiB * 1024;
constexpr std::int64_t kGiB = kMiB * 1024;

/// A pipe's permanent line interval: legible in a log, never a flood.
constexpr std::int64_t kPipeInterval = 64 * kMiB;

constexpr std::string_view kEllipsis = "…";

StatusLine::Options status_options(const DownloadProgress::Options& options) {
    StatusLine::Options out;
    out.active = options.live;
    return out;
}

}  // namespace

std::string format_progress_size(std::int64_t bytes) {
    bytes = std::max<std::int64_t>(bytes, 0);
    if (bytes < kKiB) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < kMiB) {
        return std::to_string(bytes / kKiB) + " KiB";
    }
    if (bytes < kGiB) {
        return std::to_string(bytes / kMiB) + " MiB";
    }
    // Multiply first: kGiB / 10 is itself truncated, and dividing by it reads
    // a file a few bytes short of 2 GiB as "2.0".
    const std::int64_t tenths = bytes * 10 / kGiB;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " GiB";
}

std::string format_download_progress(std::int64_t written, std::int64_t total) {
    std::string out = format_progress_size(written);
    if (total <= 0) {
        return out;
    }
    const std::int64_t percent = std::clamp<std::int64_t>(written * 100 / total, 0, 100);
    out += " of " + format_progress_size(total) + " (" + std::to_string(percent) + "%)";
    return out;
}

std::string fit_tail(std::string_view text, std::size_t width) {
    const std::size_t cells = display_width(text);
    if (cells <= width) {
        return std::string{text};
    }
    if (width == 0) {
        return {};
    }
    // Drop codepoints from the front until the tail plus the ellipsis fits.
    std::size_t drop = cells - (width - 1);
    std::size_t cut = 0;
    while (drop > 0 && cut < text.size()) {
        ++cut;
        while (cut < text.size() && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
            ++cut;
        }
        --drop;
    }
    return std::string{kEllipsis} + std::string{text.substr(cut)};
}

DownloadProgress::DownloadProgress(std::ostream& out, Options options)
    : options_{options}, writer_{out}, status_{writer_, status_options(options_)} {}

DownloadProgress::~DownloadProgress() {
    status_.clear();
}

void DownloadProgress::bytes(std::int64_t written, std::int64_t total) {
    report(written, total);
}

void DownloadProgress::file(std::size_t index, std::size_t count, std::string_view relative,
                            std::int64_t written, std::int64_t size) {
    if (index != index_) {
        if (options_.live && index_ != 0) {
            // The previous file is done: it settles into the scrollback, and
            // the live line moves on to this one.
            status_.print_line(file_line());
        }
        index_ = index;
        counter_ = "[" + std::to_string(index) + "/" + std::to_string(count) + "]";
        name_ = std::string{relative};
        painted_.clear();
        last_report_ = 0;
        if (!options_.live) {
            // A log names each file as it starts, so a stall reads as the file
            // it stalled on.
            status_.print_line(file_line());
        }
    }
    report(written, size);
}

void DownloadProgress::finish() {
    if (options_.live && index_ != 0) {
        status_.print_line(file_line());
    } else {
        status_.clear();
    }
    index_ = 0;
    painted_.clear();
    last_report_ = 0;
}

void DownloadProgress::report(std::int64_t written, std::int64_t total) {
    if (options_.live) {
        std::string line = live_line(written, total);
        if (line != painted_) {
            status_.set(line);
            painted_ = std::move(line);
        }
        return;
    }
    if (written - last_report_ < kPipeInterval) {
        return;
    }
    last_report_ = written;
    status_.print_line((index_ == 0 ? "  " : "      ") + format_download_progress(written, total));
}

std::string DownloadProgress::file_line() const {
    return "  " + counter_ + " " + name_;
}

std::string DownloadProgress::live_line(std::int64_t written, std::int64_t total) const {
    const std::string progress = format_download_progress(written, total);
    if (index_ == 0) {
        return "  " + progress;
    }
    const std::string head = "  " + counter_ + " ";
    const std::string gap = "  ";
    // One column short of the width: writing the last column wraps the cursor
    // on some terminals, and a wrapped line is one the repaint cannot erase.
    const std::size_t used = display_width(head) + gap.size() + display_width(progress) + 1;
    const std::size_t room = options_.width > used ? options_.width - used : 0;
    return head + fit_tail(name_, room) + gap + progress;
}

DownloadProgress::Options stdout_download_options() {
    DownloadProgress::Options options;
    options.live = platform::is_terminal(platform::StandardStream::Out);
    options.width = static_cast<std::size_t>(std::max(platform::terminal_width().value_or(80), 1));
    return options;
}

}  // namespace apogee::commands
