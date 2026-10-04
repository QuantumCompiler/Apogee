#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/answer_view.h"
#include "markdown/layout.h"
#include "markdown/stream_renderer.h"
#include "support/terminal_model.h"

/// Real answers, recorded from the local models on 2026-09-25 with
/// `apogee complete --raw` (which pipes the model's text untouched), rendered
/// at the widths a terminal actually has. The bar the item set: every
/// construct models emit, in the shapes they actually emit it -- setext
/// headings from Llama 3.2, three-level lists with blank lines between items
/// from Qwen3-VL, a comparison table far wider than any screen from
/// Qwen3.8-27B, emoji in headings and table cells.
namespace {

using apogee::markdown::plain_text;
using apogee::markdown::RenderOps;
using apogee::markdown::Row;
using apogee::markdown::StreamRenderer;

[[nodiscard]] std::vector<std::filesystem::path> fixtures() {
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::directory_iterator{
             std::filesystem::path{APOGEE_TEST_FIXTURES} / "markdown"}) {
        if (entry.path().extension() == ".md") {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

[[nodiscard]] std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// Committed rows for `text` fed in pieces chosen by `next_piece`.
template <typename Pieces>
[[nodiscard]] std::vector<Row> render(const std::string& text, std::size_t width,
                                      Pieces next_piece) {
    StreamRenderer renderer;
    std::vector<Row> rows;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t length = std::min(next_piece(), text.size() - at);
        const RenderOps ops = renderer.feed(std::string_view{text}.substr(at, length), width);
        rows.insert(rows.end(), ops.commit.begin(), ops.commit.end());
        at += length;
    }
    const RenderOps last = renderer.finish(width);
    rows.insert(rows.end(), last.commit.begin(), last.commit.end());
    return rows;
}

}  // namespace

TEST_CASE("the recorded answers render the same however they are chunked", "[markdown][corpus]") {
    const std::vector<std::filesystem::path> files = fixtures();
    REQUIRE(files.size() >= 4);  // never vacuously
    for (const std::filesystem::path& file : files) {
        const std::string text = read(file);
        for (const std::size_t width : {39U, 79U, 119U}) {
            INFO(file.filename().string() << " at " << width);
            const std::vector<Row> whole = render(text, width, [&text] { return text.size(); });
            CHECK(render(text, width, [] { return std::size_t{1}; }) == whole);
            std::mt19937 random{static_cast<std::uint32_t>(width)};
            CHECK(render(text, width, [&random] { return std::size_t{1 + (random() % 9)}; }) ==
                  whole);

            bool table_rule = false;
            for (const Row& row : whole) {
                const std::string line = plain_text(row);
                CHECK(apogee::markdown::row_width(row) <= width);
                CHECK(line.find("**") == std::string::npos);
                CHECK(line.find("```") == std::string::npos);
                table_rule = table_rule || line.find("┼") != std::string::npos;
            }
            // Every recorded table fits laid out at 80 and 120 columns, its
            // wide cells wrapped within their columns.
            if (width >= 79 && text.find("|---") != std::string::npos) {
                CHECK(table_rule);
            }
        }
    }
}

TEST_CASE("the recorded answers paint to exactly their committed rows on a real-sized screen",
          "[markdown][corpus][ux]") {
    for (const std::filesystem::path& file : fixtures()) {
        const std::string text = read(file);
        for (const std::size_t width : {40U, 80U, 120U}) {
            INFO(file.filename().string() << " at " << width);
            std::ostringstream out;
            apogee::commands::AnswerView::Options options;
            options.out = &out;
            options.width = width;
            options.measure_height = [] { return std::size_t{24}; };
            apogee::commands::AnswerView view{options};
            view.begin();
            std::mt19937 random{static_cast<std::uint32_t>(width) + 7};
            for (std::size_t at = 0; at < text.size();) {
                const std::size_t length =
                    std::min<std::size_t>(1 + (random() % 13), text.size() - at);
                view.write(std::string_view{text}.substr(at, length));
                at += length;
            }
            view.finish();

            apogee::testing::TerminalModel screen{width, 24};
            screen.feed(out.str());
            CHECK(screen.unhandled().empty());
            CHECK_FALSE(screen.climbed_past_top());
            CHECK(screen.widest_column() + 1 < width);

            std::vector<std::string> expected;
            for (const Row& row : render(text, width - 1, [&text] { return text.size(); })) {
                std::string line = plain_text(row);
                line.erase(line.find_last_not_of(' ') + 1);
                expected.push_back(line);
            }
            while (!expected.empty() && expected.back().empty()) {
                expected.pop_back();
            }
            CHECK(screen.lines() == expected);
        }
    }
}
