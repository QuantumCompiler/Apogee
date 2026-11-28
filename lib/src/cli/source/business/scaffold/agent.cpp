#include "scaffold/agent.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#include "contracts/assets.h"
#include "contracts/collection_name.h"
#include "contracts/config_edit.h"
#include "contracts/paths.h"

namespace apogee::scaffold {
namespace {

/// A refusal's words about `mapped`, its first mention naming the typed form
/// beside it when the sanitizer changed the name (`'.x' (as '-x')`), so the
/// person sees what they typed and what was refused (CLI-M52).
std::string naming_both(std::string words, std::string_view typed, std::string_view mapped) {
    const std::string quoted = "'" + std::string{mapped} + "'";
    if (const std::size_t at = words.find(quoted); typed != mapped && at != std::string::npos) {
        words.replace(at, quoted.size(), "'" + std::string{typed} + "' (as " + quoted + ")");
    }
    return words;
}

/// The config's text as the locked edit will read it -- the file a link
/// leads to, parsed as the edit parses it first -- for the dry run of its
/// add (CLI-M52, after `models pull`'s check_registrations): a missing or
/// broken config is refused here, before any file. A read-only question
/// before the lock (57f); only `edit_config_file` writes.
std::string config_text_as_edited(const std::filesystem::path& config_path) {
    std::string text = harness::read_config_file(harness::config_edit_target(config_path));
    (void)harness::parse_config(text, config_path.string());
    return text;
}

}  // namespace

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

AgentResult create_agent(const std::filesystem::path& config_path, const AgentSpec& spec,
                         std::chrono::milliseconds lock_wait) {
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
    // Read by every run of the agent, so a collection is named, never pathed
    // (CLI-M45): refused here, before any file, for the CLI verb and the
    // admin routes alike.
    if (!spec.collection.empty()) {
        if (const std::string refused = harness::collection_read_error(spec.collection);
            !refused.empty()) {
            throw std::runtime_error(refused);
        }
    }
    entry.collection = spec.collection;
    entry.questions = spec.questions;
    entry.save_dir = spec.save_dir;
    entry.save_filename = spec.save_filename;
    entry.save_subdir = spec.save_subdir.empty() ? result.name : spec.save_subdir;
    entry.mcp = spec.mcp;

    // The files, beside the config's data directory, by relative path --
    // named in the entry as they will be written, so the dry run adds
    // exactly the entry the locked edit adds.
    const std::filesystem::path home = harness::home_for_config(config_path);
    const std::string prompt_relative = harness::bundled_prompt_relative_path(result.name);
    const std::string schema_relative = harness::bundled_schema_relative_path(result.name);
    entry.prompts = {prompt_relative};
    if (!spec.no_schema) {
        entry.schemas = {schema_relative};
    }

    // A refusal the config is certain to give writes nothing (CLI-M52): the
    // config read as the edit reads it, the cap on a name the add would not
    // replace, then the one add run on the text and kept nowhere -- the name
    // rule, a case-folded collision, an existing entry, a layout the editor
    // refuses.
    const std::string text = config_text_as_edited(config_path);
    try {
        const std::vector<std::string> entries = harness::section_entry_names(text, "agents");
        const bool replacing = std::ranges::find(entries, result.name) != entries.end() ||
                               harness::fold_collision(entries, result.name).has_value();
        if (!replacing && result.name.size() > kNewAgentNameMax) {
            throw std::runtime_error(
                "agent name '" + result.name + "' is " + std::to_string(result.name.size()) +
                " bytes -- a new name is at most " + std::to_string(kNewAgentNameMax));
        }
        (void)harness::parse_edited_config(
            harness::append_agent(text, result.name, entry, spec.force));
    } catch (const std::exception& e) {
        throw std::runtime_error(naming_both(e.what(), spec.name, result.name));
    }

    // After the dry run, so a seeded or edited prompt with no entry is never
    // overwritten without force (Milestone X), and a case variant is told of
    // the collision before the file check speaks for the other agent's file.
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
    // From the first write on, a failure -- the schema's write, a lock held
    // past the wait, an edit raced in between -- leaves what was written, and
    // says every file.
    bool prompt_written = false;
    bool schema_written = false;
    bool editing = false;
    try {
        write(result.prompt_path, spec.prompt_body.empty()
                                      ? starter_prompt(result.name, spec.description)
                                      : spec.prompt_body);
        prompt_written = true;
        if (!spec.no_schema) {
            write(result.schema_path,
                  spec.schema_body.empty() ? starter_schema(result.name) : spec.schema_body);
            schema_written = true;
        }
        editing = true;
        harness::edit_config_file(
            config_path,
            [&](std::string_view content) {
                return harness::append_agent(content, result.name, entry, spec.force);
            },
            lock_wait);
    } catch (const std::exception& e) {
        if (!prompt_written) {
            throw;
        }
        std::string left = " (the prompt was written to " + result.prompt_path.string();
        if (schema_written) {
            left += ", and the schema to " + result.schema_path.string();
        }
        throw std::runtime_error((editing ? std::string{"updating config: "} : std::string{}) +
                                 e.what() + left + "; run it again with --force to replace " +
                                 (schema_written ? "them" : "it") + ")");
    }
    return result;
}

}  // namespace apogee::scaffold
