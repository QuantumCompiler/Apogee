/// The one translation unit that sees llama.cpp's `common` headers. See the
/// header for why nothing Apogee-shaped is included here.
///
/// One `#if` around the whole file, as in `llama_real.cpp`: it is compiled only
/// into `apogee_llama_chat`, which exists only with llama.cpp -- and a build
/// without it (the lint build among them) must still be able to read it.

#include "backends/llama_chat.h"

#if defined(APOGEE_ENABLE_LLAMA)

#include <chat.h>
#include <common.h>
#include <llama.h>
#include <log.h>

#include <algorithm>
#include <exception>
#include <mutex>
#include <utility>

namespace apogee::backends::llama_chat {

struct ReplyParser::Impl {
    common_chat_parser_params params;
};

struct Templates::Impl {
    const llama_model* model = nullptr;
    common_chat_templates_ptr templates;
};

namespace {

/// `common` logs through a logger of its own, straight to stderr -- "Template
/// supports tool calls but does not natively describe tools", "unparsed
/// output" -- and inside a chat turn that lands on top of the status line, the
/// same wall of output `llama_real.cpp` keeps llama.cpp's own log off. A
/// threshold below every level means nothing is ever formatted, and the
/// logger's worker thread is never started.
void silence_common_log() {
    static std::once_flag once;
    std::call_once(once, [] { common_log_set_verbosity_thold(-1); });
}

/// The single token `text` is, or -1 when it is more than one.
std::int32_t single_token(const llama_vocab* vocab, const std::string& text) {
    const std::vector<llama_token> ids = common_tokenize(vocab, text, false, true);
    return ids.size() == 1 ? ids.front() : -1;
}

}  // namespace

ReplyParser::ReplyParser(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

ReplyParser::~ReplyParser() = default;

bool ReplyParser::parse(const std::string& text, bool partial, Reply& out,
                        std::string& error) const {
    try {
        const common_chat_msg message = common_chat_parse(text, partial, impl_->params);
        out.content = message.content;
        out.reasoning = message.reasoning_content;
        out.tool_calls.clear();
        for (const common_chat_tool_call& call : message.tool_calls) {
            out.tool_calls.push_back(ToolCall{call.id, call.name, call.arguments});
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

Templates::Templates(std::unique_ptr<Impl> impl) : impl_{std::move(impl)} {}

Templates::~Templates() = default;

std::unique_ptr<Templates> Templates::load(const llama_model* model, std::string& error) {
    silence_common_log();
    if (model == nullptr) {
        error = "no model";
        return nullptr;
    }
    if (llama_model_chat_template(model, nullptr) == nullptr &&
        llama_model_chat_template(model, "tool_use") == nullptr) {
        error = "the model ships no chat template";
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->model = model;
    try {
        impl->templates = common_chat_templates_init(model, "");
    } catch (const std::exception& e) {
        error = std::string{"its chat template could not be parsed: "} + e.what();
        return nullptr;
    }
    if (impl->templates == nullptr) {
        error = "its chat template could not be parsed";
        return nullptr;
    }
    return std::make_unique<Templates>(std::move(impl));
}

bool Templates::render(const Inputs& inputs, Rendered& out, std::string& error) const {
    common_chat_templates_inputs request;
    request.use_jinja = true;
    request.add_generation_prompt = true;
    request.enable_thinking = inputs.enable_thinking;
    // Reasoning separated into its own field, set on the inputs as well as
    // the parser: the template's rendering and the parser's reading both key
    // on it (found by the spike, 2026-09-25).
    request.reasoning_format = COMMON_REASONING_FORMAT_AUTO;
    request.tool_choice = COMMON_CHAT_TOOL_CHOICE_AUTO;
    // One call per step: easier to gate and to show (25b, default taken).
    request.parallel_tool_calls = false;

    request.messages.reserve(inputs.messages.size());
    for (const Message& message : inputs.messages) {
        common_chat_msg converted;
        converted.role = message.role;
        converted.content = message.content;
        converted.tool_call_id = message.tool_call_id;
        converted.tool_name = message.tool_name;
        for (const ToolCall& call : message.tool_calls) {
            converted.tool_calls.push_back(
                common_chat_tool_call{call.name, call.arguments, call.id});
        }
        request.messages.push_back(std::move(converted));
    }
    request.tools.reserve(inputs.tools.size());
    for (const ToolSpec& tool : inputs.tools) {
        request.tools.push_back(common_chat_tool{tool.name, tool.description, tool.parameters});
    }

    common_chat_params params;
    try {
        params = common_chat_templates_apply(impl_->templates.get(), request);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    const llama_vocab* vocab = llama_model_get_vocab(impl_->model);

    out.prompt = std::move(params.prompt);
    out.grammar = params.grammar;
    out.grammar_lazy = params.grammar_lazy;
    out.supports_thinking = params.supports_thinking;
    out.stops = params.additional_stops;
    out.format = common_chat_format_name(params.format);

    out.preserved_tokens.clear();
    for (const std::string& text : params.preserved_tokens) {
        if (const std::int32_t token = single_token(vocab, text); token >= 0) {
            out.preserved_tokens.push_back(token);
        }
    }

    // llama-server's conversion (tools/server/server-schema.cpp, then
    // common/sampling.cpp): a trigger word that is one preserved token fires
    // on the token; any other word fires on its escaped text; patterns pass
    // through, a full-match pattern anchored at both ends.
    out.trigger_patterns.clear();
    out.trigger_tokens.clear();
    for (const common_grammar_trigger& trigger : params.grammar_triggers) {
        switch (trigger.type) {
            case COMMON_GRAMMAR_TRIGGER_TYPE_WORD: {
                const std::int32_t token = single_token(vocab, trigger.value);
                if (token >= 0 &&
                    std::find(out.preserved_tokens.begin(), out.preserved_tokens.end(), token) !=
                        out.preserved_tokens.end()) {
                    out.trigger_tokens.push_back(token);
                } else {
                    out.trigger_patterns.push_back(regex_escape(trigger.value));
                }
                break;
            }
            case COMMON_GRAMMAR_TRIGGER_TYPE_PATTERN:
                out.trigger_patterns.push_back(trigger.value);
                break;
            case COMMON_GRAMMAR_TRIGGER_TYPE_PATTERN_FULL: {
                const std::string& pattern = trigger.value;
                std::string anchored = "^$";
                if (!pattern.empty()) {
                    anchored = (pattern.front() != '^' ? "^" : "") + pattern +
                               (pattern.back() != '$' ? "$" : "");
                }
                out.trigger_patterns.push_back(std::move(anchored));
                break;
            }
            case COMMON_GRAMMAR_TRIGGER_TYPE_TOKEN:
                out.trigger_tokens.push_back(trigger.token);
                break;
        }
    }
    if (out.grammar_lazy && out.trigger_patterns.empty() && out.trigger_tokens.empty()) {
        // A lazy grammar with nothing to wake it constrains nothing and
        // llama.cpp refuses it; unconstrained is the honest reading.
        out.grammar.clear();
        out.grammar_lazy = false;
    }

    auto parser = std::make_unique<ReplyParser::Impl>();
    parser->params = common_chat_parser_params{params};
    parser->params.reasoning_format = COMMON_REASONING_FORMAT_AUTO;
    if (!params.parser.empty()) {
        try {
            parser->params.parser.load(params.parser);
        } catch (const std::exception& e) {
            error = std::string{"the reply parser could not be built: "} + e.what();
            return false;
        }
    }
    out.parser = std::make_unique<ReplyParser>(std::move(parser));
    return true;
}

}  // namespace apogee::backends::llama_chat

#endif  // APOGEE_ENABLE_LLAMA
