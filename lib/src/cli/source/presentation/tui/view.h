#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// A view the shell shows on its stage (32b): a title for the tab strip, and
/// what it draws and does -- a body built by `tui/`'s own sources over FTXUI,
/// which nothing outside `tui/` names (the link policy holds that).
namespace apogee::tui {

class View {
public:
    /// What the view draws and does: defined in `tui/view_body.h`, which only
    /// `tui/`'s sources include.
    struct Body;

    /// `takes_text`: the view's keys are typing -- a printable key, a number
    /// or `q` among them, goes to it and never switches a view or quits.
    View(std::string title, std::shared_ptr<Body> body, bool takes_text = false);
    /// As above, asked each time a key arrives: a view whose keys are typing
    /// only some of the time (the session view: its picker is not, its
    /// conversation is).
    View(std::string title, std::shared_ptr<Body> body, std::function<bool()> takes_text);

    [[nodiscard]] const std::string& title() const noexcept {
        return title_;
    }

    [[nodiscard]] bool takes_text() const {
        return takes_text_ ? takes_text_() : false;
    }

    [[nodiscard]] const std::shared_ptr<Body>& body() const noexcept {
        return body_;
    }

    /// Called each time the shell shows the view -- a list reading its data
    /// afresh (32d).
    void when_shown(std::function<void()> shown) {
        shown_ = std::move(shown);
    }

    void shown() const {
        if (shown_) {
            shown_();
        }
    }

    /// The command group the exec line is scoped to on this view (37h):
    /// `models` on Models, so `:pull …` there runs `models pull …`. Empty
    /// for a view that is no command's (Home, Keys).
    [[nodiscard]] const std::string& group() const noexcept {
        return group_;
    }

    void set_group(std::string group) {
        group_ = std::move(group);
    }

private:
    std::string title_;
    std::shared_ptr<Body> body_;
    std::function<bool()> takes_text_;
    std::function<void()> shown_;
    std::string group_;
};

/// A page of text: the lines it is given, top to bottom, scrolled with the
/// arrow keys and Page Up/Down when they outrun the stage -- what a view
/// rendering a read it was handed is, at its simplest.
[[nodiscard]] View text_view(std::string title, std::vector<std::string> lines);

}  // namespace apogee::tui
