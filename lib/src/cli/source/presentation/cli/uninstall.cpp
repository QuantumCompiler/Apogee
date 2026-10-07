#include "cli/uninstall.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>

#include "cli/helpers.h"
#include "contracts/assets.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

/// Where each shell keeps a completion file, relative to the user's home.
///
/// Only paths Apogee's own installers write are listed. Removing a file a
/// package manager owns would be worse than leaving one behind, so this is
/// deliberately narrow.
[[nodiscard]] std::vector<std::filesystem::path> completion_candidates() {
    // The stubs are the release channel's (M10): they call `apogee` by name,
    // and a dev or test install never writes any -- so its uninstall must not
    // take the release install's.
    if (harness::baked_channel() != harness::Channel::Release) {
        return {};
    }
    const std::optional<std::string> home = platform::home_directory();
    if (!home.has_value()) {
        return {};
    }
    const std::filesystem::path root{*home};
    return {
        root / ".local" / "share" / "bash-completion" / "completions" / "apogee",
        root / ".local" / "share" / "zsh" / "site-functions" / "_apogee",
        root / ".config" / "fish" / "completions" / "apogee.fish",
        root / "Documents" / "PowerShell" / "Completions" / "apogee.ps1",
    };
}

/// Whether the directory at `path` holds anything that is not Apogee's own.
[[nodiscard]] bool holds_user_data(const std::filesystem::path& home,
                                   const std::filesystem::path& path) {
    // A directory holding only Apogee's own unedited bundled files (a fresh
    // install's prompts/ and schemas/) is not the user's data; one holding
    // anything else -- an edit, a scaffolded agent -- is. An empty sessions/
    // is not worth a warning; one with fifty conversations is.
    std::error_code code;
    bool found = false;
    for (const auto& item : std::filesystem::directory_iterator(path, code)) {
        if (!harness::is_unmodified_bundled_asset(home, item.path())) {
            found = true;
            break;
        }
    }
    return !code && found;
}

}  // namespace

std::vector<PlannedRow> plan_rows(const std::filesystem::path& home,
                                  std::span<const harness::LayoutEntry> rows) {
    std::vector<PlannedRow> planned;
    planned.reserve(rows.size());
    for (const harness::LayoutEntry& entry : rows) {
        PlannedRow row;
        row.name = std::string{entry.relative_path};
        const std::filesystem::path path = home / entry.relative_path;
        std::error_code code;
        // symlink_status: a row that is a symlink is present as itself.
        row.present = std::filesystem::exists(std::filesystem::symlink_status(path, code));
        row.user_data = row.present && entry.user_data && holds_user_data(home, path);
        planned.push_back(std::move(row));
    }
    return planned;
}

std::string describe_user_data(const std::vector<std::string>& lines) {
    if (lines.empty()) {
        return {};
    }
    // Named explicitly rather than folded into "data": these are the
    // directories whose loss the user cannot undo, and a prompt that says
    // "remove ~/.apogee?" does not convey that.
    std::ostringstream out;
    out << "\nIncluding YOUR OWN DATA in:\n";
    for (const std::string& line : lines) {
        out << "  " << line << "\n";
    }
    out << "\nThis cannot be undone.\n";
    return out.str();
}

Confirmation confirm_removal(std::string_view command, bool yes, bool interactive, std::istream& in,
                             std::ostream& out, std::ostream& err) {
    if (yes) {
        return Confirmation::Proceed;
    }
    if (!interactive) {
        // Refusing beats guessing. A piped removal with no way to ask must not
        // decide on the user's behalf that the answer is yes.
        err << "\napogee " << command
            << ": not a terminal -- rerun with --yes to confirm non-interactively\n";
        return Confirmation::Refused;
    }
    out << "\nType 'yes' to proceed: " << std::flush;
    std::string answer;
    std::getline(in, answer);
    if (answer != "yes") {
        out << "cancelled\n";
        return Confirmation::Cancelled;
    }
    return Confirmation::Proceed;
}

void remove_planned(const std::filesystem::path& path, bool recursive,
                    std::vector<std::string>& removed, std::vector<std::string>& errors) {
    if (path.empty()) {
        return;
    }
    std::error_code code;
    if (recursive) {
        std::filesystem::remove_all(path, code);
    } else {
        std::filesystem::remove(path, code);
    }
    if (code) {
        errors.push_back(path.string() + ": " + code.message());
    } else {
        removed.push_back(path.string());
    }
}

UninstallPlan plan_uninstall(const std::filesystem::path& home,
                             const std::filesystem::path& binary) {
    UninstallPlan plan;
    std::error_code code;

    if (!binary.empty() && std::filesystem::exists(binary, code)) {
        plan.binary = binary;
    }

    if (std::filesystem::exists(home, code)) {
        plan.data_directory = home;
        // Which user-owned trees actually have something in them -- the walk
        // reset shares, so the two prompts agree on what is the user's.
        for (const PlannedRow& row : plan_rows(home)) {
            if (row.user_data) {
                plan.user_data.push_back(row.name);
            }
        }
    }

    for (const std::filesystem::path& candidate : completion_candidates()) {
        if (std::filesystem::exists(candidate, code)) {
            plan.completions.push_back(candidate);
        }
    }

    return plan;
}

std::string describe_plan(const UninstallPlan& plan) {
    std::ostringstream out;
    out << "This will remove:\n";

    if (!plan.binary.empty()) {
        out << "  binary        " << plan.binary.string() << "\n";
    }
    if (!plan.data_directory.empty()) {
        out << "  data          " << plan.data_directory.string() << "\n";
    }
    for (const std::filesystem::path& path : plan.completions) {
        out << "  completion    " << path.string() << "\n";
    }
    if (plan.binary.empty() && plan.data_directory.empty() && plan.completions.empty()) {
        out << "  (nothing -- this install looks already removed)\n";
    }

    std::vector<std::string> lines;
    lines.reserve(plan.user_data.size());
    for (const std::string& name : plan.user_data) {
        lines.push_back(name + "/");
    }
    out << describe_user_data(lines);
    return out.str();
}

std::vector<std::string> execute_uninstall(const UninstallPlan& plan,
                                           std::vector<std::string>& errors) {
    std::vector<std::string> removed;
    for (const std::filesystem::path& path : plan.completions) {
        remove_planned(path, false, removed, errors);
    }
    remove_planned(plan.data_directory, true, removed, errors);
    // The binary last: if removing it fails, everything else is still gone and
    // the user can delete one file. Doing it first risks a half-removal with no
    // command left to finish the job.
    remove_planned(plan.binary, false, removed, errors);
    return removed;
}

std::string_view UninstallCommand::name() const noexcept {
    return "uninstall";
}

std::string_view UninstallCommand::summary() const noexcept {
    return "Remove Apogee, its data directory, and its completions";
}

void UninstallCommand::bind(CLI::App& root, const RootContext& context) {
    struct Flags {
        bool yes = false;
        bool keep_data = false;
    };

    auto flags = std::make_shared<Flags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_flag("-y,--yes", flags->yes, "Do not prompt. Your data directory IS removed.");
    cmd->add_flag("--keep-data", flags->keep_data,
                  "Remove the binary and completions but leave ~/.apogee alone");

    cmd->callback([&context, flags]() {
        (void)context;

        std::filesystem::path home;
        try {
            home = harness::install_home();  // its own channel's, never a root flag's (M10)
        } catch (const std::exception& e) {
            std::cerr << "apogee uninstall: " << e.what() << "\n";
            throw CLI::RuntimeError(1);
        }

        UninstallPlan plan = plan_uninstall(home, platform::executable_path());
        if (flags->keep_data) {
            plan.data_directory.clear();
            plan.user_data.clear();
        }

        std::cout << describe_plan(plan);

        // `stdin_is_piped` is the terminal question with one addition: a test
        // that feeds std::cin from a buffer is a pipe too, whatever runs it.
        switch (confirm_removal("uninstall", flags->yes, !stdin_is_piped(), std::cin, std::cout,
                                std::cerr)) {
            case Confirmation::Refused:
                throw CLI::RuntimeError(1);
            case Confirmation::Cancelled:
                return;
            case Confirmation::Proceed:
                break;
        }

        std::vector<std::string> errors;
        const std::vector<std::string> removed = execute_uninstall(plan, errors);
        for (const std::string& path : removed) {
            std::cout << "removed " << path << "\n";
        }
        for (const std::string& error : errors) {
            std::cerr << "could not remove " << error << "\n";
        }
        if (!errors.empty()) {
            throw CLI::RuntimeError(1);
        }
    });
}

}  // namespace apogee::commands
