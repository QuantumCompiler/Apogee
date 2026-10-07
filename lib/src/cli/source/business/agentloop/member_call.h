#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/side_call.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "contracts/types.h"
#include "harness/harness.h"
#include "harness/roles.h"

/// The bounded, brief-only member call (27f): a brief in, one serial, capped,
/// narrated call on a role's member through the one resolver, its text out --
/// and the wire carrying exactly the brief.
///
/// **One model-calling path, three consumers.** The `consult` tool
/// (`tools/consult`) is the root model delegating; validation (27g) is a
/// verifier member checking an artifact; a symphony stage (27q) is the
/// harness driving one step of a staged process. Each is a thin consumer of
/// `call_member`, so the isolation contract, the caps, the one-at-a-time
/// discipline and the narration hold the same way for all three -- a second
/// calling path is exactly how they would come to differ.
///
/// **The brief is the whole context.** The request is one user message --
/// the brief -- and nothing else: no history, no system prompt, no
/// retrieval, no attachments, no tools. That is what lets a member with a
/// 4K window serve a root with a 32K one, and it is pinned at the wire.
namespace apogee::agentloop {

/// The consult tool's name (27f): declared here, beside the core it wraps, so
/// the loop's tool selection can keep it on offer without reaching up into
/// `tools/`.
inline constexpr std::string_view kConsultToolName = "consult";

/// What every symphony's tool is named after (27t): `play_<symphony>` -- a
/// name no native or MCP tool takes, valid as a tool name on every vendor
/// whatever the symphony is called, and the mark by which selection keeps the
/// symphony tools on offer. Declared here, beside consult's, for the same
/// reason: the loop's selection reads it without reaching up into
/// `symphony/`.
inline constexpr std::string_view kPlayToolPrefix = "play_";

/// The role `name` spells, as `members:`, `consultable:` and `validate:`
/// name one; nullopt for a word that names none.
[[nodiscard]] std::optional<harness::ModelRole> role_named(std::string_view name);

/// One member call.
struct MemberCall {
    /// The role whose member answers, resolved through the one chain
    /// (`harness/roles.h`) under the config the harness holds -- so under the
    /// session's suite.
    harness::ModelRole role = harness::ModelRole::Utility;
    /// The whole context the member sees.
    std::string brief;
    /// The conversation's backend: a helper role with no member and no
    /// pointer falls back to it, as the chain says. Empty for none.
    std::string conversation;
    /// The caps on this one call. The member's own window is the ceiling
    /// over both.
    std::int64_t brief_tokens = harness::kConsultBriefTokens;
    std::int64_t answer_tokens = harness::kConsultAnswerTokens;
    /// The narration line's kind -- `consult`, `validate`, a stage's name
    /// (26n's `SideCall::role`).
    std::string label = "consult";
    /// Refuse a member whose generation is billed per call: a call on the
    /// model's initiative never spends (the spend principle).
    bool local_only = true;
    /// A JSON Schema the answer is held to (27q: a symphony stage's), as its
    /// author wrote it: asked of the provider -- a local member's grammar
    /// (26f) -- and riding nowhere in the brief. Empty for a plain answer.
    std::string schema;
    /// Pictures the member reads with the brief, before it as vision models
    /// were trained (27q: a stage given the input's image). A member that
    /// cannot read an image is refused before anything is sent.
    std::vector<harness::ContentPart> images;
};

/// What came back.
struct MemberAnswer {
    /// Why the call was not made, in words for the model or the user: a cap,
    /// a member billed per call, a backend that is not there. Empty when it
    /// ran.
    std::string refused;
    /// Why the member failed once called: a load, a provider error. Empty
    /// when it answered.
    std::string failed;
    /// The answer, trimmed.
    std::string text;
    /// The backend that answered (or would have).
    std::string backend;
    /// The answer reached `answer_tokens` and was cut there.
    bool cut = false;
    /// The tokens the member produced, when its provider said.
    std::optional<std::int64_t> tokens;

    [[nodiscard]] bool ok() const noexcept {
        return refused.empty() && failed.empty();
    }
};

/// The request a member call sends `backend`: exactly `brief` as the one
/// user message, at most `answer_tokens` back, no tools, no reasoning, and
/// marked a side request so a local backend runs it on its own context and
/// the conversation's cache is untouched. `schema`, when set, is asked of
/// the provider beside it (never written into the brief), and `images` open
/// the message ahead of the brief's text. Exposed so the isolation is
/// testable on its own.
[[nodiscard]] harness::ChatRequest member_request(
    const std::string& backend, std::string_view brief, std::int64_t answer_tokens,
    std::string_view schema = {}, const std::vector<harness::ContentPart>& images = {});

/// The narration's detail for a call: `asking utility (l3b): <the brief's
/// first words>`.
[[nodiscard]] std::string member_call_detail(std::string_view role, std::string_view backend,
                                             std::string_view brief);

/// Makes one member call: resolves the role, refuses what the caps and the
/// spend rule refuse, then -- holding the process-wide member-call gate, so
/// no two member calls ever generate at once -- narrates it through
/// `narrate` and sends `member_request`. Never throws for a refusal or a
/// failed member; throws `CancelledError` when `cancellation` fires.
[[nodiscard]] MemberAnswer call_member(const harness::Harness& harness, const MemberCall& call,
                                       const SideCallSink& narrate,
                                       const harness::CancellationToken& cancellation);

/// One member the active suite lets the root consult (27f).
struct ConsultableMember {
    /// The role, as `consultable:` names it.
    std::string role;
    /// The backend the one resolver gives it.
    std::string backend;
    /// Why it cannot be consulted here -- billed per call, not built -- or
    /// empty when it can.
    std::string unavailable;
};

/// The active suite's consultable members, in `consultable:`'s order, each
/// resolved and asked whether it can run here. Empty with no suite active,
/// or none consultable.
[[nodiscard]] std::vector<ConsultableMember> consultable_members(const harness::Harness& harness);

/// A conversation's member calls, counted per turn (27f): the budget the
/// consult tool spends from -- and validation (27g), from the same count --
/// read against the active suite's `consult_caps:` at the
/// moment of each call, so `/suite` moves it with the suite.
///
/// The loop opens a turn (`agentloop::Options::member_calls`): the count
/// starts over and the turn's narration and cancellation are the ones a call
/// uses until the turn closes. A call with no turn open is refused, never run
/// unbounded.
class MemberCalls {
public:
    explicit MemberCalls(const harness::Harness& harness) : harness_{harness} {}

    MemberCalls(const MemberCalls&) = delete;
    MemberCalls& operator=(const MemberCalls&) = delete;
    MemberCalls(MemberCalls&&) = delete;
    MemberCalls& operator=(MemberCalls&&) = delete;
    ~MemberCalls() = default;

    /// A turn, open for as long as it lives.
    class Turn {
    public:
        Turn(const Turn&) = delete;
        Turn& operator=(const Turn&) = delete;
        Turn(Turn&& other) noexcept;
        Turn& operator=(Turn&&) = delete;
        ~Turn();

    private:
        friend class MemberCalls;

        explicit Turn(MemberCalls& calls) : calls_{&calls} {}

        MemberCalls* calls_;
    };

    /// A line the turn's surface keeps -- a play the model started and could
    /// not run, said rather than silent (27t).
    using NoticeSink = std::function<void(std::string_view)>;

    /// Opens a turn. Throws std::logic_error when one is already open.
    /// `notice` is where `notice()` says a line; null drops it.
    [[nodiscard]] Turn begin_turn(SideCallSink narrate, harness::CancellationToken cancellation,
                                  NoticeSink notice = {});

    /// Consults the member `role` names under the active suite, with `brief`:
    /// refused when no turn is open, when the role is not consultable there,
    /// or when this turn's budget is spent; otherwise `call` under the
    /// suite's caps.
    [[nodiscard]] MemberAnswer consult(std::string_view role, std::string_view brief);

    /// One call against this turn's budget: refused once the turn has made
    /// `per_turn` calls, else made, and counted, whether it answered or
    /// failed. A refusal by the call itself -- a brief over its cap -- spends
    /// nothing.
    [[nodiscard]] MemberAnswer call(const MemberCall& call, std::int64_t per_turn);

    /// Calls made this turn.
    [[nodiscard]] std::int64_t used() const noexcept {
        return used_;
    }

    /// Where this turn's model calls are said (26n): a consumer narrating
    /// what wraps its calls -- a play the model started (27t) -- says it
    /// beside them. Null when no turn is open.
    [[nodiscard]] const SideCallSink& narration() const noexcept {
        return narrate_;
    }

    /// Says `line` as a kept line on this turn's surface, when it gave one.
    void notice(std::string_view line) const;

    [[nodiscard]] bool in_turn() const noexcept {
        return in_turn_;
    }

private:
    const harness::Harness& harness_;
    bool in_turn_ = false;
    std::int64_t used_ = 0;
    SideCallSink narrate_;
    NoticeSink notice_;
    harness::CancellationToken cancellation_;
};

}  // namespace apogee::agentloop
