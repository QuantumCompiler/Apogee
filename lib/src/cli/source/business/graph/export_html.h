#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "embedstore/store.h"
#include "graph/navigate.h"

/// `apogee graph export html` (27m): one self-contained interactive file --
/// a force-directed layout coloured by community, full-text search, and a
/// click on any entity opening its card.
///
/// **A file, never a server.** The page is `html_template()`, compiled in:
/// its CSS and script inline, no library, no CDN, no font, no image. The
/// graph is embedded as **one JSON block** (`<script type="application/json"
/// id="graph-data">`), escaped so it can neither close its element nor carry
/// a `//` (`embeddable_json`), and the page's Content-Security-Policy refuses
/// the browser every fetch -- it opens from `file://` offline, forever. The
/// never-listens invariant is why this is an export and not a `graph serve`.
///
/// **One source of truth.** The entities drawn are the top of 27l's degree
/// ranking (`rank_by_degree`), and each one's card is `node_card`'s payload,
/// serialised by its own `to_json` -- byte for byte what `apogee graph
/// explain <entity> --output-format json --max-neighbors 8` prints, less the
/// `matched` word a typed address adds.
///
/// **Bounded, and said.** At most `kHtmlMaxNodes` entities, by degree rank;
/// the unresolved names are left out (a name is not structure); the cap --
/// how many were drawn of how many -- is printed in the page itself and
/// carried in the data. GraphML is the uncapped escape hatch.
namespace apogee::graph {

/// The node cap, by degree rank -- the recorded default (2026-10-03).
inline constexpr std::size_t kHtmlMaxNodes = 2000;
/// Neighbours a card in the page lists per relation.
inline constexpr int kHtmlCardNeighbors = 6;

struct HtmlOptions {
    /// 1..kHtmlMaxNodes.
    std::size_t max_nodes = kHtmlMaxNodes;
};

/// The page, and what it holds.
struct HtmlExport {
    std::string html;
    /// The entities the ranking holds (every node but the unresolved names).
    std::size_t entities = 0;
    std::size_t shown = 0;
    /// The relations drawn: every edge between two entities shown.
    std::size_t relations = 0;
    std::int64_t unresolved_names = 0;
    bool capped = false;
    /// The sentence the page prints about its cap.
    std::string cap_note;
};

/// The page over `store`, called `graph`; `members` resolves the cards'
/// chunk mentions. Throws `NavigationError` for a cap outside
/// 1..kHtmlMaxNodes. Reads only; never a model.
[[nodiscard]] HtmlExport export_html(const embedstore::Store& store,
                                     const embedstore::MemberStores& members,
                                     std::string_view graph, const HtmlOptions& options);

[[nodiscard]] HtmlExport export_html(const OpenGraph& open, const HtmlOptions& options);

/// `data` as the page embeds it: compact JSON (invalid UTF-8 replaced, never
/// a throw) with `<`, `>` and `&` written `<`, `>`, `&` and
/// `/` written `\/` -- valid JSON that parses to the same value, and can
/// never end its script element, open a comment, or hold a `//`.
[[nodiscard]] std::string embeddable_json(const nlohmann::json& data);

/// `text` for HTML: `&`, `<`, `>`, `"` and `'` as entities.
[[nodiscard]] std::string html_escape(std::string_view text);

/// `page` with each `{{name}}` replaced by `values[name]`, in one pass --
/// a value is never scanned again. Throws `std::logic_error` naming a
/// placeholder `values` lacks, or one left unterminated.
[[nodiscard]] std::string fill_template(std::string_view page,
                                        const std::map<std::string, std::string>& values);

}  // namespace apogee::graph
