#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backends/llama_runtime.h"
#include "harness/config.h"
#include "harness/provider.h"

/// Local inference: llama.cpp linked into this process.
///
/// **The architectural payoff over Ommi**, and the reason it is worth the
/// crash-model trade recorded on this item. Ommi spawned a child per turn and
/// carried an entire on-disk prompt-cache apparatus to keep multi-turn chat
/// warm -- cache files, fingerprinting, an M-RoPE replay self-heal, and rules
/// about what must never be written into a cache. All of it existed to move KV
/// state between processes that could not share memory. Linking in-process
/// deletes the whole category: the KV cache is a live `llama_context` that
/// simply stays alive between turns, and the correctness rules it enforced
/// become arithmetic over a token prefix (`llamacpp_tokens.h`).
///
/// The accepted cost, decided 2026-08-31: a fatal error inside llama.cpp takes
/// the process down, where a child could have died alone. That is narrower than
/// it sounds -- a load failure returns a clear error naming the file, so the
/// exposure is an abort during generation -- and `apogee chat` saves after
/// every completed turn, so at most one in-flight turn is lost.
namespace apogee::backends {

/// The three capabilities this backend answers for, alongside LLMProvider.
///
/// It does NOT declare `InTextToolCalling`. A local model does put tool calls
/// in its text, so that is a true fact being deliberately left unstated: the
/// per-family tool dialects live in model-profiles-and-management, and nothing
/// parses in-text calls yet. Claiming the capability now would advertise a
/// parser that does not exist.
class LlamaCppProvider final : public harness::LLMProvider,
                               public harness::TokenCounting,
                               public harness::VisionCapable,
                               public harness::StatusReporting {
public:
    /// Reads the wall clock. Injected so the idle-unload policy is testable
    /// without a test that sleeps.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    struct Options {
        std::string backend_name = "llamacpp";

        /// Display name, and the hint the chat-template registry matches on
        /// when the GGUF ships no template of its own.
        std::string model;

        /// Path to the GGUF. Required.
        std::string model_path;

        /// Path to the multimodal projector, when this entry can read images.
        /// Empty means text-only.
        std::string mmproj_path;

        /// KV context size in tokens. **0 means the model's own training
        /// length**, which is the right default: a fixed number warns on a
        /// model trained shorter and truncates one trained longer.
        std::int64_t context_size = 0;

        /// Layers to offload to the GPU. The default offloads everything,
        /// which is what Metal wants; 0 forces CPU.
        std::int64_t gpu_layers = 999;

        /// Cap on generated tokens when the request does not set one.
        std::int64_t max_tokens = 2048;

        /// Unload the model after this long with no request. Zero never
        /// unloads. A resident 16GB model is the single largest thing this
        /// process holds, so a long-lived `apogee chat` that has moved to a
        /// cloud backend should be able to give it back.
        std::chrono::seconds idle_unload{0};

        Clock clock;
    };

    /// `runtime` is injected for the same reason every other backend injects
    /// its transport: the merge-blocking build has no llama.cpp in it, so a
    /// scripted runtime is the only way these behaviours are tested at all.
    LlamaCppProvider(Options options, std::unique_ptr<LlamaRuntime> runtime);

    /// Builds one over the real llama.cpp, if this build has it.
    /// Throws harness::ProviderError when it does not, or when `model_path` is
    /// unset.
    [[nodiscard]] static std::unique_ptr<LlamaCppProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    // --- TokenCounting ------------------------------------------------------

    /// Exact count from the model's own tokenizer.
    ///
    /// Returns -1 **when the model is not already loaded**, so the caller falls
    /// back to its estimate. Deliberate: a context measurement runs before every
    /// turn, and making one page a multi-gigabyte model off disk would turn a
    /// display detail into the slowest thing in the session. Counts are exact
    /// from the first turn onward, which is when they start mattering.
    [[nodiscard]] std::int64_t count_prompt_tokens(const harness::ChatRequest& request) override;

    // --- VisionCapable ------------------------------------------------------

    /// False, and honestly so: this build wires no mtmd context, so there is
    /// nothing here that could read an image. It answers through the capability
    /// seam rather than being absent, which is what lets every surface refuse
    /// an attachment with one shared message instead of discovering the gap at
    /// inference time.
    ///
    /// Making it a real answer is multimodal-vision's job, and needs two things
    /// this build does not have: llama.cpp's `mtmd` library (which lives under
    /// its `tools/` tree and is excluded by `LLAMA_BUILD_TOOLS OFF`), and an
    /// mmproj file to point at.
    [[nodiscard]] bool accepts_images() const noexcept override;

    // --- StatusReporting ----------------------------------------------------

    [[nodiscard]] harness::StatusEvent model_status() const override;

    /// Whether the model is resident right now. For tests and diagnostics.
    [[nodiscard]] bool model_loaded() const noexcept;

    /// Drops the model and every context. The next request reloads.
    void unload();

private:
    /// Loads the model if needed. Throws ProviderError naming the file when the
    /// load fails.
    void ensure_model(const harness::StatusSink& on_status);

    /// Unloads if the idle window has passed. Called at the top of a request,
    /// so a long gap is noticed even though nothing runs in the background --
    /// a timer thread would need its own cancellation and shutdown story to buy
    /// the same freeing a beat earlier.
    void expire_if_idle();

    [[nodiscard]] harness::ChatResponse run(const harness::ChatRequest& request,
                                            const harness::StreamOptions& options);

    /// One turn's generated output.
    struct Generation {
        std::string text;
        std::vector<std::int32_t> tokens;
        harness::FinishReason finish = harness::FinishReason::Stop;
    };

    /// Samples until end-of-generation, the token cap, or the context wall.
    ///
    /// Shared by the text and image paths. Extracted when vision landed: the
    /// alternative was a second copy of the stop conditions, which would drift
    /// the first time one of them changed.
    [[nodiscard]] Generation generate(LlamaContext& context, std::int64_t prompt_end,
                                      const harness::ChatRequest& request,
                                      const harness::StreamOptions& options);

    /// The turn when the request carries images.
    ///
    /// A separate path because a multimodal prompt is not a token vector: mtmd
    /// produces interleaved text and embedding chunks, so there is nothing to
    /// prefix-match and the KV cache is rebuilt from scratch. That cost is real
    /// and is paid only on turns that actually carry a picture.
    [[nodiscard]] harness::ChatResponse run_multimodal(const harness::ChatRequest& request,
                                                       const harness::StreamOptions& options,
                                                       const std::vector<std::string>& images);

    Options options_;
    std::unique_ptr<LlamaRuntime> runtime_;
    std::unique_ptr<LlamaModel> model_;

    /// The conversation's KV cache, and the tokens known to be in it.
    ///
    /// One context per provider is one context per chat: a process runs a
    /// single conversation, and `/model` switching hands the turn to a
    /// different provider entirely. `session_tokens_` is the reusable prefix,
    /// truncated rather than extended when a request carries transient content.
    std::unique_ptr<LlamaContext> session_;
    std::vector<std::int32_t> session_tokens_;

    std::chrono::steady_clock::time_point last_use_{};
    bool used_ = false;
};

}  // namespace apogee::backends
