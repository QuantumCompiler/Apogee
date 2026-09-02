#include "commands/uninstall.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>

#include "harness/layout.h"
#include "harness/paths.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

/// Where each shell keeps a completion file, relative to the user's home.
///
/// Only paths Apogee's own installers write are listed. Removing a file a
/// package manager owns would be worse than leaving one behind, so this is
/// deliberately narrow.
[[nodiscard]] std::vector<std::filesystem::path> completion_candidates() {
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

}  // namespace

UninstallPlan plan_uninstall(const std::filesystem::path& home,
                             const std::filesystem::path& binary) {
    UninstallPlan plan;
    std::error_code code;

    if (!binary.empty() && std::filesystem::exists(binary, code)) {
        plan.binary = binary;
    }

    if (std::filesystem::exists(home, code)) {
        plan.data_directory = home;

        // Which user-owned trees actually have something in them. An empty
        // sessions/ is not worth a warning; one with fifty conversations is.
        for (const harness::LayoutEntry& entry : harness::data_directories()) {
            if (!entry.user_data) {
                continue;
            }
            const std::filesystem::path path = home / entry.relative_path;
            if (!std::filesystem::exists(path, code)) {
                continue;
            }
            const auto begin = std::filesystem::directory_iterator(path, code);
            if (code || begin == std::filesystem::directory_iterator{}) {
                continue;
            }
            plan.user_data.emplace_back(entry.relative_path);
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

    if (plan.touches_user_data()) {
        // Named explicitly rather than folded into "data": these are the
        // directories whose loss the user cannot undo, and a prompt that says
        // "remove ~/.apogee?" does not convey that.
        out << "\nIncluding YOUR OWN DATA in:\n";
        for (const std::string& name : plan.user_data) {
            out << "  " << name << "/\n";
        }
        out << "\nThis cannot be undone.\n";
    }
    return out.str();
}

std::vector<std::string> execute_uninstall(const UninstallPlan& plan,
                                           std::vector<std::string>& errors) {
    std::vector<std::string> removed;
    std::error_code code;

    const auto drop = [&](const std::filesystem::path& path, bool recursive) {
        if (path.empty()) {
            return;
        }
        if (recursive) {
            std::filesystem::remove_all(path, code);
        } else {
            std::filesystem::remove(path, code);
        }
        if (code) {
            errors.push_back(path.string() + ": " + code.message());
            code.clear();
        } else {
            removed.push_back(path.string());
        }
    };

    for (const std::filesystem::path& path : plan.completions) {
        drop(path, false);
    }
    drop(plan.data_directory, true);
    // The binary last: if removing it fails, everything else is still gone and
    // the user can delete one file. Doing it first risks a half-removal with no
    // command left to finish the job.
    drop(plan.binary, false);

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
            home = harness::apogee_home();
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

        if (!flags->yes) {
            if (!platform::is_terminal(platform::StandardStream::In)) {
                // Refusing beats guessing. A piped uninstall with no way to ask
                // must not decide on the user's behalf that the answer is yes.
                std::cerr << "\napogee uninstall: not a terminal -- rerun with --yes to confirm "
                             "non-interactively\n";
                throw CLI::RuntimeError(1);
            }
            std::cout << "\nType 'yes' to proceed: " << std::flush;
            std::string answer;
            std::getline(std::cin, answer);
            if (answer != "yes") {
                std::cout << "cancelled\n";
                return;
            }
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
