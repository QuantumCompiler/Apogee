#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "backends/llama_runtime.h"

namespace apogee::testing {

/// A word-level stand-in for llama.cpp's template reader: `<think>...</think>`
/// is reasoning, `<tool_call>{"name":...,"arguments":{...}}</tool_call>` is a
/// call, everything else is content. Streaming (`partial`), an unclosed span
/// is held back; finished, one is a format mismatch -- the two answers the
/// real reader gives.
class FakeReplyReader final : public backends::ReplyReader {
public:
    [[nodiscard]] bool read(std::string_view text, bool partial, backends::ParsedReply& out,
                            std::string& error) const override {
        out = {};
        constexpr std::string_view kThink = "<think>";
        constexpr std::string_view kThinkEnd = "</think>";
        constexpr std::string_view kCall = "<tool_call>";
        constexpr std::string_view kCallEnd = "</tool_call>";
        std::string_view rest = text;
        while (!rest.empty()) {
            const std::size_t think = rest.find(kThink);
            const std::size_t call = rest.find(kCall);
            const std::size_t next = std::min(think, call);
            out.content += std::string{rest.substr(0, next)};
            if (next == std::string_view::npos) {
                break;
            }
            if (next == think) {
                rest.remove_prefix(think + kThink.size());
                const std::size_t close = rest.find(kThinkEnd);
                if (close == std::string_view::npos) {
                    if (!partial) {
                        error = "an unclosed think block";
                        return false;
                    }
                    out.reasoning += std::string{rest};
                    break;
                }
                out.reasoning += std::string{rest.substr(0, close)};
                rest.remove_prefix(close + kThinkEnd.size());
                continue;
            }
            rest.remove_prefix(call + kCall.size());
            const std::size_t close = rest.find(kCallEnd);
            if (close == std::string_view::npos) {
                if (!partial) {
                    error = "an unclosed tool call";
                    return false;
                }
                break;  // held: nothing of it is content
            }
            const nlohmann::json body =
                nlohmann::json::parse(rest.substr(0, close), nullptr, false);
            if (body.is_discarded() || !body.is_object() || !body.contains("name")) {
                error = "a malformed tool call";
                return false;
            }
            harness::ToolCall parsed;
            parsed.name = body.value("name", std::string{});
            parsed.arguments = body.contains("arguments") ? body["arguments"].dump() : "{}";
            out.tool_calls.push_back(std::move(parsed));
            rest.remove_prefix(close + kCallEnd.size());
        }
        return true;
    }
};

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

    /// Every grammar set, in order -- including the empty ones that clear it.
    std::vector<backends::SamplingGrammar> grammars;
    /// When set, a non-empty grammar is refused with this.
    std::string grammar_error;

    [[nodiscard]] bool set_grammar(const backends::SamplingGrammar& grammar,
                                   std::string& error) override {
        grammars.push_back(grammar);
        if (!grammar.gbnf.empty() && !grammar_error.empty()) {
            error = grammar_error;
            return false;
        }
        return true;
    }

    /// False plays a recurrent or hybrid model: a trim that would cut cached
    /// positions is refused and the cache cleared, as the real context does.
    bool rewindable = true;

    [[nodiscard]] std::int64_t trim_to(std::int64_t position) override {
        trims.push_back(position);
        if (!rewindable && position < resident) {
            resident = 0;
            return 0;
        }
        resident = std::min(resident, position);
        return position;
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

    [[nodiscard]] bool set_grammar(const backends::SamplingGrammar& grammar,
                                   std::string& error) override {
        return state_->set_grammar(grammar, error);
    }

    [[nodiscard]] std::int64_t trim_to(std::int64_t position) override {
        return state_->trim_to(position);
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
    bool rewindable = true;

    /// What each new context should sample. Applied at creation.
    std::vector<std::int32_t> script;
    std::int32_t eog_token = -1;

    /// When non-empty, `apply_builtin_template` returns this with the message
    /// texts appended — the "model ships its own template" path.
    std::string builtin_template_prefix;

    /// Whether `render_chat` renders -- the model ships a template llama.cpp's
    /// chat layer can read. Off, it answers as a GGUF with no template does.
    bool chat_template = false;
    /// When set, `render_chat` fails with it: a template that cannot render.
    std::string chat_template_error;
    /// Words `token_text` renders as nothing, the way a special token is --
    /// `special_token_text` still renders them.
    std::set<std::string> special_words;
    /// The stop strings a rendering carries.
    std::vector<std::string> stops;
    /// A grammar rendering fails to compile with this, on every context.
    std::string grammar_error;

    /// What each `render_chat` call was given, in order.
    struct ChatRender {
        std::vector<harness::ChatMessage> messages;
        std::vector<harness::Tool> tools;
        bool enable_thinking = true;
    };

    mutable std::vector<ChatRender> chat_renders;

    /// Every text tokenized, in order: what the provider actually sent.
    mutable std::vector<std::string> tokenized;

    /// The window each context was asked for, in creation order.
    std::vector<std::int64_t> context_sizes;

    [[nodiscard]] std::vector<std::int32_t> tokenize(std::string_view text,
                                                     bool add_special) const override {
        (void)add_special;
        tokenized.emplace_back(text);
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
        if (it == text_.end() || special_words.contains(it->second)) {
            return {};
        }
        return it->second;
    }

    [[nodiscard]] std::string special_token_text(std::int32_t token) const override {
        const auto it = text_.find(token);
        return it == text_.end() ? std::string{} : it->second;
    }

    /// The word-level stand-in for the model's own template: every role, call
    /// and result on the prompt as words, the tools named, and the thinking
    /// switch visible -- so a test can read what the model was shown.
    [[nodiscard]] bool render_chat(const std::vector<harness::ChatMessage>& messages,
                                   const std::vector<harness::Tool>& tools, bool enable_thinking,
                                   backends::ChatRendering& out,
                                   std::string& error) const override {
        chat_renders.push_back({messages, tools, enable_thinking});
        if (!chat_template) {
            error = "the model ships no chat template";
            return false;
        }
        if (!chat_template_error.empty()) {
            error = chat_template_error;
            return false;
        }
        std::string prompt = "[template]";
        for (const harness::ChatMessage& message : messages) {
            prompt += " " + std::string{harness::to_string(message.role)} + ": ";
            prompt += message.content.plain_text();
            for (const harness::ToolCall& call : message.tool_calls) {
                prompt += " call:" + call.name + "#" + call.id;
            }
            if (!message.tool_call_id.empty()) {
                prompt += " answers:" + message.tool_call_id;
            }
        }
        if (!tools.empty()) {
            prompt += " tools:";
            for (const harness::Tool& tool : tools) {
                prompt += " " + tool.name;
            }
            out.grammar.gbnf = "root ::= fake-call";
            out.grammar.lazy = true;
            out.grammar.trigger_patterns = {"<tool_call>"};
        }
        prompt += enable_thinking ? " assistant:" : " assistant(no-think):";
        out.prompt = std::move(prompt);
        out.preserved_tokens = {id_for("<tool_call>"), id_for("</tool_call>")};
        out.stops = stops;
        out.format = "fake";
        out.reader = std::make_unique<FakeReplyReader>();
        return true;
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
        context_sizes.push_back(context_size);
        auto state = std::make_shared<FakeLlamaContext>();
        state->script = script;
        state->eog_token = eog_token;
        state->batch_limit = batch_limit;
        state->context_capacity = context_capacity;
        state->rewindable = rewindable;
        state->grammar_error = grammar_error;
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
    /// False plays a recurrent or hybrid model (see FakeLlamaContext).
    bool rewindable = true;
    std::vector<std::int32_t> script;
    std::int32_t eog_token = -1;
    std::string builtin_template_prefix;
    /// Applied to every model: see FakeLlamaModel.
    bool chat_template = false;
    std::string chat_template_error;
    std::set<std::string> special_words;
    std::vector<std::string> stops;
    std::string grammar_error;

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
        loaded->rewindable = rewindable;
        loaded->script = script;
        for (const std::string& piece : script_text) {
            loaded->script.push_back(loaded->id_for(piece));
        }
        loaded->eog_token = eog_token;
        loaded->builtin_template_prefix = builtin_template_prefix;
        loaded->chat_template = chat_template;
        loaded->chat_template_error = chat_template_error;
        loaded->special_words = special_words;
        loaded->stops = stops;
        loaded->grammar_error = grammar_error;
        model = loaded.get();
        return loaded;
    }

    std::string last_path;

    /// The projector the provider asked for, so a test can assert the config
    /// field reaches the runtime rather than being dropped on the way.
    std::string last_mmproj_path;
};

}  // namespace apogee::testing
