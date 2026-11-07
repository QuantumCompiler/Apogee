#include "cli/uninstall.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <system_error>

#include "cli/helpers.h"
#include "contracts/assets.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

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
        // symlink_status: a row that is a symlink is present as itself -- and
        // so is a link the library cannot see (a junction on Windows, 57k),
        // even one whose target is gone.
        row.present = std::filesystem::exists(std::filesystem::symlink_status(path, code)) ||
                      platform::is_link(path, code);
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
        platform::remove_tree(path, code);
    } else {
        std::filesystem::remove(path, code);
    }
    if (code) {
        errors.push_back(path.string() + ": " + code.message());
    } else {
        removed.push_back(path.string());
    }
}

CompletionRoots completion_roots() {
    CompletionRoots roots;
    if (const std::optional<std::string> home = platform::home_directory(); home.has_value()) {
        roots.home = platform::path_from_utf8(*home);
    }
    roots.documents = platform::documents_directory();
    return roots;
}

std::vector<std::filesystem::path> completion_stub_paths(const CompletionRoots& roots) {
    // The stubs are the release channel's (M10): they call `apogee` by name,
    // and a dev or test install never writes any -- so its uninstall must not
    // take the release install's.
    if (harness::baked_channel() != harness::Channel::Release) {
        return {};
    }
    std::vector<std::filesystem::path> paths;
    if (!roots.home.empty()) {
        paths.push_back(roots.home / ".local" / "share" / "bash-completion" / "completions" /
                        "apogee");
        // zsh's, in each directory an installer may have chosen (57l): the
        // first of these on the user's `fpath`, or the last as the fallback.
        // The same three, in the same order, as `install.sh` and the
        // Makefile's install-zsh-completion -- `cli.install_script` holds the
        // three lists to one set. Before 57l only the fallback was looked
        // for, and a stub in ~/.zfunc outlived the binary it calls.
        paths.push_back(roots.home / ".zfunc" / "_apogee");
        paths.push_back(roots.home / ".oh-my-zsh" / "completions" / "_apogee");
        paths.push_back(roots.home / ".local" / "share" / "zsh" / "site-functions" / "_apogee");
        paths.push_back(roots.home / ".config" / "fish" / "completions" / "apogee.fish");
    }
    // PowerShell's, where install.ps1 put it: under the Documents folder it
    // asked Windows for (57k), a OneDrive or policy redirect included -- and
    // under `<home>/Documents`, where that folder was when a stub was written
    // before the redirect moved it (57l). The same folder off Windows, so
    // one path there.
    const std::filesystem::path stub =
        std::filesystem::path{"PowerShell"} / "Completions" / "apogee.ps1";
    if (!roots.documents.empty()) {
        paths.push_back(roots.documents / stub);
    }
    if (!roots.home.empty()) {
        const std::filesystem::path unredirected = roots.home / "Documents" / stub;
        if (roots.documents.empty() ||
            unredirected.lexically_normal() != (roots.documents / stub).lexically_normal()) {
            paths.push_back(unredirected);
        }
    }
    return paths;
}

UninstallPlan plan_uninstall(const std::filesystem::path& home, const std::filesystem::path& binary,
                             const CompletionRoots& roots) {
    UninstallPlan plan;
    std::error_code code;

    if (!binary.empty() && std::filesystem::exists(binary, code)) {
        plan.binary = binary;
        plan.binary_renamed_aside = !platform::running_binary_deletable();
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

    for (const std::filesystem::path& candidate : completion_stub_paths(roots)) {
        if (!std::filesystem::exists(candidate, code)) {
            continue;
        }
        // One file reached by two candidates -- a Documents folder spelled
        // in another case, a `~/.zfunc` linked to another completion
        // directory -- is planned once: a second removal would fail on the
        // file the first just removed (57l).
        const bool planned = std::ranges::any_of(
            plan.completions, [&candidate](const std::filesystem::path& listed) {
                std::error_code same;
                return std::filesystem::equivalent(listed, candidate, same);
            });
        if (!planned) {
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
        if (plan.binary_renamed_aside) {
            // Said before the answer, not discovered after it (57k).
            out << "                (Windows cannot delete a running program: it is renamed "
                   "aside, and the last line says when it goes)\n";
        }
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
                                           std::vector<std::string>& errors,
                                           std::vector<std::string>& notes,
                                           const BinaryRemover& remove_binary) {
    std::vector<std::string> removed;
    for (const std::filesystem::path& path : plan.completions) {
        remove_planned(path, false, removed, errors);
    }
    remove_planned(plan.data_directory, true, removed, errors);
    // The binary last: if removing it fails, everything else is still gone and
    // the user can delete one file. Doing it first risks a half-removal with no
    // command left to finish the job.
    if (plan.binary.empty()) {
        return removed;
    }
    const platform::OwnBinaryRemoval binary =
        remove_binary ? remove_binary(plan.binary) : platform::remove_own_binary(plan.binary);
    const std::string path = plan.binary.string();
    switch (binary.outcome) {
        case platform::OwnBinaryOutcome::Removed:
            removed.push_back(path);
            break;
        case platform::OwnBinaryOutcome::DeletedAtRestart:
            notes.push_back("renamed " + path + " to " + binary.aside.string() +
                            ": Windows cannot delete a running program, and deletes it at the "
                            "next restart");
            break;
        case platform::OwnBinaryOutcome::RenamedAside:
            notes.push_back("renamed " + path + " to " + binary.aside.string() +
                            ": Windows cannot delete a running program -- delete " +
                            binary.aside.string() + " after this command exits");
            break;
        case platform::OwnBinaryOutcome::Failed:
            errors.push_back(path + ": " + binary.error);
            break;
    }
    return removed;
}

int report_uninstall(const std::vector<std::string>& removed, const std::vector<std::string>& notes,
                     const std::vector<std::string>& errors, std::ostream& out, std::ostream& err) {
    for (const std::string& path : removed) {
        out << "removed " << path << "\n";
    }
    for (const std::string& note : notes) {
        out << note << "\n";
    }
    for (const std::string& error : errors) {
        err << "could not remove " << error << "\n";
    }
    return errors.empty() ? 0 : 1;
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

        UninstallPlan plan = plan_uninstall(home, platform::executable_path(), completion_roots());
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
        std::vector<std::string> notes;
        const std::vector<std::string> removed = execute_uninstall(plan, errors, notes);
        if (const int status = report_uninstall(removed, notes, errors, std::cout, std::cerr);
            status != 0) {
            throw CLI::RuntimeError(status);
        }
    });
}

}  // namespace apogee::commands
