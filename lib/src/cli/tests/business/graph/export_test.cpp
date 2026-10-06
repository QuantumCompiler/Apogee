#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/graph_code.h"
#include "embedstore/store.h"
#include "graph/export_graphml.h"
#include "graph/export_html.h"
#include "graph/export_mermaid.h"
#include "graph/html_template.h"
#include "graph/navigate.h"
#include "graph/report.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// The exports (27m) over the committed fixture graph and synthetic ones:
/// the HTML page is one file with its data block, its cap note and no
/// external reference, each card 27l's own payload; GraphML round-trips
/// through an XML reader written here, independent of the writer; Mermaid
/// holds to a grammar check of its own; and nothing a chunk says beyond its
/// source reaches any of them.
namespace {

using apogee::embedstore::Store;
using Kind = apogee::graph::NavigationError::Kind;

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

[[nodiscard]] std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string read_asset(const std::string& relative) {
    const std::filesystem::path path = std::filesystem::path{APOGEE_ASSETS_DIR} / relative;
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// The named fixture graph `work`, its collection `notes` beside it.
struct Named {
    apogee::testing::TempDir dir{"graph-export-" + std::to_string(std::random_device{}())};
    Store notes{dir.path() / "notes.db"};
    Store work{dir.path() / "work.db"};

    Named() {
        apogee::testing::build_navigation_graph(work, notes, "notes");
        apogee::testing::add_report_orphans(work, notes, "notes");
    }

    [[nodiscard]] apogee::embedstore::MemberStores members() const {
        return {{"notes", &notes}};
    }
};

/// `count` functions in one file, each also calling one of ten hubs -- a
/// code graph past the HTML's cap, written in one sync.
void build_wide(Store& store, int count) {
    std::vector<apogee::embedstore::CodeNodeRow> nodes;
    std::vector<apogee::embedstore::CodeEdgeRow> edges;
    const auto meta = [](std::int64_t line) {
        apogee::embedstore::CodeNodeMetadata data;
        data.code = true;
        data.language = "python";
        data.member = "wide";
        data.file = "wide.py";
        data.line = line;
        data.end_line = line;
        return apogee::embedstore::code_node_metadata_json(data);
    };
    nodes.push_back(apogee::embedstore::CodeNodeRow{.type = "file",
                                                    .name = "wide.py",
                                                    .description = "Python file",
                                                    .metadata = meta(1),
                                                    .mentions = {{.collection = "wide",
                                                                  .file = "wide.py",
                                                                  .line = 1,
                                                                  .end_line = 1,
                                                                  .role = "definition"}}});
    const auto name_of = [](int i) {
        std::string digits = std::to_string(i);
        return "wide.f" + std::string(4 - digits.size(), '0') + digits;
    };
    for (int i = 0; i < count; ++i) {
        const std::int64_t line = i + 2;
        nodes.push_back(apogee::embedstore::CodeNodeRow{.type = "function",
                                                        .name = name_of(i),
                                                        .description = "def f()",
                                                        .metadata = meta(line),
                                                        .mentions = {{.collection = "wide",
                                                                      .file = "wide.py",
                                                                      .line = line,
                                                                      .end_line = line,
                                                                      .role = "definition"}}});
        edges.push_back(apogee::embedstore::CodeEdgeRow{
            .source_type = "function",
            .source_name = name_of(i),
            .target_type = "file",
            .target_name = "wide.py",
            .relation = "defined_in",
            .sites = {{.collection = "wide", .file = "wide.py", .line = line}}});
        if (i >= 10) {
            edges.push_back(apogee::embedstore::CodeEdgeRow{
                .source_type = "function",
                .source_name = name_of(i),
                .target_type = "function",
                .target_name = name_of(i % 10),
                .relation = "calls",
                .sites = {{.collection = "wide", .file = "wide.py", .line = line}}});
        }
    }
    (void)store.sync_code_graph(nodes, edges);
}

/// The page's one data block.
[[nodiscard]] nlohmann::json data_block(const std::string& html) {
    const std::string open = R"(<script type="application/json" id="graph-data">)";
    const std::size_t start = html.find(open);
    REQUIRE(start != std::string::npos);
    const std::size_t end = html.find("</script>", start);
    REQUIRE(end != std::string::npos);
    return nlohmann::json::parse(html.substr(start + open.size(), end - start - open.size()));
}

[[nodiscard]] std::size_t occurrences(std::string_view text, std::string_view part) {
    std::size_t count = 0;
    for (std::size_t at = text.find(part); at != std::string_view::npos;
         at = text.find(part, at + 1)) {
        ++count;
    }
    return count;
}

/// What a file that fetches nothing may never hold.
void check_no_external_reference(const std::string& html) {
    const std::string text = lower(html);
    for (const std::string_view reference :
         {"http:",       "https:",     "//",      "src=",           "href=",
          "url(",        "@import",    "<link",   "<iframe",        "<img",
          "<object",     "<embed",     "fetch(",  "xmlhttprequest", "websocket",
          "eventsource", "sendbeacon", "import(", "worker(",        "srcset"}) {
        INFO(reference);
        CHECK_FALSE(contains(text, reference));
    }
    // Exactly the two script elements: the data and the page's own.
    CHECK(occurrences(text, "<script") == 2);
    CHECK(occurrences(text, "</script") == 2);
    CHECK(contains(html, "Content-Security-Policy\" content=\"default-src 'none';"));
}

// ---- An XML reader, written here: the GraphML writer's independent check ----

struct XmlElement {
    std::string name;
    std::map<std::string, std::string> attributes;
    std::vector<XmlElement> children;
    std::string text;

    [[nodiscard]] std::vector<const XmlElement*> all(std::string_view tag) const {
        std::vector<const XmlElement*> out;
        for (const XmlElement& child : children) {
            if (child.name == tag) {
                out.push_back(&child);
            }
        }
        return out;
    }
};

/// A strict, small XML 1.0 reader: the prolog, comments, elements,
/// attributes in either quote, character data and the predefined and
/// numeric entities. Anything else -- a raw `<` or `&` in text, a tag that
/// does not close, a character XML cannot carry, invalid UTF-8 -- throws.
class XmlReader {
public:
    explicit XmlReader(std::string_view text) : text_{text} {}

    [[nodiscard]] XmlElement document() {
        check_characters();
        if (starts("<?xml")) {
            const std::size_t end = text_.find("?>", at_);
            require(end != std::string_view::npos, "an unterminated prolog");
            at_ = end + 2;
        }
        skip_misc();
        XmlElement root = element();
        skip_misc();
        require(at_ == text_.size(), "content after the root element");
        return root;
    }

private:
    void require(bool condition, const std::string& what) const {
        if (!condition) {
            throw std::runtime_error(what + " at byte " + std::to_string(at_));
        }
    }

    [[nodiscard]] bool starts(std::string_view part) const {
        return text_.substr(at_, part.size()) == part;
    }

    [[nodiscard]] char peek() const {
        require(at_ < text_.size(), "the end of the document");
        return text_[at_];
    }

    void expect(char c) {
        require(peek() == c, std::string{"expected '"} + c + "'");
        ++at_;
    }

    void skip_space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }

    void skip_comment() {
        const std::size_t end = text_.find("-->", at_ + 4);
        require(end != std::string_view::npos, "an unterminated comment");
        require(text_.substr(at_ + 4, end - at_ - 4).find("--") == std::string_view::npos,
                "a comment holding --");
        at_ = end + 3;
    }

    void skip_misc() {
        while (true) {
            skip_space();
            if (!starts("<!--")) {
                return;
            }
            skip_comment();
        }
    }

    [[nodiscard]] std::string name() {
        const std::size_t start = at_;
        while (at_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[at_])) != 0 || text_[at_] == '_' ||
                text_[at_] == ':' || text_[at_] == '-' || text_[at_] == '.')) {
            ++at_;
        }
        require(at_ > start, "a name");
        return std::string{text_.substr(start, at_ - start)};
    }

    static void append_utf8(std::string& out, std::uint32_t point) {
        if (point < 0x80U) {
            out += static_cast<char>(point);
        } else if (point < 0x800U) {
            out += static_cast<char>(0xC0U | (point >> 6U));
            out += static_cast<char>(0x80U | (point & 0x3FU));
        } else if (point < 0x10000U) {
            out += static_cast<char>(0xE0U | (point >> 12U));
            out += static_cast<char>(0x80U | ((point >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (point & 0x3FU));
        } else {
            out += static_cast<char>(0xF0U | (point >> 18U));
            out += static_cast<char>(0x80U | ((point >> 12U) & 0x3FU));
            out += static_cast<char>(0x80U | ((point >> 6U) & 0x3FU));
            out += static_cast<char>(0x80U | (point & 0x3FU));
        }
    }

    [[nodiscard]] std::string decode(std::string_view raw) const {
        std::string out;
        for (std::size_t i = 0; i < raw.size(); ++i) {
            require(raw[i] != '<', "a raw '<'");
            if (raw[i] != '&') {
                out += raw[i];
                continue;
            }
            const std::size_t end = raw.find(';', i);
            require(end != std::string_view::npos, "an unterminated entity");
            const std::string_view entity = raw.substr(i + 1, end - i - 1);
            static const std::map<std::string_view, char> named{
                {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}};
            if (const auto it = named.find(entity); it != named.end()) {
                out += it->second;
            } else {
                require(entity.size() > 1 && entity[0] == '#', "an unknown entity");
                const bool hex = entity[1] == 'x';
                const std::string digits{entity.substr(hex ? 2 : 1)};
                append_utf8(out,
                            static_cast<std::uint32_t>(std::stoul(digits, nullptr, hex ? 16 : 10)));
            }
            i = end;
        }
        return out;
    }

    [[nodiscard]] XmlElement element() {
        expect('<');
        XmlElement out;
        out.name = name();
        while (true) {
            const std::size_t before = at_;
            skip_space();
            if (starts("/>")) {
                at_ += 2;
                return out;
            }
            if (starts(">")) {
                ++at_;
                break;
            }
            require(at_ > before, "no space before an attribute");
            const std::string attribute = name();
            skip_space();
            expect('=');
            skip_space();
            const char quote = peek();
            require(quote == '"' || quote == '\'', "an unquoted attribute");
            ++at_;
            const std::size_t end = text_.find(quote, at_);
            require(end != std::string_view::npos, "an unterminated attribute");
            require(!out.attributes.contains(attribute), "a repeated attribute");
            out.attributes[attribute] = decode(text_.substr(at_, end - at_));
            at_ = end + 1;
        }
        while (true) {
            require(at_ < text_.size(), "an element that never closes: " + out.name);
            if (starts("</")) {
                at_ += 2;
                require(name() == out.name, "a closing tag that is not " + out.name);
                skip_space();
                expect('>');
                return out;
            }
            if (starts("<!--")) {
                skip_comment();
            } else if (starts("<")) {
                out.children.push_back(element());
            } else {
                const std::size_t start = at_;
                while (at_ < text_.size() && text_[at_] != '<') {
                    ++at_;
                }
                const std::string_view raw = text_.substr(start, at_ - start);
                require(raw.find("]]>") == std::string_view::npos, "]]> in character data");
                out.text += decode(raw);
            }
        }
    }

    /// Every character valid UTF-8 and one XML 1.0 can carry.
    void check_characters() const {
        std::size_t i = 0;
        while (i < text_.size()) {
            const auto byte = static_cast<unsigned char>(text_[i]);
            std::size_t length = 1;
            std::uint32_t point = byte;
            if (byte >= 0x80U) {
                length = byte >= 0xF0U ? 4 : byte >= 0xE0U ? 3 : 2;
                if (byte < 0xC2U || byte > 0xF4U || i + length > text_.size()) {
                    throw std::runtime_error("invalid UTF-8 at byte " + std::to_string(i));
                }
                point = byte & (length == 2 ? 0x1FU : length == 3 ? 0x0FU : 0x07U);
                for (std::size_t k = 1; k < length; ++k) {
                    const auto next = static_cast<unsigned char>(text_[i + k]);
                    if ((next & 0xC0U) != 0x80U) {
                        throw std::runtime_error("invalid UTF-8 at byte " + std::to_string(i));
                    }
                    point = (point << 6U) | (next & 0x3FU);
                }
            }
            const bool allowed = point == 0x9U || point == 0xAU || point == 0xDU ||
                                 (point >= 0x20U && point <= 0xD7FFU) ||
                                 (point >= 0xE000U && point <= 0xFFFDU) || point >= 0x10000U;
            if (!allowed) {
                throw std::runtime_error("a character XML cannot carry at byte " +
                                         std::to_string(i));
            }
            i += length;
        }
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

/// A node's or an edge's `<data>` by its key's `attr.name`.
[[nodiscard]] std::map<std::string, std::string> data_of(
    const XmlElement& element, const std::map<std::string, std::string>& names) {
    std::map<std::string, std::string> out;
    for (const XmlElement* data : element.all("data")) {
        const auto key = names.find(data->attributes.at("key"));
        REQUIRE(key != names.end());
        out[key->second] = data->text;
    }
    return out;
}

// ---- Mermaid's grammar, as much of it as a flowchart uses ---------------------

/// Throws naming the line Mermaid would not read. Independent of the
/// writer: it knows the grammar, not the code that emits it.
void check_mermaid(const std::string& text) {
    static const std::regex header{R"re(^flowchart (LR|RL|TB|TD|BT)$)re"};
    static const std::regex comment{R"re(^\s*%%(?!\{).*$)re"};
    static const std::regex subgraph{R"re(^\s*subgraph ([A-Za-z_][A-Za-z0-9_]*)\["([^"]*)"\]$)re"};
    static const std::regex end{R"re(^\s*end$)re"};
    static const std::regex node{R"re(^\s*([A-Za-z_][A-Za-z0-9_]*)\["([^"]*)"\]$)re"};
    static const std::regex edge{
        R"re(^\s*([A-Za-z_][A-Za-z0-9_]*) (-->|-\.->)(\|"([^"]*)"\|)? ([A-Za-z_][A-Za-z0-9_]*)$)re"};
    static const std::regex entity{R"re(#([0-9]+|[a-z]+);)re"};
    static const std::set<std::string> reserved{"end",       "subgraph", "graph",    "flowchart",
                                                "style",     "class",    "classDef", "click",
                                                "linkStyle", "default",  "direction"};
    const auto check_label = [](const std::string& label, std::size_t line) {
        // A label holds no character the grammar reads, and every `#` opens
        // an entity code.
        const std::string stripped = std::regex_replace(label, entity, "");
        for (const char c : {'"', '#', '<', '>', '`', '|', '\n'}) {
            if (stripped.find(c) != std::string::npos) {
                throw std::runtime_error("line " + std::to_string(line) + ": a raw '" +
                                         std::string(1, c) + "' in a label");
            }
        }
    };
    std::istringstream lines{text};
    std::string line;
    std::size_t number = 0;
    int depth = 0;
    std::set<std::string> declared;
    std::vector<std::pair<std::string, std::string>> links;
    std::smatch match;
    while (std::getline(lines, line)) {
        ++number;
        if (number == 1) {
            if (!std::regex_match(line, header)) {
                throw std::runtime_error("line 1 is not a flowchart header: " + line);
            }
            continue;
        }
        if (line.empty() || std::regex_match(line, comment)) {
            continue;
        }
        if (std::regex_match(line, match, subgraph)) {
            check_label(match[2], number);
            if (reserved.contains(match[1]) || !declared.insert(match[1]).second) {
                throw std::runtime_error("line " + std::to_string(number) + ": a bad subgraph id");
            }
            ++depth;
        } else if (std::regex_match(line, end)) {
            if (--depth < 0) {
                throw std::runtime_error("line " + std::to_string(number) + ": an end too many");
            }
        } else if (std::regex_match(line, match, node)) {
            check_label(match[2], number);
            if (reserved.contains(match[1]) || !declared.insert(match[1]).second) {
                throw std::runtime_error("line " + std::to_string(number) + ": a bad node id");
            }
        } else if (std::regex_match(line, match, edge)) {
            if (match[4].matched) {
                check_label(match[4], number);
            }
            links.emplace_back(match[1], match[5]);
        } else {
            throw std::runtime_error("line " + std::to_string(number) + " is not Mermaid: " + line);
        }
    }
    if (depth != 0) {
        throw std::runtime_error("a subgraph never ends");
    }
    if (links.size() > apogee::graph::kMermaidMaxEdges) {
        throw std::runtime_error("more edges than Mermaid draws");
    }
    for (const auto& [from, to] : links) {
        if (!declared.contains(from) || !declared.contains(to)) {
            throw std::runtime_error("an edge to an undeclared node: " + from + " --> " + to);
        }
    }
}

}  // namespace

// ---- HTML ------------------------------------------------------------------------

TEST_CASE("the compiled-in page byte-matches the shipped file and names three placeholders",
          "[graph][export][html][asset]") {
    const std::string& page = apogee::graph::html_template();
    CHECK(page == read_asset("graph/graph.html"));
    std::set<std::string> placeholders;
    const std::regex placeholder{R"(\{\{([a-z_]+)\}\})"};
    for (auto it = std::sregex_iterator(page.begin(), page.end(), placeholder);
         it != std::sregex_iterator(); ++it) {
        placeholders.insert((*it)[1]);
    }
    CHECK(placeholders == std::set<std::string>{"cap_note", "data", "title"});
    // The page itself fetches nothing and is small: no library, no CDN.
    check_no_external_reference(page);
    CHECK(page.size() < 200U * 1024U);
}

TEST_CASE("the page embeds one data block of 27l's cards, its cap note, and no external reference",
          "[graph][export][html]") {
    const Named fixture;
    const apogee::graph::HtmlExport page =
        apogee::graph::export_html(fixture.work, fixture.members(), "work", {});
    const std::vector<apogee::graph::RankedNode> ranked =
        apogee::graph::rank_by_degree(fixture.work);

    check_no_external_reference(page.html);
    CHECK(page.entities == ranked.size());
    CHECK(page.shown == ranked.size());
    CHECK_FALSE(page.capped);
    CHECK(page.unresolved_names == 2);
    CHECK(page.cap_note == "Showing all " + std::to_string(ranked.size()) +
                               " entities; the 2 unresolved names are left out.");
    CHECK(contains(page.html, "<p id=\"cap-note\">" + page.cap_note + "</p>"));
    CHECK(contains(page.html, "<title>work · graph</title>"));

    const nlohmann::json data = data_block(page.html);
    CHECK(data["object"] == "graph.html");
    CHECK(data["graph"] == "work");
    CHECK(data["cap"]["capped"] == false);
    CHECK(data["cap"]["note"] == page.cap_note);
    CHECK(data["cap"]["card_neighbors"] == apogee::graph::kHtmlCardNeighbors);
    REQUIRE(data["nodes"].size() == ranked.size());
    // Every card is the explain payload, serialised by 27l's own to_json.
    for (std::size_t i = 0; i < ranked.size(); ++i) {
        INFO(ranked[i].node.name);
        CHECK(data["nodes"][i] == apogee::graph::to_json(apogee::graph::node_card(
                                      fixture.work, fixture.members(), "work", ranked[i].node,
                                      apogee::graph::kHtmlCardNeighbors)));
    }
    // The relations: every edge between two entities drawn, by index.
    std::size_t expected = 0;
    std::size_t parsed = 0;
    for (const apogee::embedstore::GraphEdge& edge : fixture.work.all_edges()) {
        expected += edge.source_id != edge.target_id ? 1 : 0;
        parsed += edge.origin == "extracted" ? 1 : 0;
    }
    CHECK(data["edges"].size() == expected);
    CHECK(page.relations == expected);
    std::size_t marked = 0;
    for (const nlohmann::json& edge : data["edges"]) {
        CHECK(edge[0].get<std::size_t>() < ranked.size());
        CHECK(edge[1].get<std::size_t>() < ranked.size());
        CHECK(edge[2].get<std::size_t>() < data["relations"].size());
        marked += edge[3].get<int>() == 1 ? 1 : 0;
    }
    // Each edge says whether it was parsed: the page draws a model's dashed.
    CHECK(marked == parsed);
    CHECK(marked < expected);
    // A prose node's community: the legend carries it with its summary.
    REQUIRE(data["communities"].size() == 1);
    CHECK(data["communities"][0]["summary"] == "Atlas and the Vault it writes its readings to.");
    CHECK(data["origin"]["inferred"].get<std::int64_t>() > 0);
}

TEST_CASE("an over-cap graph is cut by degree rank, and the page says how many of how many",
          "[graph][export][html][cap]") {
    const apogee::testing::TempDir dir{"graph-export-wide-" +
                                       std::to_string(std::random_device{}())};
    Store store{dir.path() / "wide.db"};
    build_wide(store, 2050);
    const std::vector<apogee::graph::RankedNode> ranked = apogee::graph::rank_by_degree(store);
    REQUIRE(ranked.size() == 2051);

    const apogee::graph::HtmlExport page = apogee::graph::export_html(store, {}, "wide", {});
    CHECK(page.capped);
    CHECK(page.shown == apogee::graph::kHtmlMaxNodes);
    CHECK(page.entities == 2051);
    const std::string note =
        "Showing the 2000 highest-degree of 2051 entities -- capped at 2000 by degree rank. "
        "apogee graph export graphml carries every one.";
    CHECK(page.cap_note == note);
    CHECK(contains(page.html, note));
    check_no_external_reference(page.html);
    const nlohmann::json data = data_block(page.html);
    CHECK(data["cap"]["capped"] == true);
    CHECK(data["cap"]["shown"] == 2000);
    CHECK(data["cap"]["entities"] == 2051);
    REQUIRE(data["nodes"].size() == 2000);
    for (std::size_t i = 0; i < 2000; ++i) {
        CHECK(data["nodes"][i]["node"]["name"] == ranked[i].node.name);
    }
    // The hubs are first: the file, then the ten functions everything calls.
    CHECK(data["nodes"][0]["node"]["name"] == "wide.py");
    CHECK(data["nodes"][1]["degree"]["in"].get<std::int64_t>() > 200);
}

TEST_CASE("the HTML cap is an argument with a ceiling", "[graph][export][html][cap]") {
    const Named fixture;
    const apogee::graph::HtmlExport five = apogee::graph::export_html(
        fixture.work, fixture.members(), "work", apogee::graph::HtmlOptions{.max_nodes = 5});
    const std::vector<apogee::graph::RankedNode> ranked =
        apogee::graph::rank_by_degree(fixture.work);
    CHECK(five.capped);
    CHECK(five.shown == 5);
    CHECK(five.cap_note == "Showing the 5 highest-degree of " + std::to_string(ranked.size()) +
                               " entities -- capped at 5 by degree rank; the 2 unresolved names "
                               "are left out. apogee graph export graphml carries every one.");
    const nlohmann::json data = data_block(five.html);
    REQUIRE(data["nodes"].size() == 5);
    for (const nlohmann::json& edge : data["edges"]) {
        CHECK(edge[0].get<int>() < 5);
        CHECK(edge[1].get<int>() < 5);
    }
    // A cap the graph exactly fills is not a cut.
    const apogee::graph::HtmlExport exact =
        apogee::graph::export_html(fixture.work, fixture.members(), "work",
                                   apogee::graph::HtmlOptions{.max_nodes = ranked.size()});
    CHECK_FALSE(exact.capped);
    CHECK(exact.cap_note.starts_with("Showing all " + std::to_string(ranked.size()) + " entities"));
    for (const std::size_t cap : {std::size_t{0}, apogee::graph::kHtmlMaxNodes + 1}) {
        try {
            (void)apogee::graph::export_html(fixture.work, fixture.members(), "work",
                                             apogee::graph::HtmlOptions{.max_nodes = cap});
            FAIL("a cap of " << cap << " was taken");
        } catch (const apogee::graph::NavigationError& e) {
            CHECK(e.kind() == Kind::InvalidArgument);
            CHECK(contains(e.what(), "between 1 and 2000"));
        }
    }
}

TEST_CASE("the data block can neither end its element nor carry a //, and parses the same",
          "[graph][export][html][escape]") {
    const nlohmann::json hostile{
        {"name", "</script><script>alert(1)</script><!-- // http://cdn.example/x.js & more"},
        {"path", "source/a/b.cpp"},
        {"bad", std::string{"\xff\xfe"}}};
    const std::string embedded = apogee::graph::embeddable_json(hostile);
    for (const std::string_view banned : {"<", ">", "&", "//"}) {
        INFO(banned);
        CHECK_FALSE(contains(embedded, banned));
    }
    const nlohmann::json back = nlohmann::json::parse(embedded);
    CHECK(back["name"] == hostile["name"]);
    CHECK(back["path"] == "source/a/b.cpp");
    // Invalid UTF-8 is replaced, never a throw.
    CHECK(back["bad"] == "\xEF\xBF\xBD\xEF\xBF\xBD");

    CHECK(apogee::graph::html_escape("a<b & \"c\" 'd'>") ==
          "a&lt;b &amp; &quot;c&quot; &#39;d&#39;&gt;");
    CHECK(apogee::graph::fill_template("{{a}}-{{b}}{{a}}", {{"a", "{{b}}"}, {"b", "x"}}) ==
          "{{b}}-x{{b}}");
    CHECK_THROWS_AS((void)apogee::graph::fill_template("{{c}}", {{"a", ""}}), std::logic_error);
    CHECK_THROWS_AS((void)apogee::graph::fill_template("{{a", {{"a", ""}}), std::logic_error);
}

// ---- GraphML -----------------------------------------------------------------------

TEST_CASE("GraphML round-trips through an independent XML reader: every node and edge",
          "[graph][export][graphml]") {
    Named fixture;
    // A name and a description no XML writer gets right by accident.
    fixture.notes.replace_source("docs/odd.md", {"odd"});
    const std::int64_t odd = fixture.work
                                 .upsert_node("Odd <name> & \"quoted\" 'single'", "concept",
                                              std::string{"bad\x01"
                                                          "byte\xff end ]]> done"})
                                 .id;
    (void)fixture.work.add_mention(odd, "notes",
                                   fixture.notes.chunks_by_source("docs/odd.md").front().id);

    const apogee::graph::GraphmlExport exported =
        apogee::graph::export_graphml(fixture.work, "work");
    XmlElement root;
    REQUIRE_NOTHROW(root = XmlReader{exported.xml}.document());
    CHECK(root.name == "graphml");
    CHECK(root.attributes.at("xmlns") == "http://graphml.graphdrawing.org/xmlns");

    std::map<std::string, std::string> node_keys;
    std::map<std::string, std::string> edge_keys;
    for (const XmlElement* key : root.all("key")) {
        (key->attributes.at("for") == "node" ? node_keys : edge_keys)[key->attributes.at("id")] =
            key->attributes.at("attr.name");
        CHECK(std::set<std::string>{"string", "long", "double", "boolean"}.contains(
            key->attributes.at("attr.type")));
    }
    const std::vector<const XmlElement*> graphs = root.all("graph");
    REQUIRE(graphs.size() == 1);
    CHECK(graphs.front()->attributes.at("edgedefault") == "directed");

    const std::vector<apogee::embedstore::GraphNode> nodes = fixture.work.graph_nodes();
    const std::vector<const XmlElement*> node_elements = graphs.front()->all("node");
    REQUIRE(node_elements.size() == nodes.size());
    CHECK(exported.nodes == nodes.size());
    std::set<std::string> ids;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const XmlElement& element = *node_elements[i];
        INFO(nodes[i].name);
        CHECK(element.attributes.at("id") == "n" + std::to_string(nodes[i].id));
        ids.insert(element.attributes.at("id"));
        const std::map<std::string, std::string> data = data_of(element, node_keys);
        CHECK(data.at("name") == nodes[i].name);
        CHECK(data.at("type") == nodes[i].type);
        CHECK(data.at("mentions") == std::to_string(nodes[i].mention_count));
        CHECK(data.at("degree") ==
              std::to_string(apogee::graph::node_degree(fixture.work, nodes[i].id).total));
        if (nodes[i].type == "name") {
            CHECK(data.at("unresolved") == "true");
        }
        if (nodes[i].id == odd) {
            CHECK(data.at("description") ==
                  "bad\xEF\xBF\xBD"
                  "byte\xEF\xBF\xBD end ]]> done");
            CHECK(data.at("members") == "notes");
        } else if (!nodes[i].description.empty()) {
            CHECK(data.at("description") == nodes[i].description);
        }
        if (nodes[i].name == "pkg.lib.helper") {
            CHECK(data.at("file") == "pkg/lib.py");
            CHECK(data.at("line") == "1");
            CHECK(data.at("members") == "app");
        }
        if (nodes[i].name == "kr-0001") {
            CHECK(data.at("status") == "shipped");
            CHECK(data.at("discipline") == "engineering");
        }
    }

    const std::vector<apogee::embedstore::GraphEdge> edges = fixture.work.graph_edges();
    // Uncapped, against the store's own counts: every node, every edge.
    const apogee::embedstore::GraphStats stats = fixture.work.graph_stats();
    CHECK(std::cmp_equal(nodes.size(), stats.nodes));
    CHECK(std::cmp_equal(edges.size(), stats.edges));
    const std::vector<const XmlElement*> edge_elements = graphs.front()->all("edge");
    REQUIRE(edge_elements.size() == edges.size());
    CHECK(exported.edges == edges.size());
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const XmlElement& element = *edge_elements[i];
        CHECK(ids.contains(element.attributes.at("source")));
        CHECK(ids.contains(element.attributes.at("target")));
        CHECK(element.attributes.at("source") == "n" + std::to_string(edges[i].source_id));
        const std::map<std::string, std::string> data = data_of(element, edge_keys);
        CHECK(data.at("relation") == edges[i].relation);
        CHECK(data.at("origin") == edges[i].origin);
        CHECK(data.at("weight") == std::to_string(edges[i].weight));
        if (edges[i].origin == "extracted") {
            CHECK(data.at("confidence") == "1");
        }
    }
    // Uncapped: the unresolved names and their edges are all there.
    CHECK(contains(exported.xml, "<data key=\"name\">.push_back</data>"));
}

TEST_CASE("the XML reader refuses what is not well-formed", "[graph][export][graphml]") {
    for (const std::string_view bad :
         {"<a><b></a>", "<a x=1/>", "<a>&nope;</a>", "<a>1 < 2</a>", "<a/><b/>", "<a>\x01</a>",
          "<a>\xff</a>", "<a x=\"1\" x=\"2\"/>"}) {
        INFO(bad);
        CHECK_THROWS((void)XmlReader{bad}.document());
    }
    CHECK(XmlReader{"<a x='1'>&lt;&#65;&#x42;</a>"}.document().text == "<AB");
}

// ---- Mermaid -----------------------------------------------------------------------

TEST_CASE("a code graph exports its call flow, grouped by file, and parses as Mermaid",
          "[graph][export][mermaid]") {
    const Named fixture;
    const apogee::graph::MermaidExport diagram =
        apogee::graph::export_mermaid(fixture.work, "work", {});
    REQUIRE_NOTHROW(check_mermaid(diagram.text));
    CHECK(diagram.call_flow);
    // Functions calling functions: the chain, the callers, run, main, other,
    // helper -- 11 + 15 + 4.
    CHECK(diagram.candidates == 30);
    CHECK(diagram.shown == 30);
    CHECK(diagram.edges == diagram.edges_total);
    CHECK(contains(diagram.text, "subgraph f0[\"pkg/app.py\"]"));
    CHECK(contains(diagram.text, "[\"helper\"]"));
    CHECK(contains(diagram.text, "the call flow of the 30 highest-degree of 30 functions"));
    // Only calls, and never an unresolved name.
    CHECK_FALSE(contains(diagram.text, "push_back"));
    CHECK_FALSE(contains(diagram.text, "json.dumps"));
    CHECK_FALSE(contains(diagram.text, "Vault"));

    const apogee::graph::MermaidExport two = apogee::graph::export_mermaid(
        fixture.work, "work", apogee::graph::MermaidOptions{.max_nodes = 2});
    REQUIRE_NOTHROW(check_mermaid(two.text));
    CHECK(two.shown == 2);
    CHECK(contains(two.text, "the call flow of the 2 highest-degree of 30 functions"));
    for (const std::size_t cap : {std::size_t{0}, apogee::graph::kMermaidMaxNodes + 1}) {
        CHECK_THROWS_AS((void)apogee::graph::export_mermaid(
                            fixture.work, "work", apogee::graph::MermaidOptions{.max_nodes = cap}),
                        apogee::graph::NavigationError);
    }
}

TEST_CASE("Mermaid stays under its edge limit, and says so", "[graph][export][mermaid]") {
    const apogee::testing::TempDir dir{"graph-export-dense-" +
                                       std::to_string(std::random_device{}())};
    Store store{dir.path() / "dense.db"};
    // Forty functions, each calling every other: 1,560 calls.
    std::vector<apogee::embedstore::CodeNodeRow> nodes;
    std::vector<apogee::embedstore::CodeEdgeRow> edges;
    for (int i = 0; i < 40; ++i) {
        apogee::embedstore::CodeNodeMetadata meta;
        meta.code = true;
        meta.member = "dense";
        meta.file = "dense.py";
        meta.line = i + 1;
        nodes.push_back(apogee::embedstore::CodeNodeRow{
            .type = "function",
            .name = "dense.f" + std::to_string(i),
            .description = "",
            .metadata = apogee::embedstore::code_node_metadata_json(meta),
            .mentions = {{.collection = "dense",
                          .file = "dense.py",
                          .line = i + 1,
                          .end_line = i + 1,
                          .role = "definition"}}});
        for (int j = 0; j < 40; ++j) {
            if (i != j) {
                edges.push_back(apogee::embedstore::CodeEdgeRow{
                    .source_type = "function",
                    .source_name = "dense.f" + std::to_string(i),
                    .target_type = "function",
                    .target_name = "dense.f" + std::to_string(j),
                    .relation = "calls",
                    .sites = {{.collection = "dense", .file = "dense.py", .line = i + 1}}});
            }
        }
    }
    (void)store.sync_code_graph(nodes, edges);
    const apogee::graph::MermaidExport diagram = apogee::graph::export_mermaid(store, "dense", {});
    REQUIRE_NOTHROW(check_mermaid(diagram.text));
    CHECK(diagram.edges_total == 1560);
    CHECK(diagram.edges == apogee::graph::kMermaidMaxEdges);
    CHECK(contains(diagram.text, "500 of 1560 calls among them drawn (capped at 500"));
}

TEST_CASE("a prose graph exports its entities and labelled relations, every label escaped",
          "[graph][export][mermaid]") {
    const apogee::testing::TempDir dir{"graph-export-prose-" +
                                       std::to_string(std::random_device{}())};
    Store store{dir.path() / "prose.db"};
    store.replace_source("a.md", {"Atlas and Vault."});
    const std::int64_t chunk = store.chunks_by_source("a.md").front().id;
    const std::int64_t atlas = store.upsert_node("Atlas", "system", "probes").id;
    const std::int64_t odd =
        store.upsert_node("Odd \"name\" <tag> | pipe # hash `tick`", "concept", "").id;
    (void)store.add_mention(atlas, chunk);
    (void)store.add_mention(odd, chunk);
    store.upsert_edge(atlas, odd, "writes \"to\" | via #2", "");

    const apogee::graph::MermaidExport diagram = apogee::graph::export_mermaid(store, "prose", {});
    REQUIRE_NOTHROW(check_mermaid(diagram.text));
    CHECK_FALSE(diagram.call_flow);
    CHECK(diagram.shown == 2);
    CHECK(diagram.edges == 1);
    CHECK(contains(diagram.text, "Odd #34;name#34; #60;tag#62; #124; pipe #35; hash #96;tick#96;"));
    CHECK(contains(diagram.text, " -.->|\"writes #34;to#34; #124; via #35;2\"| "));
    CHECK(apogee::graph::mermaid_label("a\n\tb  c") == "a b c");
}

// ---- Privacy -----------------------------------------------------------------------

TEST_CASE("no artifact carries a chunk's text: a mention surfaces its source and nothing more",
          "[graph][export][privacy]") {
    Named fixture;
    const std::string canary = "PRIVATE-CANARY-27m-5f0c1d";
    fixture.notes.replace_source("docs/secret.md", {"The vault key is " + canary + "."});
    const std::int64_t chunk = fixture.notes.chunks_by_source("docs/secret.md").front().id;
    // Ledger, mentioned once before, so its card lists the new mention.
    const apogee::embedstore::GraphNode ledger =
        apogee::graph::resolve_node(fixture.work, "work", "Ledger").node;
    (void)fixture.work.add_mention(ledger.id, "notes", chunk);

    const std::string report =
        apogee::graph::to_json(apogee::graph::build_report(fixture.work, "work")).dump();
    const std::string html =
        apogee::graph::export_html(fixture.work, fixture.members(), "work", {}).html;
    const std::string graphml = apogee::graph::export_graphml(fixture.work, "work").xml;
    const std::string mermaid = apogee::graph::export_mermaid(fixture.work, "work", {}).text;
    for (const std::string* artifact : {&report, &html, &graphml, &mermaid}) {
        CHECK_FALSE(contains(*artifact, canary));
        CHECK_FALSE(contains(*artifact, "The vault key is"));
    }
    // The mention itself is there: the source a card names, escaped as embedded.
    CHECK(contains(html, R"(docs\/secret.md)"));
}
