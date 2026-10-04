#include "support/cli_home.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

#include "commands/registry.h"
#include "commands/root.h"

namespace apogee::testing {

CliHome::CliHome(const std::string& config) {
    std::filesystem::create_directories(config_path().parent_path());
    std::ofstream{config_path(), std::ios::binary} << config;
}

std::filesystem::path CliHome::config_path() const {
    return home() / "config" / "config.yaml";
}

std::filesystem::path CliHome::models() const {
    return home() / "models";
}

std::string CliHome::config_text() const {
    std::ifstream in{config_path(), std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

int CliHome::run(const std::vector<std::string>& args, std::string* out) const {
    // This install for this run, whatever another home in the test set.
    const EnvGuard home_guard{"APOGEE_HOME", home().string()};
    const std::ostringstream captured;
    const std::ostringstream errors;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(errors.rdbuf());
    int code = -1;
    try {
        commands::RootCommand command{commands::default_registry()};
        std::vector<std::string> full{"--config", config_path().string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        argv.reserve(full.size() + 1);
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        code = command.run(static_cast<int>(argv.size()), argv.data());
    } catch (...) {
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        throw;
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    *out = captured.str() + errors.str();
    return code;
}

}  // namespace apogee::testing
