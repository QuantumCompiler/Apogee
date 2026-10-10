#include "tui/paint.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/dom/requirement.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/util/autoreset.hpp>
#include <utility>

namespace apogee::tui {

namespace {

int g_frame_width =
    80;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the shell's thread's

[[nodiscard]] ftxui::Color to_color(ansi::Color color) {
    switch (color) {
        case ansi::Color::Red:
            return ftxui::Color::Red;
        case ansi::Color::Green:
            return ftxui::Color::Green;
        case ansi::Color::Yellow:
            return ftxui::Color::Yellow;
        case ansi::Color::Blue:
            return ftxui::Color::Blue;
        case ansi::Color::Magenta:
            return ftxui::Color::Magenta;
        case ansi::Color::Cyan:
            return ftxui::Color::Cyan;
        case ansi::Color::White:
            return ftxui::Color::White;
        case ansi::Color::BrightBlack:
            return ftxui::Color::GrayDark;
        case ansi::Color::Default:
            break;
    }
    return ftxui::Color::Default;
}

}  // namespace

ftxui::Element paint_row(const markdown::Row& row, const Theme& theme) {
    ftxui::Elements spans;
    for (const markdown::Span& span : row) {
        ftxui::Element element = ftxui::text(span.text);
        const ansi::TextAttributes& look = span.attributes;
        if (look.bold) {
            element = element | ftxui::bold;
        }
        if (look.dim) {
            element = element | ftxui::dim;
        }
        if (look.italic) {
            element = element | ftxui::italic;
        }
        if (look.underline) {
            element = element | ftxui::underlined;
        }
        if (look.strike) {
            element = element | ftxui::strikethrough;
        }
        if (theme.color && look.color != ansi::Color::Default) {
            element = element | ftxui::color(to_color(look.color));
        }
        if (!span.link.empty()) {
            element = element | ftxui::hyperlink(span.link);
        }
        spans.push_back(std::move(element));
    }
    return ftxui::hbox(std::move(spans));
}

namespace {

class Clip final : public ftxui::Node {
public:
    explicit Clip(ftxui::Element child) : Node(ftxui::Elements{std::move(child)}) {}

    void ComputeRequirement() override {
        Node::ComputeRequirement();
        // Any height will do, and it takes what there is: the child is laid
        // out to the box, not the box to the child.
        requirement_.min_y = 0;
        requirement_.flex_grow_y = 1;
        requirement_.flex_shrink_y = 1;
        // What the child focuses scrolls the child's own frames, never the
        // stage around it.
        requirement_.focused = ftxui::Requirement::Focused{};
    }

    void SetBox(ftxui::Box box) override {
        Node::SetBox(box);
        children_[0]->SetBox(box);
    }

    void Render(ftxui::Screen& screen) override {
        const ftxui::AutoReset<ftxui::Box> stencil(&screen.stencil,
                                                   ftxui::Box::Intersection(box_, screen.stencil));
        children_[0]->Render(screen);
    }
};

}  // namespace

ftxui::Element clip(ftxui::Element child) {
    return std::make_shared<Clip>(std::move(child));
}

int frame_width() noexcept {
    return g_frame_width;
}

void set_frame_width(int width) noexcept {
    if (width > 0) {
        g_frame_width = width;
    }
}

}  // namespace apogee::tui
