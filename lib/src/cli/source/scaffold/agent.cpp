#include "scaffold/agent.h"

#include <stdexcept>
#include <system_error>

#include "harness/assets.h"
#include "harness/config_edit.h"
#include "harness/paths.h"

namespace apogee::scaffold {

std::string sanitize_agent_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        out += (c == '.' || c == ':') ? '-' : c;
    }
    for (const char c : out) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            throw std::runtime_error("'" + std::string{name} +
                                     "' is not an agent name (letters, digits, '_' and '-')");
        }
    }
    if (out.empty()) {
        throw std::runtime_error("an agent name is required");
    }
    return out;
}

std::string starter_prompt(std::string_view name, std::string_view description) {
    const std::string role = description.empty() ? "a focused assistant" : std::string{description};
    return "You are " + std::string{name} + " -- " + role +
           ".\n\n"
           "Describe the task this agent performs and exactly how it should respond. This\n"
           "file is the system prompt loaded for \"apogee analyze --agent " +
           std::string{name} +
           "\"; edit it\n"
           "to shape the agent's behavior, then run the agent to try it.\n\n"
           "A report that finds nothing notable is a complete, valid result.\n";
}

std::string starter_schema(std::string_view name) {
    return "{\n"
           "  \"$schema\": \"http://json-schema.org/draft-07/schema#\",\n"
           "  \"title\": \"" +
           std::string{name} +
           " output\",\n"
           "  \"type\": \"object\",\n"
           "  \"additionalProperties\": false,\n"
           "  \"properties\": {\n"
           "    \"summary\": {\n"
           "      \"type\": \"string\",\n"
           "      \"description\": \"A concise summary of the result.\"\n"
           "    },\n"
           "    \"human_summary\": {\n"
           "      \"type\": \"string\",\n"
           "      \"description\": \"The human-readable wrap-up, written last: what a reader "
           "should take away, ready to paste.\"\n"
           "    }\n"
           "  },\n"
           "  \"required\": [\"summary\", \"human_summary\"]\n"
           "}\n";
}

std::vector<std::filesystem::path> agent_files(const std::filesystem::path& config_path,
                                               const harness::AgentConfig& agent) {
    const std::filesystem::path home = harness::home_for_config(config_path);
    std::vector<std::filesystem::path> files;
    for (const std::string& prompt : agent.prompts) {
        files.push_back(harness::resolve_agent_path(home, prompt));
    }
    for (const std::string& schema : agent.schemas) {
        files.push_back(harness::resolve_agent_path(home, schema));
    }
    return files;
}

AgentResult create_agent(const std::filesystem::path& config_path, const AgentSpec& spec) {
    AgentResult result;
    result.name = sanitize_agent_name(spec.name);
    result.config_path = config_path;

    harness::AgentConfig entry;
    entry.description = spec.description;
    entry.model = spec.model;
    if (!spec.tools.empty()) {
        const std::optional<harness::AgentToolPolicy> policy =
            harness::agent_tool_policy_from_string(spec.tools);
        if (!policy.has_value()) {
            throw std::runtime_error("invalid tools '" + spec.tools +
                                     "' (want: read-only | all | none)");
        }
        entry.tools = *policy;
    }
    if (!spec.output_format.empty()) {
        const std::optional<harness::AgentOutputFormat> format =
            harness::agent_output_format_from_string(spec.output_format);
        if (!format.has_value()) {
            throw std::runtime_error("invalid output_format '" + spec.output_format +
                                     "' (want: auto | json | markdown)");
        }
        entry.output_format = *format;
    }
    entry.collection = spec.collection;
    entry.questions = spec.questions;
    entry.save_dir = spec.save_dir;
    entry.save_filename = spec.save_filename;
    entry.save_subdir = spec.save_subdir.empty() ? result.name : spec.save_subdir;
    entry.mcp = spec.mcp;

    // The files, beside the config's data directory, by relative path.
    const std::filesystem::path home = harness::home_for_config(config_path);
    const std::string prompt_relative = harness::bundled_prompt_relative_path(result.name);
    const std::string schema_relative = harness::bundled_schema_relative_path(result.name);
    result.prompt_path = home / prompt_relative;
    std::error_code code;
    if (!spec.force && std::filesystem::exists(result.prompt_path, code)) {
        throw std::runtime_error("prompt file already exists: " + result.prompt_path.string() +
                                 " (use --force to overwrite)");
    }
    if (!spec.no_schema) {
        result.schema_path = home / schema_relative;
        if (!spec.force && std::filesystem::exists(result.schema_path, code)) {
            throw std::runtime_error("schema file already exists: " + result.schema_path.string() +
                                     " (use --force to overwrite)");
        }
    }

    const auto write = [](const std::filesystem::path& path, const std::string& content) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            throw std::runtime_error("could not create " + path.parent_path().string() + ": " +
                                     error.message());
        }
        harness::write_file_atomically(path, content);
    };
    write(result.prompt_path, spec.prompt_body.empty()
                                  ? starter_prompt(result.name, spec.description)
                                  : spec.prompt_body);
    entry.prompts = {prompt_relative};
    if (!spec.no_schema) {
        write(result.schema_path,
              spec.schema_body.empty() ? starter_schema(result.name) : spec.schema_body);
        entry.schemas = {schema_relative};
    }

    try {
        harness::edit_config_file(config_path, [&](std::string_view content) {
            return harness::append_agent(content, result.name, entry, spec.force);
        });
    } catch (const std::exception& e) {
        // The files were written; say so rather than hiding it.
        throw std::runtime_error(std::string{"updating config: "} + e.what() +
                                 " (the prompt was written to " + result.prompt_path.string() +
                                 ")");
    }
    return result;
}

}  // namespace apogee::scaffold
