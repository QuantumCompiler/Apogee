#include "backends/llamacpp_tokens.h"

#include <algorithm>

#include "backends/chat_template.h"

namespace apogee::backends::llama_tokens {

std::string render_prompt(const LlamaModel& model, std::string_view model_name,
                          const std::vector<harness::ChatMessage>& messages,
                          bool add_generation_prompt) {
    // The model's own template first -- see LlamaModel::apply_builtin_template.
    std::string builtin = model.apply_builtin_template(messages, add_generation_prompt);
    if (!builtin.empty()) {
        return builtin;
    }
    return render_for_model(model_name, messages, add_generation_prompt).text;
}

std::vector<std::int32_t> tokenize_prompt(const LlamaModel& model, std::string_view model_name,
                                          const std::vector<harness::ChatMessage>& messages,
                                          bool add_generation_prompt) {
    const std::string prompt = render_prompt(model, model_name, messages, add_generation_prompt);
    // add_special: the template already writes the model's own turn markers,
    // but BOS is a tokenizer-level concern the template does not cover.
    return model.tokenize(prompt, true);
}

std::int64_t count_prompt_tokens(const LlamaModel& model, std::string_view model_name,
                                 const std::vector<harness::ChatMessage>& messages) {
    // Counted with the generation prompt attached, because that is what an
    // actual request sends -- counting the bare transcript would under-report
    // every turn by the width of the assistant header.
    return static_cast<std::int64_t>(tokenize_prompt(model, model_name, messages, true).size());
}

std::size_t common_prefix_length(const std::vector<std::int32_t>& previous,
                                 const std::vector<std::int32_t>& next) {
    const auto [previous_end, next_end] =
        std::mismatch(previous.begin(), previous.end(), next.begin(), next.end());
    return static_cast<std::size_t>(std::distance(previous.begin(), previous_end));
}

}  // namespace apogee::backends::llama_tokens
