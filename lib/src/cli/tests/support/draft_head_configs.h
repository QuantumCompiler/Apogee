#pragma once

#include <array>

/// The `config.json`s of the user's four Gemma 4 assistants and their bases
/// (53i), written inline from the publishers' public layouts: each draft head
/// sliced to what says what it is -- its architecture, its `model_type`, the
/// width of the base it reads (`backbone_hidden_size`) and its few layers --
/// and each base to its architecture, its `model_type` and its text model's
/// width, the towers beside it as `towered_configs.h` writes them. The 12B's
/// is the draft-heads change's own fixture (2026-10-21), moved here. Strings
/// only, so every layer's suites can write them.
namespace apogee::testing {

/// Gemma4-12B-Assistant: the unified 12B's, four layers 1024 wide reading a
/// 3840-wide base through a projection.
inline constexpr const char* kGemma4Assistant12BConfig =
    R"({"architectures": ["Gemma4UnifiedAssistantForCausalLM"],
        "model_type": "gemma4_unified_assistant", "backbone_hidden_size": 3840,
        "text_config": {"model_type": "gemma4_unified_text", "hidden_size": 1024,
                        "num_hidden_layers": 4}})";

/// Gemma4-26B-Assistant: the 26B mixture's, four layers 1024 wide reading a
/// 2816-wide base, a tied dense head.
inline constexpr const char* kGemma4Assistant26BConfig =
    R"({"architectures": ["Gemma4AssistantForCausalLM"], "model_type": "gemma4_assistant",
        "backbone_hidden_size": 2816, "use_ordered_embeddings": false,
        "text_config": {"model_type": "gemma4_text", "hidden_size": 1024,
                        "num_hidden_layers": 4}})";

/// Gemma4-E2B-Assistant: four layers 256 wide reading a 1536-wide base, its
/// head over ordered embeddings.
inline constexpr const char* kGemma4AssistantE2BConfig =
    R"({"architectures": ["Gemma4AssistantForCausalLM"], "model_type": "gemma4_assistant",
        "backbone_hidden_size": 1536, "use_ordered_embeddings": true, "num_centroids": 2048,
        "text_config": {"model_type": "gemma4_text", "hidden_size": 256,
                        "num_hidden_layers": 4}})";

/// Gemma4-E4B-Assistant: four layers 256 wide reading a 2560-wide base, its
/// head over ordered embeddings -- the MLX community's E4B drafter's source.
inline constexpr const char* kGemma4AssistantE4BConfig =
    R"({"architectures": ["Gemma4AssistantForCausalLM"], "model_type": "gemma4_assistant",
        "backbone_hidden_size": 2560, "use_ordered_embeddings": true, "num_centroids": 2048,
        "text_config": {"model_type": "gemma4_text", "hidden_size": 256,
                        "num_hidden_layers": 4}})";

/// Gemma4-12B-Instruct: the unified 12B, 3840 wide.
inline constexpr const char* kGemma4Base12BConfig =
    R"({"architectures": ["Gemma4UnifiedForConditionalGeneration"],
        "model_type": "gemma4_unified",
        "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536},
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 1152},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 3840,
                        "max_position_embeddings": 131072}})";

/// Gemma4-26B-Instruct: the 26B mixture, 2816 wide, a vision tower beside it.
inline constexpr const char* kGemma4Base26BConfig =
    R"({"architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4",
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 1152},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 2816,
                        "num_experts": 128, "max_position_embeddings": 262144}})";

/// Gemma4-E2B-Instruct: 1536 wide, an audio and a vision tower beside it.
inline constexpr const char* kGemma4BaseE2BConfig =
    R"({"architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4",
        "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536},
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 768},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 1536,
                        "max_position_embeddings": 131072}})";

/// Gemma4-E4B-Instruct: 2560 wide, an audio and a vision tower beside it --
/// `towered_configs.h`'s `kGemma4Config`, the E4B 53f converted.
inline constexpr const char* kGemma4BaseE4BConfig =
    R"({"architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4",
        "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536},
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 768},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 2560,
                        "max_position_embeddings": 131072}})";

/// One assistant and its base, by the names the user stores them under.
struct DraftHeadPair {
    const char* head_model = nullptr;
    const char* head_config = nullptr;
    const char* base_model = nullptr;
    const char* base_config = nullptr;
    /// The head's `model_type`: the drafter type mlx-vlm drafts it as.
    const char* drafter_type = nullptr;
    /// The width of the base it reads.
    int width = 0;
};

/// The user's four, the unified 12B's first.
inline constexpr std::array<DraftHeadPair, 4> kUserDraftHeads{{
    {"Gemma4-12B-Assistant", kGemma4Assistant12BConfig, "Gemma4-12B-Instruct", kGemma4Base12BConfig,
     "gemma4_unified_assistant", 3840},
    {"Gemma4-26B-Assistant", kGemma4Assistant26BConfig, "Gemma4-26B-Instruct", kGemma4Base26BConfig,
     "gemma4_assistant", 2816},
    {"Gemma4-E2B-Assistant", kGemma4AssistantE2BConfig, "Gemma4-E2B-Instruct", kGemma4BaseE2BConfig,
     "gemma4_assistant", 1536},
    {"Gemma4-E4B-Assistant", kGemma4AssistantE4BConfig, "Gemma4-E4B-Instruct", kGemma4BaseE4BConfig,
     "gemma4_assistant", 2560},
}};

}  // namespace apogee::testing
