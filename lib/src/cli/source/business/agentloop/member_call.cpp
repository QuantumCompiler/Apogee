#include "agentloop/member_call.h"

#include <algorithm>
#include <array>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "agentloop/content.h"
#include "contracts/errors.h"

namespace apogee::agentloop {
namespace {

/// The most of a brief the narration shows.
constexpr std::size_t kDetailExcerpt = 60;

/// Every role, for reading one back from its name.
constexpr std::array<harness::ModelRole, 6> kRoles{
    harness::ModelRole::Chat,   harness::ModelRole::Embedding,     harness::ModelRole::Extraction,
    harness::ModelRole::Vision, harness::ModelRole::Transcription, harness::ModelRole::Utility};

/// The one gate every member call passes, whichever consumer makes it: two
/// members never generate at once on a member call's account (27f).
std::mutex& member_call_gate() {
    static std::mutex gate;
    return gate;
}

std::string trimmed(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return std::string{text.substr(begin, end - begin + 1)};
}

/// Why `key`'s backend cannot answer a member call for `role` here, or empty
/// -- with `backend` set to the name the config spells it. A name the
/// router does not know would fall to `models.default` and answer from a
/// model nobody chose, so it is refused before anything is sent.
std::string unavailable(const harness::Harness& harness, std::string_view role,
                        const std::string& key, bool local_only, std::string& backend) {
    backend = key;
    if (key.empty()) {
        return "no backend answers for " + std::string{role} +
               " -- the suite names no member for it and no pointer is set";
    }
    const auto entry = harness.config().backends.find(key);
    if (entry == harness.config().backends.end()) {
        return "the " + std::string{role} + " member names '" + key +
               "', which is not a configured backend";
    }
    backend = entry->first;
    const std::vector<std::string> built = harness.provider_names();
    if (std::ranges::find(built, backend) == built.end()) {
        return "'" + backend + "' (" + std::string{role} +
               ") could not be built in this session, so it cannot be asked";
    }
    if (local_only && harness.generation_is_metered(backend)) {
        return "'" + backend + "' (" + std::string{role} +
               ") is billed per call -- a member is consulted on the model's initiative, which "
               "never spends: only local, unmetered members can be consulted";
    }
    return {};
}

}  // namespace

std::optional<harness::ModelRole> role_named(std::string_view name) {
    for (const harness::ModelRole role : kRoles) {
        if (harness::suite_role(role) == name) {
            return role;
        }
    }
    return std::nullopt;
}

harness::ChatRequest member_request(const std::string& backend, std::string_view brief,
                                    std::int64_t answer_tokens, std::string_view schema,
                                    const std::vector<harness::ContentPart>& images) {
    harness::ChatRequest request;
    request.model = backend;
    if (images.empty()) {
        request.messages.push_back(harness::ChatMessage::user(std::string{brief}));
    } else {
        // The picture before the words about it, as vision models were
        // trained (26e's describe request keeps the same order).
        std::vector<harness::ContentPart> parts = images;
        parts.push_back(harness::ContentPart::from_text(std::string{brief}));
        request.messages.push_back(
            harness::ChatMessage::user(harness::MessageContent::from_parts(std::move(parts))));
    }
    request.transient.response_schema = std::string{schema};
    request.max_tokens = answer_tokens;
    // An answer capped this short has no room for a reasoning trace ahead
    // of it, and a member is briefed to answer, not to think aloud.
    request.thinking.mode = harness::ThinkingMode::Off;
    // Not a turn of the conversation: a local member runs it on its own
    // context, and the conversation's cache is untouched.
    request.transient.side_request = true;
    return request;
}

std::string member_call_detail(std::string_view role, std::string_view backend,
                               std::string_view brief) {
    // One line, whitespace collapsed, cut at a word near the excerpt's end.
    std::string flat;
    for (const char c : brief) {
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (space) {
            if (!flat.empty() && flat.back() != ' ') {
                flat += ' ';
            }
        } else {
            flat += c;
        }
    }
    if (!flat.empty() && flat.back() == ' ') {
        flat.pop_back();
    }
    if (flat.size() > kDetailExcerpt) {
        std::size_t cut = flat.rfind(' ', kDetailExcerpt);
        if (cut == std::string::npos || cut < kDetailExcerpt / 2) {
            cut = kDetailExcerpt;
        }
        // Never inside a multi-byte character.
        while (cut > 0 && (static_cast<unsigned char>(flat[cut]) & 0xC0U) == 0x80U) {
            --cut;
        }
        flat = flat.substr(0, cut) + "…";
    }
    return "asking " + std::string{role} + " (" + std::string{backend} + "): " + flat;
}

MemberAnswer call_member(const harness::Harness& harness, const MemberCall& call,
                         const SideCallSink& narrate,
                         const harness::CancellationToken& cancellation) {
    MemberAnswer out;
    const std::string_view role = harness::suite_role(call.role);
    const harness::Resolution resolved = harness::resolve_backend(
        harness.config(),
        harness::RoleRequest{.role = call.role, .conversation = call.conversation});
    out.refused = unavailable(harness, role, resolved.key, call.local_only, out.backend);
    if (!out.refused.empty()) {
        return out;
    }
    if (call.brief.find_first_not_of(" \t\r\n") == std::string::npos) {
        out.refused = "the question is empty -- write the whole brief the member needs";
        return out;
    }
    const std::int64_t brief = estimate_tokens(call.brief).tokens;
    if (brief > call.brief_tokens) {
        out.refused = "the question is about " + std::to_string(brief) +
                      " tokens, and a brief is at most " + std::to_string(call.brief_tokens) +
                      " -- shorten it to what the member needs and ask again";
        return out;
    }
    if (!call.images.empty() && !harness.can_read(out.backend, harness::Medium::Image)) {
        out.refused = "'" + out.backend + "' (" + std::string{role} +
                      ") cannot read an image -- name a member that can for the " +
                      std::string{role} + " role";
        return out;
    }
    if (const std::int64_t window = harness.context_window_for_model(out.backend);
        window > 0 && brief + call.answer_tokens > window) {
        out.refused = "'" + out.backend + "' runs at a " + std::to_string(window) +
                      "-token window, too small for this question (about " + std::to_string(brief) +
                      " tokens) and its answer (up to " + std::to_string(call.answer_tokens) +
                      ") -- shorten the question";
        return out;
    }

    // One member call at a time, whoever makes it.
    const std::scoped_lock gate{member_call_gate()};
    cancellation.throw_if_cancelled();
    SideCallScope said{narrate, call.label, member_call_detail(role, out.backend, call.brief)};
    harness::ChatResponse response;
    try {
        response = harness.chat(
            member_request(out.backend, call.brief, call.answer_tokens, call.schema, call.images),
            cancellation);
    } catch (const harness::CancelledError&) {
        throw;
    } catch (const std::exception& e) {
        out.failed = e.what();
        return out;
    }
    if (response.usage.completion_tokens > 0) {
        out.tokens = response.usage.completion_tokens;
        said.tokens(response.usage.completion_tokens);
    }
    out.text = trimmed(response.message.content.plain_text());
    out.cut = response.finish_reason == harness::FinishReason::Length;
    if (out.text.empty()) {
        out.failed = "'" + out.backend + "' gave no answer";
    }
    return out;
}

std::vector<ConsultableMember> consultable_members(const harness::Harness& harness) {
    std::vector<ConsultableMember> out;
    const harness::SuiteConfig* suite = harness::active_suite(harness.config());
    if (suite == nullptr) {
        return out;
    }
    for (const std::string& name : suite->consultable) {
        const std::optional<harness::ModelRole> role = role_named(name);
        if (!role.has_value()) {
            continue;  // the loader admits roles only
        }
        ConsultableMember member;
        member.role = name;
        const harness::Resolution resolved =
            harness::resolve_backend(harness.config(), harness::RoleRequest{.role = *role});
        member.unavailable = unavailable(harness, name, resolved.key, true, member.backend);
        out.push_back(std::move(member));
    }
    return out;
}

MemberCalls::Turn::Turn(Turn&& other) noexcept : calls_{std::exchange(other.calls_, nullptr)} {}

MemberCalls::Turn::~Turn() {
    if (calls_ == nullptr) {
        return;
    }
    calls_->in_turn_ = false;
    calls_->narrate_ = nullptr;
    calls_->cancellation_ = {};
}

MemberCalls::Turn MemberCalls::begin_turn(SideCallSink narrate,
                                          harness::CancellationToken cancellation) {
    if (in_turn_) {
        throw std::logic_error("a member-call turn is already open");
    }
    in_turn_ = true;
    used_ = 0;
    narrate_ = std::move(narrate);
    cancellation_ = std::move(cancellation);
    return Turn{*this};
}

MemberAnswer MemberCalls::consult(std::string_view role, std::string_view brief) {
    MemberAnswer out;
    const harness::SuiteConfig* suite = harness::active_suite(harness_.config());
    if (suite == nullptr || suite->consultable.empty()) {
        out.refused = "no member can be consulted: the active suite designates none";
        return out;
    }
    const std::optional<harness::ModelRole> named = role_named(role);
    if (!named.has_value() ||
        std::ranges::find(suite->consultable, role) == suite->consultable.end()) {
        std::string listed;
        for (const std::string& consultable : suite->consultable) {
            listed += listed.empty() ? "" : ", ";
            listed += consultable;
        }
        out.refused = "'" + std::string{role} +
                      "' is not a member you can consult (consultable: " + listed + ")";
        return out;
    }
    const harness::ConsultLimits limits = harness::consult_limits(suite->consult_caps);
    MemberCall made;
    made.role = *named;
    made.brief = std::string{brief};
    made.brief_tokens = limits.brief_tokens;
    made.answer_tokens = limits.answer_tokens;
    made.label = "consult";
    return call(made, limits.per_turn);
}

MemberAnswer MemberCalls::call(const MemberCall& call, std::int64_t per_turn) {
    MemberAnswer out;
    if (!in_turn_) {
        out.refused = "no turn is open to make a member call in";
        return out;
    }
    if (used_ >= per_turn) {
        out.refused = "consult budget spent this turn: " + std::to_string(used_) + " of " +
                      std::to_string(per_turn) + " member calls made -- answer with what you have";
        return out;
    }
    out = call_member(harness_, call, narrate_, cancellation_);
    if (out.refused.empty()) {
        ++used_;  // made: answered or failed, it spent a call
    }
    return out;
}

}  // namespace apogee::agentloop
