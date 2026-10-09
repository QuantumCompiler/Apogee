#include "support/cli_home.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <sstream>
#include <utility>

#include "cli/registry.h"
#include "cli/root.h"

namespace apogee::testing {

CliHome::CliHome(const std::string& config, std::string file) : file_(std::move(file)) {
    std::filesystem::create_directories(config_path().parent_path());
    std::ofstream{config_path(), std::ios::binary} << config;
}

std::filesystem::path CliHome::config_path() const {
    return home() / "config" / file_;
}

std::filesystem::path CliHome::models() const {
    return home() / "models";
}

std::string CliHome::config_text() const {
    std::ifstream in{config_path(), std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

int CliHome::run(const std::vector<std::string>& args, std::string* out) const {
    std::string err;
    const int code = run(args, out, &err);
    *out += err;
    return code;
}

int CliHome::run(const std::vector<std::string>& args, std::string* out, std::string* err) const {
    return run_with(args, true, out, err);
}

int CliHome::run_default(const std::vector<std::string>& args, std::string* out,
                         std::string* err) const {
    return run_with(args, false, out, err);
}

int CliHome::run_with(const std::vector<std::string>& args, bool named, std::string* out,
                      std::string* err) const {
    // This install for this run, whatever another home in the test set.
    const EnvGuard home_guard{"APOGEE_HOME", home().string()};
    // Unnamed means unnamed: not even by the variable `--config` reads.
    std::optional<EnvUnsetGuard> no_config;
    if (!named) {
        no_config.emplace("APOGEE_CONFIG");
    }
    const std::ostringstream captured;
    const std::ostringstream errors;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(errors.rdbuf());
    int code = -1;
    try {
        commands::RootCommand command{commands::default_registry()};
        std::vector<std::string> full;
        if (named) {
            full = {"--config", config_path().string()};
        }
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
    *out = captured.str();
    *err = errors.str();
    return code;
}

}  // namespace apogee::testing
