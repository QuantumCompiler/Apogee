#include "cli/code_exec_server.h"

#include <system_error>

#include "contracts/assets.h"
#include "platform/platform.h"
#include "training/python_env.h"

namespace apogee::commands {
namespace {

/// `word` as a POSIX shell reads it back unchanged: bare when it is only
/// characters a shell leaves alone, else single-quoted.
[[nodiscard]] std::string shell_word(const std::string& word) {
    const bool plain = !word.empty() && word.find_first_not_of(
                                            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                                            "0123456789_./~:@%+=,-") == std::string::npos;
    if (plain) {
        return word;
    }
    std::string out = "'";
    for (const char c : word) {
        out += c == '\'' ? std::string{"'\\''"} : std::string(1, c);
    }
    return out + "'";
}

/// Two paths name the same file: compared as written, then as resolved.
[[nodiscard]] bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.lexically_normal() == b.lexically_normal()) {
        return true;
    }
    std::error_code code;
    return std::filesystem::equivalent(a, b, code) && !code;
}

}  // namespace

std::vector<std::string> CodeExecRegistration::mcp_create_arguments(std::string_view name) const {
    if (interpreter.empty()) {
        return {};
    }
    return {"mcp",    "create",       std::string{name}, "--command", interpreter.string(),
            "--args", script.string()};
}

std::string CodeExecRegistration::mcp_create_line(std::string_view name) const {
    std::string line = "apogee";
    for (const std::string& argument : mcp_create_arguments(name)) {
        line += " " + shell_word(argument);
    }
    return line;
}

CodeExecRegistration code_exec_registration(
    const std::filesystem::path& home,
    const std::function<std::string(std::string_view)>& find_on_path) {
    CodeExecRegistration out;
    out.script = home / harness::bundled_code_exec_server_relative_path();
    const training::PythonEnv env{home / "training" / "venv"};
    std::error_code code;
    if (std::filesystem::exists(env.interpreter(), code)) {
        out.interpreter = env.interpreter();
        out.interpreter_source = "the training environment";
        return out;
    }
    const std::string found =
        find_on_path ? find_on_path("python3") : platform::find_on_path("python3");
    if (!found.empty()) {
        out.interpreter = found;
        out.interpreter_source = "python3 on PATH";
        return out;
    }
    out.missing =
        "no Python to run it: no training environment ('apogee train setup') and no python3 on "
        "PATH";
    return out;
}

std::string registered_code_exec_server(const harness::Config& config,
                                        const std::filesystem::path& script) {
    for (const auto& [name, server] : config.mcp_servers) {
        if (same_file(harness::expand_env_and_home(server.command), script)) {
            return name;
        }
        for (const std::string& argument : server.args) {
            if (same_file(harness::expand_env_and_home(argument), script)) {
                return name;
            }
        }
    }
    return {};
}

}  // namespace apogee::commands
