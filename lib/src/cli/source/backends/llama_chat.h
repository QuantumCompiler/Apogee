#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct llama_model;

/// llama.cpp's own chat layer (`common/chat.h`, the one llama-server runs),
/// behind an interface of standard types.
///
/// It renders a model's embedded Jinja template with the tool list, builds a
/// parser for that template's tool-call format, and returns a lazy grammar
/// that constrains a call once the model starts one. Every family's trained
/// format, maintained upstream with the pin -- where Ommi injected a prose
/// protocol that a model trained on tool tokens ignores (25b).
///
/// **Why a library of its own** (`apogee_llama_chat`, compiled only with
/// `APOGEE_ENABLE_LLAMA`): `common` carries its own copy of nlohmann/json, at
/// a different version from Apogee's. The two share an include guard, so a
/// translation unit that saw both would silently compile against whichever
/// came first. This file and its `.cpp` are the only code that sees `common`'s
/// headers, and nothing here names a JSON type or includes an Apogee header
/// that does -- the interface is strings and vectors, and `llama_real.cpp`
/// converts to the IR on its side.
namespace apogee::backends::llama_chat {

struct ToolCall {
    std::string id;
    std::string name;
    /// JSON text, as the IR keeps it.
    std::string arguments;
};

struct Message {
    /// `system`, `user`, `assistant` or `tool`.
    std::string role;
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string tool_call_id;
    std::string tool_name;
};

struct ToolSpec {
    std::string name;
    std::string description;
    /// The JSON Schema, as text.
    std::string parameters;
};

struct Inputs {
    std::vector<Message> messages;
    std::vector<ToolSpec> tools;
    /// The template's own switch. Off asks a thinking model to answer
    /// without reasoning first, where its template has that switch.
    bool enable_thinking = true;
};

/// A reply, read back through the template's format.
struct Reply {
    std::string content;
    std::string reasoning;
    std::vector<ToolCall> tool_calls;
};

/// Reads replies to one rendered request.
class ReplyParser {
public:
    ~ReplyParser();
    ReplyParser(const ReplyParser&) = delete;
    ReplyParser& operator=(const ReplyParser&) = delete;
    ReplyParser(ReplyParser&&) = delete;
    ReplyParser& operator=(ReplyParser&&) = delete;

    /// Parses `text` -- everything generated so far. `partial` while the
    /// reply is still streaming, so an unfinished call is held rather than
    /// refused. False with `error` when a finished reply does not match the
    /// format (llama.cpp throws; this never does).
    [[nodiscard]] bool parse(const std::string& text, bool partial, Reply& out,
                             std::string& error) const;

    struct Impl;
    explicit ReplyParser(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl_;
};

/// One request, rendered.
struct Rendered {
    std::string prompt;
    /// GBNF constraining a tool call; empty when nothing is constrained.
    std::string grammar;
    /// Applied only once a trigger fires, so prose stays free.
    bool grammar_lazy = false;
    /// Regular expressions and single tokens that start a call -- llama.cpp's
    /// own conversion (`common/sampling.cpp`), done here where `common` is.
    std::vector<std::string> trigger_patterns;
    std::vector<std::int32_t> trigger_tokens;
    /// Special tokens the parser must see as text (`<tool_call>` on Qwen):
    /// rendered, where a special token is otherwise dropped from the reply.
    std::vector<std::int32_t> preserved_tokens;
    /// Extra strings that end generation.
    std::vector<std::string> stops;
    bool supports_thinking = false;
    /// The format's name, for a diagnostic.
    std::string format;
    std::unique_ptr<ReplyParser> parser;
};

/// A model's chat templates, parsed once per model load.
class Templates {
public:
    /// Nullptr with `error` when the GGUF ships no template -- then the
    /// name-matched registry renders the prompt, as it always has, rather
    /// than `common`'s ChatML default -- or when its template does not parse.
    [[nodiscard]] static std::unique_ptr<Templates> load(const llama_model* model,
                                                         std::string& error);

    ~Templates();
    Templates(const Templates&) = delete;
    Templates& operator=(const Templates&) = delete;
    Templates(Templates&&) = delete;
    Templates& operator=(Templates&&) = delete;

    /// Renders `inputs`. False with `error` when the template cannot render
    /// them; the caller falls back and says so.
    [[nodiscard]] bool render(const Inputs& inputs, Rendered& out, std::string& error) const;

    struct Impl;
    explicit Templates(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace apogee::backends::llama_chat
