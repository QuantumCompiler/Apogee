#include "operations/acp_registration.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <system_error>

namespace apogee::operations {

namespace {

/// `path` made absolute against this process's folder, as a host that starts
/// the agent in another folder needs it.
[[nodiscard]] std::string absolute(const std::string& path) {
    std::error_code code;
    const std::filesystem::path made = std::filesystem::absolute(path, code);
    return code ? path : made.lexically_normal().string();
}

/// The Name Xcode lists: the product, and the channel when it is not the
/// release's.
[[nodiscard]] std::string agent_name(harness::Channel channel) {
    switch (channel) {
        case harness::Channel::Dev:
            return "Apogee (dev)";
        case harness::Channel::Test:
            return "Apogee (test)";
        case harness::Channel::Release:
            break;
    }
    return "Apogee";
}

/// `label`, padded to the fields' column.
[[nodiscard]] std::string field(std::string_view label) {
    std::string out = "  " + std::string{label} + ":";
    while (out.size() < 16) {
        out += ' ';
    }
    return out;
}

}  // namespace

AcpRegistration acp_registration(const AcpRegistrationInputs& inputs) {
    if (inputs.executable.empty()) {
        throw std::runtime_error(
            "this binary's own path is not known, so there is no Executable to register");
    }
    AcpRegistration out;
    out.name = agent_name(inputs.channel);
    out.channel = std::string{harness::channel_name(inputs.channel)};
    out.executable = inputs.executable;

    // The root the fields were printed from, ahead of `acp`: global flags
    // come before the command.
    if (inputs.root_flag.has_value()) {
        if (inputs.root_flag->channel.has_value()) {
            out.arguments.emplace_back(harness::channel_flag(*inputs.root_flag->channel));
        } else {
            out.arguments.emplace_back(harness::kCustomFlag);
            out.arguments.push_back(absolute(inputs.root_flag->custom_config.string()));
        }
    }
    // `--custom` names the config file too (a second one is refused), so
    // `--config` rides only beside a channel flag or none.
    const bool custom = inputs.root_flag.has_value() && !inputs.root_flag->channel.has_value();
    if (!inputs.config_flag.empty() && !custom) {
        out.arguments.emplace_back("--config");
        out.arguments.push_back(absolute(inputs.config_flag));
    }
    out.arguments.emplace_back("acp");
    out.arguments.insert(out.arguments.end(), inputs.acp_flags.begin(), inputs.acp_flags.end());

    if (!inputs.apogee_home.empty()) {
        out.environment.emplace_back(harness::kHomeEnvVar, absolute(inputs.apogee_home));
    }
    if (!inputs.apogee_config.empty()) {
        out.environment.emplace_back("APOGEE_CONFIG", absolute(inputs.apogee_config));
    }
    if (!inputs.path.empty()) {
        out.environment.emplace_back("PATH", inputs.path);
    }
    return out;
}

nlohmann::json acp_registration_document(const AcpRegistration& registration) {
    nlohmann::json environment = nlohmann::json::object();
    for (const auto& [name, value] : registration.environment) {
        environment[name] = value;
    }
    return nlohmann::json{
        {"object", "acp.registration"},
        {"where", registration.where},
        {"name", registration.name},
        {"executable", registration.executable.string()},
        {"interpreter", registration.interpreter.has_value()
                            ? nlohmann::json(*registration.interpreter)
                            : nlohmann::json(nullptr)},
        {"arguments", registration.arguments},
        {"environment", std::move(environment)},
        {"channel", registration.channel},
        {"xcode_minimum", registration.xcode_minimum},
        {"approval", registration.approval},
    };
}

std::string render_acp_registration(const AcpRegistration& registration) {
    const std::string continued(16, ' ');
    std::string out = "Add " + registration.name + " to Xcode " + registration.xcode_minimum +
                      " or later: " + registration.where + "\n";
    out += field("Name") + registration.name + "\n";
    out += field("Executable") + registration.executable.string() + "\n";
    out += field("Interpreter") + registration.interpreter.value_or("(leave empty)") + "\n";
    for (std::size_t i = 0; i < registration.arguments.size(); ++i) {
        out += (i == 0 ? field("Arguments") : continued) + registration.arguments[i] + "\n";
    }
    if (registration.environment.empty()) {
        out += field("Environment") + "(none)\n";
    }
    bool path = false;
    for (std::size_t i = 0; i < registration.environment.size(); ++i) {
        const auto& [name, value] = registration.environment[i];
        out += (i == 0 ? field("Environment") : continued) + name + "=" + value + "\n";
        path = path || name == "PATH";
    }
    out += "Arguments and Environment Variables take one entry a line.";
    if (path) {
        out +=
            " PATH is the one this apogee runs with, so the agent finds the subscription CLIs "
            "on it: print these fields again after it changes.";
    }
    out += "\n" + registration.approval + "\n";
    return out;
}

}  // namespace apogee::operations
