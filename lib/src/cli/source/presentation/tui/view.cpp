#include "tui/view.h"

#include <algorithm>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <utility>

#include "tui/view_body.h"

namespace apogee::tui {

View::View(std::string title, std::shared_ptr<Body> body, bool takes_text)
    : title_{std::move(title)},
      body_{std::move(body)},
      takes_text_{[takes_text]() { return takes_text; }} {}

View::View(std::string title, std::shared_ptr<Body> body, std::function<bool()> takes_text)
    : title_{std::move(title)}, body_{std::move(body)}, takes_text_{std::move(takes_text)} {}

View text_view(std::string title, std::vector<std::string> lines) {
    struct Page {
        std::vector<std::string> lines;
        int offset = 0;
    };

    auto page = std::make_shared<Page>(Page{.lines = std::move(lines)});
    ftxui::Component body = ftxui::Renderer([page](bool /*focused*/) {
        ftxui::Elements rows;
        for (auto line = page->lines.begin() +
                         std::min<std::ptrdiff_t>(page->offset, std::ssize(page->lines));
             line != page->lines.end(); ++line) {
            // The frame's margin: a column in from the edge, as its own rows are.
            rows.push_back(ftxui::text(" " + *line));
        }
        return ftxui::vbox(std::move(rows));
    });
    body = ftxui::CatchEvent(body, [page](const ftxui::Event& event) {
        const int last = std::max(0, static_cast<int>(page->lines.size()) - 1);
        int step = 0;
        if (event == ftxui::Event::ArrowDown) {
            step = 1;
        } else if (event == ftxui::Event::ArrowUp) {
            step = -1;
        } else if (event == ftxui::Event::PageDown) {
            step = 10;
        } else if (event == ftxui::Event::PageUp) {
            step = -10;
        } else if (event == ftxui::Event::Home) {
            page->offset = 0;
            return true;
        } else {
            return false;
        }
        page->offset = std::clamp(page->offset + step, 0, last);
        return true;
    });
    return View{std::move(title), std::make_shared<View::Body>(View::Body{std::move(body)})};
}

}  // namespace apogee::tui
