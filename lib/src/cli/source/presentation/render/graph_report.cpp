#include "render/graph_report.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace apogee::render {
namespace {

using nlohmann::json;

[[nodiscard]] std::string str(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

[[nodiscard]] std::int64_t num(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<std::int64_t>() : 0;
}

[[nodiscard]] const json& list(const json& object, const char* key) {
    static const json empty = json::array();
    const auto it = object.find(key);
    return it != object.end() && it->is_array() ? *it : empty;
}

[[nodiscard]] const json& section(const json& object, const char* key) {
    static const json empty = json::object();
    const auto it = object.find(key);
    return it != object.end() && it->is_object() ? *it : empty;
}

/// Prose on one line: every run of whitespace one space.
[[nodiscard]] std::string one_line(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += c;
    }
    return out;
}

[[nodiscard]] std::string count_of(std::int64_t count, std::string_view one,
                                   std::string_view many) {
    return std::to_string(count) + " " + std::string{count == 1 ? one : many};
}

/// A table cell: its pipes escaped.
[[nodiscard]] std::string cell(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '|') {
            out += "\\|";
        } else {
            out += c;
        }
    }
    return out;
}

/// `name` (kind, where) -- an entity as a list states it.
[[nodiscard]] std::string entity(const json& node) {
    std::string label = str(node, "type");
    if (const std::string file = str(node, "file"); !file.empty()) {
        label += ", " + code_span(file + ":" + std::to_string(num(node, "line")));
    } else if (node.value("unresolved", false)) {
        label += ", unresolved";
    } else if (const std::string status = str(node, "status"); !status.empty()) {
        label += ", " + status;
        if (const std::string discipline = str(node, "discipline"); !discipline.empty()) {
            label += ", " + discipline;
        }
    }
    return code_span(str(node, "name")) + " (" + label + ")";
}

[[nodiscard]] std::string arrow(const json& group) {
    return str(group, "direction") == "out" ? " -> " : " <- ";
}

/// `relation -> N: a, b, c, and M more` -- one relation group of 27l's.
[[nodiscard]] std::string group_line(const json& group) {
    const std::string line =
        str(group, "relation") + arrow(group) + std::to_string(num(group, "total"));
    const json& neighbors = list(group, "neighbors");
    std::string names;
    for (const json& neighbor : neighbors) {
        names += (names.empty() ? "" : ", ") + code_span(str(neighbor, "name"));
    }
    const std::int64_t more = num(group, "total") - static_cast<std::int64_t>(neighbors.size());
    if (more > 0) {
        names += ", and " + std::to_string(more) + " more";
    }
    return names.empty() ? line : line + ": " + names;
}

/// What a capped list says it is: nothing when it is whole, else `, the
/// <how> N of M`.
[[nodiscard]] std::string of_total(std::size_t shown, std::int64_t total, std::string_view how) {
    return std::cmp_equal(shown, total)
               ? std::string{}
               : ", the " + std::string{how} + " " + std::to_string(shown) + " of " +
                     std::to_string(total);
}

/// "none has a summary", "1 has a summary", "N have a summary".
[[nodiscard]] std::string summary_count(std::int64_t summarised) {
    if (summarised == 0) {
        return "none has a summary";
    }
    return summarised == 1 ? std::string{"1 has a summary"}
                           : std::to_string(summarised) + " have a summary";
}

void overview(std::string& out, const json& report) {
    const json& facts = section(report, "overview");
    out += "## Overview\n\n";
    std::string types;
    for (const auto& [type, count] : section(facts, "by_type").items()) {
        types +=
            (types.empty() ? "" : ", ") + type + " " + std::to_string(count.get<std::int64_t>());
    }
    out += "- **Entities:** " + std::to_string(num(facts, "entities")) +
           (types.empty() ? "" : " -- " + types) + "\n";
    out += "- **Relations:** " + std::to_string(num(facts, "relations")) + "\n";
    std::vector<std::string> members;
    for (const json& member : list(facts, "members")) {
        if (member.is_string() && !member.get<std::string>().empty()) {
            members.push_back(member.get<std::string>());
        }
    }
    if (!members.empty()) {
        std::string named;
        for (const std::string& member : members) {
            named += (named.empty() ? "" : ", ") + code_span(member);
        }
        out += "- **Members:** " + named + "\n";
    }
    if (const json& files = section(facts, "code_files"); !files.empty()) {
        std::int64_t total = 0;
        std::string languages;
        for (const auto& [language, count] : files.items()) {
            total += count.get<std::int64_t>();
            languages += (languages.empty() ? "" : ", ") + language + " " +
                         std::to_string(count.get<std::int64_t>());
        }
        out += "- **Code files:** " + std::to_string(total) + " -- " + languages + "\n";
    }
    if (const std::int64_t names = num(facts, "unresolved_names"); names > 0) {
        out += "- **Unresolved names:** " + std::to_string(names) +
               " -- references the parsed trees do not define, left out of every ranking below\n";
    }
    out += "\n";
}

void origin(std::string& out, const json& report) {
    const json& mix = section(report, "origin");
    out += "## Origin\n\n";
    out += count_of(num(mix, "relations"), "relation", "relations") + ": " +
           std::to_string(num(mix, "extracted")) + " extracted (parsed from source), " +
           std::to_string(num(mix, "inferred")) + " inferred (asserted by a model).\n\n";
}

void hubs(std::string& out, const json& report) {
    const json& hubs = section(report, "hubs");
    const json& shown = list(hubs, "shown");
    out += "## Hubs\n\n";
    if (shown.empty()) {
        out += "No entity to rank.\n\n";
        return;
    }
    out +=
        "Ranked by degree -- every relation that touches an entity, either way; unresolved "
        "names left out. " +
        (std::cmp_equal(shown.size(), num(hubs, "ranked"))
             ? "All " + count_of(num(hubs, "ranked"), "entity", "entities")
             : "The top " + std::to_string(shown.size()) + " of " +
                   count_of(num(hubs, "ranked"), "entity", "entities")) +
        ":\n\n";
    std::size_t rank = 0;
    for (const json& hub : shown) {
        const json& degree = section(hub, "degree");
        out += std::to_string(++rank) + ". " + entity(section(hub, "node")) + " -- " +
               count_of(num(degree, "total"), "relation", "relations") + " (" +
               std::to_string(num(degree, "out")) + " out, " + std::to_string(num(degree, "in")) +
               " in), " + count_of(num(hub, "mentions"), "mention", "mentions") + "\n";
        for (const json& group : list(hub, "relations")) {
            out += "    - " + group_line(group) + "\n";
        }
    }
    out += "\n";
}

void communities(std::string& out, const json& report, const std::string& graph) {
    const json& stored = section(report, "communities");
    const json& shown = list(stored, "shown");
    const std::int64_t total = num(stored, "total");
    out += "## Communities\n\n";
    if (total == 0) {
        out += "No communities are stored -- `apogee graph communities " + graph +
               "` clusters them (with `--no-summaries`, no model call).\n\n";
        return;
    }
    const std::int64_t summarised = total - num(stored, "unsummarised");
    out +=
        count_of(total, "community is stored", "communities are stored") + "; " +
        summary_count(summarised) +
        (summarised < total ? " -- `apogee graph communities " + graph + " -m <backend>` writes " +
                                  (summarised == 0 ? "them" : "the rest")
                            : std::string{}) +
        ". " + (std::cmp_equal(shown.size(), total) ? "Largest first:" : "The largest:") + "\n\n";
    for (const json& community : shown) {
        const std::int64_t size = num(community, "size");
        out += "### Community " + std::to_string(num(community, "id")) + " -- " +
               count_of(size, "member", "members") + "\n\n";
        const std::string summary = one_line(str(community, "summary"));
        out += summary.empty() ? "_No summary -- clustered with no model._\n\n" : summary + "\n\n";
        const json& members = list(community, "members");
        if (!members.empty()) {
            std::string named;
            for (const json& member : members) {
                named += (named.empty() ? "" : ", ") + code_span(str(member, "name")) + " (" +
                         str(member, "type") + ")";
            }
            const std::int64_t more = size - static_cast<std::int64_t>(members.size());
            out += "Members: " + named +
                   (more > 0 ? ", and " + std::to_string(more) + " more" : "") + ".\n\n";
        }
    }
    if (std::cmp_less(shown.size(), total)) {
        out += "And " +
               count_of(total - static_cast<std::int64_t>(shown.size()), "more community",
                        "more communities") +
               ".\n\n";
    }
}

[[nodiscard]] std::string member_list(const json& members) {
    std::string out;
    for (const json& member : members) {
        out += (out.empty() ? "" : ", ") + member.get<std::string>();
    }
    return out;
}

void links(std::string& out, const json& report) {
    const auto it = report.find("links");
    const std::vector<std::string> members = [&] {
        std::vector<std::string> named;
        for (const json& member : list(section(report, "overview"), "members")) {
            if (member.is_string() && !member.get<std::string>().empty()) {
                named.push_back(member.get<std::string>());
            }
        }
        return named;
    }();
    if (it == report.end() || !it->is_object()) {
        // A collection's own graph has nothing to link; a named graph of one
        // member says so.
        if (members.size() == 1) {
            out += "## Cross-collection links\n\nOne member, " + code_span(members.front()) +
                   " -- nothing to link.\n\n";
        }
        return;
    }
    const json& links = *it;
    const json& crossings = section(links, "crossings");
    const json& shared = section(links, "shared");
    std::string named;
    for (const std::string& member : members) {
        named += (named.empty() ? "" : ", ") + code_span(member);
    }
    out += "## Cross-collection links\n\n";
    out +=
        "Members (collections and source trees): " + named + ". " +
        count_of(num(crossings, "total"), "relation joins", "relations join") +
        " entities stated in different members; " +
        (num(shared, "total") == 0 ? std::string{"no entity is"}
                                   : count_of(num(shared, "total"), "entity is", "entities are")) +
        " stated in more than one.\n\n";
    const json& pairs = list(section(links, "pairs"), "shown");
    if (!pairs.empty()) {
        out += "| Members | Relations across | Entities in both |\n|---|---|---|\n";
        for (const json& pair : pairs) {
            out += "| " + cell(code_span(str(pair, "from")) + " -- " + code_span(str(pair, "to"))) +
                   " | " + std::to_string(num(pair, "relations")) + " | " +
                   std::to_string(num(pair, "shared")) + " |\n";
        }
        out += "\n";
    }
    if (const json& shown = list(crossings, "shown"); !shown.empty()) {
        out += "Relations across" + of_total(shown.size(), num(crossings, "total"), "heaviest") +
               ":\n\n";
        for (const json& crossing : shown) {
            const json& from = section(crossing, "from");
            const json& to = section(crossing, "to");
            out += "- " + code_span(str(from, "name")) + " (" + str(from, "type") + "; " +
                   member_list(list(from, "members")) + ") -[" + str(crossing, "relation") + "·" +
                   str(crossing, "origin") + "]-> " + code_span(str(to, "name")) + " (" +
                   str(to, "type") + "; " + member_list(list(to, "members")) + ")\n";
        }
        out += "\n";
    }
    if (const json& shown = list(shared, "shown"); !shown.empty()) {
        out += "Entities stated in more than one member" +
               of_total(shown.size(), num(shared, "total"), "most shared") + ":\n\n";
        for (const json& entry : shown) {
            out += "- " + entity(section(entry, "node")) + " -- in " +
                   member_list(list(entry, "members")) + "; " +
                   count_of(num(section(entry, "degree"), "total"), "relation", "relations") + "\n";
        }
        out += "\n";
    }
}

void decisions(std::string& out, const json& report) {
    const json& decisions = section(report, "decisions");
    const json& shown = list(decisions, "shown");
    const std::int64_t total = num(decisions, "total");
    if (total == 0) {
        return;  // a graph with no records has nothing to say here
    }
    out += "## Decisions\n\n";
    out += count_of(total, "decision record is attached", "decision records are attached") + ". " +
           (std::cmp_equal(shown.size(), total)
                ? "Most connected first:"
                : "The " + std::to_string(shown.size()) + " most connected:") +
           "\n\n";
    for (const json& decision : shown) {
        const json& node = section(decision, "node");
        std::string marker = str(node, "status");
        if (const std::string discipline = str(node, "discipline"); !discipline.empty()) {
            marker += (marker.empty() ? "" : ", ") + discipline;
        }
        out += "- " + code_span(str(node, "name")) + (marker.empty() ? "" : " (" + marker + ")");
        if (const std::string text = one_line(str(decision, "decision")); !text.empty()) {
            out += ": " + text;
        }
        out += "\n";
        const json& groups = list(decision, "relations");
        if (groups.empty()) {
            out += "  - concerns nothing in the graph\n";
        }
        for (const json& group : groups) {
            out += "  - " + group_line(group) + "\n";
        }
    }
    if (std::cmp_less(shown.size(), total)) {
        out +=
            "- and " + std::to_string(total - static_cast<std::int64_t>(shown.size())) + " more\n";
    }
    out += "\n";
}

void orphans(std::string& out, const json& report) {
    const json& orphans = section(report, "orphans");
    const json& shown = list(orphans, "shown");
    const std::int64_t total = num(orphans, "total");
    out += "## Orphans\n\n";
    if (total == 0) {
        out += "Every entity has at least one relation.\n\n";
        return;
    }
    out += count_of(total, "entity has", "entities have") +
           " no relation at all -- worth asking about. " +
           (std::cmp_equal(shown.size(), total)
                ? "Most mentioned first:"
                : "The " + std::to_string(shown.size()) + " most mentioned:") +
           "\n\n";
    for (const json& orphan : shown) {
        out += "- " + entity(section(orphan, "node")) + " -- " +
               count_of(num(orphan, "mentions"), "mention", "mentions");
        if (const std::string text = one_line(str(orphan, "description")); !text.empty()) {
            out += ": " + text;
        }
        out += "\n";
    }
    out += "\n";
}

}  // namespace

std::string code_span(std::string_view text_in) {
    std::string text;
    for (const char c : text_in) {
        text += (c == '\n' || c == '\r') ? ' ' : c;
    }
    std::size_t longest = 0;
    std::size_t run = 0;
    for (const char c : text) {
        run = c == '`' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    const std::string fence(longest + 1, '`');
    const bool pad = !text.empty() && (text.front() == '`' || text.back() == '`');
    return fence + (pad ? " " : "") + text + (pad ? " " : "") + fence;
}

std::string render_graph_report(const nlohmann::json& report) {
    const std::string graph = str(report, "graph");
    std::string out = "# Graph report: " + graph + "\n\n";
    out +=
        "Assembled from the store with no model call: every fact below is the graph's own, "
        "and every list is capped and says how many it had.\n\n";
    overview(out, report);
    origin(out, report);
    hubs(out, report);
    communities(out, report, graph);
    links(out, report);
    decisions(out, report);
    orphans(out, report);
    // The reserved field, always last.
    out += "## Summary\n\n" + one_line(str(report, "human_summary")) + "\n";
    return out;
}

}  // namespace apogee::render
