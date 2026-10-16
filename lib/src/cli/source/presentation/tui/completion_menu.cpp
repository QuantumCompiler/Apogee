#include "tui/completion_menu.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ansi/text_width.h"

namespace apogee::tui {

std::vector<MenuItem> grouped(std::vector<MenuItem> items) {
    std::vector<std::string> order;
    for (const MenuItem& item : items) {
        if (std::ranges::find(order, item.group) == order.end()) {
            order.push_back(item.group);
        }
    }
    std::vector<MenuItem> out;
    out.reserve(items.size());
    for (const std::string& group : order) {
        for (MenuItem& item : items) {
            if (item.group == group) {
                out.push_back(std::move(item));
            }
        }
    }
    return out;
}

std::size_t menu_window(std::size_t chosen, std::size_t top, std::size_t rows, std::size_t count) {
    if (count == 0 || rows == 0) {
        return 0;
    }
    chosen = std::min(chosen, count - 1);
    top = std::min(top, chosen);
    if (chosen >= top + rows) {
        top = chosen + 1 - rows;
    }
    return top;
}

ftxui::Elements menu_rows(const std::vector<MenuItem>& items, std::size_t chosen, std::size_t top,
                          std::size_t rows, const Theme& theme) {
    using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
    Elements out;
    if (items.empty()) {
        return out;
    }
    const std::size_t count = items.size();
    const std::size_t end = std::min(count, top + rows);
    const auto label_of = [](const MenuItem& item) {
        return item.label.empty() ? item.text : item.label;
    };
    std::size_t widest = 0;
    for (std::size_t i = top; i < end; ++i) {
        widest = std::max(widest, ansi::display_width(label_of(items.at(i))));
    }
    if (top > 0) {
        out.push_back(text("   ↑ " + std::to_string(top) + " more") | dim);
    }
    for (std::size_t i = top; i < end; ++i) {
        const MenuItem& item = items.at(i);
        // A group's heading before its first item in the window -- or its
        // first in the window, when it began above it.
        if (!item.group.empty() && (i == top || items.at(i - 1).group != item.group)) {
            Element heading = text(" " + item.group) | bold;
            out.push_back(theme.color ? heading | color(Color::GrayLight) : heading);
        }
        const std::string label = label_of(item);
        std::string row = (i == chosen ? " › " : "   ") + label;
        if (!item.description.empty()) {
            row += std::string(widest - ansi::display_width(label) + 2, ' ') + item.description;
        }
        Element line = text(std::move(row));
        if (i == chosen) {
            line = theme.color ? line | bold | color(Color::Cyan) : line | bold;
        } else {
            line = line | dim;
        }
        out.push_back(std::move(line));
    }
    if (end < count) {
        out.push_back(text("   ↓ " + std::to_string(count - end) + " more") | dim);
    }
    return out;
}

}  // namespace apogee::tui
