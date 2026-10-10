#include "cli/system_cmd.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "backends/llama_runtime.h"
#include "cli/helpers.h"
#include "contracts/layout.h"
#include "machine/json_reporter.h"

namespace apogee::commands {

operations::SystemView read_system_view(const SystemSeams& seams) {
    const platform::SystemSource& source =
        seams.source != nullptr ? *seams.source : platform::host_system();
    operations::SystemView view;
    view.machine = platform::read_machine(source, platform::kUtilizationWindow, seams.wait);
    // Read once: the number the table and the document both show.
    if (seams.machine) {
        view.model_budget = seams.machine().bytes;
    }
    if (!view.model_budget.has_value()) {
        view.model_budget_unknown = backends::llama_available()
                                        ? "no device llama.cpp offloads to reports its memory"
                                        : "this build has no llama.cpp";
    }
    // This process loads nothing: it holds no model, and the view says where
    // a session's are seen instead (27e -- residency is the holder's to say).
    view.store = operations::read_store_disk(source, harness::models_dir());
    return view;
}

SystemCommand::SystemCommand(SystemSeams seams) : seams_{std::move(seams)} {}

std::string_view SystemCommand::name() const noexcept {
    return "system";
}

std::string_view SystemCommand::summary() const noexcept {
    return "Show this machine as Apogee reads it: CPU, memory, GPU, the model store's disk";
}

void SystemCommand::bind(CLI::App& root, const RootContext& /*context*/) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(cmd, format);
    cmd->callback([this, format]() {
        const operations::SystemView view = read_system_view(seams_);
        if (*format == ReadFormat::Json) {
            write_document(std::cout, operations::system_document(view));
            return;
        }
        std::cout << operations::system_table(view);
    });
}

}  // namespace apogee::commands
