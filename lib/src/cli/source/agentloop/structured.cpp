#include "agentloop/structured.h"

#include <nlohmann/json-schema.hpp>

#include <cctype>
#include <exception>
#include <utility>

namespace apogee::agentloop {
namespace {

/// Collects every violation instead of stopping at the first: the model is
/// told everything it got wrong in the one correction it gets.
class CollectingHandler final : public nlohmann::json_schema::error_handler {
public:
    void error(const nlohmann::json::json_pointer& pointer, const nlohmann::json&,
               const std::string& message) override {
        const std::string where = pointer.to_string();
        errors.push_back((where.empty() ? "/" : where) + ": " + message);
    }

    std::vector<std::string> errors;
};

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

std::string strip_fence(std::string_view text) {
    std::string trimmed = trim(text);
    if (!trimmed.starts_with("```")) {
        return trimmed;
    }
    const std::size_t newline = trimmed.find('\n');
    if (newline != std::string::npos) {
        trimmed = trimmed.substr(newline + 1);
    }
    const std::size_t closing = trimmed.rfind("```");
    if (closing != std::string::npos) {
        trimmed = trimmed.substr(0, closing);
    }
    return trim(trimmed);
}

}  // namespace

ValidationResult validate_schema(const nlohmann::json& schema) {
    ValidationResult result;
    try {
        // The draft-07 metaschema uses `format` itself (`$id` is a uri), so
        // the built-in format checker is needed to read it at all.
        nlohmann::json_schema::json_validator meta(
            nullptr, nlohmann::json_schema::default_string_format_check);
        meta.set_root_schema(nlohmann::json_schema::draft7_schema_builtin);
        CollectingHandler handler;
        (void)meta.validate(schema, handler);
        result.errors = handler.errors;
        // The meta-schema pass catches shape; setting it as a root catches
        // what only compilation sees (a bad `$ref`, an unknown keyword form).
        if (result.errors.empty()) {
            nlohmann::json_schema::json_validator compiled(
                nullptr, nlohmann::json_schema::default_string_format_check);
            compiled.set_root_schema(schema);
        }
    } catch (const std::exception& e) {
        result.errors.push_back(std::string{"schema: "} + e.what());
    }
    result.ok = result.errors.empty();
    return result;
}

ValidationResult validate_against(const nlohmann::json& schema, const nlohmann::json& instance) {
    ValidationResult result;
    try {
        nlohmann::json_schema::json_validator validator(
            nullptr, nlohmann::json_schema::default_string_format_check);
        validator.set_root_schema(schema);
        CollectingHandler handler;
        (void)validator.validate(instance, handler);
        result.errors = std::move(handler.errors);
    } catch (const std::exception& e) {
        result.errors.push_back(std::string{"schema: "} + e.what());
    }
    result.ok = result.errors.empty();
    return result;
}

std::optional<nlohmann::json> extract_json(std::string_view text) {
    const std::string stripped = strip_fence(text);
    nlohmann::json parsed = nlohmann::json::parse(stripped, nullptr, false);
    if (!parsed.is_discarded() && (parsed.is_object() || parsed.is_array())) {
        return parsed;
    }
    // Prose around the object -- "Here is the report: {...}" -- is the
    // common failure; the outermost braces are the answer.
    const std::size_t open = stripped.find_first_of("{[");
    const std::size_t close = stripped.find_last_of("}]");
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return std::nullopt;
    }
    parsed = nlohmann::json::parse(stripped.substr(open, close - open + 1), nullptr, false);
    if (parsed.is_discarded() || !(parsed.is_object() || parsed.is_array())) {
        return std::nullopt;
    }
    return parsed;
}

std::string schema_instruction(const std::vector<std::string>& schema_texts, bool markdown) {
    std::string out = "---\nOUTPUT FORMAT\n";
    if (markdown) {
        out +=
            "Write your response as clean, well-structured GitHub-flavored Markdown -- use "
            "headings, bullet lists, tables, and fenced code blocks where they aid clarity. "
            "Do NOT wrap the whole response in a code fence, and do NOT output raw JSON.\n"
            "The JSON Schema below is a checklist of the information your response should "
            "cover, not a literal output format: translate each field into an appropriate "
            "heading, section, or list item, and omit fields that do not apply. Favor "
            "readability.\n";
    } else {
        out +=
            "Your response MUST be valid JSON conforming to the following JSON Schema. "
            "Output only the JSON object -- no surrounding text or markdown code blocks.\n";
    }
    for (std::size_t i = 0; i < schema_texts.size(); ++i) {
        if (schema_texts.size() > 1) {
            out += "\nSchema " + std::to_string(i + 1) + ":\n";
        } else {
            out += "\n";
        }
        out += schema_texts[i];
    }
    return out;
}

std::string correction_message(const std::vector<std::string>& errors) {
    std::string out =
        "Your previous answer did not conform to the required JSON Schema. The validator "
        "reported:\n";
    for (const std::string& error : errors) {
        out += "- " + error + "\n";
    }
    out +=
        "Reply with ONLY the corrected JSON object -- no prose, no code fence -- satisfying "
        "every requirement above.";
    return out;
}

StructuredResult run_structured(const harness::Harness& harness,
                                std::vector<harness::ChatMessage>& history, const Options& options,
                                Reporter& reporter, const nlohmann::json& schema) {
    Options bound = options;
    bound.response_schema = schema.dump();

    StructuredResult result;
    const auto attempt = [&]() {
        result.run = run(harness, history, bound, reporter);
        result.answer = result.run.answer;
        result.attempts += 1;
        result.json = extract_json(result.answer);
        if (!result.json.has_value()) {
            result.errors = {"/: the answer is not a JSON object"};
            result.conforms = false;
            return;
        }
        ValidationResult validation = validate_against(schema, *result.json);
        result.conforms = validation.ok;
        result.errors = std::move(validation.errors);
    };

    attempt();
    if (result.conforms) {
        return result;
    }
    // One corrective turn, carrying what the validator said. The message is
    // durable history: the model's context must stay coherent, and a caller
    // persisting the conversation should see why the second answer exists.
    history.push_back(harness::ChatMessage::user(correction_message(result.errors)));
    attempt();
    return result;
}

}  // namespace apogee::agentloop
