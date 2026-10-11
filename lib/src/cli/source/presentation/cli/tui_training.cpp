#include "cli/tui_training.h"

#include <nlohmann/json.hpp>

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cli/datasets.h"
#include "cli/line_tokens.h"
#include "cli/train.h"
#include "cli/tui_child.h"
#include "cli/tui_common.h"
#include "contracts/layout.h"
#include "operations/dataset_core.h"
#include "operations/training_reads.h"
#include "training/datasets.h"
#include "training/store.h"

namespace apogee::commands {

namespace {

/// A version row: its ledger's facts the keys need.
struct VersionRow {
    std::string backend;
    std::string run_id;
    bool can_roll_back = false;
};

struct LastVersions {
    std::mutex mutex;
    std::map<std::string, VersionRow> by_key;
};

struct LastDatasets {
    std::mutex mutex;
    std::map<std::string, training::DatasetInfo> by_name;
};

/// The file or directory name a version's weights are kept under.
[[nodiscard]] std::string weights_name(const nlohmann::json& version) {
    if (version.contains("mlx_path")) {
        return "mlx/" + std::filesystem::path{field(version, "mlx_path")}.filename().string();
    }
    return std::filesystem::path{field(version, "gguf_path")}.filename().string();
}

[[nodiscard]] std::optional<bool> optional_bool(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() ? std::optional<bool>{found->get<bool>()}
                                                        : std::nullopt;
}

[[nodiscard]] std::optional<double> optional_number(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number() ? std::optional<double>{found->get<double>()}
                                                       : std::nullopt;
}

}  // namespace

tui::ListOptions train_view_options(const RootContext& context,
                                    std::shared_ptr<tui::Progress> progress,
                                    std::filesystem::path binary) {
    const auto last = std::make_shared<LastVersions>();
    tui::ListOptions options;
    options.title = "Train";
    options.columns = {"BACKEND", "VERSION", "PROMOTED", "RUN", "EVAL", "WEIGHTS"};
    options.progress = progress;
    options.load = [&context, last]() {
        // `train versions`' own document, a row per version, under `train
        // status`'s lines.
        const nlohmann::json document = *operations::training_versions_document();
        const training::TrainingStore store{harness::training_dir()};
        std::vector<tui::ListRow> rows;
        std::map<std::string, VersionRow> by_key;
        for (const nlohmann::json& ledger : document["data"]) {
            const std::string backend = field(ledger, "backend_name");
            const int active = ledger.value("active_version", 0);
            bool can_roll_back = false;
            if (const std::optional<training::VersionLedger> held = store.list_versions(backend);
                held.has_value()) {
                can_roll_back = training::rollback_target(*held).entry != nullptr;
            }
            for (const nlohmann::json& version : ledger["versions"]) {
                const int number = version.value("version", 0);
                const std::string key = backend + " v" + std::to_string(number);
                const bool pruned = version.contains("pruned_at");
                std::string promoted = field(version, "promoted_at");
                rows.push_back(tui::ListRow{
                    .key = key,
                    .cells = {backend,
                              "v" + std::to_string(number) + (number == active ? " active" : ""),
                              promoted.substr(0, 19), field(version, "run_id"),
                              eval_glyph(optional_bool(version, "eval_passed"),
                                         optional_number(version, "eval_score")),
                              weights_name(version) + (pruned ? " (pruned)" : "")},
                    .look = number == active ? tui::ListRow::Look::Active
                            : pruned         ? tui::ListRow::Look::Dim
                                             : tui::ListRow::Look::Plain});
                by_key.emplace(key, VersionRow{.backend = backend,
                                               .run_id = field(version, "run_id"),
                                               .can_roll_back = can_roll_back});
            }
        }
        {
            const std::lock_guard lock{last->mutex};
            last->by_key = std::move(by_key);
        }
        return std::pair{lines_of(train_status_text(context)), std::move(rows)};
    };
    const auto version_of = [last](const tui::ListRow& row) -> std::optional<VersionRow> {
        const std::lock_guard lock{last->mutex};
        const auto found = last->by_key.find(row.key);
        return found == last->by_key.end() ? std::nullopt
                                           : std::optional<VersionRow>{found->second};
    };
    // Enter: the version's run evaluated, as `train eval` does.
    options.enter_label = "eval";
    options.open = [&context, progress, binary, version_of](const tui::ListRow& row) {
        if (const std::optional<VersionRow> version = version_of(row);
            version.has_value() && !version->run_id.empty()) {
            (void)start_child_run(*progress, context, binary, {"train", "eval", version->run_id},
                                  "train eval " + version->run_id);
        }
    };
    const auto launch = [&context, progress, binary](std::string key, std::string label,
                                                     std::string prefill, std::string spend) {
        return tui::ListAsk{
            .key = std::move(key),
            .label = std::move(label),
            .ask =
                [&context, progress, binary](const tui::ListRow& /*row*/, const std::string& line) {
                    return run_typed(*progress, context, binary, "train", line);
                },
            .prefill = [prefill](const tui::ListRow& /*row*/) { return prefill; },
            .confirm =
                [spend](const tui::ListRow& /*row*/, const std::string& line) {
                    return "Start 'apogee train " + line + "'? " + spend;
                },
            .here = true};
    };
    options.asks = {
        launch("r", "run", "run ", "A training run holds this machine until it ends"),
        launch("p", "pipeline", "pipeline run --pipeline ",
               "Each stage is a training run, held to the cumulative gate"),
        launch("P", "promote", "promote ",
               "Promotion fuses, converts and registers a version on the backend named")};
    options.actions = {
        tui::ListAction{
            .key = "c",
            .label = "cycle",
            .confirm =
                [](const tui::ListRow& /*row*/) {
                    return std::string{
                        "Run the training cycle once ('apogee train cycle run')? It "
                        "trains, gates and may promote -- this machine is held until "
                        "it ends"};
                },
            .run =
                [&context, progress, binary](const tui::ListRow& /*row*/) {
                    if (!start_child_run(*progress, context, binary, {"train", "cycle", "run"},
                                         "train cycle run")) {
                        return std::string{"not now: a run is going -- Ctrl-C stops it"};
                    }
                    return std::string{"running the cycle -- its narration is below"};
                },
            .whole_view = true},
        tui::ListAction{
            .key = "R",
            .label = "rollback",
            .applies =
                [version_of](const tui::ListRow& row) {
                    const std::optional<VersionRow> version = version_of(row);
                    return version.has_value() && version->can_roll_back;
                },
            .confirm =
                [version_of](const tui::ListRow& row) {
                    const std::string backend = version_of(row).value_or(VersionRow{}).backend;
                    return "Roll " + backend +
                           " back to its previous version ('apogee train "
                           "rollback " +
                           backend + "')?";
                },
            .run =
                [&context, binary, version_of](const tui::ListRow& row) {
                    return child_answer(
                        context, binary,
                        {"train", "rollback", version_of(row).value_or(VersionRow{}).backend});
                }}};
    return options;
}

tui::ListOptions datasets_view_options(const RootContext& context,
                                       std::shared_ptr<tui::Progress> progress,
                                       std::filesystem::path binary) {
    const auto last = std::make_shared<LastDatasets>();
    tui::ListOptions options;
    options.title = "Datasets";
    options.columns = {"NAME", "LINES", "SHAPE", "SIZE"};
    options.progress = progress;
    options.load = [last]() {
        // `datasets list`'s own read, as its document states it.
        const training::DatasetStore store{harness::training_datasets_dir()};
        std::vector<training::DatasetInfo> datasets = store.list();
        std::vector<tui::ListRow> rows;
        for (const training::DatasetInfo& info : datasets) {
            const nlohmann::json row = dataset_json(info);
            rows.push_back(
                tui::ListRow{.key = field(row, "name"),
                             .cells = {field(row, "name"), field(row, "lines"), field(row, "shape"),
                                       human_size(row.value("bytes", std::int64_t{0}))}});
        }
        std::vector<std::string> heading;
        if (rows.empty()) {
            heading.push_back("no datasets under " + store.dir().string());
        }
        {
            const std::lock_guard lock{last->mutex};
            last->by_name.clear();
            for (training::DatasetInfo& info : datasets) {
                std::string name = info.name;
                last->by_name.emplace(std::move(name), std::move(info));
            }
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "info";
    options.detail = [last](const tui::ListRow& row) {
        const std::lock_guard lock{last->mutex};
        const auto found = last->by_name.find(row.key);
        if (found == last->by_name.end()) {
            return std::vector<std::string>{"read again -- the datasets have changed"};
        }
        return lines_of(dataset_info_text(found->second));
    };
    const auto launch = [&context, progress, binary](std::string key, std::string label,
                                                     std::string prefill, std::string spend) {
        return tui::ListAsk{
            .key = std::move(key),
            .label = std::move(label),
            .ask =
                [&context, progress, binary](const tui::ListRow& /*row*/, const std::string& line) {
                    return run_typed(*progress, context, binary, "datasets", line);
                },
            .prefill = [prefill](const tui::ListRow& /*row*/) { return prefill; },
            .confirm =
                [spend](const tui::ListRow& /*row*/, const std::string& line) {
                    return "Start 'apogee datasets " + line + "'? " + spend;
                },
            .here = true};
    };
    options.asks = {
        launch("p", "prepare", "prepare ", "It converts the file in this machine's environment"),
        launch("s", "synth", "synth ", "The teacher is called once for every batch")};
    options.actions = {
        tui::ListAction{.key = "k",
                        .label = "kits",
                        .run = [](const tui::ListRow& /*row*/) { return dataset_kits_text(); },
                        .whole_view = true},
        tui::ListAction{.key = "x",
                        .label = "delete",
                        .confirm =
                            [](const tui::ListRow& row) {
                                return "Delete dataset " + row.key + " ('apogee datasets delete " +
                                       row.key + "')?";
                            },
                        .run =
                            [&context, binary](const tui::ListRow& row) {
                                return child_answer(context, binary,
                                                    {"datasets", "delete", row.key, "--yes"});
                            }}};
    return options;
}

}  // namespace apogee::commands
