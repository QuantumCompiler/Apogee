#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/validate.h"
#include "knowledge/record.h"

namespace apogee::harness {
class Harness;
}

/// The normalization clerk: a raw conversation in, ONE canonical record out.
///
/// The clerk is a prompt and a JSON Schema compiled into the binary, run as
/// **one structured-output call** through the shared loop -- asked of the
/// provider natively where its API has a JSON mode, validated client-side
/// always, with one corrective retry. A second miss is a failed capture that
/// stores nothing: a record is not a report, and there is no "raw with
/// `conforms: false`" for a record with no intent.
///
/// The model call arrives as an injected `ClerkFn`, so every test of the
/// capture logic runs with no backend; the surfaces bind it to
/// `make_structured_clerk` over their Harness.
namespace apogee::knowledge {

/// Extraction, not creativity. The generation cap is what keeps an untuned
/// local model that misses its stop token from decoding toward its context
/// limit -- a stall that reads as a hang.
inline constexpr double kClerkTemperature = 0.2;
inline constexpr std::int64_t kClerkMaxTokens = 2048;

/// The compiled-in prompt and schema, byte-identical to the shipped files
/// under `assets/clerks/`.
[[nodiscard]] std::string_view capture_prompt() noexcept;
[[nodiscard]] std::string_view capture_schema_text() noexcept;
[[nodiscard]] nlohmann::json capture_schema();

/// `prompt` followed by the OUTPUT FORMAT block and the capture schema --
/// the one function that words the directive, so a later revision pass asks
/// for the identical shape.
[[nodiscard]] std::string with_output_format(std::string_view prompt);

/// The capture clerk's whole system prompt.
[[nodiscard]] std::string capture_system_prompt();

/// What one clerk call produced, in the shape `run_structured` reports.
struct ClerkOutcome {
    /// The parsed answer when it parsed at all, conforming or not.
    std::optional<nlohmann::json> json;
    bool conforms = false;
    std::vector<std::string> errors;
    /// The final answer text, raw.
    std::string answer;
    /// Model turns taken: 1, or 2 after a correction.
    int attempts = 0;
};

/// The clerk as a function: the system prompt and the user message in, the
/// outcome out. Injected so the capture logic is testable model-free.
using ClerkFn =
    std::function<ClerkOutcome(std::string_view system_prompt, std::string_view user_message)>;

/// What the user said on the command line (or the body), each winning over
/// the clerk's inference for its field. Empty means "the clerk decides".
struct Overrides {
    std::string status;
    std::string discipline;
    std::string source;
    std::string link;
    std::string supersedes;
};

/// A record as a store would accept it, minus what only persistence mints
/// (id, timestamp, raw_ref) -- the shape `--dry-run` prints.
struct Draft {
    Record record;
    /// Why the draft cannot be stored; empty when it can.
    std::string error;
    /// What the suite's verifier made of the record (27g), when the
    /// extraction seam checked it: passed, revised once, disputed -- the
    /// record kept is the clerk's last word -- or unchecked, and why.
    std::optional<agentloop::Validated> validation;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// Applies `overrides` to a clerk-produced record, clears the system fields,
/// normalises and validates. The one place a clerk's output becomes a draft,
/// so every surface produces identical records.
[[nodiscard]] Draft draft_record(Record record, const Overrides& overrides);

/// Runs `clerk` over `raw` and drafts the result. A non-conforming outcome
/// is an error carrying the validator's message, never a partial record.
///
/// With a `verifier` -- the active suite's, when its `validate:` block turns
/// `extraction` on (27g) -- a draft that passed its structure is checked
/// against `raw` on the fixed rubric (`agentloop::extraction_brief`), the
/// record as the clerk wrote it, before any override, which is the user's
/// own word. An objection goes back to the clerk once, with the record and
/// the objection beside the source; the revision is checked again, and one
/// still objected to is kept with the dispute in `Draft::validation`. A
/// record that fails its structure never reaches the verifier.
[[nodiscard]] Draft run_capture(const ClerkFn& clerk, std::string_view raw,
                                const Overrides& overrides,
                                const agentloop::Verifier* verifier = nullptr);

/// The user message the clerk revises a record with (27g): the source, the
/// record it wrote, and the objection to it.
[[nodiscard]] std::string revision_message(std::string_view raw, std::string_view record,
                                           std::string_view objection);

/// Assigns the system fields a store needs: the id and the timestamp, both
/// from `now`. `raw_ref` is the store's to set when it archives.
[[nodiscard]] Record finalize(Record draft, std::chrono::system_clock::time_point now);

/// The production clerk: `agentloop::run_structured` on `harness` with
/// `model`, the capture schema, `kClerkTemperature`, `kClerkMaxTokens`, no
/// tools, and a throwaway history -- so the caller's own conversation is
/// never touched, which is what lets chat reuse its loaded model.
[[nodiscard]] ClerkFn make_structured_clerk(const harness::Harness& harness, std::string model);

}  // namespace apogee::knowledge
