#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/review_skill.h"
#include "review/diff.h"

/// The review core (48c): one merge request's change, reviewed.
///
///   plan     the skills that apply to the changed files, by their hints --
///            or the ones named -- narrowed by one cheap pick only when too
///            many apply and a narrowing pass is given
///   hunt     one pass per skill over each chunk of the numbered diff, its
///            answer held to the one finding contract
///   filter   structural and after the model, whatever it said: a finding
///            must name a line the change added; on a re-review, a line the
///            new push added; and not one this login already posted
///   dedup    one finding per identity (file, line, kind), the most severe
///            kept, every lens that found it credited
///   confirm  one bounded pass over what is left, which may confirm,
///            dismiss or tighten -- and never add
///   cap      the most severe `max_inline` post inline; the rest are
///            counted and listed in the one summary
///
/// **Silence is correct**: no finding is a complete review, posted as
/// nothing. **Generation arrives as a closure** (`PassFn`), so the command
/// composes the models and this never names one; it reads plain data and
/// returns plain data. The diff and the merge request's text reach a model
/// fenced as data, never as instructions.
namespace apogee::review {

/// Per review: how many findings post inline, the rest summarized.
inline constexpr std::size_t kMaxInlineFindings = 10;
/// More applicable skills than this, and a narrowing pass is asked.
inline constexpr std::size_t kMaxSkillsPerReview = 4;
/// The tokens one chunk of the diff may take when the model's window says
/// nothing better.
inline constexpr std::size_t kDefaultChunkTokens = 6000;
/// How much of a merge request's description a pass reads.
inline constexpr std::size_t kDescriptionExcerpt = 2000;

/// One model call the review needs.
struct PassRequest {
    /// `plan`, `hunt <skill>`, `confirm`: what it is, for narration.
    std::string label;
    std::string system;
    std::string user;
    /// The JSON Schema the answer is held to.
    std::string schema;
};

/// What came back.
struct PassAnswer {
    /// The parsed answer, when it parsed.
    std::optional<nlohmann::json> json;
    /// Whether it conforms to the schema.
    bool conforms = false;
    /// Why not, when it did not run or did not conform.
    std::string error;
};

using PassFn = std::function<PassAnswer(const PassRequest& request)>;

/// Hears each step as it starts (`hunting with security, chunk 2/3`).
using ProgressFn = std::function<void(std::string_view line)>;

struct Finding {
    std::string severity;
    std::string kind;
    std::string file;
    std::string location;
    std::string claim;
    std::string suggested_fix;
    std::string confidence;
    /// The first line `location` names: where the comment anchors.
    std::uint32_t line = 0;
    std::string identity;
    /// Every lens that found it, the first the one whose wording is kept.
    std::vector<std::string> found_by;
    /// The confirm pass tightened its wording.
    bool tightened = false;
};

/// A finding already on the merge request, as the re-review is told of it.
struct KnownFinding {
    std::string file;
    std::uint32_t line = 0;
    /// The posted comment's first line.
    std::string excerpt;
};

struct Plan {
    std::vector<const harness::ReviewSkillEntry*> skills;
    /// `named`, `hints`, `hints, narrowed`, `hints (narrowing failed: …)`.
    std::string how;
    /// Applicable skills the narrowing left out.
    std::vector<std::string> left_out;
};

/// The skills a review runs: `named` (refused naming the book when one is
/// unknown) or every skill whose hints apply to `paths`, narrowed by
/// `narrow` to `kMaxSkillsPerReview` when more apply -- the pick held to the
/// applicable set, never widened. Throws `std::runtime_error` for an unknown
/// name.
[[nodiscard]] Plan plan_review(const harness::ReviewSkillBook& book,
                               const std::vector<std::string>& paths,
                               const std::vector<std::string>& named, const PassFn* narrow,
                               const ProgressFn& progress = nullptr);

struct ReviewInput {
    std::string title;
    std::string description;
    /// The change, exclusions already applied.
    ParsedDiff diff;
    /// On a re-review, what the new push changed since the head the last
    /// review read: only its added lines are eligible. Unset for a first
    /// review (or when the comparison could not be had).
    std::optional<ParsedDiff> since;
    /// Identities already posted by this login.
    std::set<std::string> known_identities;
    /// The same findings, told to every hunt pass not to report again.
    std::vector<KnownFinding> known;
    std::vector<const harness::ReviewSkillEntry*> skills;
    std::size_t chunk_tokens = kDefaultChunkTokens;
    std::size_t max_inline = kMaxInlineFindings;
};

struct ReviewCounts {
    /// Findings the hunt passes returned.
    std::size_t candidates = 0;
    /// Left out after the model, each for its reason.
    std::size_t off_change = 0;
    std::size_t not_new = 0;
    std::size_t known = 0;
    std::size_t duplicates = 0;
    std::size_t dismissed = 0;
    std::size_t tightened = 0;
};

struct ReviewOutcome {
    /// Posted inline: the most severe, `max_inline` at most.
    std::vector<Finding> inline_findings;
    /// Past the cap: counted and listed in the summary.
    std::vector<Finding> over_cap;
    ReviewCounts counts;
    std::vector<std::string> skills;
    std::size_t chunks = 0;
    /// Passes that did not answer, and what the review did about each.
    std::vector<std::string> notes;

    [[nodiscard]] bool silent() const noexcept {
        return inline_findings.empty() && over_cap.empty();
    }
};

[[nodiscard]] ReviewOutcome run_review(const ReviewInput& input, const PassFn& pass,
                                       const ProgressFn& progress = nullptr);

/// The inline comment one finding posts as: its severity and kind, the
/// claim, the fix, its lenses and confidence, and its identity marker.
[[nodiscard]] std::string comment_body(const Finding& finding);

/// The one summary: how many findings and from which lenses, the ones past
/// the cap a line each, and the head marker the next review reads.
[[nodiscard]] std::string summary_body(const ReviewOutcome& outcome, std::string_view head);

/// One finding as the run document carries it.
[[nodiscard]] nlohmann::json to_json(const Finding& finding);

/// The schema the confirm pass is held to, and the plan's.
[[nodiscard]] const std::string& confirm_schema_text();
[[nodiscard]] const std::string& plan_schema_text();

}  // namespace apogee::review
