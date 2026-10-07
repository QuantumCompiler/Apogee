#include "graph/export_graphml.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "embedstore/graph.h"

namespace apogee::graph {
namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";  // U+FFFD

/// The length of the valid UTF-8 sequence at `text[at]` and its code point,
/// or 0 when the bytes there are not one.
[[nodiscard]] std::size_t utf8_sequence(std::string_view text, std::size_t at,
                                        std::uint32_t& point) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(at);
    std::size_t length = 0;
    if (lead < 0x80U) {
        point = lead;
        return 1;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
        point = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
        point = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
        point = lead & 0x07U;
    } else {
        return 0;
    }
    if (at + length > text.size()) {
        return 0;
    }
    for (std::size_t i = 1; i < length; ++i) {
        if ((byte(at + i) & 0xC0U) != 0x80U) {
            return 0;
        }
        point = (point << 6U) | (byte(at + i) & 0x3FU);
    }
    // Overlong forms, surrogates and points past U+10FFFF are not UTF-8.
    const bool overlong = (length == 3 && point < 0x800U) || (length == 4 && point < 0x10000U);
    if (overlong || (point >= 0xD800U && point <= 0xDFFFU) || point > 0x10FFFFU) {
        return 0;
    }
    return length;
}

/// Whether XML 1.0 can carry the code point at all.
[[nodiscard]] bool xml_char(std::uint32_t point) {
    return point == 0x9U || point == 0xAU || point == 0xDU ||
           (point >= 0x20U && point <= 0xD7FFU) || (point >= 0xE000U && point <= 0xFFFDU) ||
           (point >= 0x10000U && point <= 0x10FFFFU);
}

[[nodiscard]] std::string number(double value) {
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.6g", value);
    return std::string{buffer.data(), static_cast<std::size_t>(written > 0 ? written : 0)};
}

void data(std::string& out, std::string_view key, std::string_view value) {
    out += "      <data key=\"";
    out += key;
    out += "\">";
    out += xml_escape(value);
    out += "</data>\n";
}

void data(std::string& out, std::string_view key, std::int64_t value) {
    data(out, key, std::to_string(value));
}

void key(std::string& out, std::string_view id, std::string_view domain, std::string_view name,
         std::string_view type) {
    out += "  <key id=\"";
    out += id;
    out += "\" for=\"";
    out += domain;
    out += "\" attr.name=\"";
    out += name;
    out += "\" attr.type=\"";
    out += type;
    out += "\"/>\n";
}

}  // namespace

std::string xml_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t at = 0;
    while (at < text.size()) {
        std::uint32_t point = 0;
        const std::size_t length = utf8_sequence(text, at, point);
        if (length == 0) {
            out += kReplacement;
            ++at;
            continue;
        }
        if (!xml_char(point)) {
            out += kReplacement;
        } else if (point == '&') {
            out += "&amp;";
        } else if (point == '<') {
            out += "&lt;";
        } else if (point == '>') {
            out += "&gt;";
        } else if (point == '"') {
            out += "&quot;";
        } else if (point == '\'') {
            out += "&apos;";
        } else {
            out.append(text.substr(at, length));
        }
        at += length;
    }
    return out;
}

GraphmlExport export_graphml(const embedstore::Store& store, std::string_view graph) {
    GraphmlExport result;
    std::string& out = result.xml;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<graphml xmlns=\"http://graphml.graphdrawing.org/xmlns\">\n";
    key(out, "name", "node", "name", "string");
    key(out, "type", "node", "type", "string");
    key(out, "description", "node", "description", "string");
    key(out, "mentions", "node", "mentions", "long");
    key(out, "degree", "node", "degree", "long");
    key(out, "community", "node", "community", "long");
    key(out, "members", "node", "members", "string");
    key(out, "member", "node", "member", "string");
    key(out, "file", "node", "file", "string");
    key(out, "line", "node", "line", "long");
    key(out, "end_line", "node", "end_line", "long");
    out +=
        "  <key id=\"unresolved\" for=\"node\" attr.name=\"unresolved\" "
        "attr.type=\"boolean\"><default>false</default></key>\n";
    key(out, "status", "node", "status", "string");
    key(out, "discipline", "node", "discipline", "string");
    key(out, "relation", "edge", "relation", "string");
    key(out, "origin", "edge", "origin", "string");
    key(out, "weight", "edge", "weight", "long");
    key(out, "confidence", "edge", "confidence", "double");
    key(out, "edge_description", "edge", "description", "string");
    out += "  <graph id=\"" + xml_escape(graph) + "\" edgedefault=\"directed\">\n";
    out += "    <desc>" +
           xml_escape("apogee graph export graphml: every entity and relation of graph \"" +
                      std::string{graph} + "\", uncapped") +
           "</desc>\n";

    const std::map<std::int64_t, std::vector<std::string>> stated_in = store.node_members();
    for (const embedstore::GraphNode& node : store.graph_nodes()) {
        const NodeRef ref = node_ref(node);
        out += "    <node id=\"n" + std::to_string(node.id) + "\">\n";
        data(out, "name", node.name);
        data(out, "type", node.type);
        if (!node.description.empty()) {
            data(out, "description", node.description);
        }
        data(out, "mentions", node.mention_count);
        data(out, "degree", node_degree(store, node.id).total);
        if (const std::vector<embedstore::GraphCommunity> communities =
                store.node_communities(node.id);
            !communities.empty()) {
            data(out, "community", communities.front().id);
        }
        if (const auto stated = stated_in.find(node.id); stated != stated_in.end()) {
            std::string labels;
            for (const std::string& label : stated->second) {
                labels += (labels.empty() ? "" : ",") + label;
            }
            if (!labels.empty()) {
                data(out, "members", labels);
            }
        }
        if (!ref.file.empty()) {
            if (!ref.member.empty()) {
                data(out, "member", ref.member);
            }
            data(out, "file", ref.file);
            data(out, "line", ref.line);
            if (ref.end_line > ref.line) {
                data(out, "end_line", ref.end_line);
            }
        }
        if (ref.unresolved) {
            data(out, "unresolved", "true");
        }
        if (!ref.status.empty()) {
            data(out, "status", ref.status);
        }
        if (!ref.discipline.empty()) {
            data(out, "discipline", ref.discipline);
        }
        out += "    </node>\n";
        ++result.nodes;
    }
    for (const embedstore::GraphEdge& edge : store.graph_edges()) {
        out += "    <edge id=\"e" + std::to_string(edge.id) + "\" source=\"n" +
               std::to_string(edge.source_id) + "\" target=\"n" + std::to_string(edge.target_id) +
               "\">\n";
        data(out, "relation", edge.relation);
        data(out, "origin", edge.origin);
        data(out, "weight", edge.weight);
        if (edge.confidence >= 0.0) {
            data(out, "confidence", number(edge.confidence));
        }
        if (!edge.description.empty()) {
            data(out, "edge_description", edge.description);
        }
        out += "    </edge>\n";
        ++result.edges;
    }
    out += "  </graph>\n</graphml>\n";
    return result;
}

GraphmlExport export_graphml(const OpenGraph& open) {
    return export_graphml(open.store(), open.target().name);
}

}  // namespace apogee::graph
