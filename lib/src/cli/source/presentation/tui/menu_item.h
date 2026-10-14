#pragma once

#include <string>
#include <vector>

/// One row of a completion menu (45b) -- the shell's own surfaces only: the
/// exec line and the session's input. Its group and description travel in
/// the process and never on `__complete`'s wire, which stays bare strings.
namespace apogee::tui {

struct MenuItem {
    /// What choosing it puts in place of the word being completed.
    std::string text;
    /// What the row shows; empty means `text`.
    std::string label;
    /// The heading it sits under -- `configured`, a roster with the day it
    /// was fetched, `local` -- or empty for none.
    std::string group;
    /// One line beside it, from what the public reads already show.
    std::string description;
};

/// `items` with each group's together, the groups in the order each first
/// appears and the items within one as they came: what a menu draws under
/// its headings, and what its choice indexes.
[[nodiscard]] std::vector<MenuItem> grouped(std::vector<MenuItem> items);

}  // namespace apogee::tui
