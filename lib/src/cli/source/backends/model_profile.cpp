#include "backends/model_profile.h"

#include <algorithm>
#include <cctype>

#include "backends/native_tool_calls.h"

namespace apogee::backends {
namespace {

[[nodiscard]] std::string lowercased(std::string_view value) {
    std::string out{value};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

const std::vector<ModelProfile>& model_profiles() {
    static const std::vector<ModelProfile> profiles = [] {
        std::vector<ModelProfile> list;

        // --- Verified here, 2026-09-07 ---------------------------------------

        // Gemma 3. Runs clean: its GGUF carries a chat template, llama.cpp
        // applies it, and the answer arrives with no framing to strip.
        //
        // Note this CONTRADICTS the assumption this item inherited from Ommi's
        // Gemma 4 -- that Gemma ships no template and needs a bespoke one.
        // Gemma 3 needs no help at all. Recording the contradiction is the
        // point of characterizing rather than porting.
        list.push_back({.name = "gemma3",
                        .architectures = {"gemma3", "gemma2", "gemma"},
                        .name_hints = {"gemma"},
                        // Verified none: it emitted no reasoning wrapper.
                        .reasoning = {},
                        .tools = {},
                        .verified = true,
                        .evidence = "gemma-3-1b-it Q8_0, 2026-09-07: embedded chat template "
                                    "applied, answered cleanly, no reasoning or framing markers"});

        // Qwen 3.x. Emits <think>…</think> AROUND the answer -- observed
        // directly: "What is 2+2?" came back as "<think>\n\n</think>\n\n4",
        // with every one of those characters reaching the user.
        list.push_back({.name = "qwen3",
                        .architectures = {"qwen35", "qwen3", "qwen3moe", "qwen35moe", "qwen2"},
                        .name_hints = {"qwen"},
                        .reasoning = {{.open = "<think>", .close = "</think>"}},
                        .tools = {},
                        .verified = true,
                        .evidence = "qwen3.6-27b Q4_K_M, 2026-09-07: emitted a literal "
                                    "<think></think> block into the answer text"});

        // gpt-oss. The family this item was gated on, and the only one here
        // that emits BOTH mechanisms the item is about.
        //
        // Its framing tokens are USER_DEFINED rather than CONTROL, so
        // llama.cpp detokenizes them straight into the stream. Observed
        // verbatim for "What is 2+2? Answer briefly.":
        //
        //   <|channel|>analysis<|message|>The user asks: … The answer is 4.
        //   <|end|><|start|>assistant<|channel|>final<|message|>4
        //
        // All of which reached the caller as the answer.
        //
        // The reasoning entry is what makes the analysis channel a reasoning
        // BLOCK rather than framing to delete: its content is the model's
        // working and belongs in the thinking view, so it is an open/close pair
        // like any other. What is left afterwards -- `<|start|>assistant` and
        // `<|channel|>final<|message|>` -- is pure framing, and that is the
        // markup filter's half. Two mechanisms, composed, each doing the thing
        // it is right about.
        //
        // Ommi carries this same framing, transcribed from a manifest and
        // marked unverified because the GGUF it had would not load. It was
        // right; this is the run that says so.
        list.push_back(
            {.name = "gpt-oss",
             .architectures = {"gpt-oss", "openai-moe", "gptoss"},
             .name_hints = {"gpt-oss", "gpt_oss", "gptoss"},
             .reasoning = {{.open = "<|channel|>analysis<|message|>", .close = "<|end|>"}},
             .headers = {{.open = "<|channel|>", .close = "<|message|>"},
                         {.open = "<|start|>", .close = ""}},
             .tools = {.injected = true, .native = true},
             .verified = true,
             .evidence = "gpt-oss-20b MXFP4, 2026-09-07: emitted "
                         "<|channel|>analysis<|message|>…<|end|><|start|>assistant"
                         "<|channel|>final<|message|> framing into the answer, and called a "
                         "tool as <|channel|>commentary to=functions.NAME <|constrain|>json"
                         "<|message|>{json}"});

        // --- Registered, NOT verified ----------------------------------------

        // Llama 3.x. The two Llama files on the development machine carry no
        // chat template and a content hash where their name should be, and they
        // degenerate on any prompt -- almost certainly base rather than
        // instruction-tuned conversions. So the family is registered from its
        // published format and honestly marked unverified: nothing here was
        // seen working, and claiming otherwise would be exactly the guesswork
        // profiles exist to end.
        list.push_back({.name = "llama3",
                        .architectures = {"llama"},
                        .name_hints = {"llama-3", "llama3", "llama_3"},
                        .reasoning = {},
                        .tools = {.injected = true, .native = false},
                        .verified = false,
                        .evidence = "not verified: the llama3.2 files available carry no chat "
                                    "template and degenerate on every prompt -- likely base "
                                    "models. Registered so the family is a declared gap, not a "
                                    "silent one"});

        // ChatML-speaking families. Registered because recognising a reasoning
        // wrapper costs nothing and missing one is expensive, but unverified:
        // none of these was run.
        list.push_back({.name = "chatml",
                        .architectures = {},
                        .name_hints = {"chatml", "hermes", "openchat", "yi-"},
                        .reasoning = {{.open = "<think>", .close = "</think>"}},
                        .tools = {},
                        .verified = false,
                        .evidence = "not verified: no model of this family was available to run"});

        // DeepSeek-R1 and its distills. Same <think> convention as Qwen, which
        // IS verified -- but on a different family, so this stays unverified.
        list.push_back({.name = "deepseek",
                        .architectures = {"deepseek2", "deepseek"},
                        .name_hints = {"deepseek"},
                        .reasoning = {{.open = "<think>", .close = "</think>"}},
                        .tools = {},
                        .verified = false,
                        .evidence = "not verified: shares Qwen's <think> convention, which was "
                                    "verified on Qwen but not on this family"});

        return list;
    }();
    return profiles;
}

const ModelProfile* resolve_profile(std::string_view forced, std::string_view architecture,
                                    std::string_view name_hint) {
    const std::vector<ModelProfile>& profiles = model_profiles();

    // --- rung 1: the user said so ---------------------------------------------
    if (!forced.empty()) {
        const std::string wanted = lowercased(forced);
        for (const ModelProfile& profile : profiles) {
            if (profile.name == wanted) {
                return &profile;
            }
        }
        // A forced name that matches nothing falls through rather than failing:
        // the value is also a chat-template name for the older registry, and a
        // hard error here would break configs that predate profiles.
    }

    // --- rung 2: the file's own declaration ------------------------------------
    // Above the name hint on purpose. An architecture is a FACT recorded in the
    // GGUF; a name is a guess from a string someone chose. This is the
    // "specific before family" rule in the form that actually bites: a file
    // declaring `gemma3` must not be matched as `gemma` by its filename.
    if (!architecture.empty()) {
        const std::string wanted = lowercased(architecture);
        for (const ModelProfile& profile : profiles) {
            if (std::ranges::find(profile.architectures, wanted) != profile.architectures.end()) {
                return &profile;
            }
        }
    }

    // --- rung 3: guess from the name -------------------------------------------
    if (!name_hint.empty()) {
        const std::string haystack = lowercased(name_hint);
        for (const ModelProfile& profile : profiles) {
            for (const std::string& hint : profile.name_hints) {
                if (contains(haystack, hint)) {
                    return &profile;
                }
            }
        }
    }

    // --- rung 4: nothing. Uncharacterised, and handled permissively. ------------
    return nullptr;
}

harness::ModelBehavior behavior_for(const ModelProfile* profile) {
    harness::ModelBehavior behavior;
    if (profile == nullptr) {
        // The zero value, which every consumer must read as PERMISSIVE: an
        // unrecognised model is far likelier uncharacterised than featureless.
        return behavior;
    }

    behavior.profile = profile->name;
    behavior.native_tool_calls = profile->tools.native;
    if (profile->tools.native) {
        // The same list the parser accepts and the display suppresses on. It
        // crosses to the harness as plain strings, so a surface can ask what a
        // tool call looks like without learning that profiles exist.
        behavior.tool_call_openers = tool_call_openers();
    }
    behavior.reasoning_tags.reserve(profile->reasoning.size());
    for (const TagPair& pair : profile->reasoning) {
        behavior.reasoning_tags.emplace_back(pair.open, pair.close);
    }
    return behavior;
}

std::vector<TagPair> reasoning_pairs_for(const ModelProfile* profile) {
    if (profile == nullptr) {
        // Unprofiled: recognise everything known. Being too eager suppresses a
        // line; being too cautious prints the model's working as the answer.
        return default_think_pairs();
    }
    // A KNOWN profile's empty list is a verified "emits none", and is honoured
    // as such -- that is the whole reason known() exists.
    return profile->reasoning;
}

std::vector<HeaderMarker> header_markers_for(const ModelProfile* profile) {
    if (profile == nullptr) {
        // No permissive default here, and the asymmetry with reasoning pairs is
        // deliberate. A header is deleted outright once matched, so inventing
        // one for an uncharacterised family risks removing its answer. Missing
        // a reasoning wrapper shows the user some working; missing an answer
        // shows the user nothing.
        return {};
    }
    return profile->headers;
}

}  // namespace apogee::backends
