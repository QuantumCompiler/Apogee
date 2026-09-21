#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/loop.h"

/// Structured output: a schema-bound run, validated client-side, always.
///
/// A provider's native JSON mode -- OpenAI's `text.format`, Gemini's
/// `responseSchema`, Anthropic's structured outputs or one forced tool --
/// reduces the retry rate; it never replaces the check (decided 2026-09-13).
/// The answer the loop returns is validated against the schema here, on
/// every backend alike, and a non-conforming one gets exactly one corrective
/// turn carrying the validator's message. A second miss is delivered RAW and
/// flagged `conforms: false` -- never dropped, never silently passed.
///
/// **Structured callers always strip reasoning.** The text validated here is
/// the loop's final answer, which every backend has already run through its
/// reasoning filter -- the rule Apogee keeps for persisted history, restated
/// for the parse path: a `<think>` block never reaches the parser.
namespace apogee::agentloop {

struct ValidationResult {
    bool ok = false;
    /// One line per violation, pointer first: `/findings/0: required
    /// property 'impact' not found`.
    std::vector<std::string> errors;
};

/// Validates `instance` against a draft-07 `schema`. A schema that is not
/// itself valid is reported as an error rather than thrown.
[[nodiscard]] ValidationResult validate_against(const nlohmann::json& schema,
                                                const nlohmann::json& instance);

/// Whether `schema` is a valid draft-07 JSON Schema; the reason when not.
[[nodiscard]] ValidationResult validate_schema(const nlohmann::json& schema);

/// The JSON object or array in `text`: code fences stripped, and prose
/// around the outermost braces tolerated. nullopt when none parses.
[[nodiscard]] std::optional<nlohmann::json> extract_json(std::string_view text);

/// The OUTPUT FORMAT block appended to an agent's system prompt: for JSON,
/// the instruction plus the schema text(s); for Markdown, the schema as a
/// content checklist.
[[nodiscard]] std::string schema_instruction(const std::vector<std::string>& schema_texts,
                                             bool markdown);

/// The one corrective message a non-conforming answer earns.
[[nodiscard]] std::string correction_message(const std::vector<std::string>& errors);

struct StructuredResult {
    RunResult run;
    /// The final answer text, raw.
    std::string answer;
    /// The parsed answer when it parsed at all, conforming or not.
    std::optional<nlohmann::json> json;
    bool conforms = false;
    /// Model turns taken: 1, or 2 after a correction.
    int attempts = 0;
    /// The validator's complaints about the FINAL answer; empty when it conforms.
    std::vector<std::string> errors;
};

/// Runs the loop with `schema` asked of the provider, validates the answer,
/// retries once with the validator's message, and reports honestly.
/// `history` is extended exactly as `run` extends it, correction included.
[[nodiscard]] StructuredResult run_structured(const harness::Harness& harness,
                                              std::vector<harness::ChatMessage>& history,
                                              const Options& options, Reporter& reporter,
                                              const nlohmann::json& schema);

}  // namespace apogee::agentloop
