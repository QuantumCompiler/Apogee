#include "scaffold/symphony.h"

#include <stdexcept>
#include <vector>

#include "contracts/config_edit.h"
#include "symphony/definition.h"

namespace apogee::scaffold {

harness::SymphonySpec starter_symphony(std::string_view name, std::string_view description) {
    harness::SymphonySpec spec;
    spec.name = std::string{name};
    spec.description = std::string{description};
    spec.input.description = "What the symphony is given.";
    harness::SymphonyStage stage;
    stage.name = "answer";
    stage.role = "utility";
    stage.prompt = "{{input}}\n";
    spec.stages.push_back(std::move(stage));
    return spec;
}

SymphonyResult create_symphony(const std::filesystem::path& config_path,
                               const harness::SymphonySpec& spec, bool force) {
    SymphonyResult result;
    result.name = spec.name;
    result.config_path = config_path;
    if (!harness::is_symphony_name(spec.name)) {
        throw std::runtime_error(
            spec.name.empty()
                ? std::string{"a symphony name is required"}
                : "'" + spec.name + "' is not a symphony name (letters, digits, '_' and '-')");
    }

    harness::Config config;
    try {
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        throw std::runtime_error(e.what());
    }
    // Read back exactly as the entry will be: what the parser refuses, a
    // stage naming a backend first among it, is refused here, unwritten.
    harness::SymphonySpec parsed;
    try {
        parsed = harness::parse_symphony_spec(harness::render_symphony_spec(spec),
                                              "symphony '" + spec.name + "'", spec.name,
                                              config.backend_names());
    } catch (const harness::ConfigError& e) {
        throw std::runtime_error(e.what());
    }
    if (const std::vector<std::string> problems = symphony::validate(parsed); !problems.empty()) {
        std::string message = "symphony '" + spec.name + "': " + problems.front();
        if (problems.size() > 1) {
            message += " (and " + std::to_string(problems.size() - 1) + " more)";
        }
        throw std::runtime_error(message);
    }
    result.replaced = config.find_symphony(spec.name) != nullptr;
    try {
        harness::edit_config_file(config_path, [&](std::string_view content) {
            return harness::append_symphony(content, parsed.name, parsed, force);
        });
    } catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    }
    return result;
}

}  // namespace apogee::scaffold
