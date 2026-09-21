#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "backends/llama_runtime.h"

namespace apogee::testing {

/// A scripted llama.cpp — the seam that makes the local backend testable on a
/// build with no llama.cpp in it.
///
/// The merge-blocking target does not compile llama.cpp (its kernels are
/// expensive; it has its own non-blocking CI job), so a test that needed the
/// real thing would not run where it matters. It also would not be *hermetic*:
/// the acceptance criteria here are about how many tokens get decoded and on
/// which context, and answering that against a real model means shipping a GGUF
/// fixture and hoping its tokenizer never changes.
///
/// So this fake tokenizes deterministically — one token per whitespace-separated
/// word, ids assigned in first-seen order — and records **every decode call**.
/// That makes "turn two processed only the new tokens" an exact integer
/// assertion rather than a performance measurement.
class FakeLlamaContext final : public backends::LlamaContext {
public:
    /// Every decode this context has seen, in order.
    std::vector<backends::DecodeRecord> decodes;

    /// Positions currently resident, as a high-water mark after trims.
    std::int64_t resident = 0;

    /// The token `sample()` returns, cycling through this list; when it runs
    /// out, `eog_token` is returned so generation terminates.
    std::vector<std::int32_t> script;
    std::size_t sampled = 0;
    std::int32_t eog_token = -1;

    void decode(const std::vector<std::int32_t>& tokens, std::int64_t position) override {
        if (tokens.empty()) {
            return;
        }
        decodes.push_back({position, static_cast<std::int64_t>(tokens.size())});
        resident = position + static_cast<std::int64_t>(tokens.size());
        evaluated_ += static_cast<std::int64_t>(tokens.size());
    }

    [[nodiscard]] std::int32_t sample() override {
        if (sampled >= script.size()) {
            return eog_token;
        }
        return script[sampled++];
    }

    void trim_to(std::int64_t position) override {
        trims.push_back(position);
        resident = std::min(resident, position);
    }

    [[nodiscard]] std::int64_t eval_count() const noexcept override {
        return evaluated_;
    }

    /// The decode cap. Large by default; a test sets it small to prove the
    /// provider splits a long prompt rather than submitting it whole.
    std::int64_t batch_limit = 1000000;

    [[nodiscard]] std::int64_t max_batch_tokens() const noexcept override {
        return batch_limit;
    }

    /// Positions this context can hold. Settable so a test can prove that
    /// generation stops at the wall instead of decoding into it.
    std::int64_t context_capacity = 1000000;

    [[nodiscard]] std::int64_t capacity() const noexcept override {
        return context_capacity;
    }

    /// Every trim position, in order.
    std::vector<std::int64_t> trims;

    /// Tokens decoded as part of the PROMPT only — the number the KV-reuse
    /// criterion is stated against. Generation feeds one token at a time and
    /// would otherwise swamp the signal.
    [[nodiscard]] std::int64_t prompt_tokens_decoded() const noexcept {
        std::int64_t total = 0;
        for (const backends::DecodeRecord& record : decodes) {
            if (record.count > 1) {
                total += record.count;
            }
        }
        return total;
    }

private:
    std::int64_t evaluated_ = 0;
};

/// Forwards to a context the model keeps alive. See make_context.
class FakeLlamaHandle final : public backends::LlamaContext {
public:
    explicit FakeLlamaHandle(std::shared_ptr<FakeLlamaContext> state) : state_{std::move(state)} {}

    void decode(const std::vector<std::int32_t>& tokens, std::int64_t position) override {
        state_->decode(tokens, position);
    }

    [[nodiscard]] std::int32_t sample() override {
        return state_->sample();
    }

    void trim_to(std::int64_t position) override {
        state_->trim_to(position);
    }

    [[nodiscard]] std::int64_t eval_count() const noexcept override {
        return state_->eval_count();
    }

    [[nodiscard]] std::int64_t max_batch_tokens() const noexcept override {
        return state_->max_batch_tokens();
    }

    [[nodiscard]] std::int64_t capacity() const noexcept override {
        return state_->capacity();
    }

private:
    std::shared_ptr<FakeLlamaContext> state_;
};

class FakeLlamaModel final : public backends::LlamaModel {
public:
    using Handle = FakeLlamaHandle;

    /// Contexts handed out, oldest first, kept alive for inspection after the
    /// provider has dropped them.
    std::vector<std::shared_ptr<FakeLlamaContext>> contexts;

    /// Applied to every context this model creates.
    std::int64_t batch_limit = 1000000;
    std::int64_t context_capacity = 1000000;

    /// What each new context should sample. Applied at creation.
    std::vector<std::int32_t> script;
    std::int32_t eog_token = -1;

    /// When non-empty, `apply_builtin_template` returns this with the message
    /// texts appended — the "model ships its own template" path.
    std::string builtin_template_prefix;

    [[nodiscard]] std::vector<std::int32_t> tokenize(std::string_view text,
                                                     bool add_special) const override {
        (void)add_special;
        std::vector<std::int32_t> tokens;
        std::string word;
        for (const char character : text) {
            if (std::isspace(static_cast<unsigned char>(character)) != 0) {
                if (!word.empty()) {
                    tokens.push_back(id_for(word));
                    word.clear();
                }
                continue;
            }
            word.push_back(character);
        }
        if (!word.empty()) {
            tokens.push_back(id_for(word));
        }
        return tokens;
    }

    [[nodiscard]] std::string token_text(std::int32_t token) const override {
        const auto it = text_.find(token);
        return it == text_.end() ? std::string{} : it->second;
    }

    [[nodiscard]] bool is_eog(std::int32_t token) const noexcept override {
        return token == eog_token;
    }

    [[nodiscard]] std::string apply_builtin_template(
        const std::vector<harness::ChatMessage>& messages,
        bool add_generation_prompt) const override {
        if (builtin_template_prefix.empty()) {
            return {};  // no template shipped: caller falls back to the registry
        }
        std::string out = builtin_template_prefix;
        for (const harness::ChatMessage& message : messages) {
            out += ' ';
            out += message.content.plain_text();
        }
        if (add_generation_prompt) {
            out += " assistant:";
        }
        return out;
    }

    [[nodiscard]] std::int64_t context_length() const noexcept override {
        return 4096;
    }

    /// Width of the vectors the fake produces. Settable so a test can stage a
    /// dimension mismatch between two models.
    std::size_t embedding_width = 4;

    /// How many texts each `embed_batch` call carried, in order -- the
    /// batch-boundary assertion point.
    std::vector<std::size_t> embed_batches;

    /// When set, `embed_batch` fails with this message.
    std::string embed_error;

    [[nodiscard]] std::size_t embedding_dimensions() const noexcept override {
        return embedding_width;
    }

    [[nodiscard]] std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& texts,
                                                              std::string& error) override {
        embed_batches.push_back(texts.size());
        if (!embed_error.empty()) {
            error = embed_error;
            return {};
        }
        // Deterministic from the text, so identical inputs embed identically
        // and different ones do not, without pinning magic numbers.
        std::vector<std::vector<float>> out;
        out.reserve(texts.size());
        for (const std::string& text : texts) {
            std::vector<float> vector(embedding_width, 0.0F);
            std::size_t seed = std::hash<std::string>{}(text);
            for (float& component : vector) {
                seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
                component = static_cast<float>((seed >> 33) % 1000) / 1000.0F;
            }
            out.push_back(std::move(vector));
        }
        return out;
    }

    [[nodiscard]] std::unique_ptr<backends::LlamaContext> make_context(
        std::int64_t context_size) override {
        (void)context_size;
        auto state = std::make_shared<FakeLlamaContext>();
        state->script = script;
        state->eog_token = eog_token;
        state->batch_limit = batch_limit;
        state->context_capacity = context_capacity;
        contexts.push_back(state);
        // The provider owns its contexts and destroys a side request's the
        // moment the call returns -- so the model keeps them ALIVE and hands
        // out a forwarding handle. Recording raw pointers here instead would
        // leave every post-call assertion reading freed memory, which is how
        // the side-request test first "passed" against a dangling context.
        return std::make_unique<Handle>(state);
    }

    /// The id this fake assigns to `word`, so a test can script generation.
    [[nodiscard]] std::int32_t id_for(const std::string& word) const {
        const auto it = ids_.find(word);
        if (it != ids_.end()) {
            return it->second;
        }
        const auto id = static_cast<std::int32_t>(ids_.size() + 1);
        ids_.emplace(word, id);
        text_.emplace(id, word);
        return id;
    }

private:
    mutable std::map<std::string, std::int32_t> ids_;
    mutable std::map<std::int32_t, std::string> text_;
};

class FakeLlamaRuntime final : public backends::LlamaRuntime {
public:
    /// When set, `load` fails with this message. The load-failure criterion.
    std::string load_error;

    /// How many times a model was loaded — the idle-unload assertion.
    int loads = 0;

    /// The most recently loaded model. Non-owning.
    FakeLlamaModel* model = nullptr;

    /// Applied to every model this runtime hands out.
    std::int64_t batch_limit = 1000000;
    std::vector<std::int32_t> script;
    std::int32_t eog_token = -1;
    std::string builtin_template_prefix;

    /// Generation scripted as the exact PIECES a model emits, rather than as
    /// token ids.
    ///
    /// Needed to replay a recorded transcript: a real tokenizer splits framing
    /// across pieces in ways no word-splitting fake reproduces -- gpt-oss emits
    /// `commentary` as `comment` then `ary`, straight through the middle of a
    /// marker -- and a filter tested only on whole markers has not been tested.
    /// Each string becomes one token, so the split is the test's to choose.
    std::vector<std::string> script_text;

    [[nodiscard]] std::unique_ptr<backends::LlamaModel> load(const std::string& path,
                                                             std::int64_t gpu_layers,
                                                             const std::string& mmproj_path,
                                                             std::string& error) override {
        last_mmproj_path = mmproj_path;
        (void)gpu_layers;
        last_path = path;
        if (!load_error.empty()) {
            error = load_error;
            return nullptr;
        }
        ++loads;
        auto loaded = std::make_unique<FakeLlamaModel>();
        loaded->batch_limit = batch_limit;
        loaded->script = script;
        for (const std::string& piece : script_text) {
            loaded->script.push_back(loaded->id_for(piece));
        }
        loaded->eog_token = eog_token;
        loaded->builtin_template_prefix = builtin_template_prefix;
        model = loaded.get();
        return loaded;
    }

    std::string last_path;

    /// The projector the provider asked for, so a test can assert the config
    /// field reaches the runtime rather than being dropped on the way.
    std::string last_mmproj_path;
};

}  // namespace apogee::testing
