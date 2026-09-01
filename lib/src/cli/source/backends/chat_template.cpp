#include "backends/chat_template.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace apogee::backends {
namespace {

/// The role word a template writes for an IR role.
///
/// A tool result renders as a `user` turn on every local template here. None of
/// them has a tool role, and presenting the result as the assistant's own words
/// would let the model treat a tool's output as something it already said.
[[nodiscard]] std::string_view role_word(harness::Role role) noexcept {
    switch (role) {
        case harness::Role::System:
            return "system";
        case harness::Role::Assistant:
            return "assistant";
        case harness::Role::User:
        case harness::Role::Tool:
            break;
    }
    return "user";
}

/// A message's text as a local model should see it.
///
/// Image parts are dropped by `plain_text()`. That is the right degradation
/// here rather than an error: this backend declares itself vision-incapable, so
/// a surface refuses the attachment long before rendering -- and if one ever
/// reaches here, a prompt missing an image beats a crash.
[[nodiscard]] std::string message_text(const harness::ChatMessage& message) {
    std::string text = message.content.plain_text();
    if (message.role == harness::Role::Tool && !message.name.empty()) {
        // Name the tool, or the model sees an unattributed block of output and
        // has to guess which of several calls produced it.
        return "Result of " + message.name + ":\n" + text;
    }
    return text;
}

[[nodiscard]] std::string lowercased(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char character : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return out;
}

struct Rule {
    std::string_view needle;
    std::string_view name;
};

/// Deliberately short. Every entry is a family whose framing is materially
/// different from ChatML, and each is a claim we can defend; the broad
/// per-family layer is item 14's. Order matters -- the first match wins, so a
/// more specific needle must precede a prefix of itself.
constexpr std::array<Rule, 6> kRules{{
    {"llama-3", "llama3"},
    {"llama3", "llama3"},
    {"mistral", "mistral"},
    {"mixtral", "mistral"},
    {"qwen", "chatml"},
    {"hermes", "chatml"},
}};

}  // namespace

std::string_view to_string(TemplateKind kind) noexcept {
    switch (kind) {
        case TemplateKind::Builtin:
            return "builtin";
        case TemplateKind::Named:
            return "named";
        case TemplateKind::Fallback:
            break;
    }
    return "fallback";
}

std::string render_chatml(const std::vector<harness::ChatMessage>& messages,
                          bool add_generation_prompt) {
    std::string out;
    for (const harness::ChatMessage& message : messages) {
        out += "<|im_start|>";
        out += role_word(message.role);
        out += '\n';
        out += message_text(message);
        out += "<|im_end|>\n";
    }
    if (add_generation_prompt) {
        out += "<|im_start|>assistant\n";
    }
    return out;
}

std::string render_llama3(const std::vector<harness::ChatMessage>& messages,
                          bool add_generation_prompt) {
    std::string out = "<|begin_of_text|>";
    for (const harness::ChatMessage& message : messages) {
        out += "<|start_header_id|>";
        out += role_word(message.role);
        out += "<|end_header_id|>\n\n";
        out += message_text(message);
        out += "<|eot_id|>";
    }
    if (add_generation_prompt) {
        out += "<|start_header_id|>assistant<|end_header_id|>\n\n";
    }
    return out;
}

std::string render_mistral(const std::vector<harness::ChatMessage>& messages,
                           bool add_generation_prompt) {
    // Mistral has no system role. Its own template folds a system message into
    // the first instruction, so that is what we do -- dropping it would
    // silently discard the user's system prompt, which is worse than any
    // framing imperfection.
    std::string pending_system;
    std::string out = "<s>";

    for (const harness::ChatMessage& message : messages) {
        const std::string text = message_text(message);
        switch (message.role) {
            case harness::Role::System:
                pending_system = pending_system.empty() ? text : pending_system + "\n\n" + text;
                break;

            case harness::Role::Assistant:
                out += ' ';
                out += text;
                out += "</s>";
                break;

            case harness::Role::User:
            case harness::Role::Tool: {
                out += "[INST] ";
                if (!pending_system.empty()) {
                    out += pending_system;
                    out += "\n\n";
                    pending_system.clear();
                }
                out += text;
                out += " [/INST]";
                break;
            }
        }
    }

    // A conversation that is nothing but a system prompt still has to say it.
    if (!pending_system.empty()) {
        out += "[INST] ";
        out += pending_system;
        out += " [/INST]";
    }
    (void)add_generation_prompt;  // `[/INST]` already opens the model's turn.
    return out;
}

std::string template_name_for_model(std::string_view model_name) {
    const std::string haystack = lowercased(model_name);
    for (const Rule& rule : kRules) {
        if (haystack.find(rule.needle) != std::string::npos) {
            return std::string{rule.name};
        }
    }
    return {};
}

RenderedPrompt render_for_model(std::string_view model_name,
                                const std::vector<harness::ChatMessage>& messages,
                                bool add_generation_prompt) {
    RenderedPrompt rendered;
    rendered.template_name = template_name_for_model(model_name);

    if (rendered.template_name == "llama3") {
        rendered.kind = TemplateKind::Named;
        rendered.text = render_llama3(messages, add_generation_prompt);
        return rendered;
    }
    if (rendered.template_name == "mistral") {
        rendered.kind = TemplateKind::Named;
        rendered.text = render_mistral(messages, add_generation_prompt);
        return rendered;
    }
    if (rendered.template_name == "chatml") {
        rendered.kind = TemplateKind::Named;
        rendered.text = render_chatml(messages, add_generation_prompt);
        return rendered;
    }

    rendered.kind = TemplateKind::Fallback;
    rendered.template_name.clear();
    rendered.text = render_chatml(messages, add_generation_prompt);
    return rendered;
}

std::string flatten_text(const std::vector<harness::ChatMessage>& messages) {
    std::string out;
    for (const harness::ChatMessage& message : messages) {
        out += message_text(message);
        out += '\n';
    }
    return out;
}

}  // namespace apogee::backends
