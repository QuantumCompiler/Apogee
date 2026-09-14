#include "commands/check.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <system_error>

#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "ansi/ansi.h"
#include "commands/helpers.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "httpserver/admin_auth.h"
#include "models/gguf_inspect.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "secrets/resolve.h"
#include "secrets/store.h"
#include "tools/toolsets.h"
#include "version/version.h"

namespace apogee::commands {
namespace {

/// The environment variable a backend type conventionally reads its key from.
///
/// Deliberately mirrors what each provider's `from_config` says in its
/// missing-key error rather than inventing a second convention: a doctor that
/// names a different variable than the failure message does is worse than one
/// that stays quiet.

void add(CheckReport& report, Status status, std::string section, std::string name,
         std::string detail, std::string remedy = {}) {
    report.rows.push_back(
        {status, std::move(section), std::move(name), std::move(detail), std::move(remedy)});
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

    const secrets::CredentialStore store{secrets::credentials_path(inputs.config_path)};
    const secrets::EnvSnapshot env = secrets::EnvSnapshot::capture(inputs.env);
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
            // A full header read rather than the 4-byte magic check this used
            // to do. Magic alone passes a half-finished download -- and the row
            // then said "model loads", which was a claim four bytes cannot
            // support and which reads exactly like a pass.
            const models::GgufInfo info = models::inspect_gguf(model);
            if (!info.parsed) {
                add(report, Status::Fail, "Config", label, "unreadable GGUF -- " + info.parse_error,
                    "re-download the model to " + model.string());
                continue;
            }
            if (info.is_projector()) {
                // A projector configured as a model_path is a real mistake with
                // an exact fix, so it is a failure rather than a note.
                add(report, Status::Fail, "Config", label,
                    "this is a multimodal projector, not a model",
                    "move it to mmproj_path and set model_path to the model it projects for");
                continue;
            }
            if (info.has_vision_tensors()) {
                // Advisory, never fatal: under the open-model policy a warning
                // tells the user something, and refusing tells them nothing
                // they asked for.
                add(report, Status::Warn, "Config", label,
                    std::string{type} + " -- combined text+vision blob (" +
                        std::to_string(info.tensors - info.text_tensors) + " vision tensors)");
                continue;
            }
            add(report, Status::Ok, "Config", label,
                std::string{type} + " -- GGUF header ok" +
                    (info.architecture.empty() ? "" : " (" + info.architecture + ")"));
            continue;
        }

        if (secrets::takes_api_key(backend.type)) {
            // The ONE chain, so the doctor reports exactly what a build would
            // use -- and only WHERE it came from. NEVER the key itself.
            const secrets::KeyResolution resolution =
                secrets::resolve_api_key(backend, &store, env);
            switch (resolution.source) {
                case secrets::KeySource::Config:
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- API key from config");
                    break;
                case secrets::KeySource::Store:
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- API key from the credential store");
                    break;
                case secrets::KeySource::Environment:
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- API key from " + resolution.variable);
                    break;
                case secrets::KeySource::None: {
                    // A missing key is a WARNING, not a failure: a keyless
                    // install is valid, and the whole point of the
                    // fresh-install criterion is that it passes.
                    const std::string variable =
                        std::string{secrets::conventional_variables(backend.type).front()};
                    add(report, Status::Warn, "Config", label,
                        std::string{type} + " -- no API key found",
                        "apogee auth add " + std::string{type} + "   (or export " + variable +
                            "=..., or set api_key: \"${" + variable + "}\" on this backend)");
                    break;
                }
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

    // Collections: a typo in `retriever:` must never silently mean auto, and a
    // `rerank:` or `backend:` must name something that exists. The validator
    // is the same one the flag uses, so a value cannot pass here and fail
    // there.
    for (const auto& [name, collection] : config.embeddings) {
        const std::string label = "collection: " + name;
        if (!agentloop::valid_retriever(collection.retriever)) {
            add(report, Status::Fail, "Config", label,
                agentloop::retriever_values_message("retriever", collection.retriever),
                "set retriever to lexical, vector, hybrid, or auto");
            continue;
        }
        if (!collection.rerank.empty() && collection.rerank != agentloop::kRerankOff &&
            config.find_backend(collection.rerank) == nullptr) {
            add(report, Status::Fail, "Config", label,
                "rerank names a backend that is not configured: '" + collection.rerank + "'",
                "set rerank to a configured backend, or off");
            continue;
        }
        if (!collection.backend.empty() && config.find_backend(collection.backend) == nullptr) {
            add(report, Status::Fail, "Config", label,
                "backend names a backend that is not configured: '" + collection.backend + "'",
                "set backend to a configured backend, or remove it to use "
                "models.default_embedding");
            continue;
        }
        add(report, Status::Ok, "Config", label,
            collection.retriever.empty() ? "auto" : collection.retriever);
    }
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
        // A full header read, not the 4-byte magic check this used to do. The
        // failure that actually happens is a half-finished download, and that
        // file has perfectly valid magic -- so magic alone reported "valid
        // GGUF header" for exactly the file that cannot be loaded.
        const models::GgufInfo info = models::inspect_gguf(entry.path());
        if (!info.parsed) {
            add(report, Status::Fail, "Models", entry.path().filename().string(),
                "unreadable GGUF -- " + info.parse_error, "re-download the model");
        } else {
            add(report, Status::Ok, "Models", entry.path().filename().string(),
                info.architecture.empty() ? "valid GGUF header"
                                          : info.architecture + ", valid GGUF header");
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

/// Per-install secrets beside the config: the admin token today, the
/// credential store next. Never seeded -- each is created by the command that
/// needs it -- so install parity never sees them; but when one exists, it had
/// better be private.
void check_secrets(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_path.empty()) {
        return;
    }
    const std::filesystem::path token = httpserver::admin_token_path(inputs.config_path);
    const std::string label = "Admin token";
    std::error_code code;
    if (!std::filesystem::exists(token, code)) {
        add(report, Status::Ok, "Secrets", label,
            "not generated yet -- created at the first 'apogee serve'");
        return;
    }
    if (!harness::supports_private_modes()) {
        add(report, Status::Skipped, "Secrets", label,
            "mode check not applicable on this platform (no POSIX file modes)");
        return;
    }
    const std::filesystem::perms mode =
        std::filesystem::status(token, code).permissions() & std::filesystem::perms::mask;
    const bool leaks =
        (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
        std::filesystem::perms::none;
    if (leaks) {
        add(report, Status::Fail, "Secrets", label,
            "is readable by other users, and it is the /v1/admin bearer",
            "chmod 600 '" + token.string() + "'   (or: apogee check --fix)");
        return;
    }
    add(report, Status::Ok, "Secrets", label, "present, private (0600)");
}

/// The credential store, the same way: never seeded, private when present.
void check_credential_store(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_path.empty()) {
        return;
    }
    const std::filesystem::path file = secrets::credentials_path(inputs.config_path);
    const std::string label = "Credential store";
    std::error_code code;
    if (!std::filesystem::exists(file, code)) {
        add(report, Status::Ok, "Secrets", label, "none -- 'apogee auth add' creates it");
        return;
    }
    if (!harness::supports_private_modes()) {
        add(report, Status::Skipped, "Secrets", label,
            "mode check not applicable on this platform (no POSIX file modes)");
        return;
    }
    const std::filesystem::perms mode =
        std::filesystem::status(file, code).permissions() & std::filesystem::perms::mask;
    const bool leaks =
        (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
        std::filesystem::perms::none;
    if (leaks) {
        add(report, Status::Fail, "Secrets", label,
            "is readable by other users, and it holds provider API keys",
            "chmod 600 '" + file.string() + "'   (or: apogee check --fix)");
        return;
    }
    const secrets::CredentialStore store{file};
    const std::size_t slots = store.list().size();
    if (!store.warning().empty()) {
        add(report, Status::Warn, "Secrets", label, store.warning());
        return;
    }
    add(report, Status::Ok, "Secrets", label,
        "present, private (0600), " + std::to_string(slots) + " key(s) stored");
}

/// The native toolsets: the permission keys, the sandbox root, the toolset
/// switches, and whether `git` is there to be spawned. Warnings, never
/// failures -- a keyless, toolless install is valid.
void check_tools(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    const harness::Config& config = inputs.config;
    const std::span<const std::string_view> destructive = tools::destructive_tool_names();
    for (const auto& [tool, level] : config.permissions.levels) {
        const bool known =
            std::find(destructive.begin(), destructive.end(), tool) != destructive.end();
        const bool namespaced = tool.starts_with("mcp__");
        if (!known && !namespaced) {
            std::string names;
            for (const std::string_view name : destructive) {
                names += names.empty() ? "" : ", ";
                names += name;
            }
            add(report, Status::Warn, "Tools", "permissions." + tool,
                "not a native destructive tool (they are: " + names + ")",
                "remove the key, or check its spelling");
            continue;
        }
        add(report, Status::Ok, "Tools", "permissions." + tool,
            std::string{harness::to_string(level)});
    }
    for (const std::string_view tool : destructive) {
        if (!config.permissions.levels.contains(tool)) {
            add(report, Status::Ok, "Tools", "permissions." + std::string{tool},
                "ask (not listed; the default)");
        }
    }

    const std::string root = harness::expand_env(config.tools.fs_root);
    std::error_code code;
    if (root.empty()) {
        add(report, Status::Ok, "Tools", "fs_root", "unset -- the home directory");
    } else if (!std::filesystem::is_directory(root, code)) {
        add(report, Status::Warn, "Tools", "fs_root", root + " is not a directory",
            "set tools.fs_root to an existing directory, or remove it");
    } else {
        add(report, Status::Ok, "Tools", "fs_root", root);
    }

    const std::span<const std::string_view> toolsets = tools::toolset_names();
    for (const std::string& name : config.tools.disabled) {
        if (std::find(toolsets.begin(), toolsets.end(), name) == toolsets.end()) {
            std::string names;
            for (const std::string_view toolset : toolsets) {
                names += names.empty() ? "" : ", ";
                names += toolset;
            }
            add(report, Status::Warn, "Tools", "tools.disabled",
                "'" + name + "' is not a toolset (they are: " + names + ")");
        } else {
            add(report, Status::Ok, "Tools", "tools.disabled", name + " -- switched off");
        }
    }

    if (!config.tools.is_disabled("git")) {
        if (platform::find_on_path("git").empty()) {
            add(report, Status::Warn, "Tools", "git",
                "not found on PATH -- the git tools will fail",
                "install git, or add git to tools.disabled");
        } else {
            add(report, Status::Ok, "Tools", "git", "found on PATH");
        }
    }
}

/// Each enabled MCP server's command: a bare name is looked up on PATH; a
/// path must exist and be executable. `--fix` adds the execute bit and never
/// edits the config -- a dangling entry is the user's to delete.
void check_mcp(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    std::error_code code;
    for (const auto& [name, server] : inputs.config.mcp_servers) {
        const std::string label = "server: " + name;
        if (!server.enabled) {
            add(report, Status::Ok, "MCP", label, "disabled");
            continue;
        }
        if (server.command.empty()) {
            add(report, Status::Warn, "MCP", label, "no command set",
                "set mcp_servers." + name + ".command, or: apogee config delete-mcp-server " +
                    name);
            continue;
        }
        const bool bare = server.command.find('/') == std::string::npos &&
                          server.command.find('\\') == std::string::npos;
        if (bare) {
            if (platform::find_on_path(server.command).empty()) {
                add(report, Status::Fail, "MCP", label,
                    "command not found on PATH: " + server.command,
                    "install it, or: apogee config delete-mcp-server " + name);
            } else {
                add(report, Status::Ok, "MCP", label, server.command + " (on PATH)");
            }
            continue;
        }
        const std::filesystem::path command{server.command};
        if (!std::filesystem::exists(command, code)) {
            add(report, Status::Fail, "MCP", label, "command not found: " + server.command,
                "apogee config delete-mcp-server " + name);
            continue;
        }
        if (harness::supports_private_modes()) {
            const std::filesystem::perms mode =
                std::filesystem::status(command, code).permissions();
            if ((mode & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
                add(report, Status::Warn, "MCP", label, server.command + " is not executable",
                    "chmod +x '" + server.command + "'   (or: apogee check --fix)");
                continue;
            }
        }
        add(report, Status::Ok, "MCP", label, server.command);
    }
}

CheckReport run_checks(const CheckInputs& inputs) {
    CheckReport report;
    check_version(report, inputs);
    check_config(report, inputs);
    check_tools(report, inputs);
    check_mcp(report, inputs);
    check_filesystem(report, inputs);
    check_secrets(report, inputs);
    check_credential_store(report, inputs);
    check_models(report, inputs);
    return report;
}

/// Tightens a secret file's mode. Returns what it did, or nothing.
std::string fix_secret_mode(const std::filesystem::path& file) {
    std::error_code code;
    if (!harness::supports_private_modes() || !std::filesystem::exists(file, code)) {
        return {};
    }
    const std::filesystem::perms mode =
        std::filesystem::status(file, code).permissions() & std::filesystem::perms::mask;
    if ((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
        std::filesystem::perms::none) {
        return {};
    }
    std::filesystem::permissions(
        file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, code);
    if (code) {
        return "could not set the mode of " + file.string() + ": " + code.message();
    }
    return "set " + file.string() + " to 0600";
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
    // A scaffolded server that lost its execute bit is a repair of the same
    // kind: the file is the user's, its mode is the install's.
    if (harness::supports_private_modes() && inputs.config_error.empty() &&
        !inputs.config_missing) {
        for (const auto& [name, server] : inputs.config.mcp_servers) {
            if (!server.enabled || server.command.find('/') == std::string::npos) {
                continue;
            }
            std::error_code code;
            const std::filesystem::path command{server.command};
            if (!std::filesystem::exists(command, code)) {
                continue;
            }
            const std::filesystem::perms mode =
                std::filesystem::status(command, code).permissions();
            if ((mode & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                continue;
            }
            std::filesystem::permissions(command, std::filesystem::perms::owner_exec,
                                         std::filesystem::perm_options::add, code);
            if (!code) {
                done.push_back("made executable " + server.command);
            }
        }
    }
    // A per-install secret left readable by others is a repair too -- the
    // same kind as a private directory's mode, one file down.
    if (!inputs.config_path.empty()) {
        for (const std::filesystem::path& secret :
             {httpserver::admin_token_path(inputs.config_path),
              secrets::credentials_path(inputs.config_path)}) {
            if (const std::string fixed = fix_secret_mode(secret); !fixed.empty()) {
                done.push_back(fixed);
            }
        }
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
