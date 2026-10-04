#pragma once

#include <string>
#include <vector>

#include "ansi/ansi.h"

/// The data the Markdown renderer hands its painter. Data only.
namespace apogee::markdown {

/// One run of text with one look.
struct Span {
    std::string text;
    ansi::TextAttributes attributes;
    /// Where the text links to, painted as an OSC 8 hyperlink; empty for none.
    std::string link;

    bool operator==(const Span&) const = default;
};

/// One screen row: spans laid end to end, already wrapped to fit. An empty
/// row is a blank line.
using Row = std::vector<Span>;

/// What one step of rendering changed on screen.
///
/// Operations, not bytes, so the logic is tested with no terminal and the
/// painter alone owns the escape codes.
struct RenderOps {
    /// Rows that are final: painted once, below everything before them, and
    /// never touched again.
    std::vector<Row> commit;
    /// The open area as it now stands -- the line still arriving, and a table
    /// still streaming -- replacing whatever the open area showed before.
    std::vector<Row> open;
};

}  // namespace apogee::markdown
