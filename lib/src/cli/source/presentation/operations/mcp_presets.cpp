#include "operations/mcp_presets.h"

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>
#include <system_error>

#include "operations/code_exec_server.h"
#include "platform/platform.h"

namespace apogee::operations {
namespace {

constexpr std::string_view kCodeExec = "code-exec";
constexpr std::string_view kSelf = "self";

constexpr std::array<McpPreset, 2> kPresets{{
    McpPreset{
        .name = kCodeExec,
        .description = "Runs Python for any model, local or cloud: a working directory kept for "
                       "the session and deleted after it, the network off, each run bounded -- "
                       "Apogee's code-execution server",
        .provides = "run_python, write_input",
        .server_name = kCodeExecServerName,
        .try_tool = "run_python",
        .try_arguments = R"j({"code": "print(6 * 7)"})j",
        .refusal = "",
    },
    McpPreset{
        .name = kSelf,
        .description = "Apogee's own read-only tools, served on stdio by apogee mcp serve for "
                       "another MCP host (a desktop assistant, an editor)",
        .provides = "the read-only native tools: files, git, notes, document search, graph "
                    "navigation; and the status reads: models_list, models_status, train_status, "
                    "train_versions, check",
        .server_name = "apogee",
        .try_tool = "list_notes",
        .try_arguments = "{}",
        .refusal = "'self' is Apogee's own tools, served for another MCP host: registered into "
                   "Apogee it would serve Apogee the tools it already has natively. Give "
                   "another host its entry instead (apogee mcp presets)",
    },
}};

[[nodiscard]] std::string names_listed() {
    std::string out;
    for (const McpPreset& preset : kPresets) {
        out += (out.empty() ? "" : ", ") + std::string{preset.name};
    }
    return out;
}

}  // namespace

std::span<const McpPreset> mcp_presets() noexcept {
    return kPresets;
}

const McpPreset* find_mcp_preset(std::string_view name) noexcept {
    for (const McpPreset& preset : kPresets) {
        if (preset.name == name) {
            return &preset;
        }
    }
    return nullptr;
}

std::vector<std::string_view> mcp_preset_names() {
    std::vector<std::string_view> names;
    for (const McpPreset& preset : kPresets) {
        names.push_back(preset.name);
    }
    return names;
}

McpPresetHost mcp_preset_host(const std::filesystem::path& config_path) {
    McpPresetHost host;
    host.home = config_path.parent_path().parent_path();
    host.executable = platform::executable_path();
    return host;
}

McpPresetResolution resolve_mcp_preset(const McpPreset& preset, const McpPresetHost& host) {
    McpPresetResolution out;
    if (preset.name == kSelf) {
        if (host.executable.empty()) {
            out.missing = "this binary's own path is not known";
            return out;
        }
        out.command = host.executable.string();
        // The public spelling (54a); `__mcp-tools` stays its alias for hosts
        // registered with it before.
        out.args = {"mcp", "serve"};
        return out;
    }
    // code-exec: the seeded script under the interpreter it would run under.
    const CodeExecRegistration registration = code_exec_registration(host.home, host.find_on_path);
    out.command = registration.interpreter.string();
    out.args = {registration.script.string()};
    std::error_code code;
    if (!std::filesystem::exists(registration.script, code)) {
        out.missing = registration.script.filename().string() + " is not seeded under " +
                      registration.script.parent_path().string();
        out.remedy = "apogee check --fix";
        return out;
    }
    if (registration.interpreter.empty()) {
        out.missing = registration.missing;
        out.remedy = "apogee train setup";
    }
    return out;
}

McpPresetResolution resolve_preset_for_create(std::string_view name, const McpPresetHost& host) {
    const McpPreset* preset = find_mcp_preset(name);
    if (preset == nullptr) {
        throw std::runtime_error("no preset named '" + std::string{name} + "' (the presets: " +
                                 names_listed() + " -- see: apogee mcp presets)");
    }
    if (!preset->refusal.empty()) {
        throw std::runtime_error(std::string{preset->refusal});
    }
    McpPresetResolution resolution = resolve_mcp_preset(*preset, host);
    if (!resolution.runnable()) {
        throw std::runtime_error(
            "the '" + std::string{preset->name} +
            "' preset cannot run here: " + resolution.missing +
            (resolution.remedy.empty() ? std::string{} : " -- " + resolution.remedy));
    }
    return resolution;
}

nlohmann::json mcp_host_entry(const McpPresetResolution& resolution) {
    return nlohmann::json{{"command", resolution.command}, {"args", resolution.args}};
}

nlohmann::json mcp_presets_document(const McpPresetHost& host) {
    nlohmann::json data = nlohmann::json::array();
    for (const McpPreset& preset : kPresets) {
        const McpPresetResolution resolution = resolve_mcp_preset(preset, host);
        nlohmann::json row{
            {"name", preset.name},
            {"description", preset.description},
            {"provides", preset.provides},
            {"registers_into_apogee", preset.refusal.empty()},
            {"command", resolution.command},
            {"args", resolution.args},
            {"runnable", resolution.runnable()},
        };
        if (!resolution.runnable()) {
            row["missing"] = resolution.missing;
            if (!resolution.remedy.empty()) {
                row["remedy"] = resolution.remedy;
            }
        }
        if (preset.refusal.empty()) {
            row["register"] = "apogee mcp create " + std::string{preset.server_name} + " --from " +
                              std::string{preset.name};
        } else {
            row["refusal"] = preset.refusal;
        }
        row["host_entry"] =
            nlohmann::json{{"name", preset.server_name}, {"server", mcp_host_entry(resolution)}};
        data.push_back(std::move(row));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}};
}

}  // namespace apogee::operations
