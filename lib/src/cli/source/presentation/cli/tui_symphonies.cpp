#include "cli/tui_symphonies.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

#include "cli/symphonies_cmd.h"
#include "cli/tui_common.h"
#include "symphony/view.h"

namespace apogee::commands {

namespace {

/// The stages' roles in order, as `list` chains them: a stage that plays
/// another symphony as `play:<name>`.
[[nodiscard]] std::string stage_chain(const nlohmann::json& stages) {
    std::string out;
    for (const nlohmann::json& stage : stages) {
        out += out.empty() ? "" : " → ";
        out += stage.contains("play") ? "play:" + field(stage, "play") : field(stage, "role");
    }
    return out;
}

}  // namespace

tui::ListOptions symphonies_view_options(const RootContext& context, const WorkbenchHooks& hooks) {
    tui::ListOptions options;
    options.title = "Symphonies";
    options.columns = {"NAME", "STAGES", "SOURCE", "DESCRIPTION"};
    options.load = [&context]() {
        // `symphonies list`'s own read, its rows as its document states them.
        const nlohmann::json document = symphony::list_document(read_symphony_catalog(context));
        std::vector<tui::ListRow> rows;
        for (const nlohmann::json& definition : document["data"]) {
            const bool playable = definition["problems"].empty();
            rows.push_back(tui::ListRow{
                .key = field(definition, "name"),
                .cells = {field(definition, "name"), stage_chain(definition["stages"]),
                          field(definition, "source"),
                          playable ? field(definition, "description")
                                   : std::string{"cannot be played -- Enter says why"}},
                .look = playable ? tui::ListRow::Look::Plain : tui::ListRow::Look::Attention});
        }
        // The spec files that could not be read, as the command says them.
        std::vector<std::string> heading;
        for (const nlohmann::json& problem : document["problems"]) {
            heading.push_back("skipped " + problem.get<std::string>());
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "show";
    options.detail = [&context](const tui::ListRow& row) {
        return lines_of(symphony_show_text(context, row.key));
    };
    // `p`: the session's own `/play`, its input typed or none.
    options.asks = {tui::ListAsk{.key = "p",
                                 .label = "play",
                                 .ask =
                                     [hooks](const tui::ListRow& row, const std::string& input) {
                                         if (!hooks.play_symphony) {
                                             return std::string{
                                                 "not played: no session to play it in"};
                                         }
                                         return hooks.play_symphony(row.key, input);
                                     },
                                 .needs_row = true,
                                 .may_be_empty = true,
                                 .here = true}};
    return options;
}

}  // namespace apogee::commands
