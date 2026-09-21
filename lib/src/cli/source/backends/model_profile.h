#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "backends/markup_filter.h"
#include "backends/think_filter.h"
#include "harness/behavior.h"

/// What Apogee knows about how one **family** of local model behaves.
///
/// A bundle rather than five per-concern lists, and that is the whole point.
/// These behaviours are not independent: Ommi discovered as much across four
/// separate bug reports on one model, each fixing a different symptom of the
/// same uncharacterised family. Bundling them makes adding a family one
/// reviewable entry, and makes a half-characterised family a visible gap rather
/// than a runtime surprise.
///
/// Cloud backends need none of this — a vendor API returns typed blocks — so
/// the layer is local-only and reaches the rest of the system as
/// `harness::ModelBehavior`: plain data, never an include.
///
/// ## Characterized against real weights, on 2026-09-07
///
/// The item's rule is that a profile is written from what a model actually
/// emits, never from what its documentation claims. Three families were run:
///
/// | Family | Embedded template | Observed |
/// |---|---|---|
/// | `gemma3` | yes | Clean. Answers "Paris" with no framing to strip. |
/// | `qwen3` | yes | **Emits `<think>…</think>` into the answer.** |
/// | `llama3` | **no** | Degenerate on the files available here (see below). |
///
/// That table also **contradicts the assumption this item was written under**,
/// which came from Ommi's Gemma 4: that Gemma ships no chat template and needs
/// a bespoke one. Gemma 3 ships a perfectly good template and works without
/// help; the family that needed help was Llama.
///
/// ## What `verified` means, and why most entries are not
///
/// `verified` is true only where the behaviour was *observed on this machine*.
/// An unverified profile is still used — a documented guess beats the generic
/// fallback — but it is reported as unverified rather than quietly trusted.
/// Under the open-model policy there is no allowlist making any family a
/// promise, so honesty about characterization state is the only signal a user
/// gets.
namespace apogee::backends {

/// How a family expresses a tool call.
struct ToolDialect {
    /// Recognises Apogee's injected `TOOL_CALL: {json}` protocol.
    bool injected = true;
    /// Recognises control-token calls (`<|channel|>commentary to=…`).
    ///
    /// Permissive by default: **failing to recognise a call is the expensive
    /// direction.** Nothing dispatches AND the raw markup is printed as if it
    /// were the answer. Recognising one a model was never going to emit merely
    /// suppresses a line.
    bool native = true;
};

/// One family's behaviour.
struct ModelProfile {
    /// The id, and the value a user may set as `chat_template:` to force it.
    std::string name;

    /// The GGUF `general.architecture` values this profile claims.
    std::vector<std::string> architectures;

    /// Substrings matched against a model name or filename, when the
    /// architecture is unknown. Deliberately generous: a wrong guess still
    /// beats the generic fallback for any modern instruction-tuned model.
    std::vector<std::string> name_hints;

    /// Reasoning wrappers this family emits.
    ///
    /// **Empty on a known profile means "verified: this family emits none"** —
    /// a characterization result. That is different from an *unprofiled* model,
    /// where nothing is known and the permissive defaults apply. `known()` on
    /// `ModelBehavior` is what distinguishes the two, and conflating them is
    /// how a filter either strips nothing or strips everything.
    std::vector<TagPair> reasoning;

    /// Control-token **headers** this family leaks into its own answer.
    ///
    /// Not reasoning wrappers, and the distinction is the design: a wrapper
    /// encloses content and routes it somewhere, a header is a bare marker
    /// naming a section and carries nothing. See `markup_filter.h`.
    std::vector<HeaderMarker> headers;

    ToolDialect tools;

    /// Whether this profile was checked against real output from the model,
    /// rather than inferred from a published format.
    bool verified = false;

    /// What was seen, or why it could not be. Shown by `models info`.
    std::string evidence;
};

/// Every profile this build knows.
[[nodiscard]] const std::vector<ModelProfile>& model_profiles();

/// Resolves a profile for a model.
///
/// The ladder, in order, and it is table-tested rung by rung:
///
///   1. `forced` — an explicit `chat_template:` in config. The user wins.
///   2. `architecture` — the GGUF's own `general.architecture`. A **fact from
///      the file**, which is why it outranks guessing from a name.
///   3. `name_hint` — substrings of the model name or filename.
///   4. nothing — an unprofiled model, handled permissively.
///
/// Rung 2 above rung 3 is the "specific before family" rule in its useful form:
/// a file that says it is `gemma3` is not matched as `gemma` by a filename.
///
/// Returns nullptr when nothing matches, which callers must treat as
/// *uncharacterised and therefore permissive* — never as "no behaviours".
[[nodiscard]] const ModelProfile* resolve_profile(std::string_view forced,
                                                  std::string_view architecture,
                                                  std::string_view name_hint);

/// The plain-data view the harness consumes. `profile` may be nullptr.
///
/// This is the only thing that crosses out of `backends/`: the harness learns
/// how a model behaves without learning that profiles, or llama.cpp, exist.
[[nodiscard]] harness::ModelBehavior behavior_for(const ModelProfile* profile);

/// The reasoning pairs to filter with, for a resolved profile.
///
/// nullptr (unprofiled) yields the permissive default set; a known profile
/// yields exactly its own, which may legitimately be empty.
[[nodiscard]] std::vector<TagPair> reasoning_pairs_for(const ModelProfile* profile);

/// The control-token headers to strip, for a resolved profile.
///
/// Unlike reasoning pairs there is no permissive default: an unprofiled model
/// gets none. A header is stripped **unconditionally** once recognised, so
/// guessing at one for an uncharacterised family risks deleting its answer --
/// the opposite of the reasoning case, where the expensive direction is
/// recognising too little.
[[nodiscard]] std::vector<HeaderMarker> header_markers_for(const ModelProfile* profile);

}  // namespace apogee::backends
