#pragma once

#include <string>
#include <vector>

/// Captions as text (50b): timed cues written as SubRip (`.srt`), the
/// format the seam's caption burn reads. Pure -- no file, no process -- so
/// the composer (50c) times its cues from owned text and measured durations
/// and this only writes them down.
namespace apogee::harness {

/// One caption: what it says, and when -- seconds from the video's start.
struct CaptionCue {
    std::string text;
    double start = 0.0;
    double end = 0.0;
};

/// `cues` as an SRT document: numbered from 1, `HH:MM:SS,mmm --> …` times
/// (milliseconds rounded), each cue's text valid UTF-8 with its blank lines
/// closed up -- a blank line would end the cue early -- and a cue with no
/// text or no length left out.
///
/// Caption text is data, never styling: the characters a renderer reads as
/// markup -- `<` `>` (SubRip's tags), `{` `}` (ASS override blocks) and `\`
/// (ASS escapes such as `\N`) -- are written as their fullwidth forms
/// (`＜ ＞ ｛ ｝ ＼`), which look the same and mean nothing to one.
[[nodiscard]] std::string write_srt(const std::vector<CaptionCue>& cues);

/// `seconds` as an SRT time: `00:01:02,345`.
[[nodiscard]] std::string srt_time(double seconds);

}  // namespace apogee::harness
