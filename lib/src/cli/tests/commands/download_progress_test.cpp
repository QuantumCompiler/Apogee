#include "commands/download_progress.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "ansi/ansi.h"
#include "commands/thinking_view.h"

using apogee::ansi::kEraseLine;
using apogee::commands::display_width;
using apogee::commands::DownloadProgress;
using apogee::commands::fit_tail;
using apogee::commands::format_download_progress;
using apogee::commands::format_progress_size;

namespace {

constexpr std::int64_t kMiB = 1024LL * 1024;
constexpr std::int64_t kGiB = kMiB * 1024;

/// What a terminal would show for `bytes`: the lines that scrolled into
/// history, and the one the cursor is still on. The erase sequence is the only
/// escape the progress line writes, so it is the only one interpreted.
struct Screen {
    std::vector<std::string> scrollback;
    std::string current;
};

Screen render(const std::string& bytes) {
    Screen screen;
    for (std::size_t i = 0; i < bytes.size();) {
        if (bytes.compare(i, kEraseLine.size(), kEraseLine) == 0) {
            screen.current.clear();
            i += kEraseLine.size();
        } else if (bytes[i] == '\n') {
            screen.scrollback.push_back(screen.current);
            screen.current.clear();
            ++i;
        } else {
            screen.current += bytes[i];
            ++i;
        }
    }
    return screen;
}

DownloadProgress::Options live(std::size_t width = 80) {
    DownloadProgress::Options options;
    options.live = true;
    options.width = width;
    return options;
}

DownloadProgress::Options piped() {
    DownloadProgress::Options options;
    options.live = false;
    return options;
}

/// Streams one tree file the way `acquire_file` reports it: a callback per chunk.
void stream_file(DownloadProgress& progress, std::size_t index, std::size_t count,
                 const std::string& name, std::int64_t size, std::int64_t chunk) {
    std::int64_t written = 0;
    while (written < size) {
        written = std::min(written + chunk, size);
        progress.file(index, count, name, written, size);
    }
}

}  // namespace

TEST_CASE("progress sizes show GiB to a tenth, truncated", "[ux][download]") {
    CHECK(format_progress_size(0) == "0 B");
    CHECK(format_progress_size(1023) == "1023 B");
    CHECK(format_progress_size(1536) == "1 KiB");
    CHECK(format_progress_size(5 * kMiB) == "5 MiB");
    CHECK(format_progress_size(1023 * kMiB) == "1023 MiB");
    CHECK(format_progress_size(kGiB) == "1.0 GiB");
    CHECK(format_progress_size(kGiB + kGiB / 2) == "1.5 GiB");
    // Truncated, never rounded: a file one byte short of 2 GiB is not "2.0".
    CHECK(format_progress_size(2 * kGiB - 1) == "1.9 GiB");
    CHECK(format_progress_size(51 * kGiB + 308 * kMiB) == "51.3 GiB");
    CHECK(format_progress_size(-5) == "0 B");
}

TEST_CASE("a progress line carries the total and a percentage when both are known",
          "[ux][download]") {
    CHECK(format_download_progress(1434 * kMiB, 3277 * kMiB) == "1.4 GiB of 3.2 GiB (43%)");
    CHECK(format_download_progress(64 * kMiB, 0) == "64 MiB");
    CHECK(format_download_progress(3 * kGiB, 3 * kGiB) == "3.0 GiB of 3.0 GiB (100%)");
    // A source that over-delivers is the verifier's problem, not the bar's.
    CHECK(format_download_progress(4 * kGiB, 3 * kGiB) == "4.0 GiB of 3.0 GiB (100%)");
}

TEST_CASE("fit_tail keeps the end of a name, codepoint-safe", "[ux][download]") {
    CHECK(fit_tail("config.json", 20) == "config.json");
    CHECK(fit_tail("config.json", 11) == "config.json");
    CHECK(fit_tail("model-00001-of-00018.safetensors", 12) == "…safetensors");
    CHECK(display_width(fit_tail("model-00001-of-00018.safetensors", 12)) == 12);
    CHECK(fit_tail("abc", 1) == "…");
    CHECK(fit_tail("abc", 0).empty());
    // Two-byte codepoints are dropped whole, never split.
    const std::string accented = "données-été.parquet";
    const std::string fitted = fit_tail(accented, 10);
    CHECK(display_width(fitted) == 10);
    CHECK(fitted == "…é.parquet");
}

TEST_CASE("a live single-file download repaints one line and leaves nothing behind",
          "[ux][download]") {
    std::ostringstream out;
    DownloadProgress progress{out, live()};

    for (std::int64_t written = kMiB; written <= 300 * kMiB; written += kMiB) {
        progress.bytes(written, 300 * kMiB);
    }
    const Screen during = render(out.str());
    CHECK(during.scrollback.empty());
    CHECK(during.current == "  300 MiB of 300 MiB (100%)");

    progress.finish();
    const Screen after = render(out.str());
    CHECK(after.scrollback.empty());
    CHECK(after.current.empty());
}

TEST_CASE("a live line is repainted only when its text changes", "[ux][download]") {
    std::ostringstream out;
    DownloadProgress progress{out, live()};

    // Sixteen chunks inside one displayed MiB: one paint, not sixteen.
    for (std::int64_t chunk = 1; chunk <= 16; ++chunk) {
        progress.bytes(kMiB + chunk * 1024, 3 * kGiB);
    }
    const std::string bytes = out.str();
    std::size_t paints = 0;
    for (std::size_t at = bytes.find(kEraseLine); at != std::string::npos;
         at = bytes.find(kEraseLine, at + 1)) {
        ++paints;
    }
    CHECK(paints == 1);
}

TEST_CASE("a live tree download leaves exactly one line per file", "[ux][download]") {
    // The shape of the report that motivated this: small files, then a shard
    // large enough that the old reporter printed a line per 64 MiB of it.
    std::ostringstream out;
    DownloadProgress progress{out, live()};

    stream_file(progress, 1, 3, "config.json", 700, 700);
    stream_file(progress, 2, 3, "model-00001-of-00018.safetensors", 3 * kGiB, 16 * kMiB);
    stream_file(progress, 3, 3, "tokenizer.json", 11 * kMiB, kMiB);

    const Screen during = render(out.str());
    REQUIRE(during.scrollback.size() == 2);
    CHECK(during.scrollback[0] == "  [1/3] config.json");
    CHECK(during.scrollback[1] == "  [2/3] model-00001-of-00018.safetensors");
    CHECK(during.current == "  [3/3] tokenizer.json  11 MiB of 11 MiB (100%)");

    progress.finish();
    const Screen after = render(out.str());
    REQUIRE(after.scrollback.size() == 3);
    CHECK(after.scrollback[2] == "  [3/3] tokenizer.json");
    CHECK(after.current.empty());
    for (const std::string& line : after.scrollback) {
        CHECK(line.find(" of ") == std::string::npos);
    }

    // Idempotent: a second finish prints nothing more.
    const std::size_t before = out.str().size();
    progress.finish();
    CHECK(out.str().size() == before);
}

TEST_CASE("a live line stays narrower than the terminal", "[ux][download]") {
    // A line that wraps cannot be erased by the repaint, so a long name must
    // give way rather than push the count onto a second row.
    constexpr std::size_t kWidth = 50;
    std::ostringstream out;
    DownloadProgress progress{out, live(kWidth)};

    const std::string name = "data/train-00000-of-00042-9f3c1a7b2d4e6f80.parquet";
    progress.file(1, 42, name, 900 * kMiB, 3 * kGiB);

    const Screen screen = render(out.str());
    CHECK(display_width(screen.current) < kWidth);
    CHECK(screen.current.find("[1/42] …") != std::string::npos);
    CHECK(screen.current.ends_with(".parquet  900 MiB of 3.0 GiB (29%)"));

    // The permanent line is never truncated: scrollback may wrap.
    progress.finish();
    CHECK(render(out.str()).scrollback.back() == "  [1/42] " + name);
}

TEST_CASE("a piped download keeps a coarse permanent line and no escapes", "[ux][download]") {
    SECTION("single file") {
        std::ostringstream out;
        DownloadProgress progress{out, piped()};
        for (std::int64_t written = kMiB; written <= 200 * kMiB; written += kMiB) {
            progress.bytes(written, 200 * kMiB);
        }
        progress.finish();

        const std::string bytes = out.str();
        CHECK(bytes.find('\033') == std::string::npos);
        CHECK(bytes.find('\r') == std::string::npos);
        CHECK(bytes ==
              "  64 MiB of 200 MiB (32%)\n"
              "  128 MiB of 200 MiB (64%)\n"
              "  192 MiB of 200 MiB (96%)\n");
    }

    SECTION("tree") {
        std::ostringstream out;
        DownloadProgress progress{out, piped()};
        stream_file(progress, 1, 2, "config.json", 700, 700);
        stream_file(progress, 2, 2, "model.safetensors", 130 * kMiB, kMiB);
        progress.finish();

        CHECK(out.str() ==
              "  [1/2] config.json\n"
              "  [2/2] model.safetensors\n"
              "      64 MiB of 130 MiB (49%)\n"
              "      128 MiB of 130 MiB (98%)\n");
    }
}
