#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backends/llama_runtime.h"
#include "backends/model_profile.h"
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
/// `InTextToolCalling` is declared **conditionally**, and that is the point:
/// it answers true only for a model whose resolved profile says the family
/// emits native calls. Claiming it unconditionally would advertise a parser
/// for grammars nobody has characterized; refusing it unconditionally is what
/// it used to do, and left gpt-oss's calls printing as the answer.
class LlamaCppProvider final : public harness::LLMProvider,
                               public harness::ModelBehaviorReporting,
                               public harness::TokenCounting,
                               public harness::VisionCapable,
                               public harness::InTextToolCalling,
                               public harness::EmbeddingCapable,
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

    // --- ModelBehaviorReporting ---------------------------------------------

    /// What Apogee knows about this model's family.
    ///
    /// Resolved from the GGUF's own `general.architecture` when the model is
    /// loaded, falling back to the configured name. The zero value means
    /// **uncharacterised**, which every consumer must read as permissive.
    [[nodiscard]] harness::ModelBehavior model_behavior() const override;

    // --- EmbeddingCapable ---------------------------------------------------

    /// In-process embeddings from the loaded GGUF, mean-pooled and
    /// L2-normalised, through a context of their own so the conversation's
    /// warm KV state is never touched. Any GGUF will embed; a model trained
    /// for it embeds well, a chat model embeds with variable quality, and the
    /// config's `default_embedding` is where the user says which.
    [[nodiscard]] std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs,
        const harness::CancellationToken& cancellation) override;

    /// The model's hidden size once loaded, else 0 -- the interface's "known
    /// after the first call". Deliberately does NOT load the model to answer:
    /// this is asked from capability probes, and paging gigabytes off disk to
    /// answer a yes/no question would make every probe the slowest thing in
    /// the session.
    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override;

    // --- InTextToolCalling --------------------------------------------------

    /// True when the resolved profile says this family emits native calls.
    ///
    /// Answered from the profile rather than from the backend type, so it stays
    /// true of the model actually loaded. An unprofiled model answers false:
    /// there is no grammar to parse, and claiming otherwise would make a
    /// surface wait for structured calls that never arrive.
    [[nodiscard]] bool uses_in_text_tool_calls() const noexcept override;

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
        /// Native tool calls parsed out of the stream. The text they were
        /// parsed from is not in `text` -- the gate withheld exactly those
        /// bytes from the display and handed them to the parser, so the two
        /// cannot disagree about what a tool call is.
        std::vector<harness::ToolCall> tool_calls;
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

    /// The resolved profile, and the architecture it was resolved from.
    ///
    /// Cached at load rather than recomputed: it is asked once per turn by the
    /// display and once more by the loop, and the answer cannot change while a
    /// model is resident.
    [[nodiscard]] const ModelProfile* profile() const;

    Options options_;
    std::unique_ptr<LlamaRuntime> runtime_;
    /// `general.architecture` of the loaded model, empty before the first load.
    mutable std::string architecture_;
    mutable const ModelProfile* profile_ = nullptr;
    mutable bool profile_resolved_ = false;
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
