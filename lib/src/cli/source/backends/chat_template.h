#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "harness/types.h"

/// Rendering an IR conversation into the flat prompt string a local model
/// expects.
///
/// A cloud API takes a structured message list and applies the model's template
/// server-side. A local model takes **one string**, and getting its framing
/// wrong does not fail loudly -- it produces fluent nonsense, or a model that
/// never stops. Ommi learned this the expensive way: `gemma4` matched a
/// `gemma` substring, got Gemma 2/3's `<start_of_turn>` markers, and emitted
/// token soup, because those markers are not in Gemma 4's vocabulary at all.
///
/// Two consequences shape this file. First, **a template the model ships with
/// always wins** -- the GGUF's own `tokenizer.chat_template` is the model's
/// statement about itself, and llama.cpp applies it for us. Second, the
/// name-matching fallback is deliberately **small and conservative** here: the
/// per-family profile layer (dialects, filters, tool markup) is item 14's, and
/// a generous guesser that ships early is exactly what produced the Ommi bug.
/// An unrecognised model gets ChatML, which is the closest thing to a lingua
/// franca, and the caller is told the fallback was used.
namespace apogee::backends {

/// Which framing was used to render a prompt, for logging and diagnostics.
enum class TemplateKind : std::uint8_t {
    /// The model's own template, applied by llama.cpp.
    Builtin,
    /// Recognised by name from the small built-in registry.
    Named,
    /// Nothing matched; ChatML was used.
    Fallback,
};

[[nodiscard]] std::string_view to_string(TemplateKind kind) noexcept;

/// A rendered prompt plus how it was produced.
struct RenderedPrompt {
    std::string text;
    TemplateKind kind = TemplateKind::Fallback;
    /// The registry entry's name, or empty for builtin/fallback.
    std::string template_name;
};

/// Renders `messages` in ChatML: `<|im_start|>role\ncontent<|im_end|>`.
///
/// Ends with an open assistant header when `add_generation_prompt`, which is
/// what tells the model to answer rather than to continue the transcript.
[[nodiscard]] std::string render_chatml(const std::vector<harness::ChatMessage>& messages,
                                        bool add_generation_prompt);

/// Renders `messages` in Llama 3's header format.
[[nodiscard]] std::string render_llama3(const std::vector<harness::ChatMessage>& messages,
                                        bool add_generation_prompt);

/// Renders `messages` in Mistral's `[INST] … [/INST]` format.
///
/// Mistral has no system role: a leading system message is folded into the
/// first user instruction, which is what its own template does.
[[nodiscard]] std::string render_mistral(const std::vector<harness::ChatMessage>& messages,
                                         bool add_generation_prompt);

/// Picks a renderer by model name and applies it.
///
/// Matching is on the lowercased name and is intentionally narrow -- see the
/// file comment. `model_name` is the config's `model:` or the GGUF filename.
[[nodiscard]] RenderedPrompt render_for_model(std::string_view model_name,
                                              const std::vector<harness::ChatMessage>& messages,
                                              bool add_generation_prompt);

/// The registry entry `model_name` resolves to, or empty when nothing matches.
/// Exposed so the fallback decision is testable on its own.
[[nodiscard]] std::string template_name_for_model(std::string_view model_name);

/// The text of every message flattened with no framing at all.
///
/// Used only for token counting when no other rendering is available; never
/// sent to a model.
[[nodiscard]] std::string flatten_text(const std::vector<harness::ChatMessage>& messages);

}  // namespace apogee::backends
