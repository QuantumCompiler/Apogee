#pragma once

#include <string>
#include <string_view>

/// A finished run's notes, said once, on stderr, in the house voice
/// (CLI-M58). Not `business/tools/notes.h`, the notes tool.
///
/// The rule: stdout is a run's result -- its outcome, a listing, a count, a
/// read's qualifier, a dry run's prediction, next-step advice -- and every
/// other note about the run (a skip, a degraded mode, a non-fatal failure, a
/// consequence the result alone does not show) is one stderr line,
/// `apogee <verb>: <words>`, the verb the producer's (`graph` inside
/// `embed ingest --graph`). It is the shape the registration and validation
/// notes have always had (the ingest's registration note, `graph build`'s
/// plan note, `knowledge capture`'s notes), the one the full-screen shell's
/// runner states (`cli/tui_child.cpp`: a command narrates on stderr and says
/// its outcome on stdout at the end), and the one the Mac app keeps from a
/// finished job's stderr as a note (APP-M22). A JSON face carries a note as a
/// field instead, and an open job stream will get it as a `notice` (CLI-M60).
///
/// `std::cerr` is tied to `std::cout`, so a note said after a stdout line
/// follows it on a merged pipe. `--quiet` silences progress, never a note. A
/// note said while a busy line is live goes through `BusyLine::above`.
namespace apogee::commands {

/// `apogee <verb>: ` -- the house voice's prefix; the one place a note's
/// literal lives.
[[nodiscard]] std::string house_voice(std::string_view verb);

/// Says `text` as exactly one stderr line: `apogee <verb>: <text>\n`.
void say_note(std::string_view verb, std::string_view text);

}  // namespace apogee::commands
