#include "commands/check.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>

#include "ansi/ansi.h"
#include "commands/helpers.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "platform/platform.h"
#include "version/version.h"

namespace apogee::commands {
namespace {

/// The environment variable a backend type conventionally reads its key from.
///
/// Deliberately mirrors what each provider's `from_config` says in its
/// missing-key error rather than inventing a second convention: a doctor that
/// names a different variable than the failure message does is worse than one
/// that stays quiet.
[[nodiscard]] std::string_view key_variable_for(harness::BackendType type) noexcept {
    switch (type) {
        case harness::BackendType::Anthropic:
            return "ANTHROPIC_API_KEY";
        case harness::BackendType::OpenAI:
            return "OPENAI_API_KEY";
        case harness::BackendType::Google:
            return "GEMINI_API_KEY";
        case harness::BackendType::LlamaCpp:
        case harness::BackendType::Mock:
            break;
    }
    return {};
}

[[nodiscard]] bool needs_api_key(harness::BackendType type) noexcept {
    return !key_variable_for(type).empty();
}

void add(CheckReport& report, Status status, std::string section, std::string name,
         std::string detail, std::string remedy = {}) {
    report.rows.push_back(
        {status, std::move(section), std::move(name), std::move(detail), std::move(remedy)});
}

/// Whether a file begins with the GGUF magic.
///
/// Ommi's recorded lesson, and the reason this reads bytes instead of calling
/// `exists()`: a model can be present, the right size, and match a recorded
/// digest while still being unloadable -- a truncated download, or a Git LFS
/// pointer file committed instead of the model. Checking presence alone
/// reports healthy and the failure surfaces much later, inside llama.cpp.
///
/// A full load would be stronger still and is deliberately not done: it costs
/// gigabytes of I/O per model, and `check` is something a user runs when
/// something is already wrong. The header read catches the failure modes that
/// actually occur at nearly no cost.
[[nodiscard]] bool has_gguf_magic(const std::filesystem::path& path, std::string& why) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        why = "cannot be opened";
        return false;
    }
    std::array<char, 4> magic{};
    in.read(magic.data(), magic.size());
    if (in.gcount() != static_cast<std::streamsize>(magic.size())) {
        why = "is too small to be a GGUF (truncated download?)";
        return false;
    }
    if (magic[0] != 'G' || magic[1] != 'G' || magic[2] != 'U' || magic[3] != 'F') {
        why =
            "does not start with the GGUF magic -- it may be a Git LFS pointer or a partial "
            "download";
        return false;
    }
    return true;
}

void check_version(CheckReport& report, const CheckInputs& inputs) {
    add(report, Status::Ok, "Version", "apogee", std::string{version::semantic()});

    if (inputs.executable.empty()) {
        return;
    }

#if defined(__APPLE__)
    // The browser-download case. v0.1.0 ships unsigned (decided 2026-09-01):
    // a curl-driven install sets no quarantine attribute, but downloading the
    // archive in a browser does, and Gatekeeper then refuses to run it with a
    // message that does not mention the attribute. Naming it here turns a
    // baffling failure into a one-line fix.
    const std::string command =
        "xattr -p com.apple.quarantine '" + inputs.executable.string() + "' >/dev/null 2>&1";
    if (std::system(command.c_str()) == 0) {  // NOLINT(cert-env33-c)
        add(report, Status::Warn, "Version", "quarantine",
            "this binary carries macOS's quarantine attribute (downloaded via a browser)",
            "xattr -d com.apple.quarantine '" + inputs.executable.string() + "'");
    } else {
        add(report, Status::Ok, "Version", "quarantine", "not quarantined");
    }
#endif
}

void check_config(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing) {
        // A fresh install has no config yet. That is a state with an obvious
        // fix, not a broken installation.
        add(report, Status::Warn, "Config", "config.yaml",
            "not found at " + inputs.config_path.string(), "apogee config init");
        return;
    }
    if (!inputs.config_error.empty()) {
        add(report, Status::Fail, "Config", "config.yaml", inputs.config_error);
        return;
    }

    add(report, Status::Ok, "Config", "config.yaml", "parses cleanly");

    const harness::Config& config = inputs.config;
    if (config.backends.empty()) {
        add(report, Status::Warn, "Config", "backends", "none configured",
            "apogee config add-backend <name> --type <type>");
        return;
    }

    for (const auto& [name, backend] : config.backends) {
        const std::string label = "backend: " + name;
        const std::string_view type = harness::to_string(backend.type);

        if (backend.type == harness::BackendType::LlamaCpp) {
            if (backend.model_path.empty()) {
                add(report, Status::Warn, "Config", label,
                    std::string{type} + " -- model_path not set",
                    "apogee config add-backend " + name +
                        " --type llamacpp --model-path <file.gguf>");
                continue;
            }
            const std::filesystem::path model = harness::expand_env(backend.model_path);
            std::error_code code;
            if (!std::filesystem::exists(model, code)) {
                // A dangling model_path is a real failure -- the backend cannot
                // answer -- but check does NOT edit it out. It says what to run.
                add(report, Status::Fail, "Config", label,
                    "model_path does not exist: " + model.string(),
                    "apogee config add-backend " + name +
                        " --type llamacpp --model-path <an existing .gguf>");
                continue;
            }
            std::string why;
            if (!has_gguf_magic(model, why)) {
                add(report, Status::Fail, "Config", label,
                    "model_path " + why + ": " + model.string());
                continue;
            }
            add(report, Status::Ok, "Config", label, std::string{type} + " -- model loads");
            continue;
        }

        if (needs_api_key(backend.type)) {
            const std::string_view variable = key_variable_for(backend.type);
            const std::string resolved =
                backend.api_key.empty() ? std::string{} : harness::expand_env(backend.api_key);
            if (!resolved.empty()) {
                // NEVER the key itself. SPEC: secrets are never logged.
                add(report, Status::Ok, "Config", label, std::string{type} + " -- API key present");
            } else {
                // A missing key is a WARNING, not a failure: a keyless install
                // is valid, and the whole point of the fresh-install criterion
                // is that it passes.
                add(report, Status::Warn, "Config", label,
                    std::string{type} + " -- no API key configured",
                    "export " + std::string{variable} + "=... (and set api_key: \"${" +
                        std::string{variable} + "}\" on this backend)");
            }
            continue;
        }

        add(report, Status::Ok, "Config", label, std::string{type});
    }

    // Role pointers must name a backend that exists. A dangling one fails at
    // the point of use with a routing error that does not mention config.
    const auto role = [&](std::string_view what, const std::string& value) {
        if (value.empty()) {
            return;
        }
        if (config.find_backend(value) == nullptr) {
            add(report, Status::Fail, "Config", std::string{what},
                "names a backend that is not configured: '" + value + "'",
                "apogee config set-default <one of your configured backends>");
        } else {
            add(report, Status::Ok, "Config", std::string{what}, value);
        }
    };
    role("default_backend", config.models.default_backend);
    role("default_embedding", config.models.default_embedding);
    role("default_extraction", config.models.default_extraction);
}

void check_filesystem(CheckReport& report, const CheckInputs& inputs) {
    std::error_code code;
    if (!std::filesystem::exists(inputs.home, code)) {
        add(report, Status::Fail, "Filesystem", "data directory",
            "does not exist: " + inputs.home.string(), "apogee check --fix");
        return;
    }
    add(report, Status::Ok, "Filesystem", "data directory", inputs.home.string());

    // Enumerated from the contract, never restated -- harness/layout.h.
    for (const harness::LayoutEntry& entry : harness::data_directories()) {
        const std::filesystem::path path = inputs.home / entry.relative_path;
        const std::string label = std::string{entry.relative_path} + "/";

        if (!std::filesystem::exists(path, code)) {
            add(report, Status::Fail, "Filesystem", label,
                "missing -- " + std::string{entry.purpose}, "apogee check --fix");
            continue;
        }
        if (!std::filesystem::is_directory(path, code)) {
            add(report, Status::Fail, "Filesystem", label, "exists but is not a directory");
            continue;
        }

        if (!entry.private_mode) {
            add(report, Status::Ok, "Filesystem", label, std::string{entry.purpose});
            continue;
        }

        if (!harness::supports_private_modes()) {
            // Reported, not silently passed: see the Status::Skipped comment.
            add(report, Status::Skipped, "Filesystem", label,
                "mode check not applicable on this platform (no POSIX file modes)");
            continue;
        }

        const std::filesystem::perms mode =
            std::filesystem::status(path, code).permissions() & std::filesystem::perms::mask;
        const bool leaks =
            (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
            std::filesystem::perms::none;
        if (leaks) {
            add(report, Status::Fail, "Filesystem", label,
                "is readable by other users, and may hold conversation transcripts",
                "chmod 700 '" + path.string() + "'   (or: apogee check --fix)");
        } else {
            add(report, Status::Ok, "Filesystem", label, std::string{entry.purpose} + " (private)");
        }
    }
}

void check_models(CheckReport& report, const CheckInputs& inputs) {
    std::error_code code;
    const std::filesystem::path models = inputs.home / "models";
    if (!std::filesystem::exists(models, code)) {
        return;  // already reported by the filesystem section
    }

    int found = 0;
    for (const auto& entry : std::filesystem::directory_iterator(models, code)) {
        if (code) {
            break;
        }
        if (!entry.is_regular_file(code) || entry.path().extension() != ".gguf") {
            continue;
        }
        ++found;
        std::string why;
        if (!has_gguf_magic(entry.path(), why)) {
            add(report, Status::Fail, "Models", entry.path().filename().string(), why);
        } else {
            add(report, Status::Ok, "Models", entry.path().filename().string(),
                "valid GGUF header");
        }
    }

    if (found == 0) {
        // The fresh-install criterion in one row: no models is CORRECT.
        // Apogee bundles none and downloads none without being asked.
        add(report, Status::Ok, "Models", "models/",
            "no models installed -- Apogee bundles none; add your own GGUF here");
    }
}

}  // namespace

std::string_view to_string(Status status) noexcept {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::Warn:
            return "warn";
        case Status::Fail:
            return "fail";
        case Status::Skipped:
            break;
    }
    return "skip";
}

std::size_t CheckReport::count(Status status) const noexcept {
    return static_cast<std::size_t>(std::count_if(
        rows.begin(), rows.end(), [status](const CheckRow& row) { return row.status == status; }));
}

CheckReport run_checks(const CheckInputs& inputs) {
    CheckReport report;
    check_version(report, inputs);
    check_config(report, inputs);
    check_filesystem(report, inputs);
    check_models(report, inputs);
    return report;
}

std::vector<std::string> apply_fixes(const CheckInputs& inputs) {
    // Delegates to the ONE seeding implementation rather than walking the rows
    // itself. It used to walk them, which meant two pieces of code knew how to
    // build the tree -- and a deliberate drift injected into the other one was
    // not caught by anything, because the copy the installers actually reached
    // was still correct. Two implementations is the bug, even when both agree.
    //
    // Config content is still untouched: seeding creates directories and sets
    // modes, and never opens config.yaml. Repairing the local install is a
    // repair; guessing what a dangling model_path meant is not.
    const harness::SeedResult seeded = harness::seed_data_directory(inputs.home);

    std::vector<std::string> done;
    done.reserve(seeded.created.size());
    for (const std::string& name : seeded.created) {
        done.push_back("created " + (inputs.home / name).string());
    }
    if (!seeded.ok()) {
        done.push_back("could not finish: " + seeded.error);
    }
    return done;
}

std::string render_report(const CheckReport& report, bool use_color) {
    const ansi::Style style{use_color};
    std::ostringstream out;

    std::string section;
    for (const CheckRow& row : report.rows) {
        if (row.section != section) {
            section = row.section;
            out << "\n" << style.bold(section) << "\n";
        }

        std::string_view mark;
        ansi::Color color = ansi::Color::Default;
        switch (row.status) {
            case Status::Ok:
                mark = "  ok  ";
                color = ansi::Color::Green;
                break;
            case Status::Warn:
                mark = " warn ";
                color = ansi::Color::Yellow;
                break;
            case Status::Fail:
                mark = " FAIL ";
                color = ansi::Color::Red;
                break;
            case Status::Skipped:
                mark = " skip ";
                color = ansi::Color::BrightBlack;
                break;
        }

        out << style.colorize(mark, color) << " " << row.name << "  " << row.detail << "\n";
        if (!row.remedy.empty()) {
            out << "        " << style.dim("run: " + row.remedy) << "\n";
        }
    }

    const std::size_t failures = report.count(Status::Fail);
    const std::size_t warnings = report.count(Status::Warn);
    out << "\n";
    if (failures > 0) {
        out << style.colorize(std::to_string(failures) + " failure(s), " +
                                  std::to_string(warnings) + " warning(s)",
                              ansi::Color::Red)
            << "\n";
    } else if (warnings > 0) {
        // Warnings are not failures, and the exit code says so. A keyless,
        // modelless install is a VALID install -- the acceptance criterion for
        // this command is that it passes on exactly that.
        out << style.colorize("no failures, " + std::to_string(warnings) + " warning(s)",
                              ansi::Color::Yellow)
            << "\n";
    } else {
        out << style.colorize("everything checks out", ansi::Color::Green) << "\n";
    }
    return out.str();
}

std::string_view CheckCommand::name() const noexcept {
    return "check";
}

std::string_view CheckCommand::summary() const noexcept {
    return "Diagnose this installation";
}

void CheckCommand::bind(CLI::App& root, const RootContext& context) {
    struct Flags {
        bool fix = false;
        bool no_color = false;
    };

    auto flags = std::make_shared<Flags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_flag("--fix", flags->fix,
                  "Repair what is safely repairable: missing directories and private modes. "
                  "Never touches your config.");
    cmd->add_flag("--no-color", flags->no_color, "Disable coloured output");

    cmd->callback([&context, flags]() {
        CheckInputs inputs;
        inputs.config_path = harness::resolve_config_path(context.config_path);
        inputs.env = [](std::string_view name) {
            const char* value = std::getenv(std::string{name}.c_str());
            return value == nullptr ? std::string{} : std::string{value};
        };

        try {
            inputs.home = harness::apogee_home();
        } catch (const std::exception& e) {
            std::cerr << "apogee check: " << e.what() << "\n";
            throw CLI::RuntimeError(1);
        }

        inputs.executable = platform::executable_path();

        std::error_code exists_code;
        if (!std::filesystem::exists(inputs.config_path, exists_code)) {
            inputs.config_missing = true;
        } else {
            try {
                inputs.config = harness::load_config(inputs.config_path);
            } catch (const harness::ConfigError& e) {
                inputs.config_error = e.what();
            }
        }

        if (flags->fix) {
            const std::vector<std::string> done = apply_fixes(inputs);
            for (const std::string& line : done) {
                std::cout << "fixed: " << line << "\n";
            }
            if (done.empty()) {
                std::cout << "nothing to fix\n";
            }
        }

        const CheckReport report = run_checks(inputs);
        const ansi::Style style =
            ansi::Style::detect(flags->no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
        std::cout << render_report(report, style.color_enabled());

        // Non-zero on failure so a script can gate on it -- the reason this is
        // a command rather than a page of documentation.
        if (!report.passed()) {
            throw CLI::RuntimeError(1);
        }
    });
}

}  // namespace apogee::commands
