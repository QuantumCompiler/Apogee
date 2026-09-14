#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>

/// Renders a schema-conforming JSON report as Markdown or plain text,
/// **in-process** -- no second model call, ever (decided 2026-09-13). Ommi's
/// reason holds: a second call to prettify a report costs money, time and
/// fidelity for a transformation a hundred lines of code do deterministically.
///
/// Top-level keys are alphabetised, except the reserved `human_summary`,
/// which is always rendered LAST: it is the copy-paste-ready section every
/// bundled schema ends with, and alphabetical order would land it mid-report.
/// Each top-level key becomes a `## Title`; arrays of objects become bullets
/// keyed by their longest string value with the rest as inline metadata.
///
/// A text that does not parse as JSON is returned unchanged -- rendering
/// never loses an answer.
namespace apogee::render {

/// The reserved last field.
inline constexpr std::string_view kHumanSummaryKey = "human_summary";

enum class ReportFormat : std::uint8_t { Markdown, Text };

/// `snake_case` or `camelCase` to "Title Case".
[[nodiscard]] std::string key_to_title(std::string_view key);

[[nodiscard]] std::string render_markdown(const nlohmann::json& report);
[[nodiscard]] std::string render_text(const nlohmann::json& report);

/// Strips a Markdown code fence a model may have wrapped the JSON in.
[[nodiscard]] std::string strip_code_fence(std::string_view text);

/// Renders `raw` in `format` when it parses as JSON (fences stripped),
/// else returns it unchanged.
[[nodiscard]] std::string render_report(std::string_view raw, ReportFormat format);

}  // namespace apogee::render
