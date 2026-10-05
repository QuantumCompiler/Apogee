#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/member_call.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// Rubber-duck validation (27g): a suite's members checking each other's
/// work at the three places an error propagates -- a tool's arguments before
/// it runs, a knowledge capture's record against its source, and an answer
/// on request -- as the active suite's `validate:` block opts each in.
///
/// **Cheap first, model last.** A seam's pipeline is its structural checks --
/// a parse, a schema, a path that must exist -- and only when every one
/// passes, and only when the seam is on, the verifier member. A model is
/// never asked what structure already answered, and the call counts say so.
///
/// **Bounded rounds, by construction.** An objection goes back to the
/// producer once; a second -- or the producer standing by what was objected
/// to -- is surfaced with both positions. `Rounds` is that counter, and it
/// has no third round to give.
///
/// **One model-calling path.** The verifier is a member call
/// (`agentloop/member_call`): briefed with the artifact and the criterion and
/// nothing else, serial, capped, narrated as `validate`, and spending from
/// the same per-turn budget the consult tool does. This file adds policy, and
/// everything in it is pure over the checks and the call it is handed.
namespace apogee::agentloop {

/// The seams.
enum class Seam : std::uint8_t { ToolArgs, Extraction, Answer };

/// `tool_args`, `extraction`, `answers` -- as `validate:` spells them.
[[nodiscard]] std::string_view to_string(Seam seam) noexcept;

/// Who decided a check no model was asked about.
inline constexpr std::string_view kStructure = "structure";

/// The most rounds an artifact goes through: the check, and the check of
/// the one revision. There is no third.
inline constexpr int kMaxRounds = 2;

/// The longest excerpt a surfaced line quotes before it cuts.
inline constexpr std::size_t kSurfacedExcerpt = 400;

/// What a verifier's reply says.
struct Verdict {
    enum class Kind : std::uint8_t { Agree, Object, Unread };
    Kind kind = Kind::Unread;
    /// The words after the verdict, trimmed -- the objection, for `Object`.
    std::string reason;
};

/// Reads a verifier's reply: `AGREE` or `OBJECT` as its first word, any
/// case, past markdown emphasis; failing that, exactly one of the two
/// written in capitals somewhere in it. Anything else is `Unread`, never
/// guessed at.
[[nodiscard]] Verdict read_verdict(std::string_view reply);

/// A structural check: decides without a model. Why the artifact fails it,
/// or empty when it passes.
using StructuralCheck = std::function<std::string()>;

/// The verifier a seam asks.
struct Verifier {
    /// The role whose member checks, as `validate.verifier` names it.
    std::string role;
    /// Asks the member, the brief being all it sees: `MemberCalls::call`
    /// bound to the suite's caps (`bind_verifier`), or a test's script.
    std::function<MemberAnswer(const std::string& brief)> ask;
    /// The longest brief the suite lets it be sent.
    std::int64_t brief_tokens = harness::kConsultBriefTokens;
    /// Why it cannot be asked now -- the turn's budget spent -- or empty.
    std::function<std::string()> unavailable;
};

/// One check of one artifact.
struct Check {
    enum class Outcome : std::uint8_t { Pass, Object, Unchecked };
    Outcome outcome = Outcome::Pass;
    /// Who decided: `kStructure`, or the verifier as `utility (l3b)`.
    std::string by;
    /// The objection, verbatim.
    std::string objection;
    /// Why the verifier did not decide -- the budget, a refusal, a failure,
    /// a reply that was neither verdict. `Unchecked` always carries one.
    std::string note;
    /// Model calls the check made: 0 or 1.
    int model_calls = 0;

    [[nodiscard]] bool structural() const noexcept {
        return by == kStructure;
    }
};

/// The cheap-first pipeline: every structural check in order, the first
/// failure returned as an objection without waking anything; then -- only
/// with a `verifier`, which a seam that is off does not have -- the verifier
/// on `brief()`, built only when it will be sent. With no verifier, what
/// passed structure passes. A verifier that cannot decide leaves the check
/// `Unchecked` with its note: degraded to structure, never silent.
[[nodiscard]] Check run_checks(const std::vector<StructuralCheck>& structure,
                               const Verifier* verifier, const std::function<std::string()>& brief);

/// The bounded rounds: what a check means at the round it is in. A pass, or
/// a check that could not decide, proceeds; the first objection goes back
/// for one revision; the second is surfaced. Once it has proceeded or
/// surfaced it is over, and asking again throws std::logic_error -- there
/// is no round three to reach.
class Rounds {
public:
    enum class Step : std::uint8_t { Proceed, Revise, Surface };

    [[nodiscard]] Step next(const Check& check);

    /// The round the next check is in: 1, or 2 after a revision.
    [[nodiscard]] int round() const noexcept {
        return objections_ + 1;
    }

    [[nodiscard]] bool over() const noexcept {
        return over_;
    }

private:
    int objections_ = 0;
    bool over_ = false;
};

/// One artifact through the rounds.
struct Validated {
    enum class Result : std::uint8_t {
        /// Checked, and agreed with.
        Passed,
        /// Objected to, revised once, and the revision agreed with.
        Revised,
        /// An objection stands beside the producer's last word: surfaced.
        Disputed,
        /// Structure passed, and the verifier could not decide (the notes say
        /// why).
        Unchecked,
    };
    Result result = Result::Passed;
    /// What goes on: the revision, when there was one.
    std::string artifact;
    /// The artifact as it was first produced.
    std::string original;
    /// The objection -- the one answered, for `Revised`; the one standing,
    /// for `Disputed`.
    std::string objection;
    /// Who objected, or who agreed.
    std::string by;
    std::vector<std::string> notes;
    /// Verifier calls made: 0, 1 or 2.
    int model_calls = 0;
    /// Revisions asked of the producer: 0 or 1.
    int revisions = 0;
    /// The producer gave back what was objected to, unchanged: surfaced
    /// without asking the verifier the same question twice.
    bool insisted = false;
};

/// `seam`'s result as a word: `passed`, `revised`, `disputed`, `unchecked`.
[[nodiscard]] std::string_view to_string(Validated::Result result) noexcept;

/// The producer's one revision: `artifact`, shown `objection`, made again --
/// or nullopt, with `note` saying why, when it could not be (a revision
/// that fails its own structure, a failed call).
using ReviseFn = std::function<std::optional<std::string>(
    const std::string& artifact, const std::string& objection, std::string& note)>;

/// Runs `artifact` through the rounds: `check` it; on an objection, one
/// `revise`, and -- when `recheck` -- the revision checked in round two; a
/// second objection is surfaced. A revision identical to the artifact
/// objected to is the producer standing by it, and is surfaced without the
/// same question being asked twice; without `recheck` the revision is
/// surfaced beside the objection, unverified.
[[nodiscard]] Validated validate_artifact(std::string artifact,
                                          const std::function<Check(const std::string&)>& check,
                                          const ReviseFn& revise, bool recheck);

/// Tool-argument validation across a turn's steps: the rounds kept per tool
/// in the loop, because a revision arrives as the model's next call. Round
/// one's objection is the call's result -- it does not run, and the
/// permission gate is never consulted for it; the revision is checked in
/// round two, and runs after a pass or, at the round limit, with the
/// dispute said first. One per turn.
class ToolArgChecks {
public:
    struct Decision {
        /// Set when the call does not run: the result the model reads in its
        /// place.
        std::optional<std::string> result;
        /// Lines to say, in order, before the call runs or instead of it.
        std::vector<std::string> said;
        /// Model calls the decision made.
        int model_calls = 0;
    };

    /// Decides one call of `tool` with `arguments`.
    [[nodiscard]] Decision check(std::string_view tool, std::string_view arguments,
                                 const std::vector<StructuralCheck>& structure,
                                 const Verifier* verifier,
                                 const std::function<std::string()>& brief);

    /// Whether a revision of `tool` is awaited.
    [[nodiscard]] bool pending(std::string_view tool) const;

private:
    struct Pending {
        Rounds rounds;
        std::string arguments;
        std::string objection;
        std::string by;
    };

    std::map<std::string, Pending, std::less<>> pending_;
};

// ---- the briefs: the artifact and the criterion, nothing else -------------

/// The tool-argument brief: the call, the request it serves, what the tool
/// does, and what is grounds to object.
[[nodiscard]] std::string tool_args_brief(std::string_view request, std::string_view tool,
                                          std::string_view description, std::string_view arguments);

/// The extraction brief -- a fixed rubric, never free-form critique: every
/// required field supported by the source, every name, number, count and
/// link matching it, nothing the source contradicts.
[[nodiscard]] std::string extraction_brief(std::string_view source, std::string_view record,
                                           const std::vector<std::string>& required);

/// The answer brief: the question and the answer.
[[nodiscard]] std::string answer_brief(std::string_view question, std::string_view answer);

/// The fields `schema` requires, nested ones dotted (`provenance.source`), in
/// the order its `required` lists name them.
[[nodiscard]] std::vector<std::string> required_fields(const nlohmann::json& schema);

// ---- the policy -----------------------------------------------------------

/// Whether `seam` is switched on under `config`'s active suite: `tool_args`
/// and `extraction` when on, the answer seam when `answers: always`. Off with
/// no suite, and with no `validate:` block.
[[nodiscard]] bool seam_on(const harness::Config& config, Seam seam);

/// The active suite's verifier role -- `validate.verifier`, or the utility
/// member by default -- or, in `missing`, why there is none.
struct VerifierRole {
    std::string role;
    std::string missing;
};

[[nodiscard]] VerifierRole verifier_role(const harness::Config& config);

/// The verifier `role` names under the harness's active suite, asked through
/// `calls` -- so within the turn `calls` has open, under the suite's
/// `consult_caps:`, spending the same count consults do, narrated as
/// `validate`. `unavailable` says when the turn's budget is spent.
[[nodiscard]] Verifier bind_verifier(const harness::Harness& harness, MemberCalls& calls,
                                     const std::string& role);

// ---- what is said ---------------------------------------------------------

/// `text` on one line, verbatim when it fits `limit` bytes, else cut at a
/// word with `…`.
[[nodiscard]] std::string excerpt(std::string_view text, std::size_t limit = kSurfacedExcerpt);

/// The result a call objected to in round one reads in its place.
[[nodiscard]] std::string objection_result(std::string_view tool, const Check& check);

/// The lines an answer check says, each led by `label` (`check` for
/// `/check`, `validate` for a standing check): agreement, the two positions,
/// or why it could not be checked.
[[nodiscard]] std::vector<std::string> answer_lines(const Validated& validated,
                                                    std::string_view label = "check");

/// The lines an extraction check says, for a capture's report.
[[nodiscard]] std::vector<std::string> extraction_lines(const Validated& validated);

/// A validation as a JSON report carries it: `result`, `verifier`, the
/// `objection` when there is one, `revisions`, `verifier_calls`, and `notes`
/// and `insisted` when they say anything.
[[nodiscard]] nlohmann::json validation_json(const Validated& validated);

}  // namespace apogee::agentloop
