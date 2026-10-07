#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

/// Renders `graph report`'s document (27m, `{"object":"graph.report",…}`)
/// as Markdown, **in-process** -- the report is assembled from the store
/// and rendered here, never by a model.
///
/// The sections in reading order -- overview, origin, hubs, communities,
/// cross-collection links (a named graph of two or more members), decisions,
/// orphans -- and the reserved `human_summary` LAST, the discipline every
/// report here keeps. A renderer of its own rather than `render_markdown`:
/// that one alphabetises sections and titles a list item by its longest
/// string, which reads well for a model's free-form report and badly for an
/// architecture page whose order is the point.
///
/// Deterministic: the same document renders the same bytes, so a report
/// over one store is byte-stable (the golden test holds it). Every name is a
/// code span whatever backticks it holds, every prose field one line.
namespace apogee::render {

[[nodiscard]] std::string render_graph_report(const nlohmann::json& report);

/// `text` as an inline code span that holds it whole: a fence one backtick
/// longer than its longest run, padded when it starts or ends with one;
/// line breaks as spaces.
[[nodiscard]] std::string code_span(std::string_view text);

}  // namespace apogee::render
