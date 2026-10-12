#include "cli/tui_doctor.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cli/check.h"
#include "cli/providers_cmd.h"
#include "cli/tui_common.h"
#include "contracts/config.h"
#include "operations/system_view.h"

namespace apogee::commands {

namespace {

/// The key a row of the doctor's report is known by.
[[nodiscard]] std::string check_key(const CheckRow& row) {
    return row.section + "/" + row.name;
}

[[nodiscard]] tui::ListRow::Look check_look(Status status) {
    switch (status) {
        case Status::Fail:
        case Status::Warn:
            return tui::ListRow::Look::Attention;
        case Status::Skipped:
            return tui::ListRow::Look::Dim;
        case Status::Ok:
            return tui::ListRow::Look::Plain;
    }
    return tui::ListRow::Look::Plain;
}

/// The last read, kept for Enter's detail: the read and the detail both run
/// on the view's one worker, one after the other, and the lock keeps a
/// reader on another thread honest.
template <typename Rows>
struct LastRead {
    std::mutex mutex;
    Rows rows;
};

}  // namespace

tui::ListOptions check_view_options(const RootContext& context) {
    const auto last = std::make_shared<LastRead<std::vector<CheckRow>>>();
    tui::ListOptions options;
    options.title = "Check";
    options.columns = {"STATUS", "SECTION", "CHECK", "DETAIL"};
    options.load = [&context, last]() {
        // `apogee check`'s own read: the same inputs, the same report, its
        // rows as its JSON document words them.
        const CheckInputs inputs = check_inputs(context);
        const CheckReport report = read_check_report(inputs);
        const nlohmann::json document = render_report_document(report, std::nullopt);
        std::vector<tui::ListRow> rows;
        for (std::size_t i = 0; i < report.rows.size(); ++i) {
            const nlohmann::json& row = document["rows"].at(i);
            rows.push_back(tui::ListRow{.key = check_key(report.rows.at(i)),
                                        .cells = {field(row, "status"), field(row, "section"),
                                                  field(row, "name"), field(row, "detail")},
                                        .look = check_look(report.rows.at(i).status)});
        }
        {
            const std::lock_guard lock{last->mutex};
            last->rows = report.rows;
        }
        return std::pair{
            std::vector<std::string>{"checked " + inputs.home.string(), check_verdict(report)},
            std::move(rows)};
    };
    options.enter_label = "detail";
    options.detail = [last](const tui::ListRow& row) {
        const std::lock_guard lock{last->mutex};
        for (const CheckRow& checked : last->rows) {
            if (check_key(checked) != row.key) {
                continue;
            }
            std::vector<std::string> lines{std::string{to_string(checked.status)} + "  " +
                                               checked.section + ": " + checked.name,
                                           checked.detail};
            if (!checked.remedy.empty()) {
                lines.push_back("run: " + checked.remedy);
            }
            return lines;
        }
        return std::vector<std::string>{"read again -- the report has changed"};
    };
    options.actions = {tui::ListAction{
        .key = "f",
        .label = "fix",
        .confirm =
            [](const tui::ListRow& /*row*/) {
                return std::string{
                    "Run the fix pass? It creates what the layout is missing, corrects private "
                    "modes, and seeds the starter config only where no config file exists -- "
                    "an existing config is never touched"};
            },
        .run =
            [&context](const tui::ListRow& /*row*/) {
                // `apogee check --fix`'s own lines.
                std::string said;
                for (const std::string& line : fix_install(check_inputs(context))) {
                    said += line + "\n";
                }
                return said;
            },
        .whole_view = true}};
    return options;
}

tui::ListOptions providers_view_options(const RootContext& context) {
    const auto last = std::make_shared<LastRead<std::vector<ProviderRow>>>();
    tui::ListOptions options;
    options.title = "Providers";
    options.columns = {"PROVIDER", "TIER", "BACKEND"};
    options.load = [&context, last]() {
        // What the last explicit scan found -- read, never run.
        const std::optional<harness::Config> config = config_if_any(config_file(context));
        const std::vector<backends::ProviderStatus> statuses = last_provider_scan();
        std::vector<ProviderRow> read = provider_rows(statuses, config ? &*config : nullptr);
        std::vector<tui::ListRow> rows;
        for (const ProviderRow& row : read) {
            rows.push_back(tui::ListRow{.key = row.provider,
                                        .cells = {row.provider, row.tier, row.backend},
                                        .look = row.backend.starts_with("backend: ")
                                                    ? tui::ListRow::Look::Active
                                                : row.backend.empty() ? tui::ListRow::Look::Dim
                                                                      : tui::ListRow::Look::Plain});
        }
        {
            const std::lock_guard lock{last->mutex};
            last->rows = std::move(read);
        }
        std::vector<std::string> heading{
            statuses.empty() ? "not scanned yet -- s scans this machine for providers"
                             : "as the last scan found them -- s scans again"};
        if (!config.has_value()) {
            heading.emplace_back("no config file yet -- 'apogee config init' writes one");
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "evidence";
    options.detail = [last](const tui::ListRow& row) {
        const std::lock_guard lock{last->mutex};
        for (const ProviderRow& provider : last->rows) {
            if (provider.provider != row.key) {
                continue;
            }
            std::vector<std::string> lines{provider.label + " (" + provider.type + ")  " +
                                           provider.tier};
            for (const std::string& evidence : provider.evidence) {
                lines.push_back(evidence);
            }
            return lines;
        }
        return std::vector<std::string>{"read again -- the scan has changed"};
    };
    options.actions = {
        tui::ListAction{.key = "s",
                        .label = "scan",
                        .run =
                            [&context](const tui::ListRow& /*row*/) {
                                // `providers scan`'s own scan, on this key.
                                const std::size_t found =
                                    scan_providers_now(config_file(context), false).size();
                                return "scanned this machine: " + std::to_string(found) +
                                       " providers";
                            },
                        .whole_view = true},
        tui::ListAction{.key = "r",
                        .label = "register",
                        .confirm =
                            [](const tui::ListRow& row) {
                                // The offer's own question (28b).
                                return "Found " + row.key + " -- register it as a backend?";
                            },
                        .run =
                            [&context](const tui::ListRow& row) {
                                return register_provider(config_file(context), row.key);
                            }}};
    return options;
}

tui::ListOptions system_view_options(SystemSeams seams) {
    tui::ListOptions options;
    options.title = "System";
    options.page = true;
    options.load = [seams = std::move(seams)]() {
        // `apogee system`'s one reading, worded as it prints it.
        return std::pair{lines_of(operations::system_table(read_system_view(seams))),
                         std::vector<tui::ListRow>{}};
    };
    return options;
}

std::vector<std::unique_ptr<tui::ListView>> make_doctor_views(tui::Pump& pump, tui::Theme theme,
                                                              const RootContext& context) {
    std::vector<std::unique_ptr<tui::ListView>> views;
    views.push_back(std::make_unique<tui::ListView>(pump, theme, check_view_options(context)));
    views.push_back(std::make_unique<tui::ListView>(pump, theme, providers_view_options(context)));
    views.push_back(std::make_unique<tui::ListView>(pump, theme, system_view_options()));
    return views;
}

}  // namespace apogee::commands
