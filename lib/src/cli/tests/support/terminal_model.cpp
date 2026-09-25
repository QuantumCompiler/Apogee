#include "support/terminal_model.h"

#include <algorithm>

#include "ansi/text_width.h"

namespace apogee::testing {

TerminalModel::TerminalModel(std::size_t width, std::size_t height)
    : width_{width}, height_{height} {
    rows_.emplace_back();
}

void TerminalModel::ensure_row() {
    while (rows_.size() <= row_) {
        rows_.emplace_back();
    }
}

void TerminalModel::put(std::string_view cell, std::size_t cells) {
    if (pending_wrap_ || column_ + cells > width_) {
        ++row_;
        column_ = 0;
        pending_wrap_ = false;
        ensure_row();
    }
    std::vector<std::string>& row = rows_[row_];
    if (row.size() < column_ + cells) {
        row.resize(column_ + cells, " ");
    }
    row[column_] = std::string{cell};
    for (std::size_t extra = 1; extra < cells; ++extra) {
        row[column_ + extra] = "";
    }
    widest_ = std::max(widest_, column_ + cells - 1);
    column_ += cells;
    if (column_ >= width_) {
        column_ = width_ - 1;
        pending_wrap_ = true;
    }
}

void TerminalModel::feed(std::string_view bytes) {
    for (std::size_t i = 0; i < bytes.size();) {
        const char c = bytes[i];
        if (c == '\r') {
            column_ = 0;
            pending_wrap_ = false;
            ++i;
            continue;
        }
        if (c == '\n') {
            // The tty's ONLCR: a newline is a carriage return and a line feed.
            ++row_;
            column_ = 0;
            pending_wrap_ = false;
            ensure_row();
            ++i;
            continue;
        }
        if (c == '\033') {
            if (i + 1 < bytes.size() && bytes[i + 1] == '[') {
                std::size_t end = i + 2;
                while (end < bytes.size() && !((bytes[end] >= 'A' && bytes[end] <= 'Z') ||
                                               (bytes[end] >= 'a' && bytes[end] <= 'z'))) {
                    ++end;
                }
                if (end >= bytes.size()) {
                    unhandled_.emplace_back(bytes.substr(i));
                    return;
                }
                const std::string params{bytes.substr(i + 2, end - i - 2)};
                const char command = bytes[end];
                if (command == 'm') {
                    // colour and style: not modelled
                } else if (command == 'A') {
                    const std::size_t count = params.empty() ? 1 : std::stoul(params);
                    const std::size_t top = rows_.size() > height_ ? rows_.size() - height_ : 0;
                    if (row_ < top + count) {
                        climbed_ = true;
                        row_ = top;
                    } else {
                        row_ -= count;
                    }
                    pending_wrap_ = false;
                } else if (command == 'K' && params == "2") {
                    rows_[row_].clear();
                } else if (command == 'K' && (params.empty() || params == "0")) {
                    if (rows_[row_].size() > column_) {
                        rows_[row_].resize(column_);
                    }
                } else {
                    unhandled_.emplace_back(bytes.substr(i, end - i + 1));
                }
                i = end + 1;
                continue;
            }
            if (i + 1 < bytes.size() && bytes[i + 1] == ']') {
                // OSC: runs to BEL or ST (ESC \).
                std::size_t end = i + 2;
                while (
                    end < bytes.size() && bytes[end] != '\a' &&
                    !(bytes[end] == '\033' && end + 1 < bytes.size() && bytes[end + 1] == '\\')) {
                    ++end;
                }
                i = end < bytes.size() && bytes[end] == '\a' ? end + 1 : end + 2;
                continue;
            }
            unhandled_.emplace_back(bytes.substr(i, 2));
            i += 2;
            continue;
        }
        const std::size_t length =
            std::min(ansi::utf8_sequence_length(static_cast<unsigned char>(c)), bytes.size() - i);
        const std::string_view cell = bytes.substr(i, length);
        const std::size_t cells = ansi::codepoint_cells(ansi::decode_utf8(bytes, i, length));
        if (cells == 0) {
            // A combining mark joins the character before it.
            if (column_ > 0 && !rows_[row_].empty()) {
                rows_[row_][std::min(column_, rows_[row_].size()) - 1] += cell;
            }
        } else {
            put(cell, cells);
        }
        i += length;
    }
}

std::vector<std::string> TerminalModel::lines() const {
    std::vector<std::string> out;
    for (const std::vector<std::string>& row : rows_) {
        std::string line;
        for (const std::string& cell : row) {
            line += cell;
        }
        const std::size_t last = line.find_last_not_of(' ');
        out.push_back(last == std::string::npos ? std::string{} : line.substr(0, last + 1));
    }
    while (!out.empty() && out.back().empty()) {
        out.pop_back();
    }
    return out;
}

std::string TerminalModel::text() const {
    std::string joined;
    for (const std::string& line : lines()) {
        joined += line;
        joined += '\n';
    }
    return joined;
}

}  // namespace apogee::testing
