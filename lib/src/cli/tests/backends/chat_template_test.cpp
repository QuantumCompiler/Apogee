#include "backends/chat_template.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "backends/llamacpp_tokens.h"
#include "support/fake_llama.h"

/// Prompt framing for local models.
///
/// The stakes are set by an Ommi bug worth restating: `gemma4` matched a
/// `gemma` substring, was rendered with Gemma 2/3's `<start_of_turn>` markers,
/// and produced token soup -- because those markers are not in Gemma 4's
/// vocabulary at all. Getting framing wrong does not raise an error; it makes
/// the model answer badly, which is much harder to notice.
namespace {

using apogee::harness::ChatMessage;

std::vector<ChatMessage> conversation() {
    return {ChatMessage::system("be brief"), ChatMessage::user("hello"),
            ChatMessage::assistant("hi"), ChatMessage::user("more")};
}

}  // namespace

TEST_CASE("chatml frames every role and opens the assistant turn", "[backends][template]") {
    const std::string out = apogee::backends::render_chatml(conversation(), true);

    CHECK(out.find("<|im_start|>system\nbe brief<|im_end|>") != std::string::npos);
    CHECK(out.find("<|im_start|>user\nhello<|im_end|>") != std::string::npos);
    CHECK(out.find("<|im_start|>assistant\nhi<|im_end|>") != std::string::npos);

    // The generation prompt must be LAST and left open -- that is the whole
    // signal telling the model to answer rather than continue the transcript.
    CHECK(out.substr(out.size() - std::string("<|im_start|>assistant\n").size()) ==
          "<|im_start|>assistant\n");
}

TEST_CASE("without a generation prompt the transcript is closed", "[backends][template]") {
    const std::string out = apogee::backends::render_chatml(conversation(), false);
    CHECK(out.substr(out.size() - std::string("<|im_end|>\n").size()) == "<|im_end|>\n");
}

TEST_CASE("llama3 uses header ids, not chatml markers", "[backends][template]") {
    const std::string out = apogee::backends::render_llama3(conversation(), true);

    CHECK(out.find("<|begin_of_text|>") == 0);
    CHECK(out.find("<|start_header_id|>user<|end_header_id|>") != std::string::npos);
    CHECK(out.find("<|eot_id|>") != std::string::npos);
    // The Ommi lesson in assertion form: one family's markers must never
    // appear in another's rendering.
    CHECK(out.find("<|im_start|>") == std::string::npos);
}

TEST_CASE("mistral folds the system prompt into the first instruction", "[backends][template]") {
    // Mistral has no system role. Dropping the system message would silently
    // discard the user's instructions -- worse than any framing imperfection.
    const std::string out = apogee::backends::render_mistral(conversation(), true);

    CHECK(out.find("[INST] be brief\n\nhello [/INST]") != std::string::npos);
    CHECK(out.find("system") == std::string::npos);
    CHECK(out.find("hi</s>") != std::string::npos);
}

TEST_CASE("a system-only conversation still states its system prompt", "[backends][template]") {
    // The fold has an edge: with no user turn to fold into, a naive
    // implementation drops the prompt entirely.
    const std::string out =
        apogee::backends::render_mistral({ChatMessage::system("be brief")}, true);
    CHECK(out.find("be brief") != std::string::npos);
}

TEST_CASE("a tool result is attributed to the tool that produced it", "[backends][template]") {
    // None of these templates has a tool role. Rendering the output as the
    // assistant's own words would let the model treat a tool's result as
    // something it already said.
    std::vector<ChatMessage> messages{ChatMessage::user("what is 2+2?")};
    messages.push_back(apogee::harness::ChatMessage::from_tool_result(
        apogee::harness::ToolResult{"call_1", "calculator", "4", false}));

    const std::string out = apogee::backends::render_chatml(messages, true);
    CHECK(out.find("Result of calculator:") != std::string::npos);
    CHECK(out.find("<|im_start|>user\nResult of calculator:") != std::string::npos);
}

TEST_CASE("model names resolve to a template, or to nothing", "[backends][template]") {
    using apogee::backends::template_name_for_model;

    CHECK(template_name_for_model("Meta-Llama-3-8B-Instruct") == "llama3");
    CHECK(template_name_for_model("llama3.2-3b-q4km") == "llama3");
    CHECK(template_name_for_model("mistral-7b-instruct") == "mistral");
    CHECK(template_name_for_model("mixtral-8x7b") == "mistral");
    CHECK(template_name_for_model("qwen3-14b") == "chatml");

    // The conservative half, and the important one: an unrecognised model gets
    // NO match, so it takes the documented ChatML fallback rather than a guess
    // from a substring that happens to appear in its name.
    CHECK(template_name_for_model("some-new-model-v2").empty());
    CHECK(template_name_for_model("").empty());
}

TEST_CASE("an unrecognised model falls back and says so", "[backends][template]") {
    const auto rendered =
        apogee::backends::render_for_model("some-new-model-v2", conversation(), true);
    CHECK(rendered.kind == apogee::backends::TemplateKind::Fallback);
    CHECK(rendered.template_name.empty());
    CHECK(rendered.text.find("<|im_start|>") != std::string::npos);
}

TEST_CASE("a recognised model reports which template it got", "[backends][template]") {
    const auto rendered =
        apogee::backends::render_for_model("Meta-Llama-3-8B", conversation(), true);
    CHECK(rendered.kind == apogee::backends::TemplateKind::Named);
    CHECK(rendered.template_name == "llama3");
}

TEST_CASE("the model's own template beats our registry", "[backends][template]") {
    // A GGUF's template is the model's statement about itself; ours is a guess
    // from its name. When both exist the model wins -- that precedence is what
    // keeps a name-matching mistake from reaching a model that told us better.
    apogee::testing::FakeLlamaModel model;
    model.builtin_template_prefix = "BUILTIN";

    const std::string out = apogee::backends::llama_tokens::render_prompt(model, "Meta-Llama-3-8B",
                                                                          conversation(), true);

    CHECK(out.find("BUILTIN") == 0);
    CHECK(out.find("<|start_header_id|>") == std::string::npos);
}

TEST_CASE("a model with no template of its own takes the registry", "[backends][template]") {
    apogee::testing::FakeLlamaModel model;  // ships no template

    const std::string out = apogee::backends::llama_tokens::render_prompt(model, "Meta-Llama-3-8B",
                                                                          conversation(), true);

    CHECK(out.find("<|start_header_id|>") != std::string::npos);
}

TEST_CASE("a common prefix is measured exactly", "[backends][llamacpp][kv]") {
    // The KV cache's entire decision, so it gets its own test rather than
    // being inferred from provider behaviour.
    using apogee::backends::llama_tokens::common_prefix_length;

    CHECK(common_prefix_length({1, 2, 3}, {1, 2, 3, 4, 5}) == 3);
    CHECK(common_prefix_length({1, 2, 3, 4}, {1, 2, 9, 4}) == 2);
    CHECK(common_prefix_length({}, {1, 2}) == 0);
    CHECK(common_prefix_length({1, 2}, {}) == 0);
    CHECK(common_prefix_length({1, 2}, {1, 2}) == 2);

    // Divergence at the very first token: nothing is reusable. This is the
    // resumed-session and post-compaction case, and it must not report a
    // reusable prefix it cannot honour.
    CHECK(common_prefix_length({7, 8}, {1, 2}) == 0);
}
