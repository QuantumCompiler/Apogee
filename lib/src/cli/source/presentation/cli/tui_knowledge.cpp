#include "cli/tui_knowledge.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cli/embed.h"
#include "cli/graph.h"
#include "cli/graph_navigate.h"
#include "cli/knowledge.h"
#include "cli/tui_common.h"
#include "contracts/config.h"
#include "knowledge/record.h"

namespace apogee::commands {

namespace {

/// The records the last read drew, kept for Enter's card: the read and the
/// card both run on the view's one worker, and the lock keeps a reader on
/// another thread honest.
struct LastRecords {
    std::mutex mutex;
    std::vector<knowledge::Record> records;
};

/// A cell's first line: a record's intent runs on in the card, not the row.
[[nodiscard]] std::string first_line(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

}  // namespace

tui::ListOptions knowledge_view_options(const RootContext& context) {
    const auto last = std::make_shared<LastRecords>();
    tui::ListOptions options;
    options.title = "Knowledge";
    options.columns = {"ID", "STATUS", "DISCIPLINE", "CAPTURED", "INTENT"};
    options.load = [&context, last]() {
        // `knowledge list`'s own read, its rows as its document words them.
        KnowledgeRecords read;
        try {
            read = read_knowledge_records(context);
        } catch (const KnowledgeRefusal& e) {
            // No collection yet: said in the command's words, not an error.
            return std::pair{std::vector<std::string>{e.what()}, std::vector<tui::ListRow>{}};
        }
        const nlohmann::json document = knowledge_list_document(read);
        std::vector<tui::ListRow> rows;
        for (const nlohmann::json& record : document["data"]) {
            rows.push_back(tui::ListRow{
                .key = field(record, "id"),
                .cells = {field(record, "id"), field(record, "status"), field(record, "discipline"),
                          field(record, "timestamp"), first_line(field(record, "intent"))},
                .look = field(record, "status") == knowledge::kStatusShipped
                            ? tui::ListRow::Look::Plain
                            : tui::ListRow::Look::Dim});
        }
        std::vector<std::string> heading{lines_of(knowledge_list_text(read)).front()};
        {
            const std::lock_guard lock{last->mutex};
            last->records = std::move(read.records);
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "info";
    options.detail = [last](const tui::ListRow& row) {
        const std::lock_guard lock{last->mutex};
        for (const knowledge::Record& record : last->records) {
            if (record.id == row.key) {
                return lines_of(knowledge_record_text(record));
            }
        }
        return std::vector<std::string>{"read again -- the records have changed"};
    };
    // `knowledge query`'s own search: its defaults, its retriever said.
    options.ask_label = "query";
    options.ask = [&context](const tui::ListRow& /*row*/, const std::string& text) {
        KnowledgeQuery query;
        query.question = text;
        return knowledge_query_text(context, query);
    };
    options.actions = {tui::ListAction{.key = "x",
                                       .label = "delete",
                                       .confirm =
                                           [](const tui::ListRow& row) {
                                               return "Delete " + row.key +
                                                      " and its archived conversation?";
                                           },
                                       .run =
                                           [&context](const tui::ListRow& row) {
                                               return delete_knowledge_record(context, {}, row.key);
                                           }}};
    return options;
}

tui::ListOptions collections_view_options(const RootContext& context) {
    tui::ListOptions options;
    options.title = "Collections";
    options.columns = {"COLLECTION", "CHUNKS", "SOURCES"};
    options.load = []() {
        // `embed list`'s own rows, as its document words them.
        const std::vector<CollectionRow> read = collection_rows();
        const nlohmann::json document = collection_list_document(read);
        std::vector<tui::ListRow> rows;
        for (const nlohmann::json& row : document["data"]) {
            const std::string error = field(row, "error");
            rows.push_back(tui::ListRow{
                .key = field(row, "name"),
                .cells = error.empty()
                             ? std::vector<std::string>{field(row, "name"), field(row, "chunks"),
                                                        field(row, "sources")}
                             : std::vector<std::string>{field(row, "name"), "",
                                                        "could not read: " + error},
                .look = error.empty() ? tui::ListRow::Look::Plain : tui::ListRow::Look::Attention});
        }
        std::vector<std::string> heading;
        if (rows.empty()) {
            heading = lines_of(render_collection_rows(read));
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "info";
    options.detail = [](const tui::ListRow& row) {
        return lines_of(collection_info_text(row.key));
    };
    // `embed query`'s own search over the selected collection.
    options.ask_label = "query";
    options.ask_needs_row = true;
    options.ask = [&context](const tui::ListRow& row, const std::string& text) {
        CollectionQuery query;
        query.collection = row.key;
        query.text = text;
        return collection_query_text(context, query);
    };
    options.actions = {tui::ListAction{
        .key = "x",
        .label = "delete",
        .confirm =
            [](const tui::ListRow& row) {
                return "Delete the whole collection " + row.key + ", every chunk in it?";
            },
        .run = [](const tui::ListRow& row) { return delete_collection(row.key); }}};
    return options;
}

tui::ListOptions graph_view_options(const RootContext& context) {
    tui::ListOptions options;
    options.title = "Graph";
    options.columns = {"GRAPH", "KIND", "NODES", "EDGES"};
    options.load = [&context]() {
        // The named graphs as configured, then each collection holding a
        // graph of its own -- every name `graph stats` reads, graphs-first,
        // each row its document's counts.
        std::vector<std::string> names;
        std::set<std::string> named;
        if (const std::optional<harness::Config> config = config_if_any(config_file(context));
            config.has_value()) {
            for (const auto& [name, graph] : config->graphs) {
                names.push_back(name);
                named.insert(name);
            }
        }
        for (const CollectionRow& collection : collection_rows()) {
            if (collection.error.empty() && !named.contains(collection.name)) {
                names.push_back(collection.name);
            }
        }
        std::vector<tui::ListRow> rows;
        for (const std::string& name : names) {
            const nlohmann::json document = graph_stats_document(context, name);
            const bool built = document.value("built", false);
            if (!built && field(document, "kind") == "collection") {
                continue;  // a collection with no graph is not a graph
            }
            rows.push_back(
                tui::ListRow{.key = name,
                             .cells = {field(document, "graph"), field(document, "kind"),
                                       built ? field(document, "nodes") : std::string{"not built"},
                                       built ? field(document, "edges") : std::string{}},
                             .look = built ? tui::ListRow::Look::Plain : tui::ListRow::Look::Dim});
        }
        std::vector<std::string> heading;
        if (rows.empty()) {
            heading.emplace_back(
                "no graph yet -- 'apogee graph build <collection>' builds one over a collection");
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "stats";
    options.detail = [&context](const tui::ListRow& row) {
        return lines_of(graph_stats_text(context, row.key));
    };
    // `graph explain`'s own card, for a node of the selected graph.
    options.ask_label = "explain";
    options.ask_needs_row = true;
    options.ask = [&context](const tui::ListRow& row, const std::string& text) {
        return graph_explain_text(context, row.key, text);
    };
    return options;
}

std::vector<std::unique_ptr<tui::ListView>> make_knowledge_views(tui::Pump& pump, tui::Theme theme,
                                                                 const RootContext& context) {
    std::vector<std::unique_ptr<tui::ListView>> views;
    views.push_back(std::make_unique<tui::ListView>(pump, theme, knowledge_view_options(context)));
    views.push_back(
        std::make_unique<tui::ListView>(pump, theme, collections_view_options(context)));
    views.push_back(std::make_unique<tui::ListView>(pump, theme, graph_view_options(context)));
    return views;
}

}  // namespace apogee::commands
