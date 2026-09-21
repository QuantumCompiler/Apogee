#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// How one model family writes reasoning and calls tools.
///
/// **Plain data on purpose.** The profiles themselves live in the backends
/// layer, which includes this header; declaring the shared shape here is what
/// lets the agent loop ask "how does this model behave?" without the harness
/// ever including backends. That is the layering seam, and it is a Core
/// constraint of this item, not a style preference -- in Ommi the same
/// structure exists for the same reason, because the obvious alternative is an
/// import cycle.
namespace apogee::harness {

struct ModelBehavior {
    /// Resolved profile id ("gemma4", "llama3", …), for diagnostics and
    /// session records. Empty when unknown.
    std::string profile;

    /// Literals that begin a tool call for this family.
    ///
    /// The streaming display uses these to decide whether a response is a tool
    /// call (suppressed) or prose (shown), so the list must stay in step with
    /// what the parser accepts: a marker missing here leaks raw markup to the
    /// user, one missing there silently drops the call.
    std::vector<std::string> tool_call_openers;

    /// Whether this family emits control-token tool calls in addition to, or
    /// instead of, an injected text protocol.
    bool native_tool_calls = false;

    /// Open/close wrappers this family puts private reasoning in. An empty
    /// vector on a KNOWN profile is a verified "this family emits none"; on an
    /// unknown profile it means "no information", and the caller should use
    /// its own defaults. `known()` is what distinguishes the two.
    std::vector<std::pair<std::string, std::string>> reasoning_tags;

    /// Whether a profile was resolved at all.
    ///
    /// **The zero value means unknown, and every consumer must treat unknown as
    /// PERMISSIVE** -- recognise every format it knows. An unrecognised backend
    /// is far more likely uncharacterised than genuinely featureless, and
    /// failing to recognise a tool call is the expensive direction: nothing
    /// dispatches, AND the raw markup is printed to the user as if it were the
    /// answer. Being too eager merely suppresses a line of text.
    [[nodiscard]] bool known() const noexcept {
        return !profile.empty();
    }

    [[nodiscard]] bool has_opener(std::string_view candidate) const noexcept;
};

}  // namespace apogee::harness
