#pragma once

/// The `config.json`s of the sets 53f routes, written inline (2026-10-21):
/// the user's Qwen3-VL, Qwen3-Omni, Gemma 4 and Qwen3.8 releases, and a
/// Llama beside them. Each is sliced to what says what the weights are -- the
/// architecture, the `model_type`, and the towers (`vision_config`,
/// `audio_config`, an omni model's `thinker_config`) with a few of their
/// own keys -- as the publishers' configurations lay them out. Strings only,
/// so every layer's suites can write them.
namespace apogee::testing {

/// Qwen3-VL-8B-Instruct: a vision tower beside its text model.
inline constexpr const char* kQwen3VlConfig =
    R"({"architectures": ["Qwen3VLForConditionalGeneration"], "model_type": "qwen3_vl",
        "image_token_id": 151655, "video_token_id": 151656,
        "text_config": {"model_type": "qwen3_vl_text", "hidden_size": 4096,
                        "max_position_embeddings": 262144},
        "vision_config": {"model_type": "qwen3_vl", "depth": 27, "hidden_size": 1152,
                          "out_hidden_size": 4096}})";

/// Qwen3-Omni-30B-A3B-Instruct: its towers nested in the thinker, a talker
/// and a vocoder beside it -- no `vision_config` at the top, so the files
/// rule of 27c never claims it.
inline constexpr const char* kQwen3OmniConfig =
    R"({"architectures": ["Qwen3OmniMoeForConditionalGeneration"],
        "model_type": "qwen3_omni_moe", "enable_audio_output": true,
        "thinker_config": {
          "model_type": "qwen3_omni_moe_thinker",
          "audio_config": {"model_type": "qwen3_omni_moe_audio_encoder", "d_model": 1280,
                           "encoder_layers": 32},
          "vision_config": {"model_type": "qwen3_omni_moe_vision_encoder", "depth": 27,
                            "hidden_size": 1152},
          "text_config": {"model_type": "qwen3_omni_moe_text", "hidden_size": 2048,
                          "num_experts": 128, "max_position_embeddings": 65536}},
        "talker_config": {"model_type": "qwen3_omni_moe_talker"},
        "code2wav_config": {"model_type": "qwen3_omni_moe_code2wav"}})";

/// Gemma 4's E4B: an audio and a vision tower beside its text model.
inline constexpr const char* kGemma4Config =
    R"({"architectures": ["Gemma4ForConditionalGeneration"], "model_type": "gemma4",
        "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536},
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 768},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 2560,
                        "max_position_embeddings": 131072}})";

/// Gemma 4's 12B unified release.
inline constexpr const char* kGemma4UnifiedConfig =
    R"({"architectures": ["Gemma4UnifiedForConditionalGeneration"],
        "model_type": "gemma4_unified",
        "audio_config": {"model_type": "gemma4_audio", "hidden_size": 1536},
        "vision_config": {"model_type": "gemma4_vision", "hidden_size": 1152},
        "text_config": {"model_type": "gemma4_text", "hidden_size": 3840,
                        "max_position_embeddings": 131072}})";

/// Qwen3.8-27B: Qwen 3.5's architecture, a vision tower beside it.
inline constexpr const char* kQwen35Config =
    R"({"architectures": ["Qwen3_5ForConditionalGeneration"], "model_type": "qwen3_5",
        "text_config": {"model_type": "qwen3_5_text", "hidden_size": 5120,
                        "max_position_embeddings": 262144},
        "vision_config": {"model_type": "qwen3_5", "depth": 27, "hidden_size": 1152}})";

/// Llama-3.2-1B-Instruct: a language model, no tower.
inline constexpr const char* kLlamaConfig =
    R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama", "hidden_size": 2048,
        "max_position_embeddings": 131072, "torch_dtype": "bfloat16"})";

}  // namespace apogee::testing
