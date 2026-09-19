#include "knowledge/refine.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <utility>

#include "knowledge/clerk.h"

// The refine prompt is GENERATED from lib/src/cli/assets/clerks/refine_prompt.txt
// (the same script that generates the capture clerk's literals, recorded in
// MILESTONES.md). The shipped file and this literal are byte-identical, and
// tests/knowledge/refine_test.cpp fails the build the moment they drift.
// Compiled in, not seeded: a fixed system concern, no install-parity surface.

namespace apogee::knowledge {
namespace {

constexpr std::string_view kRefinePrompt =
    R"PROMPT(You are the same disciplined normalization clerk that produced a draft canonical
intent record from a raw conversation. A human reviewer has read that draft and
given you ONE instruction. Your single job is to produce a revised record that
applies the instruction — and nothing else — and emit it as strict JSON.

You will receive three sections:
  CURRENT DRAFT        — the record as it stands (JSON).
  REVIEWER INSTRUCTION — what to change, in the reviewer's own words.
  RAW CONVERSATION     — the original source (may be absent).

Follow these rules exactly:

1. APPLY THE INSTRUCTION FAITHFULLY. Do what it asks: correct a field, restore
   reasoning that was left out, change the status branch, add or remove a link,
   tighten or expand a passage. If it names a field, change that field.

2. CHANGE NOTHING ELSE. Every field the instruction does not touch must be
   carried over verbatim from the current draft — do not re-extract, re-word,
   or "improve" untouched fields. A revision is a diff, not a fresh capture.

3. STAY GROUNDED. When the instruction asks for content (a quote, a rationale, a
   detail), take it from the RAW CONVERSATION. If the raw conversation does not
   support it — or was not supplied — do not invent it: leave the field as it
   was (or empty, if the instruction asked to remove it) and let restraint win.
   The one exception is a value the reviewer states outright in the instruction
   (a ticket ID, a status, a discipline): use it exactly as given.

4. THE ORIGINAL RULES STILL HOLD. Intent is the why, in the participants' own
   words; decision is what was chosen, not what was discussed; status is the
   branch marker (never label an abandoned idea shipped); discipline is one of
   program / product / project / ux / eng; downstream_link is never fabricated;
   names live only in provenance.attribution, never in intent or decision.

5. IF THE INSTRUCTION CANNOT BE APPLIED — it contradicts the raw conversation,
   or asks for something the schema cannot express — return the draft unchanged
   rather than guessing.

Output ONLY the JSON object described by the schema — the complete revised
record with every field present, no commentary, no markdown code fences.
)PROMPT";

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

/// Codepoints, not bytes: a reviewer's note in any script gets the same
/// allowance.
[[nodiscard]] std::size_t codepoints(std::string_view text) {
    std::size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

}  // namespace

std::string_view refine_prompt() noexcept {
    return kRefinePrompt;
}

std::string refine_system_prompt() {
    // The same OUTPUT FORMAT block and the same schema the capture pass
    // asks for: one output shape for both passes, so a refined draft is
    // storable through exactly the finished-record path a captured one is.
    return with_output_format(kRefinePrompt);
}

std::string validate_refine_instruction(std::string_view instruction) {
    const std::string trimmed = trim(instruction);
    if (trimmed.empty()) {
        return "instruction is required: what to change, in the reviewer's words";
    }
    if (const std::size_t length = codepoints(trimmed); length > kMaxRefineInstructionLen) {
        return "instruction is too long (" + std::to_string(length) + " characters; the limit is " +
               std::to_string(kMaxRefineInstructionLen) +
               " -- put the source material in the raw conversation, not the instruction)";
    }
    return {};
}

std::string refine_user_message(const Record& draft, std::string_view instruction,
                                std::string_view raw) {
    // The clerk owns six fields and sees six fields: id, raw_ref and
    // timestamp are the system's, supersedes is the user's, and none of
    // them may reach the model's input or its output.
    nlohmann::ordered_json view{
        {"intent", draft.intent},
        {"decision", draft.decision},
        {"status", draft.status},
        {"discipline", draft.discipline},
        {"downstream_link", draft.downstream_link},
        {"provenance", nlohmann::ordered_json{{"source", draft.provenance.source}}}};
    if (!draft.provenance.attribution.empty()) {
        view["provenance"]["attribution"] = draft.provenance.attribution;
    }
    std::string out = "CURRENT DRAFT\n";
    out += view.dump(2);
    out += "\n\nREVIEWER INSTRUCTION\n";
    out += trim(instruction);
    out += "\n\nRAW CONVERSATION\n";
    const std::string grounding = trim(raw);
    if (grounding.empty()) {
        // Said outright, so the model knows there is nothing to reach for.
        out +=
            "(not supplied -- revise from the draft and the instruction alone; do not invent "
            "content)";
    } else {
        out += grounding;
    }
    return out;
}

Draft run_refine(const ClerkFn& clerk, const Record& draft, std::string_view instruction,
                 std::string_view raw) {
    // The guards run BEFORE any clerk call: a bad instruction costs nothing.
    if (const std::string why = validate_refine_instruction(instruction); !why.empty()) {
        Draft refused;
        refused.error = why;
        refused.record = draft;
        return refused;
    }
    const ClerkOutcome outcome =
        clerk(refine_system_prompt(), refine_user_message(draft, instruction, raw));
    if (!outcome.conforms || !outcome.json.has_value()) {
        Draft failed;
        failed.error = "the clerk did not return a record";
        for (const std::string& error : outcome.errors) {
            failed.error += "; " + error;
        }
        if (outcome.attempts > 0) {
            failed.error += " (after " + std::to_string(outcome.attempts) + " attempt" +
                            (outcome.attempts == 1 ? "" : "s") + ")";
        }
        failed.record = draft;
        return failed;
    }
    Record revised;
    try {
        revised = outcome.json->get<Record>();
    } catch (const nlohmann::json::exception& e) {
        Draft failed;
        failed.error = std::string{"the clerk's record could not be read: "} + e.what();
        failed.record = draft;
        return failed;
    }
    // The clerk owns only the schema fields. `supersedes` is the user's and
    // is carried through untouched; a source the model dropped keeps the
    // draft's value rather than being defaulted -- `normalize` would
    // otherwise rewrite a field the instruction never mentioned.
    revised.supersedes = draft.supersedes;
    if (trim(revised.provenance.source).empty()) {
        revised.provenance.source = draft.provenance.source;
    }
    return draft_record(std::move(revised), {});
}

}  // namespace apogee::knowledge
