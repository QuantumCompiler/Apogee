#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "commands/command.h"

/// `apogee analyze` -- run a named agent against an input, one-shot or
/// interactively, on any API-billing or local backend.
///
/// An agent is data (`agents:` in the config, or one of the bundled three)
/// executed by the SAME loop every other surface runs. What this command
/// adds over `complete --tools` is the agent's persona as the system prompt,
/// its tool policy as the registry filter, structured output validated
/// client-side, the in-process report renderer, and the quiet-save rule.
namespace apogee::commands {

/// The quiet-by-default rule for a one-shot run: a saved run at a terminal
/// prints only its `Saved:` line. The report still goes to stdout when the
/// user asked (`--show`), when raw JSON was requested, when stdout is a pipe
/// or a file (so `>` captures it), or when nothing was saved -- so the
/// result is never silently lost.
[[nodiscard]] bool should_print_report(bool show, bool json, bool stdout_is_tty, bool will_save);

/// `<base>-YYYYMMDD-HHMMSS.<extension>` in local time. No colon: Windows is
/// a target, and Ommi's `HH-MM-SS:MM-DD-YYYY` put one in every filename.
[[nodiscard]] std::string report_filename(std::string_view base, std::string_view extension,
                                          std::chrono::system_clock::time_point when);

/// How the answer is delivered.
enum class Rendering : std::uint8_t {
    /// Rendered to Markdown from JSON.
    Markdown,
    /// Rendered to plain text from JSON.
    Text,
    /// The JSON, pretty-printed, unrendered.
    Json,
    /// The answer as it came: prose, or JSON that was never a schema's.
    Raw,
};

/// Which rendering a run gets: `--json`, `--text`, `--markdown` in that
/// precedence; else Markdown when the agent has a schema and the answer
/// looks like JSON; else raw.
[[nodiscard]] Rendering resolve_rendering(bool json_flag, bool text_flag, bool markdown_flag,
                                          bool has_schema, std::string_view answer);

/// The file extension a rendering saves under.
[[nodiscard]] std::string_view extension_for(Rendering rendering) noexcept;

class AnalyzeCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
