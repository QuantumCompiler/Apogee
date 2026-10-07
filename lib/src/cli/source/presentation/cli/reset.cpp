#include "cli/reset.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "cli/check.h"
#include "cli/helpers.h"
#include "contracts/paths.h"
#include "secrets/store.h"

namespace apogee::commands {
namespace {

[[nodiscard]] bool contains(const std::vector<std::string>& list, std::string_view word) {
    return std::ranges::find(list, word) != list.end();
}

[[nodiscard]] std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        out += (out.empty() ? "" : ", ") + word;
    }
    return out;
}

}  // namespace

bool ResetPlan::removes_anything() const noexcept {
    return !unlisted.empty() || std::ranges::any_of(rows, [](const PlannedRow& row) {
        return row.present && !row.kept;
    });
}

std::vector<std::string> keepable_rows(std::span<const harness::LayoutEntry> rows) {
    std::vector<std::string> names;
    names.reserve(rows.size());
    for (const harness::LayoutEntry& entry : rows) {
        names.emplace_back(entry.relative_path);
    }
    return names;
}

ResetPlan plan_reset(const std::filesystem::path& home, const std::vector<std::string>& keep,
                     const std::filesystem::path& secrets_store,
                     std::span<const harness::LayoutEntry> rows) {
    const std::vector<std::string> keepable = keepable_rows(rows);
    for (const std::string& name : keep) {
        if (!contains(keepable, name)) {
            throw std::invalid_argument("'" + name +
                                        "' is not a row of the data directory -- --keep takes one "
                                        "of: " +
                                        joined(keepable));
        }
    }

    ResetPlan plan;
    plan.home = home;
    plan.rows = plan_rows(home, rows);
    for (PlannedRow& row : plan.rows) {
        row.kept = contains(keep, row.name);
    }

    // Whatever else is at the top of the directory, found rather than listed:
    // a list of what to expect there would be a second layout declaration.
    std::error_code code;
    if (std::filesystem::is_directory(home, code)) {
        for (const auto& item : std::filesystem::directory_iterator(home, code)) {
            std::string name = item.path().filename().string();
            if (!contains(keepable, name)) {
                plan.unlisted.push_back(std::move(name));
            }
        }
        // Directory order is the filesystem's; the prompt's is stable.
        std::ranges::sort(plan.unlisted);
    }

    // The secrets store, by the row it lives in -- asked of where the store
    // says it is, so a store that moves rows is still named.
    std::error_code store_code;
    if (!secrets_store.empty() && std::filesystem::exists(secrets_store, store_code)) {
        const std::filesystem::path relative = secrets_store.lexically_relative(home);
        if (!relative.empty() && *relative.begin() != "..") {
            const std::string top = relative.begin()->string();
            const auto row = std::ranges::find(plan.rows, top, &PlannedRow::name);
            const bool removed = row != plan.rows.end() ? !row->kept : contains(plan.unlisted, top);
            if (removed) {
                plan.secrets_row = top;
                plan.secrets_file = secrets_store.filename().string();
            }
        }
    }
    return plan;
}

std::string describe_plan(const ResetPlan& plan) {
    std::ostringstream out;
    out << "This will reset " << plan.home.string() << " to a fresh install:\n";
    for (const PlannedRow& row : plan.rows) {
        std::string_view action = "absent";
        if (row.kept) {
            action = "keep";
        } else if (row.present) {
            action = "remove";
        }
        out << "  " << std::left << std::setw(8) << action << row.name << "/\n";
    }
    for (const std::string& name : plan.unlisted) {
        out << "  " << std::left << std::setw(8) << "remove" << name << "   (not in the layout)\n";
    }
    if (!plan.removes_anything()) {
        out << "  (nothing to remove -- every row is kept or absent)\n";
    }

    // The user's own data, warned exactly as uninstall warns it -- and the
    // secrets store by name, since "config/" does not say "your API keys".
    const auto with_secrets = [&plan](std::string line, std::string_view name) {
        if (name == plan.secrets_row) {
            line += "   the secrets store (" + plan.secrets_file + "): your stored API keys";
        }
        return line;
    };
    std::vector<std::string> lines;
    for (const PlannedRow& row : plan.rows) {
        if (!row.kept && (row.user_data || row.name == plan.secrets_row)) {
            lines.push_back(with_secrets(row.name + "/", row.name));
        }
    }
    // Outside the layout nothing says what an entry is, so it is not assumed
    // to be Apogee's: the chat line editor's history is what the user typed.
    for (const std::string& name : plan.unlisted) {
        lines.push_back(with_secrets(name, name));
    }
    out << describe_user_data(lines);

    out << "\nA kept row is left exactly as it is. Everything else is then recreated as a fresh "
           "install has it ('apogee check --fix'), and checked.\n";
    return out.str();
}

std::vector<std::string> execute_reset(const ResetPlan& plan, std::vector<std::string>& errors) {
    std::vector<std::string> removed;
    for (const PlannedRow& row : plan.rows) {
        if (!row.kept && row.present) {
            remove_planned(plan.home / row.name, true, removed, errors);
        }
    }
    for (const std::string& name : plan.unlisted) {
        remove_planned(plan.home / name, true, removed, errors);
    }
    return removed;
}

std::string_view ResetCommand::name() const noexcept {
    return "reset";
}

std::string_view ResetCommand::summary() const noexcept {
    return "Reset the data directory to a fresh install, keeping the rows you name";
}

void ResetCommand::bind(CLI::App& root, const RootContext& context) {
    struct Flags {
        bool yes = false;
        std::vector<std::string> keep;
    };

    auto flags = std::make_shared<Flags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    // The accepted set IS the layout's rows, read now -- and completion reads
    // this validator, so what TAB offers and what the command takes are one
    // list, and it is the layout's (ADR 0007).
    cmd->add_option("--keep", flags->keep,
                    "A row of the data directory to leave exactly as it is (repeatable); every "
                    "other row is reset")
        ->check(CLI::IsMember(keepable_rows()))
        ->allow_extra_args(false);
    cmd->add_flag("-y,--yes", flags->yes, "Do not prompt. Every row not kept IS removed.");

    cmd->callback([&context, flags]() {
        std::filesystem::path home;
        std::filesystem::path secrets_store;
        try {
            home = harness::apogee_home();
            // The store inside THIS root -- a `--config` elsewhere is not reset.
            secrets_store = secrets::credentials_path(harness::default_config_path());
        } catch (const std::exception& e) {
            std::cerr << "apogee reset: " << e.what() << "\n";
            throw CLI::RuntimeError(1);
        }

        ResetPlan plan;
        try {
            plan = plan_reset(home, flags->keep, secrets_store);
        } catch (const std::invalid_argument& e) {
            std::cerr << "apogee reset: " << e.what() << "\n";
            throw CLI::RuntimeError(1);
        }

        std::cout << describe_plan(plan);

        switch (confirm_removal("reset", flags->yes, !stdin_is_piped(), std::cin, std::cout,
                                std::cerr)) {
            case Confirmation::Refused:
                throw CLI::RuntimeError(1);
            case Confirmation::Cancelled:
                return;
            case Confirmation::Proceed:
                break;
        }

        std::vector<std::string> errors;
        const std::vector<std::string> removed = execute_reset(plan, errors);
        for (const std::string& path : removed) {
            std::cout << "removed " << path << "\n";
        }
        for (const std::string& error : errors) {
            std::cerr << "could not remove " << error << "\n";
        }

        // The skeleton, recreated by the one seeding path and verified by the
        // doctor: the `check --fix` pass itself, in this process. Run after a
        // partial failure too, so the report says what the directory really is.
        std::cout << "\n";
        CheckInputs inputs;
        inputs.home = home;
        inputs.config_path = harness::resolve_config_path(context.config_path);
        const bool passed =
            run_check_pass(std::move(inputs), CheckPassOptions{.fix = true, .fold_created = true});

        if (!errors.empty()) {
            std::cerr << "apogee reset: " << errors.size()
                      << " path(s) could not be removed (above); the report is the directory as "
                         "it now is\n";
        }
        if (!errors.empty() || !passed) {
            throw CLI::RuntimeError(1);
        }
    });
}

}  // namespace apogee::commands
