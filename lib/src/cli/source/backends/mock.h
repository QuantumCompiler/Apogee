#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "harness/provider.h"

/// A provider that answers from a script, with no network and no model.
///
/// This is what makes every downstream item testable. Without it, testing the
/// agent loop, `apogee complete`, the chat REPL, or the HTTP layer would mean
/// either a live API key in CI or a bespoke fake per test — and a bespoke fake
/// per test is how the fakes drift from the interface they are supposed to
/// stand in for. MockProvider implements the real LLMProvider, so it breaks the
/// day the interface changes, which is exactly when it should.
///
/// It is also a real backend type (`type: mock` in config), so a user can point
/// a config at it and exercise the CLI end to end with nothing installed.
namespace apogee::backends {

/// One scripted turn.
struct MockTurn {
    /// Text the model "produces". Streamed in chunks; returned whole by chat().
    ///
    /// Two placeholders are expanded against the request the turn answers,
    /// so a script can prove what reached the model without a real one:
    /// `{{last_tool_result}}` is the content of the most recent tool result
    /// in the request, `{{system}}` the concatenated system messages; the
    /// `{{last_tool_result:json}}` / `{{system:json}}` forms expand to a
    /// JSON string literal, quotes included, for use inside a JSON answer.
    std::string text;

    /// Tool calls to attach to the response.
    std::vector<harness::ToolCall> tool_calls;

    harness::FinishReason finish_reason = harness::FinishReason::Stop;
    harness::Usage usage;
};

/// Turns from a JSON script: `{"turns": [{"text": "...", "tool_calls":
/// [{"name": "git_diff", "arguments": "{}"}]}, ...]}`. A call's `id` is
/// generated when absent; `arguments` may be a JSON object or a string.
/// Throws std::runtime_error naming what is wrong.
[[nodiscard]] std::vector<MockTurn> parse_mock_script(const nlohmann::json& script);

/// `parse_mock_script` over a file. A `type: mock` entry whose `model_path`
/// names one answers from it -- which is what lets the CLI be driven end to
/// end, tool calls and all, with nothing installed.
[[nodiscard]] std::vector<MockTurn> load_mock_script(const std::filesystem::path& path);

/// Whether a script file carries a top-level `"metered": true`. False for a
/// bare list of turns or an absent key.
[[nodiscard]] bool load_mock_script_metered(const std::filesystem::path& path);

/// `text` with its placeholders expanded against `request`.
[[nodiscard]] std::string expand_mock_text(std::string_view text,
                                           const harness::ChatRequest& request);

class MockProvider final : public harness::LLMProvider, public harness::ModelBehaviorReporting {
public:
    struct Options {
        std::string backend_name = "mock";
        std::string model = "mock-1";

        /// Turns answered in order. After the last one, the provider keeps
        /// replaying it rather than failing — a test that runs one extra turn
        /// should not get a confusing "script exhausted" error.
        std::vector<MockTurn> turns;

        /// How many characters to emit per streamed chunk. Small values are
        /// how framing bugs get caught: a sink that mishandles a token split
        /// across chunks fails here rather than in production.
        std::size_t chunk_size = 8;

        /// Behavior profile to report. The default is the zero value, i.e.
        /// "unknown", which consumers must treat as permissive.
        harness::ModelBehavior behavior;

        /// Called before each turn, so a test can observe exactly what the
        /// harness handed the provider — the assertion point for transient
        /// regions, tool definitions, and routing.
        std::function<void(const harness::ChatRequest&)> on_request;

        /// Whether the mock claims each generation call costs money. Off by
        /// default; a script file sets it with a top-level `"metered": true`,
        /// so the spend policies are testable end to end with nothing
        /// installed and no network.
        bool metered = false;
    };

    explicit MockProvider(Options options);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    /// Costs nothing per call unless the script says otherwise.
    [[nodiscard]] bool generation_is_metered() const noexcept override {
        return options_.metered;
    }

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ModelBehavior model_behavior() const override;

    /// Requests seen so far, in order. The other assertion point.
    [[nodiscard]] const std::vector<harness::ChatRequest>& requests() const noexcept {
        return requests_;
    }

    /// How many turns have been served.
    [[nodiscard]] std::size_t turn_count() const noexcept {
        return turn_index_;
    }

private:
    [[nodiscard]] const MockTurn& next_turn();
    [[nodiscard]] harness::ChatResponse build_response(const MockTurn& turn,
                                                       const std::string& text) const;
    void record(const harness::ChatRequest& request);

    Options options_;
    std::vector<harness::ChatRequest> requests_;
    std::size_t turn_index_ = 0;
};

/// A mock that can also embed, for the RAG items and the capability probe.
///
/// Vectors are derived deterministically from the input text, so a test can
/// assert that the same text embeds the same way and different text does not,
/// without pinning magic numbers.
class MockEmbeddingProvider final : public harness::LLMProvider, public harness::EmbeddingCapable {
public:
    explicit MockEmbeddingProvider(std::string backend_name, std::size_t dimensions = 8);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    /// Costs nothing per call.
    [[nodiscard]] bool generation_is_metered() const noexcept override {
        return false;
    }

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] std::vector<std::vector<float>> embed(
        const std::vector<std::string>& inputs,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override;

    /// Its own name, so a store built with one mock embedder is distinct from
    /// one built with another -- which is what a mismatch test needs.
    [[nodiscard]] std::string embedding_model_name() const override {
        return model_name_;
    }

    /// Free unless a test says otherwise -- the spend rules are testable
    /// against a paid embedder with nothing installed.
    [[nodiscard]] bool embedding_is_metered() const noexcept override {
        return metered_;
    }

    void set_model_name(std::string name) {
        model_name_ = std::move(name);
    }

    void set_metered(bool metered) noexcept {
        metered_ = metered;
    }

private:
    std::string backend_name_;
    std::size_t dimensions_;
    std::string model_name_ = "mock-embed";
    bool metered_ = false;
};

}  // namespace apogee::backends
