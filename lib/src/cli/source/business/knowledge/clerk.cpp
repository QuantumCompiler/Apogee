#include "knowledge/clerk.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <utility>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "agentloop/structured.h"
#include "contracts/types.h"
#include "harness/harness.h"

// The clerk's prompt and schema are GENERATED from lib/src/cli/assets/clerks/
// capture_prompt.txt and capture_schema.json (the script is recorded in
// MILESTONES.md). The shipped files and these literals are byte-identical, and
// tests/business/knowledge/clerk_test.cpp fails the build the moment they drift -- the
// same contract the config template and the bundled agents keep. Edit the
// FILES, then regenerate; never edit a literal here.
//
// Compiled in rather than seeded under the data directory on purpose: the
// clerk is a fixed system concern, not a user-editable agent, and so it adds
// no install-parity surface.

namespace apogee::knowledge {
namespace {

constexpr std::string_view kCapturePrompt =
    R"PROMPT(You are a disciplined normalization clerk for an organizational knowledge layer.
You are given a raw brainstorm, design discussion, or development conversation.
Your single job is to extract exactly ONE canonical intent record from it — the
durable "why" behind a decision — and emit it as strict JSON.

You are not a brainstormer and not a summarizer. You preserve reasoning; you do
not invent it. Follow these rules exactly:

1. INTENT (the why). Capture the rationale that explains why the decision was
   made — the thing that normally evaporates and is missing from tickets and
   design files later. Prefer the participants' own words; do not pre-summarize
   the reasoning away into a bland one-liner. If the conversation only states
   what was done with no why, say so plainly in the intent rather than inventing
   a rationale.

2. DECISION (distinct from discussion). State what was actually chosen, not the
   range of things discussed. Discussion is not decision.

3. STATUS (the branch marker). Brainstorms are mostly roads not taken. Determine
   whether the idea this record describes was "shipped" (chosen / implemented),
   "rejected" (considered and abandoned), or "superseded" (replaced by a later
   decision). If a conversation contains several competing ideas, write the
   record for the branch that won and mark it shipped; if you are asked to record
   an abandoned option, mark it rejected. Never label an abandoned idea shipped.

4. DISCIPLINE. Classify the originating discipline: program, product, project,
   ux, or eng. Choose the closest single value.

5. DOWNSTREAM_LINK (the write-time link). If the conversation references the
   artifact this decision produced — a ticket ID, a Figma component, a commit, a
   PR, a file — record it. This is the link that makes the record traceable. If
   none is present, leave it empty; do not fabricate one.

6. PROVENANCE. Record the source surface (e.g. claude-code, copilot, chat,
   meeting, manual) and, separately, attribution (who said it) when clearly
   identifiable. Keep attribution in its own field; never weave names into the
   intent or decision text, so attribution can be stripped without losing the
   reasoning.

Output ONLY the JSON object described by the schema — no commentary, no markdown
code fences. Leave a field as an empty string when the conversation genuinely
does not support a value. Accuracy and restraint matter more than completeness.
)PROMPT";

constexpr std::string_view kCaptureSchema = R"SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "CanonicalKnowledgeRecord",
  "type": "object",
  "additionalProperties": false,
  "description": "One canonical intent record extracted from a raw brainstorm or development conversation. Captures the durable 'why' behind a decision so it can be retrieved later. The system assigns id, raw_ref, and timestamp — do not emit them.",
  "properties": {
    "intent": {
      "type": "string",
      "description": "The why — the rationale that explains the decision, in the participants' own words where possible. This is the load-bearing field; preserve it verbatim rather than pre-summarizing it away. Required."
    },
    "decision": {
      "type": "string",
      "description": "What was actually chosen — distinct from the range of options discussed. Discussion is not decision."
    },
    "status": {
      "type": "string",
      "enum": ["shipped", "rejected", "superseded"],
      "description": "Which branch this record represents: shipped (chosen/implemented), rejected (considered and abandoned), or superseded (replaced by a later decision). Never label an abandoned idea shipped."
    },
    "discipline": {
      "type": "string",
      "enum": ["program", "product", "project", "ux", "eng"],
      "description": "The originating discipline. Choose the closest single value."
    },
    "downstream_link": {
      "type": "string",
      "description": "The artifact this decision produced — a ticket ID, Figma component, commit, PR, or file path. The write-time link that makes the record traceable. Empty when the conversation references none; do not fabricate."
    },
    "provenance": {
      "type": "object",
      "additionalProperties": false,
      "description": "Where the record came from. Source and attribution are deliberately separate so attribution can be stripped without losing the chain.",
      "properties": {
        "source": {
          "type": "string",
          "description": "The source surface, e.g. claude-code, copilot, chat, meeting, manual."
        },
        "attribution": {
          "type": "string",
          "description": "Who said it, when clearly identifiable. Keep names ONLY here — never in intent or decision."
        }
      },
      "required": ["source"]
    }
  },
  "required": ["intent", "decision", "status", "discipline", "provenance"]
}
)SCHEMA";

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

/// The record a clerk's outcome holds, or nullopt with `error` saying why:
/// a non-conforming outcome is an error carrying the validator's message,
/// never a partial record.
std::optional<Record> read_outcome(const ClerkOutcome& outcome, std::string& error) {
    if (!outcome.conforms || !outcome.json.has_value()) {
        // A record is not a report: there is no "raw with conforms: false"
        // here. Twice non-conforming is a failed capture that stores nothing.
        error = "the clerk did not return a record";
        for (const std::string& message : outcome.errors) {
            error += "; " + message;
        }
        if (outcome.attempts > 0) {
            error += " (after " + std::to_string(outcome.attempts) + " attempt" +
                     (outcome.attempts == 1 ? "" : "s") + ")";
        }
        return std::nullopt;
    }
    try {
        return outcome.json->get<Record>();
    } catch (const nlohmann::json::exception& e) {
        error = std::string{"the clerk's record could not be read: "} + e.what();
        return std::nullopt;
    }
}

}  // namespace

std::string_view capture_prompt() noexcept {
    return kCapturePrompt;
}

std::string_view capture_schema_text() noexcept {
    return kCaptureSchema;
}

nlohmann::json capture_schema() {
    return nlohmann::json::parse(kCaptureSchema);
}

std::string with_output_format(std::string_view prompt) {
    // The OUTPUT FORMAT block is worded in ONE place for the whole product --
    // `agentloop::schema_instruction`, which every agent's persona ends with
    // -- so the clerk asks for JSON in exactly the words an agent does, and a
    // later revision pass words it identically to this one.
    return trim(prompt) + "\n\n" +
           agentloop::schema_instruction({std::string{kCaptureSchema}}, false);
}

std::string capture_system_prompt() {
    return with_output_format(kCapturePrompt);
}

Draft draft_record(Record record, const Overrides& overrides) {
    // Whatever the user said wins over whatever the clerk inferred, field by
    // field: an override is the user already knowing the answer.
    if (!overrides.status.empty()) {
        record.status = overrides.status;
    }
    if (!overrides.discipline.empty()) {
        record.discipline = overrides.discipline;
    }
    if (!overrides.source.empty()) {
        record.provenance.source = overrides.source;
    }
    if (!overrides.link.empty()) {
        record.downstream_link = overrides.link;
    }
    if (!overrides.supersedes.empty()) {
        record.supersedes = overrides.supersedes;
    }
    // A draft is what a store would accept minus what only persistence mints.
    record.id.clear();
    record.timestamp.clear();
    record.raw_ref.clear();
    normalize(record);
    Draft draft;
    draft.error = validate(record);
    draft.record = std::move(record);
    return draft;
}

std::string revision_message(std::string_view raw, std::string_view record,
                             std::string_view objection) {
    return trim(raw) + "\n\n---\nYOUR PREVIOUS RECORD:\n" + trim(record) +
           "\n\nA REVIEWER, CHECKING IT AGAINST THE CONVERSATION ABOVE, OBJECTED:\n" +
           trim(objection) +
           "\n\nReturn the record again as the schema describes: corrected where the objection "
           "is right, unchanged where it is wrong.";
}

Draft run_capture(const ClerkFn& clerk, std::string_view raw, const Overrides& overrides,
                  const agentloop::Verifier* verifier) {
    const ClerkOutcome outcome = clerk(capture_system_prompt(), raw);
    std::string error;
    const std::optional<Record> record = read_outcome(outcome, error);
    if (!record.has_value() || !outcome.json.has_value()) {
        Draft draft;
        draft.error = std::move(error);
        return draft;
    }
    Draft draft = draft_record(*record, overrides);
    if (!draft.ok() || verifier == nullptr) {
        // Structure first: a record that does not validate is a failed
        // capture, and no verifier is woken for it.
        return draft;
    }

    // The record as the clerk wrote it, against its source (27g).
    const std::vector<std::string> required = agentloop::required_fields(capture_schema());
    std::optional<Record> revised;
    const std::string written = outcome.json->dump(2);
    agentloop::Validated validated = agentloop::validate_artifact(
        written,
        [&](const std::string& artifact) {
            return agentloop::run_checks(
                {}, verifier, [&] { return agentloop::extraction_brief(raw, artifact, required); });
        },
        [&](const std::string& artifact, const std::string& objection,
            std::string& note) -> std::optional<std::string> {
            // The producer's one revision: the clerk again, the source with
            // its record and the objection beside it.
            const ClerkOutcome again =
                clerk(capture_system_prompt(), revision_message(raw, artifact, objection));
            std::string why;
            std::optional<Record> second = read_outcome(again, why);
            if (!second.has_value()) {
                note = "its revision failed: " + why;
                return std::nullopt;
            }
            if (const Draft checked = draft_record(*second, overrides); !checked.ok()) {
                note = "its revision does not validate: " + checked.error;
                return std::nullopt;
            }
            revised = std::move(second);
            return again.json->dump(2);
        },
        true);
    if (revised.has_value() && validated.artifact != validated.original) {
        draft = draft_record(std::move(*revised), overrides);
    }
    draft.validation = std::move(validated);
    return draft;
}

Record finalize(Record draft, std::chrono::system_clock::time_point now) {
    draft.id = new_id(now);
    draft.timestamp = timestamp_for(now);
    return draft;
}

ClerkFn make_structured_clerk(const harness::Harness& harness, std::string model) {
    return [&harness, model = std::move(model)](std::string_view system_prompt,
                                                std::string_view user_message) {
        // A throwaway history, so the clerk never touches a caller's
        // conversation -- which is what lets chat's /capture reuse the live
        // model with no second load. Marked a side request so a local
        // backend runs it on its own context and the session's cache stays.
        std::vector<harness::ChatMessage> history{
            harness::ChatMessage::system(std::string{system_prompt}),
            harness::ChatMessage::user(std::string{user_message})};
        agentloop::Options options;
        options.model = model;
        options.temperature = kClerkTemperature;
        options.max_tokens = kClerkMaxTokens;
        options.stream_answer = false;
        options.side_request = true;
        agentloop::NullReporter reporter;
        const agentloop::StructuredResult result =
            agentloop::run_structured(harness, history, options, reporter, capture_schema_text());
        ClerkOutcome outcome;
        outcome.json = result.json;
        outcome.conforms = result.conforms;
        outcome.errors = result.errors;
        outcome.answer = result.answer;
        outcome.attempts = result.attempts;
        return outcome;
    };
}

}  // namespace apogee::knowledge
