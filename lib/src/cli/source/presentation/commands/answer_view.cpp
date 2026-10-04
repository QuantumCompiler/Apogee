#include "commands/answer_view.h"

#include <algorithm>
#include <utility>

namespace apogee::commands {

AnswerView::AnswerView(Options options)
    : options_{std::move(options)},
      renderer_{markdown::StreamRenderer::Options{.hyperlinks = options_.hyperlinks}} {}

void AnswerView::begin() {
    finish();
    renderer_.reset();
    shown_.clear();
    painted_ = 0;
    active_ = true;
}

void AnswerView::write(std::string_view chunk) {
    if (!active_ || chunk.empty()) {
        return;
    }
    paint(renderer_.feed(chunk, content_width()));
}

void AnswerView::finish() {
    if (!active_) {
        return;
    }
    active_ = false;
    paint(renderer_.finish(content_width()));
}

std::size_t AnswerView::content_width() const {
    std::size_t width = options_.measure_width ? options_.measure_width() : 0;
    if (width == 0) {
        width = options_.width;
    }
    // One short of the terminal: see the class comment.
    return width > 1 ? width - 1 : 1;
}

std::size_t AnswerView::max_open_rows() const {
    const std::size_t height = options_.measure_height ? options_.measure_height() : 0;
    if (height == 0) {
        return 0;  // unknown: no limit
    }
    // The row the next prompt needs, and one to spare.
    return height > 3 ? height - 2 : 1;
}

std::string AnswerView::render_row(const markdown::Row& row) const {
    std::string out;
    for (const markdown::Span& span : row) {
        std::string text = options_.style.paint(span.text, span.attributes);
        if (options_.hyperlinks && options_.style.color_enabled() && !span.link.empty()) {
            text = ansi::hyperlink(text, span.link);
        }
        out += text;
    }
    return out;
}

void AnswerView::paint(const markdown::RenderOps& ops) {
    if (options_.out == nullptr) {
        return;
    }
    std::vector<markdown::Row> open = ops.open;
    if (const std::size_t limit = max_open_rows(); limit > 0 && open.size() > limit) {
        open.erase(open.begin(), open.end() - static_cast<std::ptrdiff_t>(limit));
    }
    if (ops.commit.empty() && open == shown_) {
        return;
    }

    std::string bytes;
    if (painted_ > 0) {
        // Back to the start of the open area's first row, erasing as it goes.
        bytes += ansi::kEraseLine;
        for (std::size_t row = 1; row < painted_; ++row) {
            bytes += ansi::kUpAndErase;
        }
    }
    for (const markdown::Row& row : ops.commit) {
        bytes += render_row(row);
        bytes += '\n';
    }
    for (std::size_t row = 0; row < open.size(); ++row) {
        bytes += render_row(open[row]);
        if (row + 1 < open.size()) {
            bytes += '\n';
        }
    }
    painted_ = open.size();
    shown_ = std::move(open);
    options_.out->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    options_.out->flush();
}

}  // namespace apogee::commands
